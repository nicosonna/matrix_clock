# Spec — Liaison Home Assistant (WebSocket) (2026-11-05)

> **Statut : spécifié, rien d'implémenté.** Composant pur `homeassistant`,
> adaptateur `clock_homeassistant`.

---

## 1. Principe et pourquoi ce choix

L'horloge lit des valeurs de Home Assistant par l'**API WebSocket**
(`ws://<hôte>:8123/api/websocket`) avec un **jeton longue durée**.

Pourquoi le WebSocket plutôt que le REST ou MQTT :

| Option | Verdict |
|---|---|
| **WebSocket + `render_template`** (retenu) | Chaque valeur suivie est un **abonnement** qui rend la valeur **tout de suite** puis **à chaque changement**. Aucune scrutation, messages de quelques dizaines d'octets, aucune configuration côté Home Assistant, et le même canal transporte les notifications. |
| `get_states` en WebSocket | **Écarté** : renvoie l'état **complet** de Home Assistant, plusieurs centaines de kilooctets. Impossible sur un ESP32 sans PSRAM. |
| REST `GET /api/states/<entité>` | Simple, mais c'est de la **scrutation** : latence, trafic, et il faut un deuxième canal pour les notifications. |
| MQTT pour **lire** | Exige `mqtt_statestream` ou des automatisations de publication côté Home Assistant. **Écarté pour la lecture.** |
| MQTT pour **publier** (retenu, §6 bis) | Les capteurs **locaux** (ENS160 + AHT21) partent vers Home Assistant par **découverte MQTT**, comme la borne (`mqtt_bridge`) et ThermoSync : aucun YAML côté Home Assistant, et l'horloge devient un **appareil** avec ses entités. |

Deux transports, donc, chacun dans un seul sens : **WebSocket pour lire**
Home Assistant, **MQTT pour lui publier** ce que l'horloge mesure elle-même.

La liaison est **locale**, en **lecture seule**, et **sans effet sur
l'affichage de l'heure** : Home Assistant éteint, l'horloge continue à afficher
l'heure et à évaluer ses règles sur ce qu'elle a (EX-61).

---

## 2. Déroulé du dialogue

```
          ESP32                                   Home Assistant
            │ ── ouverture ws://hôte:8123/api/websocket ──▶
            │ ◀── {"type":"auth_required","ha_version":"…"}
            │ ── {"type":"auth","access_token":"<jeton>"} ──▶
            │ ◀── {"type":"auth_ok","ha_version":"…"}        (ou auth_invalid)
            │
            │ ── {"id":1,"type":"render_template",
            │      "template":"{{ states('sensor.exterieur') }}",
            │      "report_errors":true} ──▶
            │ ◀── {"id":1,"type":"result","success":true}
            │ ◀── {"id":1,"type":"event","event":{"result":"3.4", …}}   ← valeur initiale
            │ ◀── {"id":1,"type":"event","event":{"result":"3.2", …}}   ← à chaque changement
            │
            │ ── {"id":9,"type":"subscribe_events",
            │      "event_type":"horloge_notification"} ──▶
            │ ◀── {"id":9,"type":"result","success":true}
            │ ◀── {"id":9,"type":"event","event":{"event_type":"horloge_notification",
            │        "data":{…}}}
            │
            │ ── {"id":10,"type":"ping"} ──▶   (toutes les 30 s)
            │ ◀── {"id":10,"type":"pong"}
```

Machine d'état pure (`homeassistant::ClientHa`) :

`Repos → Connexion → Authentification → Abonnements → EnMarche → (Erreur) → Attente → Connexion …`

- Les `id` sont **croissants à partir de 1 par connexion** (exigence de Home
  Assistant). Une reconnexion repart à 1 et **réabonne tout**.
- `auth_invalid` ⇒ état `JetonRefuse` : on **arrête** de reconnecter en boucle
  (délai porté à 5 min) et on l'affiche sur la page. Marteler un mot de passe
  faux n'a jamais aidé personne.
- `report_errors: true` : un gabarit fautif renvoie un `event` avec `error`, qu'on
  **borne à 96 caractères** et qu'on affiche sur la page web à côté de la valeur.
  Sans ça un gabarit mal écrit resterait muet pour toujours, sans explication.

### Reconnexion et surveillance

