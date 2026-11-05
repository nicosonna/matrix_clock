# Spec — Liaison au hub `app-house` (2026-11-05)

> ⚠️ **Statut : PHASE 2, proposition à valider. Rien d'implémenté, rien de
> décidé.** Ce document répond à la question posée au cadrage : *« peut-on avoir
> une logique d'alerte paramétrable via le hub ? »* — et la réponse est **oui,
> sans modifier le contrat du hub**, à une condition près (§4).
>
> Le hub ne lit pas ce dépôt. Quand la phase 2 démarre, la demande se dépose dans
> **`../data_logger_website/documentation/retours-horloge.md`**, comme
> `retours-thermosync.md` pour ThermoSync. Le brouillon de cette entrée est en
> §5, prêt à déposer. **On ne modifie rien d'autre dans le dépôt du hub.**

---

## 1. Ce que le contrat du hub permet déjà

Lecture de `../data_logger_website/documentation/contrat-ws-appareils.md`
(protocole « Appareils » v0.4, protocole 2) : l'essentiel de ce qu'il faut à une
horloge **existe déjà**.

| Besoin de l'horloge | Message du contrat | Changement côté hub |
|---|---|---|
| Recevoir une notification décidée par le hub | **`commande`** descendante `{nom, params}` — l'horloge déclare la commande `notifier` dans son descripteur, le hub la dessine et la pousse | **aucun** |
| Recevoir des réglages (luminosité, silence, budget) | **`config`** descendante, map plate de réglages déclarés | **aucun** |
| Remonter ses propres alertes au hub | **`evenement`** `{nom, gravite, message, cle, actif}` — le protocole 2 gère le **défaut qui dure** : `cle` + `actif: true` ouvre, `actif: false` ferme | **aucun** |
| Remonter des mesures (luminosité ambiante, courant estimé) | **`mesure`** | **aucun** |
| Remonter son état (heure sûre, liaison Home Assistant, alertes en cours) | **`etat`** `{etats: {…}}` — bien sous `etats`, pas à plat (correction du 2026-11-04, retours-thermosync H11) | **aucun** |
| Avoir l'heure si SNTP est injoignable | **`heure`** `{epoch}`, poussée à tout appareil qui déclare la capacité `heure` | **aucun** |
| Mise à jour du firmware ordonnée par le hub | `commande mise_a_jour_firmware {url, version, md5}`, avec `maj_auto` en opt-out | **aucun** |

> C'est le point important : **la « logique d'alerte paramétrable via le hub »
> ne demande pas de répliquer le moteur de règles dans le hub.** Le hub a déjà
> toutes les données de la maison (régulation, solaire, borne) ; qu'il **décide**
> et pousse une `commande notifier` est à la fois plus simple et plus puissant
> que de lui faire configurer 16 règles dans l'horloge. Les règles locales de
> `spec-alertes.md` restent, pour que l'horloge alerte **même si le hub est
> injoignable**.

---

## 2. Descripteur proposé

```json
{
  "type_appareil": "horloge",
  "version": "<version du firmware>",
  "version_protocole": 2,
  "icone": "clock-outline",
  "capacites": ["heure"],
  "mesures": [
    {"grandeur": "luminosite_ambiante", "unite": "", "libelle": "Lumière ambiante"},
    {"grandeur": "luminosite_appliquee", "unite": "", "libelle": "Luminosité de la dalle"},
    {"grandeur": "courant_estime", "unite": "mA", "libelle": "Courant de la dalle"}
  ],
  "etats": [
    {"nom": "heure_sure",     "libelle": "Heure synchronisée"},
    {"nom": "liaison_ha",     "libelle": "Home Assistant"},
    {"nom": "etat_reseau",    "libelle": "Réseau WiFi"},
    {"nom": "alertes_actives","libelle": "Alertes en cours"},
    {"nom": "scene",          "libelle": "Scène affichée"}
  ],
  "commandes": [
    {"nom": "notifier", "params": {
       "cle": "texte", "titre": "texte", "texte": "texte",
       "priorite": "entier", "duree_s": "entier",
       "couleur": "texte", "icone": "texte",
       "persistante": "booleen", "effacer": "booleen"}},
    {"nom": "effacer_notifications", "params": {}},
    {"nom": "test_affichage", "params": {"motif": "texte"}}
  ],
  "reglages": [
    "mode_luminosite", "lum_manuelle", "lum_min", "lum_max", "budget_ma",
    "silence_actif", "silence_debut", "silence_fin", "maj_auto"
  ]
}
```

