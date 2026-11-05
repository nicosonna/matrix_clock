# Spec — Réseau, portail captif et reprise après coupure (2026-11-05)

> **Statut : spécifié, rien d'implémenté.** Composant pur `network`
> (**repris de ThermoSync**, `ThermoSync/firmware/components/reseau/`, traduit
> en anglais à la copie),
> adaptateur `clock_web`.

---

## 1. Ce qu'on reprend tel quel

Le composant `reseau` de ThermoSync est déjà pur, borné, sans allocation et
testé sur host. On le reprend **sans le réécrire** :

| Fichier | Rôle |
|---|---|
| `wifi_credentials.hpp` | Bornes du modèle (SSID 32, phrase de passe 8–63, ASCII imprimable), `validate()`, `form_field()` (décodage `x-www-form-urlencoded` borné, `%XX` et `+`, rejet d'un `%` mal formé plutôt que réparation), `json_escape()`, `model_json()`, `derive_ap_password()`. |
| `captive_dns.hpp` | `build_captive_response()` : toute requête de type A reçoit l'IP de la carte, les autres types une réponse **vide mais sans erreur** — c'est ce qui fait ouvrir la fenêtre du portail sur iOS, Android et Windows. Décodage borné, sans récursion, refus des paquets mal formés (compression dans la question, longueurs incohérentes, plus d'une question). |
| `wifi_supervisor.hpp` | Machine d'état : essais, repli, nouvel essai périodique. |

Comportement déjà éprouvé et **conservé** :

- Pas de réseau configuré ⇒ point d'accès + portail.
- Réseau configuré ⇒ `essais` essais de `essai_ms`, **point d'accès éteint**
  pendant les essais.
- Tous les essais échouent ⇒ **repli** : point d'accès + portail, **station au
  repos**. Une station qui réessaie en fond fait sauter le canal radio et
  déconnecte les clients du point d'accès — constaté sur la borne.
- En repli, **nouvel essai** de `nouvel_essai_fenetre_ms` toutes les
  `nouvel_essai_periode_ms`, point d'accès maintenu.
- Lien perdu une fois connecté ⇒ nouvelle série d'essais.
- La logique rend un **plan** (`{ ap_on, portal_on, sta_connect, attempt_id }`)
  que la couche ESP-IDF applique. Aucune dépendance ESP-IDF.

---

## 2. Ce qu'on ajoute : la reprise après coupure de courant (EX-22, EX-23)

### Le problème

Au retour du courant, l'ESP32 démarre en 1 s ; la box Internet met 30 s à 2 min.
Avec le comportement actuel, l'horloge épuise ses 3 essais de 10 s en 30 s et
**ouvre son portail captif** alors que rien n'est cassé : le réseau arrive juste
après. L'utilisateur trouve une horloge en mode « mise en service » le matin.

Deuxième cas : le point d'accès répond mais le serveur DHCP n'est pas encore
prêt. L'horloge s'associe, n'obtient pas d'IP, et le compte comme un échec de
réseau — alors que **l'association prouve que les identifiants sont bons**.

### L'entrée enrichie

`step()` reçoit désormais un état de lien à deux bits au lieu d'un booléen :

```cpp
struct EtatLien {
    bool associe;      ///< Associé au point d'accès (STA_CONNECTED).
    bool ip_obtenue;   ///< Bail DHCP obtenu (STA_GOT_IP).
};

enum class CauseEchec : uint8_t {
    Aucune = 0U,
    PointAccesAbsent,   ///< NO_AP_FOUND : le réseau n'est pas là.
    Authentification,   ///< AUTH_FAIL, HANDSHAKE_TIMEOUT : mauvaise phrase de passe.
    BailAbsent,         ///< Associé, pas d'IP au bout de `bail_ms`.
    Autre,
};
```

### La fenêtre de grâce au démarrage

| État | Quand | Point d'accès | Portail | Station |
|---|---|---|---|---|
| `SansIdentifiants` | NVS vide | **oui** | **oui** | repos |
| `GraceDemarrage` | identifiants présents, < `grace_demarrage_ms` depuis le boot | non | non | essais à délai croissant |
| `Essais` | après la grâce | non | non | `essais` × `essai_ms` |
| `AttenteBail` | déjà associé au moins une fois, pas d'IP | non | non | réessais toutes les `attente_bail_ms` |
| `BailDesespere` | `bail_sans_succes_ms` d'association sans IP | **oui** | **oui** | réessais poursuivis |
| `Connecte` | IP obtenue | non | non | connectée |
| `Repli` | essais épuisés hors grâce | **oui** | **oui** | repos |
| `NouvelEssai` | en repli, fenêtre de réessai | **oui** | oui | essai |

Règles qui font la différence :

1. **Pendant la grâce, aucun portail.** Les échecs `PointAccesAbsent` déclenchent
   un réessai à **délai croissant** (10 s, 20 s, 40 s, 60 s, puis 60 s) sans
   jamais lever le point d'accès. L'horloge affiche l'heure dès que SNTP répond.
2. **`Authentification` met fin à la grâce.** Une phrase de passe fausse ne
   s'arrange pas en attendant : deux échecs d'authentification (le premier peut
   venir d'une box à moitié démarrée) font passer directement en `Repli`, donc
   au portail, pour que l'utilisateur corrige.
3. **`BailAbsent` ne mène jamais au portail tout seul.** On s'est associé, donc
   les identifiants sont bons : le portail ne servirait à rien. On va en
   `AttenteBail` et on réessaie toutes les `attente_bail_ms` (60 s, plafonné à
   300 s), indéfiniment. C'est le « attendre un moment et retenter » demandé.
4. **Mais pas indéfiniment en silence.** Au bout de `bail_sans_succes_ms`
   (30 min) d'association sans IP, on passe en `BailDesespere` : le point d'accès
   **et** le portail sont levés **en plus** des réessais, parce qu'à ce stade
   quelque chose est mal réglé côté box (pool DHCP saturé, filtrage MAC) et il
   faut pouvoir joindre la page. Une règle d'alerte interne le signale.
5. **Une fois `Connecte`,** une perte de lien repart en `Essais` (pas en grâce :
   la box tournait il y a une minute).

### Réglages

| Réglage | Défaut | Bornes | Note |
|---|---|---|---|
| `essais` | 3 | 1–10 | Comme la borne. |
| `essai_ms` | 10 000 | 3 000–60 000 | Comme la borne. |
| `grace_demarrage_ms` | **300 000** | 0–1 800 000 | 5 min. 0 = pas de grâce (comportement ThermoSync d'origine). |
| `backoff_grace_ms` | 10 000, ×2, plafond 60 000 | — | Délai croissant pendant la grâce. |
| `bail_ms` | 20 000 | 5 000–60 000 | Attente d'un bail DHCP après association. |
| `attente_bail_ms` | 60 000, plafond 300 000 | — | Réessai en `AttenteBail`. |
| `bail_sans_succes_ms` | 1 800 000 | 60 000–7 200 000 | Vers `BailDesespere`. |
| `nouvel_essai_periode_ms` | 900 000 | — | 15 min, comme la borne. |
| `nouvel_essai_fenetre_ms` | 30 000 | — | Comme la borne. |
| `redemarrage_si_sans_ip_h` | 24 | 0–168 | 0 = jamais. Redémarrage de récupération si la pile WiFi s'est figée ; **une seule fois** par tranche, journalisé. |

Les compteurs de millisecondes sont en `uint32_t` avec **débordement toléré**
(comparaisons par différence), comme dans ThermoSync. C'est testé.

---

## 3. Point d'accès et portail

| Élément | Valeur |
|---|---|
| SSID | `Horloge-XXXX` — `XXXX` = 2 derniers octets de la MAC en hexadécimal |
| Phrase de passe | 10 caractères `[0-9a-z]` **dérivés de la MAC** (`derive_ap_password`, comme la borne) |
| Canal | 1 |
| Clients simultanés | 4 |
| Adresse | 192.168.4.1 / 24, serveur DHCP actif |

> ⚠️ **Dette connue, reprise de ThermoSync** : la phrase de passe dérivée de la
> MAC est déterministe, donc **devinable** par qui connaît l'algorithme et lit le
> BSSID. Acceptable pour un portail de mise en service, à remplacer par un
> réglage (`dev/todo.md`, Q5 du cadrage).

Le SSID et la phrase de passe du point d'accès sont **affichés sur la dalle**
(scène `Defilement`, déclenchée tant qu'on est en `SansIdentifiants`,
`Repli` ou `BailDesespere`) : c'est le seul moyen de les connaître sans câble
série, et c'est un avantage qu'une horloge a sur une borne de recharge.

### Détection de portail par les systèmes

En plus du DNS captif, le serveur HTTP répond pour que la fenêtre s'ouvre
vraiment :

| Requête | Réponse |
|---|---|
| `GET /hotspot-detect.html`, `/library/test/success.html` (Apple) | 200 avec une page qui **n'est pas** « Success » ⇒ iOS ouvre le portail |
| `GET /generate_204`, `/gen_204` (Android) | **302** vers `http://192.168.4.1/wifi` |
| `GET /ncsi.txt`, `/connecttest.txt` (Windows) | 302 vers `/wifi` |
| `GET /canonical.html` (Firefox) | 302 vers `/wifi` |
| Toute autre URI inconnue en mode portail | 302 vers `/wifi` |

---

## 4. Intégration ESP-IDF (`clock_web`)

- Lecture des identifiants en **NVS** (espace `clk_wifi`) **avant** l'init WiFi.
- `esp_netif` station + point d'accès, `WIFI_MODE_APSTA` seulement quand le plan
  demande les deux ; sinon mode unique (le mode mixte coûte du courant et perturbe
  le canal).
- Les identifiants écrits par le portail sont pris en compte **sans redémarrage**
  (EX-24) : écriture NVS, puis nouvelle série d'essais via un `attempt_id`
  incrémenté.
- **mDNS** (EX-25) : annonce `horloge.local` et le service `_http._tcp`. C'est de
  l'annonce, pas de la résolution — ThermoSync n'a pas de résolution mDNS (le
  broker doit être donné en IP), ce qui est une autre affaire.
- SNTP démarré **dès l'obtention de l'IP**, pas au boot : l'heure doit apparaître
  dans les secondes qui suivent le retour du réseau.
- Le serveur HTTP tourne **dans tous les états**, portail ou pas : la page est
  joignable depuis le réseau de la maison comme depuis le point d'accès.
- L'état réseau est publié pour l'affichage (icône) et pour les règles d'alerte
  internes, et il **ne bloque jamais** la boucle d'affichage (EX-61).

---

## 5. Tests

Tests host du composant pur `network`. `RES-01` à `RES-11` sont **repris de
ThermoSync** et doivent continuer à passer ; `RES-20` et suivants sont nouveaux.

| Id | Vérifie |
|---|---|
| RES-01 | Pas d'identifiants ⇒ point d'accès + portail dès le premier `step()`. |
| RES-02 | Identifiants présents ⇒ essais, point d'accès éteint. |
| RES-03 | 3 essais échoués (hors grâce) ⇒ `Repli` : point d'accès + portail, station au repos. |
| RES-04 | En repli, nouvel essai de 30 s toutes les 15 min, point d'accès maintenu. |
| RES-05 | Connexion réussie ⇒ `Connecte`, point d'accès et portail éteints. |
| RES-06 | Lien perdu après connexion ⇒ nouvelle série d'essais. |
| RES-07 | `attempt_id` change à chaque nouvel essai et seulement là. |
| RES-08 | Débordement du compteur 32 bits de millisecondes sans faux déclenchement. |
| RES-09 | `validate()` : SSID vide / trop long, passe trop courte / trop longue / caractère non ASCII, réseau ouvert accepté. |
| RES-10 | `form_field()` : champ absent, `%XX` valide, `%` mal formé rejeté, `+`, octet nul décodé rejeté, débordement du tampon. |
| RES-11 | `build_captive_response()` : requête A, requête AAAA (vide sans erreur), paquet tronqué, compression dans la question, deux questions, réponse au lieu de requête. |
| **RES-20** | Grâce au démarrage : 5 min d'échecs `PointAccesAbsent` ⇒ **jamais** de portail, délais 10/20/40/60/60 s. |
| **RES-21** | Fin de la grâce ⇒ comportement `Essais` puis `Repli` normal. |
| **RES-22** | `grace_demarrage_ms = 0` ⇒ comportement ThermoSync d'origine, bit pour bit. |
| **RES-23** | Deux échecs `Authentification` pendant la grâce ⇒ `Repli` immédiat (portail). |
| **RES-24** | Un seul échec `Authentification` ne termine pas la grâce. |
| **RES-25** | `associe = true, ip_obtenue = false` pendant `bail_ms` ⇒ `AttenteBail`, **pas** de portail, réessai à 60 s puis plafond 300 s. |
| **RES-26** | `AttenteBail` tenu 29 min ⇒ toujours pas de portail ; à 30 min ⇒ `BailDesespere` : point d'accès **et** portail levés, réessais poursuivis. |
| **RES-27** | Bail obtenu depuis `AttenteBail` ou `BailDesespere` ⇒ `Connecte`, point d'accès éteint. |
| **RES-28** | Association réussie une fois ⇒ un échec `BailAbsent` ultérieur ne mène jamais seul au `Repli`. |
| **RES-29** | `redemarrage_si_sans_ip_h` : demande de redémarrage émise une seule fois par tranche, et jamais si la valeur est 0. |

Vérifications matérielles (plan de test) :

| Id | Procédure |
|---|---|
| HW-RES-01 | Coupure secteur de la maison (horloge **et** box) : l'horloge revient, affiche l'heure, **n'ouvre pas** le portail. |
| HW-RES-02 | Horloge alimentée, box éteinte 10 min puis rallumée : reconnexion seule, pas de portail. |
| HW-RES-03 | Serveur DHCP désactivé sur la box : `AttenteBail`, pas de portail ; au bout de 30 min, portail **en plus** et alerte interne. |
| HW-RES-04 | Phrase de passe changée sur la box : portail en moins de 2 min. |
| HW-RES-05 | Portail ouvert : fenêtre captive qui s'ouvre d'elle-même sur iPhone, Android et Windows ; SSID et phrase de passe lisibles sur la dalle. |
| HW-RES-06 | Identifiants saisis au portail ⇒ connexion **sans redémarrage**. |
| HW-RES-07 | `horloge.local` résolu depuis le PC et depuis un téléphone. |
