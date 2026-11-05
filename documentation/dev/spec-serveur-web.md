# Spec — Serveur web et page de paramétrage (2026-11-05)

> **Statut : spécifié, rien d'implémenté.** Composant `clock_web`, modèle pur
> dans `settings`.

---

## 1. Principes

- Page **autonome** : un seul fichier HTML, **aucune ressource externe, aucun
  CDN**, embarquée dans l'image (`EMBED_FILES`) et servie **gzippée**.
  Budget : **≤ 60 Ko compressés** (4 Mo de flash, deux emplacements OTA).
- **Aucun emoji.** Icônes = **SVG inline** style ligne, centralisées dans un
  `<symbol>` réutilisé par `<use>`.
- Thème **clair/sombre automatique** (`prefers-color-scheme`), lisible sur
  téléphone (c'est l'appareil qui se connecte au portail).
- **Paramétrage complet** (EX-50) **et** un **wizard par domaine** (EX-51), dont
  les bornes et défauts viennent de `/api/<domaine>/modele` : **le firmware est
  la seule source de vérité**, rien n'est recopié dans le HTML.
- Le serveur tourne **dans tous les états réseau** : portail ou réseau de la
  maison, même page, mêmes URL.

---

## 2. Pages

| URL | Contenu | Wizard |
|---|---|---|
| `/` | Tableau de bord : heure et sa validité, luminosité mesurée / appliquée / écrêtage de courant, état réseau, état de la liaison Home Assistant, les 8 valeurs avec leur **fraîcheur**, alertes en cours (**listées, pas acquittables** : elles tombent d'elles-mêmes, EX-46). | — |
| `/wifi` | Réseaux détectés, SSID, phrase de passe, état du superviseur (`GraceDemarrage`, `AttenteBail`, …). **C'est la page du portail captif.** | mise en service |
| `/affichage` | Correspondance du panneau, scènes et durées, polices, budget de courant, séparateur, barre de secondes, transitions. | correspondance (§4.1) |
| `/luminosite` | Mode, courbe, plancher/plafond, pas, compensation, plage d'extinction. | étalonnage (§4.2) |
| `/ha` | Schéma, hôte, port, jeton, 8 valeurs suivies, notifications, météo de repli. | liaison en 4 étapes |
| `/mqtt` | Broker, port, utilisateur, mot de passe (écriture seule), préfixe, période ; état de la liaison ; bouton « renvoyer la découverte ». **Page reprise de ThermoSync.** | broker → utilisateur → découverte |
| `/alertes` | Les 16 règles, leur état en direct, la file de notifications. | création de règle en 5 étapes |
| `/air` | État du capteur (présent, préchauffage avec compte à rebours, défaut), 5 grandeurs en direct, niveau courant, seuils, couleurs, textes, logo, décalages. | détection → préchauffage → auto-échauffement → niveaux (`spec-qualite-air.md` §7) |
| `/systeme` | **Fuseau horaire** (liste de fuseaux IANA, défaut `Europe/Zurich`, appliqué à chaud), serveurs SNTP, OTA, redémarrage, réglages d'usine, journal, diagnostics, version. | — |

En mode portail, **toute URI inconnue** est redirigée vers `/wifi`, et les
sondes de détection de portail des systèmes sont traitées
(`spec-reseau-portail.md` §3).

---

## 3. API

Entrées : `application/x-www-form-urlencoded`, décodées par
`network::form_field()` (borné, sans allocation, rejet d'un `%` mal formé).
Corps limité à **2 Ko** sauf l'OTA. Sorties : JSON écrit par un **écrivain sans
allocation**, dans un tampon statique.

| Méthode | URI | Rôle |
|---|---|---|
| `GET` | `/api/etat` | État complet pour le tableau de bord. Rafraîchi par le client toutes les 2 s. |
| `GET` | `/api/<domaine>/modele` | Bornes, défauts, énumérés et textes d'aide d'un domaine (`wifi`, `display`, `brightness`, `air_quality`, `ha`, `mqtt`, `alerts`, `systeme`). **Construit par le firmware.** |
| `GET` | `/api/air/brut` | Les 5 grandeurs brutes et le drapeau de validité en direct, pour le wizard. |
| `POST` | `/api/<domaine>` | Écrit les réglages du domaine. |
| `GET` | `/api/wifi/reseaux` | Résultat du dernier balayage (SSID, puissance, chiffré ou non). |
| `GET` | `/api/luminosite/brut` | Mesure brute en direct, pour le wizard d'étalonnage. |
| `POST` | `/api/affichage/test` | Motif de test : `pixel`, `ligne`, `diagonale`, `plein`, `aucun`. |
| `POST` | `/api/ha/test` | Teste la liaison : `auth_ok`, `auth_invalid`, `injoignable`, avec la version de Home Assistant. *(Réalisé autrement : le wizard active la liaison et suit `/api/etat.ha.etat` — `running`, `token_rejected`, autre.)* |
| `POST` | `/api/ha/valeur/<0..7>` | Écrit une valeur suivie (tout ou rien). |
| `POST` | `/api/mqtt/decouverte` | Renvoie la découverte MQTT (bouton de `/mqtt`). |
| `POST` | `/api/notification` | **Canal d'alerte entrant** (mêmes champs que l'événement Home Assistant, `spec-home-assistant.md` §4). |
| `POST` | `/api/notification/effacer` | Retire une notification **externe** par `cle` — réservé à la **source** qui l'a levée (Node-RED, hub, script). Une alerte de règle ne se retire pas par ici : elle tombe quand sa condition retombe (EX-46). |
| `GET` | `/api/journal` | Les 64 dernières lignes de log (tampon circulaire en RAM). |
| `POST` | `/api/systeme/redemarrer` | Redémarrage. |
| `POST` | `/api/systeme/usine` | Effacement NVS + redémarrage. Demande une confirmation explicite (champ `confirme=OUI`). |
| `POST` | `/api/ota` | Image envoyée en corps brut, écrite dans l'emplacement inactif. |

### Validation : tout ou rien

Un `POST` est **validé en entier avant d'écrire quoi que ce soit**. Un champ hors
bornes ⇒ **400**, rien n'est modifié, et la réponse nomme le champ et la raison
en français :

```json
{"ok":false,"champ":"budget_ma","raison":"doit valoir entre 400 et 15000 mA"}
```

Un réglage à moitié écrit est pire qu'un réglage refusé : une configuration
d'affichage incohérente laisserait une dalle noire sans que l'utilisateur sache
pourquoi.

### Mise à jour sans redémarrage

Les réglages sont appliqués **à chaud** : identifiants WiFi (EX-24), luminosité,
scènes, règles, valeurs suivies (ce qui ferme et réouvre les abonnements
WebSocket concernés), fuseau horaire (`setenv("TZ")` + `tzset()`). Seul le
brochage demande un redémarrage, et la page le dit.

---

## 4. Deux wizards qui méritent leur détail

### 4.1 Correspondance du panneau (EX-06)

L'utilisateur n'a pas à savoir ce qu'est un serpentin. Le wizard allume, et
demande :

1. Un seul pixel allumé. « Où s'allume-t-il ? » — quatre coins à cliquer ⇒
   `origine`.
2. Les 8 premières LEDs. « Une colonne verticale, ou une ligne horizontale ? »
   ⇒ `axe_premier`.
3. Les 16 premières LEDs. « Le deuxième trait repart-il du même côté, ou du côté
   opposé ? » ⇒ `serpentin`.
4. Une diagonale complète. « La diagonale est-elle nette, ou brisée tous les
   8 pixels ? » ⇒ `largeur_module`.
5. `12:34` affiché pour confirmation finale.

### 4.2 Étalonnage de la luminosité (EX-14)

Quatre mesures, dont une qui **mesure l'auto-éclairage** de la dalle sur le
capteur au lieu de le supposer nul. Détail : `spec-luminosite.md` §4.

---

## 5. OTA (EX-53)

- Table de partitions **figée** (4 Mo) :

```
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0xC000      # 48 Ko
otadata,    data, ota,     0x15000,  0x2000
phy_init,   data, phy,     0x17000,  0x1000
ota_0,      app,  ota_0,   0x20000,  0x1E0000    # 1920 Ko
ota_1,      app,  ota_1,   0x200000, 0x1E0000    # 1920 Ko
```

- `esp_https_ota` n'est pas utilisé : l'image est **poussée** par la page web
  (`POST /api/ota`), pas tirée d'un serveur.
- Écriture dans l'emplacement inactif, vérification de l'en-tête et de la
  somme, puis `esp_ota_set_boot_partition` et redémarrage.
- **Rollback actif** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) : la nouvelle
  image doit appeler `esp_ota_mark_app_valid_cancel_rollback()` après avoir
  réussi son initialisation (dalle allumée **et** serveur web en écoute). Sinon,
  retour automatique à l'ancienne image au redémarrage suivant. Une horloge
  briquée par une mauvaise image serait à démonter du mur.