| Réglage | Défaut | Bornes |
|---|---|---|
| `attente_ms` | 5 000, ×2, plafond 60 000 | — |
| `ping_s` | 30 | 10–300 |
| `pong_ms` | 10 000 | 2 000–30 000 |
| `peremption_s` | 300 | 30–3600 |

Deux surveillances superposées : le *keepalive* du transport
(`esp_websocket_client`) détecte une socket morte, le **ping applicatif** détecte
un Home Assistant qui ne répond plus alors que la socket tient. Pas de `pong`
dans `pong_ms` ⇒ fermeture et reconnexion.

---

## 3. Valeurs suivies

Au plus **8** valeurs (`kValeursMax = 8`), `std::array`, aucune allocation.

| Champ | Type | Bornes | Note |
|---|---|---|---|
| `nom` | texte | 16 car. | Libellé sur la page et dans les alertes. |
| `mode` | `Entite` / `Gabarit` | — | — |
| `entite` | texte | 64 car., `[a-z0-9_.]` | Mode simple : `sensor.temperature_exterieure`. |
| `attribut` | texte | 32 car. | Optionnel : `state_attr()` au lieu de `states()`. |
| `gabarit` | texte | 128 car., ASCII imprimable | Mode expert, écrit tel quel (échappé JSON). |
| `type` | `Nombre` / `Texte` / `Booleen` | — | Détermine le décodage. |
| `decimales` | 0–2 | | `Nombre` : stocké en entier × 10^décimales. **Aucun flottant.** |
| `unite` | texte | 8 car. | `°C`, `%`, `W`. Affichée telle quelle. |
| `icone` | énuméré | — | Icône 8×8 de la scène `Valeur` (thermomètre, goutte, éclair, porte, vent, …). |
| `peremption_s` | 30–3600 | 300 | Au-delà, la valeur devient **invalide**. |

Le gabarit envoyé est construit par le firmware :

| Mode | Gabarit émis |
|---|---|
| `Entite` sans attribut | `{{ states('<entite>') }}` |
| `Entite` avec attribut | `{{ state_attr('<entite>','<attribut>') }}` |
| `Gabarit` | le texte tel quel |

### Décodage du résultat

Le résultat arrive en **texte** (`"3.4"`, `"on"`, `"unavailable"`), au plus
32 caractères.

| Résultat | `Nombre` | `Texte` | `Booleen` |
|---|---|---|---|
| `"3.4"` | 34 (1 décimale) | `"3.4"` | invalide |
| `"on"`, `"true"`, `"home"`, `"open"` | invalide | `"on"` | vrai |
| `"off"`, `"false"`, `"not_home"`, `"closed"` | invalide | `"off"` | faux |
| `"unknown"`, `"unavailable"`, `"none"`, `""`, `null` | **invalide** | **invalide** | **invalide** |
| non numérique, ou hors de ±2 000 000 000 | **invalide** | — | — |

Une valeur invalide est **invalide**, pas 0 : la scène `Valeur` affiche `--` et
aucune règle d'alerte sur cette source ne déclenche (EX-44).

### Fraîcheur (EX-32)

Chaque valeur porte `recue_ms`. Au-delà de `peremption_s` sans nouvel `event`,
elle **devient invalide**. C'est volontairement sévère : un capteur dont la
température ne bouge pas pendant 10 min n'émet pas de nouvel `event`
(`render_template` ne rend que sur **changement**), donc `peremption_s` doit
rester large (300 s par défaut) et il faut le savoir. Pour une valeur qui bouge
rarement, mettre une péremption longue, ou un gabarit qui inclut un horodatage.

> Dette identifiée : une valeur stable longtemps est indiscernable d'une liaison
> morte. Le ping applicatif tranche (si la liaison vit, la valeur est bonne) :
> la péremption n'est donc appliquée que **si la liaison est tombée** depuis
> `peremption_s`. Détail à figer à l'implémentation, test HA-12.

---

## 4. Canal de notification entrant (EX-40)

Abonnement `subscribe_events` sur l'événement **`horloge_notification`**.
Côté Home Assistant, une automatisation :

```yaml
automation:
  - alias: "Horloge — garage ouvert"
    trigger:
      - platform: state
        entity_id: binary_sensor.porte_garage
        to: "on"
        for: "00:10:00"
    action:
      - event: horloge_notification
        event_data:
          cle: garage_ouvert
          titre: "GARAGE"
          texte: "Porte ouverte depuis 10 min"
          priorite: 2
          duree_s: 60
          couleur: "#ff8800"
          icone: porte
          persistante: true
```

