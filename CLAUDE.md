# Contexte projet — Horloge

Firmware embarqué **ESP-IDF** (pas de PlatformIO, pas d'Arduino) d'une **horloge
matricielle connectée** : dalle **WS2812B 8×32** (256 LEDs), **luminosité
automatique** par photorésistance, heure par **SNTP**, lecture de capteurs de
**Home Assistant** en **WebSocket**, capteur de **qualité d'air** ENS160 + AHT21
**publié vers Home Assistant en MQTT** (découverte, comme la borne et
ThermoSync), **canal d'alerte** et **moteur de règles
d'alerte** local, **page web** de paramétrage complet avec **wizards**,
**portail captif** de mise en service repris de la borne et de ThermoSync.
Cible : devkit **ESP32-WROOM-32** (4 Mo, sans PSRAM), carte `horloge-v1`.
Dépôt **public** : `github.com/nicosonna/matrix_clock` (dossier local
`horloge`, remote `origin`). Langues (décision du 2026-11-05) :
- **Code en anglais** : identifiants, commentaires, Doxygen, logs `ESP_LOGx`,
  messages de commit, `README.md` de la racine.
- **Page web en français** (interface de la maison) ; **documentation en
  français** (`documentation/`, ce fichier).
- Les composants repris de ThermoSync (`reseau` → `network`, `domotique` →
  `homeassistant`) sont **traduits à la copie** : mêmes algorithmes, mêmes
  tests, identifiants anglais.
- Les identifiants cités dans les specs (`TamponImage`, `kBudgetDefautMa`…)
  sont **descriptifs** : le code les porte en anglais (`FrameBuffer`,
  `kDefaultBudgetMa`), et c'est le code qui fait foi pour le nom.

> ⚠️ **Cadrage fonctionnel en cours.** Vision, exigences, découpage en phases et
> questions ouvertes : `documentation/dev/cadrage-fonctionnel.md` — **le lire
> avant toute implémentation**. Quand un dossier est créé/renommé ou qu'un choix
> est arrêté, **mettre à jour ce fichier** pour qu'il reste fidèle au dépôt.

> ⚠️ **État du dépôt (2026-11-05)** : squelette ESP-IDF, `clock_board`,
> composant pur **`display`** (tampon, correspondance, gamma, plafond de
> courant, rendu — 100 % couvert) et pilote `clock_display` avec les motifs de
> mise en service. **Tout le reste** des composants décrits ci-dessous est la
> **cible**, pas l'existant. `dev/journal.md` et `dev/todo.md` disent où on en est.

## Structure du dépôt

- **`firmware/`** — firmware **ESP-IDF** (`idf.py` / CMake). Build (PowerShell) :
  `. "$HOME\esp\esp-idf\export.ps1"` puis `idf.py -C firmware build`.
  - **Logique pure** (aucun `#include` ESP-IDF, host-testable) :
    - `components/display/` — tampon d'image 8×32, correspondance panneau
      (serpentin / progressif), gamma, **plafond de courant**, polices,
      défilement, ordonnanceur de scènes.
    - `components/brightness/` — filtrage de la mesure brute (médiane + moyenne
      exponentielle), courbe de correspondance mesure → luminosité, hystérésis,
      plancher de nuit, repli sûr si la mesure est invalide.
    - `components/network/` — **repris de ThermoSync** : validation des
      identifiants, décodage du formulaire du portail, réponse DNS captive,
      superviseur WiFi, plus l'**extension coupure de courant** (distinction
      « point d'accès introuvable » / « associé mais pas d'IP », fenêtre de
      grâce au démarrage).
    - `components/homeassistant/` — **Home Assistant** pur, deux sens :
      `ws_client` (**lecture** : machine d'état du WebSocket, authentification,
      abonnements `render_template`, ping, péremption) et `mqtt`
      (**publication** : découverte MQTT, état JSON unique, `null` pour
      l'invalide, testament, `MqttConfig` — **repris de ThermoSync**
      `domotique/home_assistant.hpp` + `mqtt_config.hpp`, en remplaçant
      `thermosync` par `horloge`). Contient son **lecteur JSON sans allocation**
      (`homeassistant/json_reader.hpp`, repris de `energie/json_path` de ThermoSync).
      ⚠️ **Ne jamais créer un composant nommé `json`** : ESP-IDF en possède un
      (cJSON) qu'un homonyme projet remplacerait silencieusement — même piège
      que `hal` sur ThermoSync.
    - `components/alerts/` — moteur de **règles d'alerte** (source, comparaison,
      seuil, durée de confirmation, hystérésis, priorité, heures silencieuses) et
      **file de notifications** bornée (priorité, dédoublonnage, durée de vie).
    - `components/air_quality/` — capteur **ENS160 + AHT21** (I²C) : décodage des
      registres, CRC8, **drapeau de validité** (préchauffage 3 min, première mise
      en service 1 h : valeurs invalides, pas inventées), compensation
      température/humidité, niveaux de qualité d'air avec hystérésis et
      confirmation, logo coloré, messages `AERER` / `AIR MAUVAIS`. Bus I²C
      derrière une interface injectée. Spec : `dev/spec-qualite-air.md`.
    - `components/settings/` — modèle de configuration **pur**, validé et borné,
      **source de vérité unique** des bornes exposées à la page web.
  - **Adaptateurs ESP-IDF** :
    - `components/clock_board/` — brochage `constexpr` par carte, sélectionné
      par Kconfig. Jamais un numéro de GPIO ailleurs.
    - `components/clock_display/` — pilote de la dalle (`espressif/led_strip`,
      **dorsale SPI avec DMA** préférée sur ESP32 classique, RMT en repli).
    - `components/clock_hal/` — ADC de la photorésistance (ADC1, oneshot +
      étalonnage), **bus I²C** (ENS160 + AHT21, réinitialisation du bus bloqué),
      NVS.
    - `components/clock_time/` — SNTP, fuseau horaire, **validité de l'heure**.
    - `components/clock_homeassistant/` — transport WebSocket
      (`esp_websocket_client`) vers Home Assistant, client **`esp-mqtt`**
      (testament, découverte QoS 1 retenue, état QoS 0), client HTTP de la météo
      de repli (Open-Meteo). MQTT ne sert **qu'à publier**, en local ; on ne
      lit rien par MQTT.
    - `components/clock_web/` — serveur HTTP, page **embarquée dans l'image**,
      portail captif, mDNS.
    - `main/app_main.cpp` — câble HAL ↔ logique, boucle d'affichage et boucle de 1 s.
  - `partitions.csv` (4 Mo, deux emplacements OTA, NVS 48 Ko, **figée**),
    `sdkconfig.defaults` et `sdkconfig.defaults.horloge-v1`.
  - `test/` — **tests host** (CMake + g++, sans ESP-IDF) et campagnes de banc.