- Pendant l'OTA, la dalle affiche une **barre de progression** : c'est le seul
  retour visuel si le navigateur se ferme.

---

## 6. Dimensionnement et robustesse

| Point | Choix |
|---|---|
| Sockets simultanées | 4 (`CONFIG_LWIP_MAX_SOCKETS` en conséquence) |
| Gestionnaires d'URI | bornés, déclarés à l'init, aucun ajout dynamique |
| Corps de requête | 2 Ko max (sauf OTA, par blocs de 4 Ko) |
| En-têtes | limite par défaut d'`esp_http_server`, non relevée |
| Tampon JSON de sortie | 4 Ko statique ; une réponse qui n'y tient pas est **tronquée proprement avec un drapeau `incomplet`**, jamais un JSON invalide |
| Priorité de tâche | **inférieure** à la boucle d'affichage : une requête HTTP ne doit jamais faire sauter une trame (EX-61) |

> ⚠️ **Risque accepté en phase 1, comme sur ThermoSync** : **aucune
> authentification**. Quiconque est sur le réseau local peut lire les réglages,
> pousser une notification, et **envoyer une image OTA**. Acceptable sur un
> réseau domestique de confiance, mais c'est un choix, pas un oubli : mot de
> passe d'administration en phase 3 (`dev/todo.md`, Q6 du cadrage). Le **jeton
> Home Assistant**, lui, est déjà protégé : il s'écrit et ne se relit pas
> (EX-34).