| Champ | Type | Bornes | Défaut |
|---|---|---|---|
| `cle` | texte | 24 car., `[a-z0-9_]` | obligatoire — sert au **dédoublonnage** et à l'effacement |
| `titre` | texte | 16 car. | vide |
| `texte` | texte | 128 car. | vide |
| `priorite` | 0–3 | | 1 |
| `duree_s` | 1–3600 | | 20 |
| `couleur` | `#RRGGBB` | | couleur de la priorité |
| `icone` | énuméré | | aucune |
| `persistante` | booléen | | faux — à vrai, la notification est **tenue** : rejouée toutes les `rappel_s` jusqu'à ce que **la source** envoie `effacer`. Personne ne l'acquitte à la main. |
| `rappel_s` | 10–3600 | | 300 — intervalle des passages d'une tenue |
| `effacer` | booléen | | faux — à vrai, **retire** la notification de cette `cle` : c'est le seul moyen de faire tomber une tenue externe, et il appartient au système qui l'a levée |

Un champ hors bornes est **remplacé par son défaut** et compté dans les
diagnostics ; un `cle` absent ou invalide fait **rejeter** tout le message (on ne
peut ni dédoublonner ni effacer sans clé). Le rendu est décidé par `alerts` +
`display`, pas ici (`spec-alertes.md`).

Le même contenu est acceptable sur `POST /api/notification` du serveur web local,
pour Node-RED, un script, ou le hub en phase 2 (`spec-serveur-web.md`).

---

## 5. Réglages de la liaison (page `/ha`)

Bornes lues sur `/api/ha/modele` — **le firmware est la seule source de vérité**.

| Réglage | Défaut | Règle |
|---|---|---|
| Liaison active | non | — |
| Schéma | `ws` | `ws` ou `wss`. En `wss`, vérification par le **paquet de certificats** d'ESP-IDF : un certificat auto-signé **n'est pas accepté** en phase 1. |
| Hôte | — | IP ou nom DNS, `[A-Za-z0-9.-]`, 64 car. **Les noms `.local` ne sont pas résolus** (pas de résolution mDNS) : mettre l'**adresse IP** de Home Assistant. |
| Port | 8123 | 1–65535 |
| Jeton longue durée | — | 512 car. max, ASCII imprimable. **S'écrit mais ne se relit pas** depuis la page (EX-34). Stocké en NVS (`clk_ha`), jamais dans le dépôt. |
| Valeurs suivies | — | Jusqu'à 8, voir §3. |
| Notifications actives | oui | Abonnement à `horloge_notification`. |

> ⚠️ **Risque accepté en phase 1** : en `ws`, le jeton circule **en clair** sur le
> réseau local. C'est le même choix que MQTT sans TLS sur ThermoSync. Créer un
> **utilisateur Home Assistant dédié** à l'horloge et lui faire le jeton, pour
> pouvoir le révoquer seul. `wss` ou un TLS complet : phase 3, `dev/todo.md`.

