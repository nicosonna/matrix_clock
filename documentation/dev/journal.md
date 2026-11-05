# Journal de développement — Horloge

> Récent en haut. Une entrée par modification non triviale : date, quoi,
> pourquoi, état.

---

## 2026-11-05 (suite 14) — Moteur de règles d'alerte, file de notifications, heures silencieuses, page `/alertes`

**Quoi.** Étape 10 de la phase 1 (`spec-alertes.md`), composant pur `alerts` :

- `queue.hpp/.cpp` : **file de 16** dédoublonnée par clé (remplacement sans
  changer le rang, expiration repoussée), éviction de la plus basse priorité
  la plus ancienne **seulement** si strictement inférieure, sinon rejet
  compté ; tenues (règle levée, externe `persistante`) rejouées toutes les
  `rappel_s`, ponctuelles jouées une fois ; **heures silencieuses** (fenêtre
  franchissant minuit, jamais silencieux sans heure sûre) qui suspendent les
  passages sans rien perdre, P3 `outrepasse_silence` exempte ; rendu par
  priorité (P0 attend la scène, P2 clignote 1 Hz, P3 2 Hz + plancher 64).
- `rules.hpp/.cpp` : **17 sources** (8 valeurs HA, heure, liaison, jeton,
  réseau, bail, donnée périmée, LDR, écrêtage, 5 grandeurs d'air, capteur
  en défaut, préchauffage, compensation), 9 comparaisons, hystérésis selon
  le sens, confirmation remise à zéro à la moindre coupure, `Unavailable`
  dès que la source ne vaut rien (liaison désactivée ⇒ `LiaisonHaPerdue`
  indisponible, pas « levée »), gabarit `{n}` `{v}` `{u}`, synchronisation
  avec la file : la règle **seule** pousse et retire sa notification
  `regle_<i>` (un `clear` extérieur est annulé au pas suivant). Règles par
  défaut de la spec (8 internes actives, GEL / CANICULE / CO2 / HUMIDITE
  prêtes mais inactives).
- `settings` : domaine **alertes** (silence + 16 règles par
  `POST /api/alertes/regle/<i>`), seuil et hystérésis saisis en **décimal
  de l'unité de la source** (refusés s'ils ont plus de décimales qu'elle,
  remis à zéro quand la source change), couleur `#RRGGBB` ou vide, blob NVS
  `alerts` v1. Tampon JSON porté à 8 Ko et **partagé** entre tous les GET
  (13 × 4 Ko de statiques en moins).
- `app_main` : file + moteur sous un **mutex** (portes HTTP et WebSocket),
  pas de 1 Hz avec les entrées réelles, passage suivant choisi par la file
  (priorité, âge, silence), rendu : couleur, clignotement, défilement ou
  bandeau avec icône, plancher relevé ; **`!` dans la zone d'icône** tant
  qu'une alerte est tenue (prend le pas sur le logo air) ; `/api/etat.alertes`
  (règles avec état et valeur, file, silence, rejets).