---

## 7. Tests

| Id | Vérifie |
|---|---|
| WEB-01 | `form_field()` sur chaque formulaire : champ absent, encodage, débordement (couvert aussi par RES-10). |
| WEB-02 | Validation tout-ou-rien : un champ fautif ⇒ 400, **aucun** réglage modifié. |
| WEB-03 | Message d'erreur : champ nommé, raison en français, bornes exactes. |
| WEB-04 | `/api/<domaine>/modele` ⇄ modèle de config : **aucun paramètre orphelin** d'un côté ou de l'autre, pour les 8 domaines (règle des wizards). |
| WEB-05 | Écrivain JSON : échappement, troncature propre avec drapeau `incomplet`, jamais de JSON invalide. |
| WEB-06 | `/api/notification` : mêmes bornes et mêmes rejets que le canal Home Assistant (partage la même fonction pure). |
| WEB-07 | `/api/systeme/usine` sans `confirme=OUI` ⇒ refus. |
| WEB-08 | Motifs de test de l'affichage : chaque motif produit le tampon attendu. |

Vérifications matérielles : page lue sur iPhone, Android et PC en thème clair et
sombre ; les deux wizards joués de bout en bout sur la vraie dalle ; OTA d'une
image valide, puis d'une image **volontairement cassée** pour vérifier le
rollback ; taille de la page gzippée relevée à chaque build et consignée.