**Wizard en 4 étapes** : hôte et port → jeton (avec un test de connexion qui dit
`auth_ok` / `auth_invalid` / injoignable) → choix des valeurs (saisie de l'entité,
aperçu de la valeur rendue **en direct** avant d'enregistrer) → notifications.

---

## 6. Météo de repli (EX-35)

Quand Home Assistant est absent, ou qu'aucune valeur « extérieur » n'est
configurée, une température extérieure peut venir d'**Open-Meteo** — pas de clé,
pas de compte :

```
GET https://api.open-meteo.com/v1/forecast?latitude=<lat>&longitude=<lon>&current=temperature_2m
```

| Réglage | Défaut | Bornes |
|---|---|---|
| Actif | **non** | — |
| Latitude / longitude | — | ±90 / ±180, 4 décimales, en entier (1/10000ᵉ de degré) |
| Période | 600 s | 300–3600 |

HTTPS avec le paquet de certificats d'ESP-IDF. Réponse décodée par le **même
lecteur JSON sans allocation** que le WebSocket. Un échec laisse la valeur
**invalide**, jamais la précédente présentée comme fraîche. C'est la **seule**
sortie Internet du firmware, et elle est désactivée par défaut.

---

## 6 bis. Publication MQTT des capteurs locaux (EX-36 → EX-38)

> Ajout du 2026-11-05, à la demande de Nicolas : « renvoyer la valeur des deux
> capteurs I²C sur Home Assistant via MQTT comme sur la ev_charger ».

La borne le fait en Arduino (`utils_esp/mqttClient/mqtt_bridge.hpp` :
`publishSensorConfig`, `sendMqttSensorValue`). En ESP-IDF, l'équivalent **pur
et testé** existe déjà dans ThermoSync (`components/domotique/home_assistant.hpp`
et `mqtt_config.hpp`) : on le **reprend**, en remplaçant `thermosync` par
`horloge` et les entités de régulation par celles du capteur d'air. Même
découverte, mêmes conventions, même page `/mqtt`.

### Topics

`<id>` = adresse MAC en hexadécimal minuscule (`1020ba5c7df4`).

| Topic | Sens | Contenu |
|---|---|---|
| `<prefixe>/<plateforme>/horloge_<id>/<objet>/config` | horloge → HA, **retenu** | Description d'une entité, toutes rattachées à l'appareil **Horloge** (fabricant, modèle, version, lien vers la page locale, MAC). |
| `horloge/<id>/disponible` | horloge → HA, **retenu** | `online` ; `offline` posé en **testament** (LWT) chez le broker, et publié avant une désactivation. |
| `horloge/<id>/etat` | horloge → HA, retenu | **Un seul** JSON avec toutes les valeurs, toutes les `periode_s` (défaut 30 s) **et** à chaque changement de niveau d'air. Chaque entité lit sa clé par `value_template`. |
| `<prefixe>/status` | HA → horloge | `online` au redémarrage de Home Assistant : l'horloge renvoie sa découverte. |

### Entités (phase 1)

| Entité | Plateforme | Classe | Unité, `state_class` | Clé JSON |
|---|---|---|---|---|
| Température | `sensor` | `temperature` | °C, `measurement` | `temperature` (1 décimale) |
| Humidité | `sensor` | `humidity` | %, `measurement` | `humidite` (1 décimale) |
| eCO2 | `sensor` | `carbon_dioxide` | ppm, `measurement` | `eco2` |
| TVOC | `sensor` | `volatile_organic_compounds_parts` | ppb, `measurement` | `tvoc` |
| Indice de qualité d'air | `sensor` | `aqi` | —, `measurement` | `aqi` (1–5) |
| Niveau d'air | `sensor` | `enum` : `excellent`, `bon`, `moyen`, `mauvais`, `tres_mauvais` | — | `niveau` |
| Capteur d'air | `binary_sensor` | `problem` (diagnostic) | — | `capteur_defaut` |
| Préchauffage | `binary_sensor` | `running` (diagnostic) | — | `prechauffage` |
| Lumière ambiante | `sensor` | — (diagnostic) | —, `measurement` | `luminosite_brute` |
| Heure synchronisée | `binary_sensor` | `connectivity` (diagnostic) | — | `heure_sure` |

Dix messages de découverte, un message d'état. La liste est **générée depuis la
table** `kEntities` de `homeassistant/mqtt.cpp` (`mqtt_entity_count()`,
`mqtt_discovery()`), pas recopiée : ajouter une grandeur = une ligne dans la
table **et** sa clé dans `mqtt_state_json()`, et le test MQ-03 vérifie que
découverte et état restent cohérents. La politique de publication (période,
changement de niveau, renvoi, `offline` avant arrêt) est le `MqttPublisher`
pur, que la tâche `mqtt` de `clock_homeassistant` ne fait qu'exécuter.

### Valeurs invalides

Une grandeur en préchauffage, un capteur absent, un CRC faux : la clé est
publiée **`null`** et l'entité passe **inconnue** dans Home Assistant. **Jamais**
une valeur inventée ni périmée présentée comme fraîche — c'est la même règle que
pour l'affichage, et c'est ce qui permet à une automatisation Home Assistant de
ne pas déclencher « aérer » sur un capteur qui chauffe encore.

### Réglages (page `/mqtt`, reprise de ThermoSync)

Bornes lues sur `/api/mqtt/modele`. Validation **reprise telle quelle** de
`mqtt_config.hpp` (`MqttConfig`, `MqttStatus`).

| Réglage | Défaut | Règle |
|---|---|---|
| Liaison active | non | — |
| Adresse du broker | — | IP ou nom DNS, `[A-Za-z0-9.-]`, 64 car. Pas de `.local` (pas de résolution mDNS). |
| Port | 1883 | 1–65535. Pas de TLS (réseau local). |
| Utilisateur, mot de passe | vides | ASCII imprimable, 64 car. ; un mot de passe exige un utilisateur. Le mot de passe **s'écrit mais ne se relit pas**. |
| Préfixe de découverte | `homeassistant` | `[a-z0-9_-/]`, 32 car. |
| `periode_s` | 30 | 5–600 |

NVS `clk_mqtt`, jamais dans le dépôt. Côté Home Assistant : le broker
**Mosquitto** (module complémentaire officiel), un **utilisateur dédié** à
l'horloge, l'intégration MQTT avec la découverte activée (par défaut).

**Wizard en trois étapes** (celui de ThermoSync) : broker → utilisateur →
découverte, avec l'état de la liaison en tête et un bouton « renvoyer la
découverte ».

### Transport (`clock_homeassistant`)

`esp-mqtt`, client unique créé au démarrage ; testament configuré à la
connexion ; découverte publiée en QoS 1 retenue, état en QoS 0 retenu ;
reconnexion gérée par le client. Hors ligne, l'horloge **n'empile rien** : le
prochain état remplace les précédents, ce sont des mesures instantanées.

### Ce que ça change pour le §7

Avec la découverte, l'horloge **est** un appareil dans Home Assistant. Lui
ajouter des entités de **commande** (luminosité en `number`, extinction en
`switch`, scène en `select`, message en `text`) n'est plus qu'une extension du
même composant, reportée en phase 3 pour garder la phase 1 courte — plus une
question d'architecture.

---

## 7. Ce que le WebSocket ne donne pas — et comment faire quand même

Le WebSocket donne la **lecture** et les **événements**. La découverte MQTT
(§6 bis) fait apparaître l'horloge comme un **appareil** avec ses capteurs —
mais en phase 1 sans entité de **commande** : on ne verra pas encore un curseur
de luminosité ni un interrupteur « horloge » dans l'interface.

Deux façons de piloter l'horloge depuis Home Assistant en attendant :

1. **Phase 1, par l'API HTTP de l'horloge** — trois lignes de YAML :

```yaml
rest_command:
  horloge_luminosite:
    url: "http://horloge.local/api/luminosite"
    method: POST
    content_type: "application/x-www-form-urlencoded"
    payload: "mode=Manuel&lum_manuelle={{ valeur }}"
  horloge_message:
    url: "http://horloge.local/api/notification"
    method: POST
    content_type: "application/x-www-form-urlencoded"
    payload: "cle=ha&texte={{ texte }}&priorite=1&duree_s=30"
```

2. **Phase 3, par des entités de commande MQTT** — luminosité en `number`,
   extinction en `switch`, scène en `select`, message en `text`, ajoutées à la
   découverte déjà en place (§6 bis), avec les commandes sur
   `horloge/<id>/<réglage>/set` comme `thermosync/<id>/chauffage/<réglage>/set`.
   Noté dans `dev/todo.md`.

---

## 8. Robustesse du décodage

Tout ce qui arrive du WebSocket est une **entrée non sûre** :

- Réassemblage des trames fragmentées dans un tampon **statique de 2048 octets**
  (`esp_websocket_client` livre `payload_offset` / `payload_len` / `data_len`).
  Un message plus long est **abandonné avec un log**, jamais tronqué puis
  interprété — un JSON tronqué peut se décoder en autre chose.
- Lecture par **chemin de clés sans allocation** (`homeassistant/json_reader.hpp`,
  reprise de `energie/json_path` de ThermoSync) : pas d'arbre en mémoire, pas de
  récursion, profondeur bornée.
- Nombres en **virgule fixe entière**, bornés ; un nombre hors bornes rend la
  valeur invalide.
- Chaînes **tronquées à la copie** vers des tampons dimensionnés à la compilation.
- ⚠️ **Ne jamais nommer un composant `json`** : ESP-IDF en a un (cJSON) qu'un
  homonyme projet remplacerait silencieusement, et tout IDF cesserait de
  compiler. Le lecteur vit **dans** `homeassistant`.

---

## 9. Tests

Tests host du composant pur `homeassistant` (doublure de transport injectée) :

| Id | Vérifie |
|---|---|
| HA-01 | Déroulé complet : `auth_required` → `auth` → `auth_ok` → abonnements → `EnMarche`. |
| HA-02 | `auth_invalid` ⇒ `JetonRefuse`, délai porté à 5 min, pas de martèlement. |
| HA-03 | Construction du gabarit : entité seule, entité + attribut, gabarit expert, échappement JSON des apostrophes et guillemets. |
| HA-04 | `id` croissants à partir de 1, repartant à 1 après reconnexion, tous les abonnements rejoués. |
| HA-05 | `event` `render_template` : valeur initiale puis changements ; association correcte `id` → valeur. |
| HA-06 | Décodage `Nombre` : 0, 1 et 2 décimales, négatif, non numérique, hors bornes, `unavailable`, `unknown`, `null`, vide. |
| HA-07 | Décodage `Booleen` et `Texte`, y compris les alias (`home`, `open`, `closed`). |
| HA-08 | `report_errors` : un gabarit fautif remonte une erreur bornée à 96 caractères, la valeur reste invalide. |
| HA-09 | Trame fragmentée réassemblée ; trame > 2048 octets abandonnée avec compteur, **pas** interprétée. |
| HA-10 | JSON malformé, tronqué, profondeur excessive, clé absente ⇒ message ignoré, état inchangé. |
| HA-11 | Ping sans `pong` dans `pong_ms` ⇒ reconnexion ; délai croissant 5/10/20/40/60 s plafonné. |
| HA-12 | Péremption : valeur invalidée après `peremption_s` **liaison tombée**, et **non** invalidée si la liaison vit et que la valeur est simplement stable. |
| HA-13 | Notification entrante : champs valides, champ hors bornes remplacé par son défaut, `cle` absente ⇒ message rejeté, `effacer` retire la bonne notification. |
| HA-14 | Décodage de la réponse Open-Meteo (réponse réelle en `test/fixtures/`), réponse amputée, valeur absente. |
| HA-15 | Validation des réglages : hôte, port, longueur du jeton, entité, gabarit, latitude/longitude ; repli sur les défauts. |

Tests host de `homeassistant::mqtt` (repris et adaptés de ThermoSync, HA-01..12
de son plan) :

| Id | Vérifie |
|---|---|
| MQ-01 | Topics : `disponible`, `etat`, `config` de chaque entité, `<prefixe>/status`, avec l'`id` MAC ; débordement de tampon refusé. |
| MQ-02 | Message de découverte de chaque entité : plateforme, classe, unité, `state_class`, `value_template`, `availability`, bloc `device` commun ; JSON valide. |
| MQ-03 | **Cohérence découverte ⇄ état** : chaque `value_template` lit une clé que l'état publie, et chaque clé de l'état est lue par une entité. |
| MQ-04 | État : grandeurs valides publiées avec les bonnes décimales ; invalides, préchauffage, capteur absent ⇒ **`null`**. |
| MQ-05 | `niveau` énuméré : les cinq valeurs, et `null` en préchauffage. |
| MQ-06 | Publication à `periode_s` **et** sur changement de niveau, pas sur chaque pas de mesure. |
| MQ-07 | `<prefixe>/status = online` ⇒ découverte renvoyée. |
| MQ-08 | `MqttConfig` : tous les cas de `MqttStatus` (hôte vide / invalide, port nul, mot de passe sans utilisateur, préfixe invalide), repli sur les défauts. |
| MQ-09 | Modèle `/api/mqtt/modele` ⇄ `MqttConfig` : aucun champ orphelin. |
| MQ-10 | Désactivation ⇒ `offline` publié avant la déconnexion. |

Vérifications matérielles : entités visibles dans le Home Assistant **de test en
Docker** (jamais celui de la maison depuis les tests), valeurs confrontées à la
page `/air`, entité `inconnue` pendant le préchauffage, `offline` à la coupure de
l'horloge (testament), redémarrage de Home Assistant ⇒ entités retrouvées.

Vérifications matérielles : liaison au Home Assistant **de la maison** en lecture
seule, valeur affichée confrontée à l'interface de Home Assistant, Home Assistant
redémarré (reconnexion), jeton révoqué (`JetonRefuse` visible sur la page),
notification poussée par une vraie automatisation.

> **Banc de test en Docker** — comme sur ThermoSync, on ne pilote **jamais** le
> Home Assistant de la maison depuis les tests : une campagne
> `campagne_ha_docker.py` monte un Home Assistant de test où les tests ont tous
> les droits. À écrire en même temps que le composant (`dev/todo.md`).