- Page : onglet **Alertes** (file **listée, sans bouton**, règles en direct,
  silence, 16 cartes depuis `modele.regle`, widgets `decimal` et `couleur`),
  **wizard en 5 étapes** (source avec valeur actuelle → condition avec
  aperçu « se lèverait / ne se lèverait pas » → stabilité → rendu avec
  aperçu du texte → récapitulatif en français et choix de l'emplacement).
  Le tableau de bord compte les alertes en cours.

**Résultats.** Tests host : **12 100+ vérifications, 0 échec, couverture
96,7 %** (5 012 lignes ; `rules.cpp` 95,3 %, `queue.cpp` 98,9 %). Image
1 334 Ko (32 % libre). Sur la carte (OTA) : à 23 h le silence est actif,
`AERER` en `confirmation` à AQI 4 puis `surveille` à AQI 3 ; silence coupé
le temps du banc : ponctuelle P1 ⇒ scène `Alerte` puis `Heure` ; tenue P2
⇒ passage puis `tenues:true` jusqu'à `effacer` par sa source ; règle
`CO2 >= 900` posée par l'API ⇒ `levee`, `CO2 906 ppm` tenu ; `seuil=0.55`
refusé par son nom ; réglages remis (règle 12 inactive, silence actif).

**Reste.** HW-ALR-01 (sonde refroidie), HW-ALR-03 (une nuit), HW-AIR-01
(aucun `AERER` au préchauffage, logique couverte par AIR-20), plan de test
final consolidé.

---

## 2026-11-05 (suite 13) — Publication MQTT vers Home Assistant : découverte, état `null`, page `/mqtt`, broker de banc

**Quoi.** Étape 9 bis de la phase 1 (`spec-home-assistant.md` §6 bis) :

- `homeassistant/mqtt.hpp/.cpp` (pur, repris de ThermoSync
  `domotique/home_assistant.hpp` + `mqtt_config.hpp`, traduit) :
  `MqttConfig` + `validate()` (9 statuts, raisons pour la page),
  `DeviceIdentity` (MAC, version, carte, URL locale), topics
  `horloge/<id>/disponible` et `/etat`, **10 entités** générées depuis une
  table (température, humidité, eCO2, TVOC, AQI, niveau `enum`, capteur en
  défaut, préchauffage, lumière brute, heure sûre), **un seul** état JSON où
  tout ce qui est invalide (préchauffage, première heure, défaut, absent,
  capteur désactivé, LDR, CRC) est **`null`** — un niveau confirmé mais
  dont l'air est invalide est `null` aussi. `MqttPublisher` : machine de
  décision (connect / online / découverte / état / offline / disconnect)
  avec période, publication immédiate sur changement de niveau, renvoi sur
  `<prefixe>/status` = `online` ou sur bouton, `offline` **avant** tout arrêt.
- `settings` : domaine **mqtt** (8 champs, mot de passe écrit seulement,
  `effacer_mdp`, préfixe vide = défaut), modèle `/api/mqtt/modele`, blob NVS
  `mqtt` v1 dans `clk_cfg`.
- `clock_homeassistant/mqtt_link` : tâche `mqtt` (cœur 0, 250 ms, 4 Ko) sur
  `esp-mqtt` — testament `offline` retenu QoS 1, keepalive 30 s, reconnexion
  par le client, découverte QoS 1 retenue, état QoS 0 retenu, abonnement
  unique à `<prefixe>/status`. `app_main` fournit l'état (air, LDR, heure)
  et `/api/etat.mqtt` ; hook `mqtt_discovery` pour `POST /api/mqtt/decouverte`.
- Page : onglet **MQTT** (tuiles, bouton « renvoyer la découverte »,
  formulaires broker / avancés) et **wizard** en 3 étapes (broker →
  utilisateur et mot de passe avec test de connexion qui suit
  `/api/etat.mqtt.etat` → découverte).
- Banc : `test/bench/mini_broker.py`, broker MQTT 3.1.1 minimal (CONNACK,
  SUBACK, PUBACK, PINGRESP, journal, injection de messages) pour vérifier
  l'horloge **sans toucher au Home Assistant de la maison**.

**Résultats.** Tests host : **11 395 vérifications, 0 échec, couverture
96,7 %** (4 121 lignes ; `mqtt.cpp` 97,4 %). Image 1 302 Ko (34 % libre). Sur
la carte, OTA puis broker de banc sur le PC : `CONNECT` avec utilisateur,
testament, abonnement `homeassistant/status`, `online`, 10 découvertes (JSON
valides, relus par Python), état toutes les 10 s avec les vraies valeurs
(28,5 °C, eCO2 1000 ppm, TVOC 992 ppb, niveau `mauvais` — la pièce était à
aérer), `status` = `online` injecté ⇒ tout renvoyé, bouton idem,
désactivation ⇒ `offline` retenu **puis** `DISCONNECT` propre,
`/api/etat.mqtt` cohérent (1 connexion, 1 coupure, 3 découvertes, 5 états).

**Reste.** HW-MQ-01/03 sur le vrai Home Assistant + Mosquitto (Nicolas),
HW-MQ-02 (préchauffage `null`) au prochain démarrage à froid. Suite :
moteur de règles d'alerte (étape 10), plan de test final.

---

## 2026-11-05 (suite 12) — Home Assistant : lecteur JSON, client WebSocket pur, liaison, page `/ha`, scènes Valeur

**Quoi.** Étape 8 de la phase 1 :

- `components/homeassistant/` (pur) : `json_reader` (repris de
  `energie/json_path` de ThermoSync, traduit, profondeur bornée à 16, chaînes
  désechappées), `values` (configuration d'une valeur suivie, gabarit
  `states()` / `state_attr()` / expert échappé JSON, décodage `Nombre` en
  virgule fixe, `Booleen` avec alias, `Texte` ; `unknown` / `unavailable` /
  `none` ⇒ **invalide**), `reassembler` (2 Ko statiques, trame trop longue
  **abandonnée**, jamais tronquée), **`WsClient`** (auth, `render_template`
  avec `report_errors`, `subscribe_events(horloge_notification)` décodé par
  le **même** décodeur que `POST /api/notification`, ping/pong, attente
  5 → 60 s, jeton refusé 5 min, péremption appliquée **seulement liaison
  tombée** — dette D6 tranchée), `weather` (Open-Meteo, fixture réelle).
- `settings` : domaine **ha** (liaison, 8 valeurs par `POST /api/ha/valeur/<i>`,
  météo), jeton en **écriture seule** (`jeton_defini` sur l'API, vide = gardé,
  `effacer_jeton`), types de modèle `secret` et `coordonnee`.
- `clock_homeassistant` : tâche `ha`, `esp_websocket_client` (`^1.2.3`,
  reconnexion automatique **désactivée** : c'est le client pur qui décide),
  `wss` par le paquet de certificats, météo par `esp_http_client`, notifications
  remises à `app_main` comme celles du web.
- `display/value_icons` : 7 icônes 8×8 ; `app_main` : scènes **Valeur**
  (icône + valeur, `--` si invalide), **DeuxLignes** (valeurs 1 et 2 en 3×4),
  **Defilement** (toutes les valeurs valides, une passe), **Nuit** (heure
  atténuée fixe) ; disponibilité par valeur ⇒ une scène sans donnée est
  sautée ; `/api/etat.ha` et `.meteo`.
- Page : onglet **Home Assistant** (liaison, statistiques, valeurs vivantes,
  météo), **wizard** en 4 étapes (adresse/port → jeton avec test de connexion
  qui suit l'état `running` / `token_rejected` / injoignable → première
  valeur → notifications), 8 cartes de valeur construites depuis
  `modele.valeur`, météo de repli, réglages avancés.

**Défaut trouvé et corrigé.** Avec le domaine `ha`, `settings::Settings`
pèse ~4 Ko : `clock_settings::get()` par valeur et `default_settings()` dans
`init()` faisaient **déborder la pile de la tâche `main`** (3,5 Ko) avant tout
journal — boucle de redémarrage, image rejetée par le rollback (ce qui est le
comportement attendu, et une bonne nouvelle pour HW-WEB-03). Correctifs :
accès **par domaine** (`display()`, `brightness()`, `system()`, `air()`,
`ha(out&)`, `get(out&)`), défauts construits **en place**, snapshots copiés
dans des statiques, pile de `main` à 6 Ko (`sdkconfig`, pas seulement les
defaults : le fichier généré garde l'ancienne valeur).

**Résultats.** Tests host : **10 761 vérifications, 0 échec, couverture
96,7 %** (3 613 lignes). Image 1 262 Ko (36 % libre : le paquet de certificats
et le client WebSocket pèsent ~250 Ko). Sur la carte (flash série après la
boucle) : démarrage propre, liaison `disabled` par défaut, hôte fictif ⇒
`connecting` → `waiting` avec coupures comptées, valeur enregistrée, jeton
jamais relu, désactivation à chaud. **ENS160 sorti de sa première heure** :
AQI 3, eCO2 764 ppm, TVOC 261 ppb, 27,9 °C (auto-échauffement visible : le
wizard de décalage a son utilité), niveau `Moyen`.

**Reste** (phase 1) : publication **MQTT** (étape 9 bis), **moteur de règles**
d'alerte (étape 10), plan de test final consolidé, liaison avec le vrai Home
Assistant (jeton de Nicolas).

---

## 2026-11-05 (suite 11) — Qualité de l'air : ENS160 + AHT21 sur I²C, niveaux, logo, page `/air`

**Quoi.** Étape 9 de la phase 1 (« oublie pas la partie I²C ») :

- `components/air_quality/` (pur) : `I2cBus` injecté (`write`, `read`,
  `write_read`, `recover`) ; `aht21` (CRC-8 poly 0x31, décodage en centi-°C et
  dixièmes de %, statuts `Busy` / `CrcError` / `NotCalibrated`) ; `ens160`
  (registres, `PART_ID`, décodage de `DEVICE_STATUS` et du bloc AQI / TVOC /
  eCO2, encodage K×64 / %×512 écrêté) ; **`AirSensor`**, machine d'état **non
  bloquante** (une transaction par `step()`, délais par horodatage) : sonde →
  mode standard → vérification AHT21 → cycle de 1 s (déclenchement, lecture,
  compensation écrite **avant** la lecture des données, status, données si
  valides et fraîches) ; absence (nouvelle sonde / 60 s), défaut après 3
  échecs consécutifs puis `recover()`, AHT21 absent ou en défaut ⇒ repli 25 °C
  / 50 % drapeauté ; `levels` (`LevelConfig` validée, `raw_level`,
  **`LevelTracker`** avec confirmation et hystérésis asymétrique, `Unknown`
  immédiat sur lecture invalide, couleurs avec gris pour l'inconnu,
  `build_offset`).
- `display/air_logo` : glyphes 6×6 et 8×8 ; `SceneKind::Air` (ajouté **en fin
  d'énumération** pour ne pas casser les blobs NVS) + `SceneAvailability.air`.
- `settings` : domaine **air** (`AirSettings` : actif, adresse, `LevelConfig`,
  logo, décalage), formulaire tout-ou-rien, modèle JSON avec le type
  `temperature` (centi-°C stockés, °C affichés), blob NVS `air`.
- `clock_hal/i2c_bus` : `i2c_master` d'ESP-IDF 5.3, 100 kHz, pull-ups
  internes, délai 50 ms par transaction, **récupération** d'un bus bloqué (bus
  détruit, SCL cadencé 9 fois en GPIO, STOP, bus recréé).
- `app_main` : tâche `air` (cœur 0, priorité 3, pas de 20 ms, niveau à 1 Hz,
  journal au changement), snapshot partagé, `/api/etat.air` complet (dont
  `statut_brut` et `opmode_brut` pour le diagnostic), scène **Air** (logo 8×8
  + `AQI n` ou valeur, `--` si invalide), logo 6×6 à côté de l'heure
  (`ZoneIcone`, heure alignée à gauche), hook de température brute pour le
  wizard.
- `clock_web` : `/air`, `/api/air/modele`, `GET`/`POST /api/air`,
  `POST /api/air/wizard` (`reference` en centi-°C ⇒ décalage calculé sur la
  mesure brute, avertissement > 2 °C).
- Page : onglet **Air** (tuiles vivantes, explications préchauffage / première
  mise en service / compensation par défaut / défaut), **wizard** en 3 étapes
  (détection, décalage avec thermomètre de référence, logo), deux formulaires.

**Sur le module réel** (HW-AIR-08) : `PART_ID` 0x0160 à 0x53, `OPMODE` relu
0x02, AHT21 calibré à 0x38 : 24,9 °C / 37,6 %. `DEVICE_STATUS` = 0x8B : le
capteur, neuf, est dans sa **première heure** (validité 2) — la page le dit,
l'AQI reste `--`, le logo gris : la règle d'honnêteté fonctionne telle quelle.
Le wizard de décalage a été exercé par HTTP (380 centi avec une référence
fictive, remis à 0 ensuite). Dette D11 (registres) levée pour `PART_ID`,
`OPMODE`, `DEVICE_STATUS`, AHT21 ; `DATA_*` à confronter après la première
heure.

**Résultats.** Tests host : **9 955 vérifications, 0 échec, couverture 97,8 %**
(2 014 lignes ; `air_quality` : `aht21` / `ens160` / `levels` 100 %, `sensor`
97 %). Image 1 045 Ko (47 % libre). OTA ×2.

---

## 2026-11-05 (suite 10) — Réglages, page web complète, wizards, OTA, couleurs

**Quoi.** Étape 7 de la phase 1, plus deux demandes de Nicolas (« l'IP une
seule fois à la connexion », « plus de couleur ») et, pendant la nuit, « une
page plus intuitive avec de vraies heures », « choisir ce que la dalle affiche
et l'ordre » :

- `components/settings/` (pur) : `JsonWriter` borné (profondeur 8, `nullptr`
  refusé, débordement ⇒ longueur 0), modèle `DisplaySettings` /
  `SystemSettings` / `brightness::Config`, formulaires **tout-ou-rien**
  (`apply_*_form`, `FormError{champ, raison}`), texte des scènes
  `Heure:20:1,Valeur0:8:1` et de la courbe `300:8,900:40`, JSON de **modèle**
  (`type`, `libelle`, `unite`, bornes, défaut, aide ; types `heure`, `scenes`,
  `courbe` pour les widgets) et de valeurs. **Source de vérité unique** : la
  page ne recopie aucune borne ni aucun nom.
- `components/clock_settings/` : blobs NVS versionnés (`clk_cfg/display`,
  `bright`, `system`), copie partagée sous mutex, compteur de version que la
  tâche d'affichage scrute pour **appliquer à chaud** (contrôleur de luminosité
  reconstruit, ordonnanceur relancé, fuseau), `replaced_on_boot()` signalé sur
  le tableau de bord, `factory_reset()` (réglages + Wi-Fi).
- `clock_web` : `Hooks` (notification, `status_json`, motif de test, mesure
  brute, progression OTA) ; routes `/`, `/affichage`, `/luminosite`,
  `/systeme` (une page), `/api/etat`, `/api/<domaine>/modele`, `GET`/`POST
  /api/<domaine>` (400 `{"ok":false,"champ","raison"}`), `/api/affichage/test`,
  `/api/luminosite/brut`, `/api/luminosite/wizard` (4 mesures →
  `build_from_wizard`), `/api/systeme/redemarrer`, `/api/systeme/usine`
  (`confirme=OUI`), **`POST /api/ota`** (écriture par blocs de 4 Ko dans
  l'emplacement inactif, barre sur la dalle, redémarrage, rollback armé —
  l'image se confirme au premier rafraîchissement de la dalle).
- `www/app.html` (19,5 Ko, aucune ressource externe, thème auto) : tableau de
  bord, formulaires **construits depuis le modèle** avec libellés en clair,
  unités, aide et bornes du firmware ; **heures en HH:MM** (`<input
  type=time>` ↔ minutes du jour) ; **éditeur de scènes** (type, durée, active,
  monter/descendre/retirer, ≤ 8) — c'est là qu'on choisit ce que la dalle
  affiche et dans quel ordre ; éditeur de **courbe** par points ; wizard de
  correspondance (pixel, colonne, chenillard pour le serpentin, diagonale,
  plein) ; wizard de luminosité (4 mesures, avertissement « déportez le
  capteur ») ; OTA avec barre ; redémarrage ; usine avec confirmation.
- `app_main` : thème de couleurs (chiffres blancs, deux-points ambre, barre des
  secondes bleue, jour ambre, date blanche, état bleu clair, notification par
  priorité) ; le texte d'état est une passe **unique** (priorité 0, durée
  calculée sur la largeur du texte) à l'obtention de l'IP, et toutes les 60 s
  en portail ; une notification entrante prime et ré-arme l'état. Rotation par
  défaut Heure 20 s / Date 5 s (le défilement permanent est retiré).
- `clock_scene` : `ClockColors` / `DateColors` à la place d'une couleur unique.

**Défauts trouvés et corrigés.** (1) `clock_time::start()` passait à lwIP un
pointeur vers un nom de serveur NTP **sur la pile de `app_main`** : plus aucune
synchro après le passage aux réglages NVS — copie statique désormais (et
l'horloge se synchronise en 6 s). (2) La passe d'état était « oubliée » entre
sa mise en file et son affichage, et sortait avec le texte/la couleur d'une
notification : machine d'état `pending` → `queued` → `showing` explicite.
(3) `JsonWriter` déréférençait un `nullptr` (trouvé par le nouveau test).

**Résultats.** Tests host : **9 299 vérifications, 0 échec, couverture 97,9 %**
(1 865 lignes) ; `settings` 96,5 % (`forms.cpp`). Image 1 006 Ko (49 % libre).
Sur la carte : API complète vérifiée par `curl` (HW-WEB-04), **OTA par HTTP**
(HW-WEB-02 : 998 Ko en 5,6 s, image confirmée), heure SURE à 6 s, ordre des
scènes changé à chaud. **À observer par Nicolas** : la passe d'IP (HW-WEB-05),
les couleurs (HW-WEB-06), la page au téléphone (HW-WEB-01).

---

## 2026-11-05 (suite 9) — L'heure : règles POSIX en logique pure, SNTP, `--:--` devient l'heure

**Quoi.** Étape 6 de la phase 1 :

- `components/timekeeping/` (pur) : `parse_posix_tz()` (`STD offset DST[offset],Mm.w.d[/time],…`,
  offsets en minutes **est** de l'UTC, demi-heures, secondes, heures de
  bascule négatives ou > 24), `days_from_civil` / `civil_from_days` (Hinnant,
  64 bits : pas de falaise 2038), `transition_epoch()` (semaine 5 = dernier
  jour-de-semaine du mois), `is_dst()` (hémisphère nord **et** sud),
  `to_local()`, `SyncValidity` (48 h), et une table de **18 zones** IANA →
  POSIX pour la future page `/systeme`, `Europe/Zurich` par défaut.
  **Choix** : ne pas utiliser le `TZ` de la libc — celui de MinGW ne comprend
  pas les règles `M3.5.0`, donc impossible à tester sur host ; la logique
  maison est testée **à la seconde** sur les deux bascules suisses de 2026,
  celle des États-Unis et celle de Sydney (avertissement de Nicolas :
  « attention au changement d'heure été/hiver »).
- `components/clock_time/` : `esp_netif_sntp` (`pool.ntp.org`, sans attente),
  rappel de synchro → `SyncValidity::mark_synced()` sous section critique,
  `now()` = epoch UTC de `time()` converti par la règle courante ;
  `set_timezone()` à chaud pour la page de réglages.
- `app_main` : lit la source de temps à chaque trame, log au basculement
  `sure`, scène **Date** ajoutée à la rotation (Heure 6 s, Date 4 s, texte
  d'état), minute du jour fournie au contrôleur de luminosité pour la fenêtre
  d'extinction.

**Résultats.** Tests host : **9108 vérifications, 0 échec, couverture 99,1 %**
(1280 lignes). Build propre, image 907 Ko. Sur la carte : `boot_grace` →
`connected` à 23 s (la grâce en action : la box a mis 20 s), `SNTP synchronised`
à 31 s, **`time is now SURE (2026-11-05 03:33:24)`**, heure de Zurich sur la
dalle, puis `JEU 05.11` en 3×5.

**Vérifié par Nicolas** : heure juste, date en 3×5 lisible. **Non vérifié** :
la bascule réelle du 29 mars 2026 (HW-TIME-02), le réglage du fuseau sur la
page (HW-SYS-01, page à écrire). Serveur NTP et fuseau encore constants dans `app_main` (réglages
NVS à venir avec `settings`).

---

## 2026-11-05 (suite 8) — Réseau : portail captif, reprise après coupure, canal de notification

**Quoi.** Étape 5 de la phase 1, à la demande de Nicolas « rien de ThermoSync
sauf le réseau, et le hub doit pouvoir envoyer des messages » :

- `components/network/` (pur) : `credentials` (validation, formulaire borné,
  JSON, mot de passe du point d'accès), `captive_dns` et `supervisor` **repris
  de ThermoSync et traduits** ; le superviseur gagne l'**extension coupure de
  courant** : états `BootGrace` (délais 10/20/40/60/60 s, jamais de portail,
  fin anticipée après 2 échecs d'authentification), `WaitingLease` (associé
  sans IP : réessais 60 s → 5 min, jamais de portail), `LeaseDesperate` (30 min
  sans bail : portail levé **en plus**), et la demande de redémarrage de
  récupération (24 h sans IP, une fois). Entrée `LinkStatus{associated, has_ip}`
  + `FailCause` ; l'ancienne signature `step(now, link_up)` reste pour
  l'héritage.
- `components/alerts/` (pur, amorce) : `decode_form()` du canal de
  notification — `cle` obligatoire `[a-z0-9_]`, champs hors bornes remplacés
  par leur défaut et comptés, textes tronqués. Partagé par toutes les portes
  d'entrée (HTTP local aujourd'hui, Home Assistant et hub ensuite).
- `components/clock_web/` : Wi-Fi APSTA, événements → `LinkStatus`/`FailCause`
  (`NO_AP_FOUND` → `ApNotFound`, `AUTH_FAIL`/handshake → `AuthFailed`), tâche
  de supervision à 1 s qui **applique le plan**, DNS captif (tâche), identifiants
  en NVS `clk_wifi` pris en compte **sans redémarrage**, mDNS `horloge.local`,
  serveur HTTP : `/wifi` (page embarquée, 6,3 Ko), `/api/wifi/*`, `/api/info`,
  sondes de détection de portail, 404 → portail, et **`POST /api/notification`**
  qui pousse une notification à l'affichage (priorité → couleur, durée →
  interruption de scène).
- `app_main` : **tâche d'affichage dédiée** sur le cœur 1, priorité 6 (au-dessus
  du serveur HTTP et des tâches réseau, EX-61) ; le réseau démarre ensuite et
  son échec ne touche pas l'affichage. Le texte défilant montre **comment
  joindre l'horloge** : `WIFI Horloge-XXXX MDP …` en portail, l'adresse IP une
  fois connectée.
- Mot de passe du point d'accès : alphabet **chiffres + majuscules sans O ni I**
  — la police 5×7 n'a pas de minuscules, et un mot de passe lu sur la dalle doit
  se taper exactement. Puis, à la demande de Nicolas, mot de passe **fixé à
  `12345678`** dans `sdkconfig.defaults.horloge-v1` (`CLOCK_WIFI_AP_PASSWORD`) :
  il ne garde que le portail de mise en service, et un mot de passe trivial
  lisible sur la dalle vaut mieux qu'un mot de passe dérivé que personne ne
  retient. La dérivation reste le repli quand le réglage est vide.

**Résultats.** Tests host : **8901 vérifications, 0 échec, couverture 99,1 %**
(1075 lignes). Build propre, image **894 Ko** (53 % libres : le WiFi, lwIP,
mbedTLS et mDNS pèsent). Flashé sur COM21 : `access point Horloge-C995 (WPA2)`,
`captive DNS ready`, `mDNS: horloge.local`, `HTTP server ready`, affichage à
25 i/s et LDR vivants pendant que le réseau tourne.

**Ce que les tests ont attrapé.** Un test de troncature mal construit (préfixe
compté à la main à 44 au lieu de 45 caractères : le champ `texte` n'existait
pas) ; corrigé par `strlen`. Le décodeur acceptait d'abord un texte trop long
en le **refusant** au lieu de le tronquer : corrigé (tampon de travail de 1 Ko,
troncature comptée).

**Non vérifié.** Portail ouvert depuis un téléphone (HW-RES-05), connexion à la
box depuis le portail (HW-RES-06), `horloge.local` (HW-RES-07), coupure
secteur (HW-RES-01/02) : ça demande un téléphone et la box de Nicolas.

---

## 2026-11-05 (suite 7) — Luminosité automatique : `brightness` pur, ADC1, asservissement

**Quoi.** Étape 4 de la phase 1 :

- `components/brightness/` (pur) : `Median5` (réseau de comparaisons),
  `AsymmetricAverage` (Q8, montée 10 s / descente 60 s), `Config` + `Curve`
  validés, `interpolate()` entier arrondi, `in_window()` franchissant minuit,
  `Controller` (compensation d'auto-éclairage **signée en comptes à 255**,
  zone morte d'un pas + arrondi au multiple du pas, plancher/plafond, 5 ratés
  ⇒ `kFallbackLevel` 64, rails 0/4095 = défaut, fenêtre d'extinction appliquée
  **seulement si l'heure est sûre**, modes Auto/Manual/Off),
  `build_from_wizard()` (3 points triés par mesure, compensation signée).
- `components/clock_hal/` : `LdrAdc` — `adc_oneshot` sur le canal résolu depuis
  le GPIO (refus explicite si ADC2), 12 dB, 12 bits, 64 échantillons moyennés,
  étalonnage *line fitting* par l'eFuse, lecture ratée ⇒ échantillon invalide.
- `app_main` : lecture à 1 Hz, `Controller::step()`, le niveau appliqué nourrit
  `render()` ; log brut / mV / filtré / niveau / défaut toutes les 5 s.

**Écarts de spec, consignés dans `spec-luminosite.md`** : compensation en
**comptes ADC** (signée) plutôt qu'en ‰ ; courbe du wizard à **3 points**
(nuit/soir/jour) et non 4 — la 4ᵉ mesure est l'auto-éclairage, pas un niveau
d'ambiance ; « hystérésis d'un demi-pas » précisée en **zone morte d'un pas +
arrondi au pas**.

**Résultats.** Tests host : **8661 vérifications, 0 échec, couverture 98,9 %**
(629 lignes). Build propre, 241 Ko, flashé sur COM21 : `ADC1 channel 5 on
GPIO33, 12 dB, calibrated=1`, ~2086 comptes (1,9 V) sous l'éclairage du bureau
⇒ niveau 170 avec la courbe par défaut (câblage (a) supposé).

**Câblage confirmé (a)** : main sur la LDR ⇒ la dalle s'assombrit (Nicolas).
La courbe par défaut est dans le bon sens. La baisse de mesure vue dans les
logs (2086 → 1754) pendant que la dalle passait de 48 à 170 n'était pas de
l'auto-éclairage — avec ce câblage il ferait *monter* les comptes — mais
l'ambiance qui changeait (lampe allumée entre-temps, déplacements). Aucune
compensation inventée : le wizard la mesurera dans le noir, dalle éteinte puis
allumée (HW-LUM-02).

**Ce que les tests ont attrapé.** Une attente d'arrondi fausse dans LUM-05
(131,5 → 131, l'arrondi va à l'opposé de zéro sur une pente négative) ; le code
était cohérent, l'attente a été corrigée et documentée dans le test.

---

## 2026-11-05 (suite 6) — Polices, scènes, défilement, ordonnanceur ; `--:--` sur la dalle

**Quoi.** Étape 3 de la phase 1, dans `display` (pur) :

- `font.hpp` / `fonts.cpp` : trois polices colonne-majeure (bit 0 = ligne du
  haut) — **5×7** (jeu GLCD classique, `' '..'`'`), **3×5** et **3×4** dessinées
  pour la dalle ; le degré occupe le slot du backtick (`kDegreeChar`).
- `text.hpp` : `text_width()`, `draw_text()` avec **clipping propre** (un texte
  qui défile hors dalle n'incrémente pas le compteur hors-dalle), et
  `transliterate()` : UTF-8 → capitales ASCII, accents Latin-1 et Œ réduits à
  leur base, `°` → degré, inconnu → `?`, borné et toujours NUL-terminé.
- `clock_scene.hpp` : `TimeInfo{sure, …}`, `draw_clock()` (25 px, deux-points
  étroit d'un pixel, clignotement, barre des secondes) qui affiche **`--:--`
  sans barre** quand `sure` est faux, `draw_date()` (`SAM 05.11` en 3×5, 30 px,
  rien si pas sûr), abréviations françaises des jours.
- `scroll.hpp` : `Scroller` à vitesse entière exacte (position en 1/256 px +
  reste en ms : 20 px/s font 20 px/s à 40 ms comme à 33 ms), 1..n passages ou
  infini, troncature à 128 caractères.
- `scheduler.hpp` : rotation des scènes, saut des scènes sans donnée (y compris
  **en cours** si la donnée disparaît), horloge synthétique si tout est sauté,
  durée 0 = jusqu'à `finished`, notifications (P≥1 interrompt et rend la main
  **au temps restant**, P0 attend la fin de scène, remplacement par priorité ≥),
  débordement 32 bits toléré.
- `test_patterns.hpp` : les motifs de mise en service et le chenillard
  physique sortent de `app_main` pour devenir des fonctions pures (le wizard
  s'en servira tel quel).
- `app_main` : boucle à **25 i/s** pilotée par l'ordonnanceur ; sans source de
  temps, la scène Heure montre **`--:--`** avec le deux-points qui clignote, en
  alternance avec un texte défilant translittéré (« MATRIX CLOCK — ESP-IDF —
  HEURE NON SYNCHRONISEE »).

**Résultats.** Tests host : **8530 vérifications, 0 échec, couverture 99,5 %**
(422 lignes ; les deux lignes restantes sont des branches défensives
injoignables de `scheduler.cpp`). `idf.py build` propre (`esp_timer` ajouté
aux `REQUIRES` de `main`), image 230 Ko, **flashé sur COM21** : `scene 0`
(Heure) puis `scene 4` (Défilement) dans les logs, `oob=0`.

**Ce que les tests ont attrapé.** L'ordonnanceur attendait une seconde en
« horloge synthétique » avant de choisir la première scène : onze échecs d'un
coup, tous décalés de 1 s — corrigé par un `started_` qui fait choisir dès le
premier `step()`. Deux pièges de compilation notés : `"\xC3\xA9e"` est **un
seul** échappement hexadécimal invalide (le `e` est un chiffre hexa), il faut
couper la chaîne ; et le `.gcda` d'un run précédent fait râler gcov après une
recompilation (le script les efface désormais avant `ctest`).

**Vérifié sur la dalle** (Nicolas) : `--:--` clignotant puis le texte qui
défile. **Non vérifié** : la lisibilité des glyphes 3×5 / 3×4 dessinés à la
main — la date n'apparaît qu'avec une heure sûre, donc à l'étape SNTP.

---

## 2026-11-05 (suite 5) — Premier code : squelette ESP-IDF et composant `display`

**Quoi.** Étapes 1 et 2 de l'ordre de la phase 1 (`cadrage-fonctionnel.md` §4) :

- `firmware/` : `CMakeLists.txt` (projet `matrix_clock`, `PROJECT_VER 0.1.0`),
  `partitions.csv` (4 Mo, deux emplacements OTA de 1920 Ko, NVS 48 Ko),
  `sdkconfig.defaults` (pas d'exceptions/RTTI, tick 1 ms, watchdog de tâche,
  rollback OTA) et la surcouche `sdkconfig.defaults.horloge-v1` (flash 4 Mo,
  DIO 40 MHz, profil de carte).
- `clock_board` : brochage `constexpr` sélectionné par Kconfig, profils
  `horloge-v1` et `devkit`. **I²C relevé sur GPIO27 (SDA) / GPIO26 (SCL)** à la
  demande de Nicolas ; la proposition de bouton passe de GPIO27 à GPIO25.
- `display` (pur, anglais) : `FrameBuffer` (hors dalle ignoré et compté),
  `led_index()` (origine × axe × serpentin × modules, bijection prouvée pour
  les 32 combinaisons), `kGammaTable` (générée par `tools/gen_gamma.py`),
  `estimate_current_ua()` / `apply_current_limit()` (écrêtage proportionnel en
  virgule fixe Q8, repos non écrêtable), `render()` (luminosité → gamma →
  plafond → correspondance, sortie RGB en ordre physique).
- `clock_display` : `LedPanel` sur `espressif/led_strip` **2.5.5**, dorsale
  SPI/DMA par défaut, RMT par Kconfig.
- `main/app_main.cpp` : efface la dalle, confirme l'image OTA, fait tourner
  les **quatre motifs de mise en service** (pixel, colonne, diagonale, blanc)
  toutes les 3 s en loguant le courant estimé et l'écrêtage.
- Harnais host `firmware/test/` : macros `CHECK*` sans `assert`, **piège
  `operator new`** (`AllocGuard`), agrégation gcov, échec sous 95 %.

**Résultats.** Tests host : **8357 vérifications, 0 échec, couverture
100 %** (128 lignes de `display`). `idf.py build` **propre, sans
avertissement** ; `matrix_clock.bin` = **227 Ko**, 88 % de l'emplacement OTA
de 1920 Ko libres.

**Ce que les tests ont attrapé.** Le premier passage de AFF-06 a **échoué** :
j'attendais l'écrêtage à 1 % du budget, il tombe à ~3 % en dessous (plancher sur
l'échelle 29/256 puis sur chaque canal 255 → 28). Le budget n'est jamais
dépassé, c'est l'invariant qui compte ; l'attente est passée à « jamais plus de
5 % en dessous », consigné dans le test. Le harnais a bien rendu un code
d'erreur (GEN-05 vérifié en vrai).

**Pièges notés.** `CHECK(a == Rgb{1,2,3})` : les virgules de l'agrégat
cassent la macro, il faut une paire de parenthèses. Un en-tête public qui inclut
`led_strip.h` impose `espressif__led_strip` dans `REQUIRES`, pas en privé.

**Flashé sur COM21** (à la demande de Nicolas) : démarrage propre, table de
partitions 4 Mo relue par le bootloader, `led_panel: SPI backend (DMA) on
GPIO13: ESP_OK`, les quatre motifs s'enchaînent toutes les 3 s. Estimations
loguées à luminosité 48 : pixel 257 mA, colonne 259 mA, diagonale 271 mA,
**blanc plein 617 mA** (gamma(48) = 5 : 256 × 3 × 5 × 20 mA / 255 + 256 mA de
repos — cohérent). Le CPU tourne à **160 MHz** (défaut IDF) : 240 MHz
possible, à décider (todo).

**Correspondance : le schéma du fabricant était faux, le chenillard a
tranché.** Sur le dessin fourni par Nicolas (rangées, entrée bas-droite) j'avais
passé le défaut à (ligne, serpentin, bas-droite) ; les motifs semblaient faux.
Un **chenillard physique** (LED 0 rouge fixe, LED 255 bleue fixe, point blanc
dans l'ordre du ruban, ajouté à `app_main`) a montré : rouge **haut-gauche**,
parcours **vertical colonne par colonne**, bleue **haut-droite** — donc
serpentin. Défaut remis à **(colonne, serpentin, haut-gauche)**, test AFF-01
complété par ces positions (LED 0 en (0,0), LED 255 en (31,0), LED 15 en (1,0)).
Leçon consignée dans la fiche carte : on ne se fie qu'à ce qu'on allume. Le
schéma reste utile pour le **fil d'alimentation central** (injection).

**Premier allumage.** DIN confirmé sur **GPIO13**. Une alerte « la dalle
montre n'importe quoi » s'est révélée une **fausse alerte** (Nicolas : l'écran
était juste). Au passage, la dorsale **RMT** a été construite et flashée en
contre-essai (`sdkconfig.defaults.rmt`, `build-rmt/`) et elle affiche aussi
proprement : **les deux dorsales fonctionnent** sur cette dalle. Le défaut reste
**SPI/DMA** ; l'overlay RMT est conservé comme outil de diagnostic.

**HW-AFF-01 validé** : avec la correspondance mesurée, Nicolas confirme pixel
en haut à gauche, colonne verticale au bord gauche, diagonale. La mise en
service matérielle de l'affichage est faite, en SPI/DMA.

**Non vérifié.** Le courant **réel** à la pince (HW-AFF-02), la tension à la
dalle (HW-AFF-03), la charge processeur SPI vs RMT.

---

## 2026-11-05 (suite 4) — Carte identifiée : compatible

`esptool flash_id` sur la carte désignée par Nicolas (**COM21**, CP210x) :
**ESP32-D0WD-V3 rév. 3.0, 4 Mo de flash (GigaDevice GD25Q32), quartz 40 MHz,
étalonnage Vref en eFuse, pas de PSRAM**. Tout correspond à la cible spécifiée :
la table de partitions 4 Mo tient, l'ADC sera étalonné, aucun contournement de
révision à prévoir. Relevé consigné dans `cartes/horloge-v1/README.md` §0 (sans
l'adresse MAC : dépôt public).

Deux pièges notés : `esptool.py` absent du `PATH` PowerShell (passer par le
Python de l'IDF) ; et **plusieurs cartes branchées** — un premier relevé avait
lu un autre WROOM-32 (COM6, CH340, rév. 3.1, flash Puya, compatible aussi), et
**COM15 est la carte ThermoSync (ESP32-S3, 16 Mo)**, à ne jamais flasher d'ici.

---

## 2026-11-05 (suite 3) — Aucun acquittement, fuseau réglable, code en anglais, Git

**Quoi.** Trois décisions de Nicolas, répercutées partout :

1. **Aucun acquittement** des alertes (EX-46) : une alerte de règle est rejouée
   toutes les `rappel_s` tant que sa condition tient et tombe d'elle-même à la
   retombée ; une notification externe tenue ne tombe que par `effacer` de sa
   source. Supprimé : les champs `persistante` / `reconduction_s` des règles
   (remplacés par `rappel_s`), le champ `acquittee` de la file, le bouton
   d'acquittement de la page, le rôle « acquitter » du bouton physique (Q1).
2. **Fuseau `Europe/Zurich` par défaut, réglable** sur `/systeme`, appliqué à
   chaud (EX-03, Q3 tranchée, HW-SYS-01).
3. **Code en anglais** pour un dépôt **public** : identifiants, commentaires,
   Doxygen, logs, commits. Page web et documentation en français, `README.md`
   anglais à la racine. Composants repris de ThermoSync traduits à la copie.
   Noms de composants anglais dans toute la doc (`display`, `brightness`,
   `network`, `homeassistant`, `alerts`, `air_quality`, `settings`,
   `clock_*`) ; les identifiants internes cités dans les specs restent
   descriptifs, le code fait foi.

**Git.** `git init` (branche `main`), `.gitignore` ESP-IDF, `README.md`, puis
**commit initial** à la demande de Nicolas (specs seules). Remote `origin` =
`https://github.com/nicosonna/matrix_clock.git` (dépôt public, vide à la
création). Licence à choisir avant publication.

**État.** Specs à jour, rien d'implémenté.

---

## 2026-11-05 (suite 2) — Publication MQTT des capteurs vers Home Assistant

**Quoi.** À la demande de Nicolas (« comme sur la ev_charger »), les grandeurs
locales (ENS160 + AHT21, lumière ambiante, heure sûre) sont **publiées vers Home
Assistant par MQTT avec découverte** : `spec-home-assistant.md` §6 bis, page
`/mqtt`, exigences `EX-36` → `EX-38`, tests `MQ-01` → `MQ-10` et
`HW-MQ-01` → `HW-MQ-03`. Répercuté dans `CLAUDE.md`, `architecture.md`, le
cadrage, les specs air et serveur web, le plan de test et le backlog.

**Ce que ça révise.** La décision du matin « pas de MQTT en phase 1 » est
**remplacée** par « MQTT pour publier seulement » : le WebSocket reste le seul
moyen de **lire** Home Assistant, MQTT ne sert qu'à lui **donner** ce que
l'horloge mesure. Conséquence heureuse : avec la découverte, l'horloge devient un
**appareil** dans Home Assistant, et les entités de commande de la phase 3 ne
sont plus qu'une extension du même composant.

**Comment.** La borne fait ça en Arduino (`utils_esp/mqttClient/mqtt_bridge.hpp`,
`publishSensorConfig` / `sendMqttSensorValue`). En ESP-IDF, l'équivalent pur et
testé est le composant `domotique` de ThermoSync (`home_assistant.hpp`,
`mqtt_config.hpp`) : on le **reprend** (topics `horloge/<id>/…`, testament,
état JSON unique, `null` pour l'invalide, `MqttConfig` et sa page `/mqtt`).

**État.** Spécifié, rien d'implémenté.

---

## 2026-11-05 (suite) — Capteur de qualité d'air ENS160 + AHT21

**Quoi.** Ajout du module **ENS160 + AHT21** (I²C, GPIO21/22) et de l'**alarme
qualité d'air** avec **logo air** optionnel coloré selon le niveau, messages
`AERER` / `AIR MAUVAIS`. Nouvelle spec `dev/spec-qualite-air.md`, nouveau
composant pur `air_quality`, nouvelles exigences `EX-70` → `EX-77`, tests `AIR-01` →
`AIR-22` et `HW-AIR-01` → `HW-AIR-08`, page `/air` avec wizard. Répercuté dans
`CLAUDE.md`, `architecture.md`, le cadrage (Q10, Q11, décision), les specs
affichage / alertes / serveur web, la fiche carte, le plan de test et le backlog.

**Choix.** L'alarme passe par le **moteur de règles existant** (sources
`AirAqi`, `AirEco2`, `AirTvoc`, …) : un seul rendu, une seule file, une seule
logique de silence. Le **drapeau de validité** de l'ENS160 est traité comme une
donnée de premier ordre : préchauffage (3 min) et première mise en service
(1 h) ⇒ `--`, logo gris, aucune alerte — pas de « AERER » au retour du courant
à 3 h du matin. L'AHT21 **compense** l'ENS160 à chaque cycle ; AHT21 en défaut ⇒
compensation de repli nommée et signalée, ENS160 toujours exploité.

**Pièges identifiés.** Auto-échauffement du boîtier (dette D10 : capteur
déporté, écart mesuré par le wizard, refus au-delà de 2 °C) ; table des
registres **non vérifiée** sur le module (dette D11 : lire `PART_ID` en
première tâche).

**État.** Spécifié, rien d'implémenté.

---

## 2026-11-05 — Création du dépôt et des specs

**Quoi.** Mise en place du canevas de documentation et des spécifications
complètes de la phase 1, sans une ligne de code :

- `CLAUDE.md`, `documentation/gardefous-dev.md`, `documentation/architecture.md` ;
- `dev/cadrage-fonctionnel.md` (vision, 40 exigences `EX-nn`, phases, 9 questions
  ouvertes, tableau des décisions) ;
- `dev/spec-affichage.md`, `dev/spec-luminosite.md`,
  `dev/spec-reseau-portail.md`, `dev/spec-home-assistant.md`,
  `dev/spec-alertes.md`, `dev/spec-serveur-web.md` ;
- `dev/spec-liaison-hub.md` (phase 2, **proposition à valider**) ;
- `cartes/horloge-v1/README.md` (brochage, courant, adaptation de niveau,
  placement du capteur) ;
- `dev/test-plan-hw.md`, `dev/todo.md`, ce journal.

**Pourquoi.** Reprendre tel quel le canevas qui fonctionne sur ThermoSync et
*Regulation_chauffage* : logique pure host-testable séparée du HAL, documentation
vivante, plan de test permanent, wizards de configuration, zéro secret.

**Décisions arrêtées ce jour** (détail et raisons dans
`dev/cadrage-fonctionnel.md` §6) :

| Décision | Raison courte |
|---|---|
| **ESP32-WROOM-32** classique, carte `horloge-v1` | Cohérent avec le montage (LDR sur GPIO33 = ADC1_CH5) |
| **Photorésistance analogique** sur ADC1 | C'est le montage en place ; pas de lux absolus, courbe à étalonner |
| **WebSocket Home Assistant**, abonnements `render_template` | Valeur initiale + poussée sur changement, messages minuscules ; `get_states` serait impossible sans PSRAM |
| **Pas de MQTT** en phase 1 | Un seul transport ; conséquence assumée : l'horloge n'est pas un *appareil* dans Home Assistant |
| **Règles d'alerte locales** (phase 1) + **liaison hub** spécifiée puis validée (phase 2) | L'horloge doit alerter même si Home Assistant et le hub sont tombés |
| Composant **`network` repris de ThermoSync**, pas réécrit | Déjà pur, déjà testé, comportement déjà éprouvé sur la borne |
| **Dorsale SPI avec DMA** pour `led_strip`, RMT en repli | Le RMT de l'ESP32 classique n'a pas de DMA : 7,7 ms par trame en interruptions |

**Apports par rapport à ThermoSync.** Trois points que ce projet ajoute :

1. **Extension « coupure de courant »** du superviseur WiFi : distinction
   « point d'accès introuvable » / « associé sans bail DHCP », et **fenêtre de
   grâce au démarrage** (5 min sans portail). Sans ça, l'horloge ouvre son
   portail captif après chaque coupure de courant, parce que la box redémarre
   plus lentement que la carte. Tests `RES-20` à `RES-29`.
2. **Plafond de courant** calculé dans le firmware : 256 LEDs en blanc plein
   tirent 15,6 A, un réglage de luminosité ne suffit pas à s'en protéger.
3. **Règle d'honnêteté de l'affichage** (`gardefous-dev.md` §4 bis) :
   l'équivalent, pour une horloge, de l'état sûr d'une régulation — une heure
   fausse est pire qu'un `--:--`, parce qu'on la croit.

**Lecture du contrat du hub.** `../data_logger_website/documentation/contrat-ws-appareils.md`
couvre déjà **tout** ce dont l'horloge a besoin (`commande`, `config`,
`evenement` avec `cle`/`actif`, capacité `heure`) : la « logique d'alerte
paramétrable via le hub » ne demande **aucune extension du contrat**. Reste une
seule question — le hub sait-il pousser une commande **automatiquement, sur
condition** — rédigée et prête à déposer dans `retours-horloge.md`
(`spec-liaison-hub.md` §5), **non déposée** à ce stade puisque la phase 2 n'a pas
commencé.

**État.** Specs complètes pour la phase 1, **rien d'implémenté**. Dépôt pas
encore sous Git. Prochaine étape : squelette ESP-IDF et composant `display`
(ordre dans `dev/cadrage-fonctionnel.md` §4).