- Le descripteur est **généré depuis le modèle de réglages** (`settings`), jamais
  écrit à la main : comme sur ThermoSync, une seule source de vérité.
- `config` reçue est **validée contre les réglages déclarés** avant application,
  et un `resultat` est renvoyé pour chaque `commande`.
- `notifier` partage **la même fonction pure de validation** que l'événement
  Home Assistant et que `POST /api/notification` : trois portes d'entrée, un
  seul décodeur testé (`spec-alertes.md`, `spec-home-assistant.md` §4).
- Seuls les réglages de **confort** sont pilotables depuis le hub. L'horloge ne
  porte aucune sécurité, donc la question ne se pose pas ici — mais la
  correspondance du panneau, le brochage et le réseau restent **locaux**, parce
  qu'un mauvais réglage poussé à distance rendrait la page web injoignable.

---

## 3. Transport

Même chemin que ThermoSync, et c'est l'essentiel du travail de la phase 2 :

1. Enrôlement **HTTP** : `POST /api/appareils/enrolement` `{id_materiel, type_appareil}`
   → `{appareil_id, jeton, code_appairage, liee}`. `429` ⇒ respecter `Retry-After`.
   `409 raison:"reinitialisation_a_confirmer"` ⇒ **retenter vite (30–60 s)**, pas
   de backoff long.
2. **WSS** `GET /ws/appareil`, `Authorization: Bearer <jeton>`, **sans en-tête
   `Origin`** (un `Origin` présent est refusé en 403).
3. Enveloppe `{type, id, ts, payload}`, `id` **monotone par appareil, jamais remis
   à zéro** ⇒ compteur **persisté en NVS**, pas en RAM (un redémarrage ne doit pas
   rejouer des `id`).
4. Acquitter **dès réception**, avant traitement.
5. `spool` borné en RAM pour les mesures émises hors ligne, avec perte comptée et
   remontée dans `etat.mesures_perdues`.
6. **Le code d'appairage s'affiche sur la dalle** tant que `liee` est faux, et il
   est **persisté** : un code régénéré (message `appairage` avec
   `code_appairage`) remplace le précédent. C'est un avantage concret d'une
   horloge : pas besoin de câble série pour lire son code.

> ⚠️ Le point d'entrée du hub est **exposé sur Internet** : TLS obligatoire
> (`wss`, paquet de certificats), et tout ce qui descend est une **entrée non
> sûre**, décodée de façon bornée comme le WebSocket Home Assistant
> (`spec-home-assistant.md` §8).

---

## 4. La seule vraie question pour le hub

Pousser une `commande notifier` **à la main** depuis le panneau de l'appareil
fonctionne avec le contrat actuel. Ce que l'horloge ne sait pas, c'est si le hub
peut la pousser **automatiquement**, sur condition.

Trois issues possibles, par ordre de préférence :

| # | Issue | Coût |
|---|---|---|
| A | Le hub possède (ou veut) un mécanisme d'**automatisation** (« si la mesure X de l'appareil Y franchit Z, envoyer la commande `notifier` à l'horloge »). L'horloge n'a **rien** à changer. | côté hub |
| B | Le hub n'en veut pas : **Home Assistant** reste le cerveau (il a déjà ses automatisations), et le hub ne sert qu'à l'appairage, aux réglages et à l'historique. L'horloge n'a rien à changer non plus. | nul |
| C | Les règles sont **éditées dans le hub et exécutées dans l'horloge** : il faudrait descendre 16 règles structurées, ce que la map **plate** de `config` ne permet pas proprement (ce serait ~220 réglages `regle3_seuil`, `regle3_source`, …). Demanderait au hub un réglage structuré ou un groupe répété. | **côté hub, non trivial** |