- **`documentation/`** — toute la doc vit ici :
  - `architecture.md` — modules, flux, choix, dette connue.
  - `gardefous-dev.md` — standard de dev détaillé (`CLAUDE.md` en est le résumé).
  - `dev/journal.md` — **historique des modifications** (récent en haut).
  - `dev/todo.md` — **backlog** : tâches identifiées, décisions en suspens.
  - `dev/test-plan-hw.md` — **plan de test** (matrice de traçabilité).
  - `dev/cadrage-fonctionnel.md` — vision, exigences, phases, questions ouvertes.
  - `dev/spec-affichage.md` — dalle, correspondance, gamma, plafond de courant,
    polices, scènes, défilement.
  - `dev/spec-luminosite.md` — chaîne de mesure, filtrage, courbe, wizard
    d'étalonnage.
  - `dev/spec-reseau-portail.md` — portail captif et **reprise après coupure**.
  - `dev/spec-home-assistant.md` — liaison WebSocket (lecture d'entités,
    événements de notification), **publication MQTT** des capteurs locaux
    (découverte, page `/mqtt`), météo de repli.
  - `dev/spec-alertes.md` — canal d'alerte et moteur de règles.
  - `dev/spec-qualite-air.md` — ENS160 + AHT21, niveaux, logo air, alarme
    qualité d'air, auto-échauffement.
  - `dev/spec-serveur-web.md` — pages, API, wizards.
  - `dev/spec-liaison-hub.md` — liaison au hub `app-house` (**phase 2,
    proposition à valider, rien d'implémenté**).
  - `cartes/horloge-v1/` — fiche matériel : brochage, alimentation, budget de
    courant, adaptation de niveau, pièges.
  - `maquette-web/` — maquette **simulée** de la page web (un fichier HTML
    autonome), base de design ; ce n'est pas la page du firmware.

## ⚠️ Règle de documentation (À RESPECTER)

Avant de modifier une partie du projet, **lis** `documentation/dev/` (journal,
todo, plan de test) et `documentation/architecture.md`.

Après toute modification non triviale, **mets à jour / complète** ces notes (ce
qui a changé, pourquoi, état) : une entrée en tête de `dev/journal.md`, les
tâches de `dev/todo.md`, `architecture.md` si la structure bouge. La doc de dev
est la **mémoire vivante** du projet : on la garde **toujours à jour
automatiquement**, sans attendre qu'on le demande.

## ⚠️ Règle de plan de test (À RESPECTER, en tout temps)

**Tout se teste.** Le **plan de test doit rester à jour EN PERMANENCE** et
couvrir **tous** les tests à tout moment :
- Tests host : `firmware/test/` — composants purs `display`, `brightness`,
  `network`, `homeassistant`, `alerts`, `air_quality`, `settings`.
  Lancer : `bash firmware/test/run_host_tests.sh` ; couverture (objectif ≥ 95 %,
  échec en dessous) : `bash firmware/test/run_host_tests.sh --coverage`.
- Plan de test : `documentation/dev/test-plan-hw.md` (matrice test ↔ code
  couvert ↔ procédure ↔ attendu ↔ statut/date), tests host **et** vérifications
  sur matériel réel.

À **chaque** modification de code OU ajout de test, **automatiquement** (sans
attendre qu'on le demande) :
1. **Re-jouer** les tests dont le code couvert change, **mettre à jour le statut**.
2. **Actualiser** procédure/attendu si le comportement change.
3. **Retirer** les tests obsolètes ; **ajouter** un test pour toute nouvelle
   feature. **Si une dépendance (dalle, ADC, réseau, horloge, WebSocket Home
   Assistant) empêche de tester, on la moque** derrière une interface injectée,
   avec une doublure dans `firmware/test/` — on ne renonce pas au test. Un
   nouveau test doit d'abord **échouer** sur le défaut qu'il vise.
Aucune feature ne doit rester sans test ni avec un test périmé. Les tests
tournent **sur le PC**, jamais sur la cible ; la vérif matérielle est une étape
séparée, documentée dans le plan de test.

## ⚠️ Règle des wizards de configuration (À RESPECTER, en tout temps)

La page web permet un **paramétrage complet**, et chaque domaine est aussi
accessible par un **wizard** pas-à-pas qui rend la mise en service simple
(correspondance du panneau vérifiée à l'œil, étalonnage de la photorésistance,
détection et préchauffage du capteur d'air, liaison Home Assistant, création
d'une règle d'alerte).

À **chaque** modification qui touche la configuration (paramètre ajouté,
renommé, supprimé, borne ou valeur par défaut changée, scène, règle, entité
suivie), **le wizard concerné est mis à jour dans le même changement**,
automatiquement :
1. Le wizard couvre **tous** les paramètres de son domaine, ni plus ni moins que
   la page de paramétrage complet et que le modèle de config du firmware.
2. Ses textes d'aide, valeurs par défaut et bornes sont **ceux du firmware**
   (une seule source de vérité, jamais recopiés à la main :
   `/api/<domaine>/modele`).
3. Un **test** vérifie la cohérence wizard ↔ modèle de config (aucun paramètre
   orphelin d'un côté ou de l'autre), et le plan de test le référence.
Un wizard périmé est un **défaut**, au même titre qu'un test périmé.

## ⚠️ Règle « aucun acquittement » (À RESPECTER, en tout temps)

Une alerte **revient** (passage toutes les `rappel_s`) **tant que sa condition
tient**, et tombe **d'elle-même** quand le système concerné revient à la normale.
Ni bouton, ni page, ni commande ne la fait taire. Une notification externe tenue
ne tombe que par `effacer` de **sa source**. Ajouter un acquittement est un
**défaut** (EX-46, `dev/spec-alertes.md` §3).

## ⚠️ Règle d'honnêteté de l'affichage (À RESPECTER, en tout temps)

L'horloge **n'invente jamais** ce qu'elle affiche :
- Heure jamais synchronisée depuis le démarrage ⇒ `--:--`, pas une heure fausse.
- Valeur Home Assistant absente, invalide ou **périmée** ⇒ `--`, pas la dernière
  valeur connue présentée comme fraîche.
- Mesure de luminosité invalide ⇒ luminosité de **repli** nommée et signalée
  dans les diagnostics, jamais 0 ni 255 par accident.
- ENS160 en **préchauffage** (3 min après chaque mise sous tension) ou en défaut
  ⇒ qualité d'air `--`, logo **gris** (jamais vert), **aucune** alerte « AERER »
  sur une mesure que le capteur déclare lui-même invalide.
- Vers Home Assistant en MQTT, une grandeur invalide est publiée **`null`**
  (entité *inconnue*), jamais la dernière valeur connue.
- Une règle d'alerte dont la source est invalide **ne déclenche pas** ; c'est une
  règle **interne** (`donnee_perimee`, `liaison_perdue`, `heure_non_sure`) qui
  signale le problème.

## ⚠️ Normes de code — firmware (sûreté)

Tout le C++ du firmware est écrit comme par un **ingénieur logiciel embarqué
senior orienté sûreté**, en **C++17**, respectant : **MISRA C++ 2023**,
**AUTOSAR C++14** (si pertinent), **CERT C++**.

Contraintes **impératives** :
- **Aucune allocation dynamique après init** (pas de `new`/`malloc` en runtime ;
  tampons statiques dimensionnés à la compilation).
- **Pas d'exceptions, pas de RTTI, pas de récursion.**
- **`std::array`** (jamais `std::vector` ni conteneurs dynamiques).
- Types entiers explicites **`uint8_t/uint16_t/uint32_t/int32_t`** (pas de `int`
  nu) ; unités physiques entières explicites (**centi-°C**, **mA**, **µA**, ms) —
  **pas de flottant** dans la logique.
- **Gestion d'erreur robuste** par valeur de retour / `enum class Status` ;
  vérifier tous les retours d'API (`esp_err_t` inclus).
- **Doxygen complet** (`@brief`, `@param`, `@return`, `@note`) sur chaque API.
- **Séparation `.hpp`/`.cpp`**, logique pure **isolée du HAL/ESP-IDF** derrière
  des interfaces → testable sur host (> 95 %).
- Build host en **`-Werror`** avec `-Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow -Wold-style-cast …`.
- Compatible **FreeRTOS** et **ESP32**.
- **Entrée non sûre** : tout ce qui vient du réseau (formulaire du portail,
  requête DNS, message WebSocket de Home Assistant, corps de `POST /api/…`) est
  décodé de façon bornée, sans allocation, et **rejeté** plutôt que réparé.

## Conventions

- **Zéro valeur magique** : constantes nommées avec leur unité, réglages validés
  (`validate()` / `is_valid()`, repli sur des valeurs sûres), brochage dans
  `clock_board`. **Zéro secret dans le dépôt** (jeton Home Assistant, clés
  WiFi : NVS uniquement).
- **Git : commit/push uniquement sur demande.** Sur `main`, créer une branche
  d'abord. Commits finissant par une ligne de co-auteur nommant le **modèle
  réellement utilisé**. Actuellement :
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
- Résultats rapportés **fidèlement** : un test qui échoue, on le dit.
- Logs ESP explicites (`ESP_LOGx`, tag par module), **en anglais** comme le code.
- Dépôt **public** : aucun secret, aucune adresse IP de la maison, aucun
  identifiant dans les commits ni dans les fixtures (numéros de série masqués).
- UI : **aucun emoji**, icônes = **SVG inline** style ligne, **aucune ressource
  externe / CDN**, thème clair/sombre auto (`prefers-color-scheme`).

## Matériel / env

- Cible **`horloge-v1`** : devkit **ESP32-WROOM-32** (4 Mo de flash, **pas de
  PSRAM**), dalle **WS2812B 8×32** (256 LEDs), **photorésistance sur GPIO33**
  (ADC1_CH5 — ADC1 fonctionne avec le WiFi actif, ADC2 non), module
  **ENS160 + AHT21** sur I²C (**GPIO27 SDA / GPIO26 SCL**, adresses `0x53` et
  `0x38`).
- ⚠️ **Auto-échauffement** : 256 LEDs chauffent le boîtier ; un AHT21 à côté lit
  plusieurs degrés de trop, et cette erreur se propage à l'AQI de l'ENS160 par la
  compensation. Capteur **déporté**, écart mesuré par le wizard, décalage
  visible sur la page. Voir `dev/spec-qualite-air.md` §8.
- ⚠️ **Adaptation de niveau** : 3,3 V en sortie ESP32 contre un Vih de 3,5 V
  attendu par le WS2812B à 5 V — **hors spec**, même si « ça marche souvent ».
  Prévoir un 74AHCT125, ou alimenter la première LED à ~4,5 V. Voir
  `documentation/cartes/horloge-v1/README.md`.
- ⚠️ **Courant** : 256 LEDs en blanc plein ≈ **15 A**. Le firmware **plafonne le
  courant** (budget en mA, écrêtage proportionnel) ; alimentation 5 V / 5 A
  minimum, injection aux deux extrémités de la dalle.
- ESP-IDF **v5.3.2** dans `~/esp/esp-idf`. ⚠️ `idf.py` **refuse Git Bash/MSys** :
  build en **PowerShell** via `export.ps1`. Les tests host tournent sous Git Bash
  (g++ MinGW `C:\msys64\mingw64\bin`).
- USB/série **instable** sous Windows : revérifier le port
  (`[System.IO.Ports.SerialPort]::GetPortNames()`) avant de flasher. La carte
  en main : **ESP32-D0WD-V3 rév. 3.0, 4 Mo, Vref en eFuse, pont CP210x** (COM21
  le 2026-11-05 — relevé dans `documentation/cartes/horloge-v1/README.md` §0).
  ⚠️ **COM15 est la carte ThermoSync (ESP32-S3)** : ne jamais la flasher d'ici.
  `esptool.py` n'est pas dans le `PATH` PowerShell : utiliser
  `~\.espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe -m esptool`.