**Position de l'horloge : A si le hub a déjà de l'automatisation, sinon B.**
On ne demande **pas** C : la configuration des règles reste sur la page locale,
où elle est déjà spécifiée, testée et accessible hors Internet.

---

## 5. Brouillon de l'entrée pour `retours-horloge.md`

> À déposer **quand la phase 2 démarre**, pas avant. Rédigée pour être
> **autonome** : la session du hub ne lit que ce fichier.

```markdown
## 2026-xx-xx — Nouvel appareil « horloge » — INFORMATION + 1 question

### Ce que c'est
Une horloge matricielle WS2812 8×32 sur ESP32, firmware ESP-IDF, qui affiche
l'heure, des capteurs lus dans Home Assistant, et des notifications. Elle
s'enrôlerait comme appareil générique du SDK (`type_appareil: "horloge"`).

### Ce qu'elle demande au hub : rien de nouveau
Son descripteur n'utilise que l'existant (`mesure`, `etat`, `evenement` avec
`cle`/`actif`, `commande`, `config`, capacité `heure`). Descripteur complet :
voir le tableau ci-dessous. Aucune extension du contrat n'est nécessaire pour
l'appairage, les réglages, les mesures et les alertes montantes.

### La question (une seule)
Le hub peut-il pousser une `commande` **automatiquement, sur condition** —
« si la mesure `t_exterieur` de l'appareil ThermoSync passe sous 0 °C, envoyer
`notifier {cle:"gel", texte:"GEL", priorite:2}` à l'horloge » ?

- **Si oui** (mécanisme d'automatisation existant ou prévu) : dis-nous juste
  comment on le déclare, l'horloge n'a rien à changer.
- **Si non, et que ce n'est pas au programme** : dis-le aussi, c'est une réponse
  parfaitement acceptable. Home Assistant tient déjà ce rôle chez l'utilisateur,
  et le hub resterait l'appairage, les réglages et l'historique. **On ne demande
  pas** de porter un moteur de règles dans le hub.

### Ce qu'on ne demande pas
Pas de réglage structuré ni de groupe répété dans `config` pour descendre les
16 règles de l'horloge : elles restent configurées sur sa page locale.

### Tableau d'état
| Point | Statut |
|---|---|
| Descripteur `horloge` dans l'existant | INFORMATION |
| Automatisation côté hub pour pousser `notifier` | **QUESTION** |
| Règles structurées dans `config` | NON DEMANDÉ |
```

---

## 6. Non-buts de la phase 2

- **Pas de MQTT vers le hub.** Le hub l'a rejeté pour ThermoSync (protocole
  Appareils en WSS) ; on ne redemande pas.
- **Pas de double source de vérité** sur les règles : soit le hub décide et
  pousse (A), soit il ne décide pas (B). Jamais les deux à moitié.
- **Pas de dépendance au hub pour fonctionner.** Hub injoignable ⇒ l'horloge
  affiche l'heure, lit Home Assistant et applique ses règles locales. Comme pour
  Home Assistant, la liaison est un **plus**, jamais un prérequis.
- **Pas de vocabulaire devancé** : l'horloge n'invente pas de champ qui ne soit
  pas écrit dans `contrat-ws-appareils.md`.

---

## 7. Tests (quand la phase 2 démarre)

Sur le modèle de `spec-test-hub.md` de ThermoSync : un **hub de test en Docker**
construit depuis le dépôt voisin, face à une horloge **émulée** puis à la vraie
carte. Identifiants `HUB-01…` à écrire avec le composant. Rien à écrire
maintenant — mais le plan de test porte déjà la ligne, marquée `PHASE 2`.
