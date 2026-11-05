# Backlog — Horloge

> Tâches identifiées et décisions en suspens. Coché quand c'est fait **et testé
> et documenté**. Retiré quand ça devient sans objet.

---

## Décisions en attente (bloquent peu, mais à trancher)

- [ ] **Q1 — bouton physique** sur GPIO25 ? Sans lui : aucun portail forcé
      hors page web. Proposition : court = scène suivante, long 5 s = portail,
      très long 15 s = usine. (Pas d'acquittement : EX-46.)
- [x] ~~Q3 — fuseau horaire par défaut~~ : `Europe/Zurich`, réglable sur la page
      (tranché le 2026-11-05).
- [ ] **Q4 — entités Home Assistant** à afficher au départ (température
      extérieure + une pièce ?) : à saisir sur `/ha` (valeur 1 = extérieur,
      remplacée par la météo Open-Meteo si elle est activée et que la valeur
      manque).
- [ ] **Q2 — RTC DS3231** : utile seulement si les coupures sans réseau sont
      fréquentes. Phase 3 par défaut.
- [x] ~~Mettre le dépôt sous Git~~ : `git init` + `.gitignore` + `README.md`
      anglais, commit initial le 2026-11-05.
- [ ] Choisir une **licence** avant de publier (MIT ? GPL-3.0 ? — c'est un choix
      de Nicolas, pas un défaut).
- [x] ~~Table fuseaux IANA → POSIX, sélection sur `/systeme`, application à chaud~~
      (règle maison, pas `tzset()` ; 2026-11-05).

## Phase 1 — à faire, dans l'ordre

- [x] ~~Squelette ESP-IDF~~ : `CMakeLists.txt`, `main/`, `partitions.csv`,
      `sdkconfig.defaults` + surcouche `horloge-v1`, build propre (2026-11-05).
- [x] ~~`clock_board`~~ : profils `horloge-v1` et `devkit`, I²C sur 27/26
      (2026-11-05).
- [x] ~~Harnais de test host~~ : `CHECK*` sans `assert`, `AllocGuard`
      (`operator new` piégé), gcov, échec sous 95 % (2026-11-05).
- [x] ~~`display` pur~~ : `AFF-01` → `AFF-07` + `AFF-17`, 100 % couvert
      (2026-11-05).
- [x] ~~`clock_display`~~ : `led_strip` 2.5.5, SPI/DMA, chenillard et motifs
      vérifiés sur la dalle (HW-AFF-01, 2026-11-05).
- [ ] **Mesurer** la charge processeur SPI vs RMT (les deux dorsales marchent ;
      overlay `sdkconfig.defaults.rmt`) et consigner au journal.
- [ ] HW-AFF-02/03 : courant à la pince et tension à la dalle sur blanc plein.
- [x] ~~Polices 5×7, 3×5, 3×4 + translittération + scènes Heure/Date +
      défilement + ordonnanceur~~ (`AFF-08` → `AFF-15`, `AFF-18`, 2026-11-05).
      Restent dans ce domaine : scène `Valeur` (icônes 8×8), `DeuxLignes`,
      `Nuit`, zone d'icônes (`AFF-16`), transition glissée (option), et le rendu
      des notifications (bandeau / défilement selon la priorité) qui attend le
      composant `alerts`.
- [x] ~~`brightness` pur (`LUM-01` → `LUM-14`) ; `clock_hal` ADC1 oneshot +
      étalonnage ; asservissement~~ (2026-11-05, câblage (a) confirmé). Reste :
      relevés nuit/soir/jour en conditions stables (HW-LUM-01) et le wizard
      quand la page web existera.
- [x] ~~Lire `PART_ID` sur le module réel~~ : 0x0160 à 0x53, OPMODE / STATUS
      conformes, AHT21 calibré (HW-AIR-08, 2026-11-05). Reste à confronter les
      registres `DATA_*` quand le capteur sort de sa première heure.
- [x] ~~`air_quality` pur : `I2cBus` injecté, ENS160, AHT21 (CRC8), machine
      d'état non bloquante, niveaux, décalage~~ (`AIR-01` → `AIR-19`, `AIR-21`,
      `AIR-22`, 2026-11-05). Reste : trames AHT21 **réelles** en
      `test/fixtures/` (AIR-06 est synthétique), AIR-20 avec le moteur de règles.
- [x] ~~`clock_hal` I²C : `i2c_master`, délai 50 ms par transaction,
      réinitialisation d'un bus bloqué (9 coups d'horloge) ; tâche `air`
      (cœur 0, 20 ms)~~ (2026-11-05).
- [x] ~~Logo air (glyphes 6×6 et 8×8) + scène `Air` + logo dans la zone
      d'icônes~~ (2026-11-05). Reste : alternance de la zone d'icônes avec
      d'autres icônes (`AFF-16`), couleurs par niveau réglables.
- [x] ~~`network` : repris de ThermoSync, traduit, extension coupure
      (`LinkStatus`, `FailCause`, `BootGrace`, `WaitingLease`,
      `LeaseDesperate`, redémarrage)~~ — `RES-01` → `RES-12`, `RES-20` → `RES-29`
      (2026-11-05).
- [x] ~~`clock_web` : HTTP, DNS captif, sondes de portail, mDNS, page `/wifi`,
      SSID + mot de passe sur la dalle, `POST /api/notification`~~ (2026-11-05).
      Reste : **vérifier au téléphone** (HW-RES-05/06/07), coupure secteur
      (HW-RES-01/02), DHCP coupé (HW-RES-03).
- [x] ~~`alerts` : moteur de règles, file bornée, heures silencieuses, rendu
      par priorité, page `/alertes` et wizard~~ (`ALR-01` → `ALR-18`,
      2026-11-05). Reste : HW-ALR-01 (sonde refroidie), HW-ALR-03 (une nuit
      complète), passages `AERER` observés sur une heure.
- [x] ~~`timekeeping` pur (règles POSIX, heure locale + DST, validité) +
      `clock_time` (SNTP)~~ — `TIME-01` → `TIME-08`, HW-TIME-01 (2026-11-05).
      Reste : serveur NTP et fuseau en **NVS** via la page `/systeme` (liste
      `named_zones()`), bascule réelle à observer le 29 mars 2026 (HW-TIME-02).
- [x] ~~`settings` pur + `/api/<domaine>/modele` + `POST` tout-ou-rien
      (`WEB-01` → `WEB-08`), pages `/affichage`, `/luminosite`, `/systeme` et
      wizards (correspondance, photorésistance)~~ (2026-11-05). Reste :
      HW-WEB-01 (téléphone), HW-SYS-01, couleurs **réglables** sur la page
      (thème fixe pour l'instant), libellés des `Valeur n` = nom de l'entité
      quand `homeassistant` existera.
- [x] ~~OTA + **rollback** + barre de progression sur la dalle~~ (2026-11-05,
      HW-WEB-02). Reste HW-WEB-03 (image cassée ⇒ rollback).
- [x] ~~`homeassistant` pur : machine d'état WebSocket, `render_template`, lecteur
      JSON sans allocation, décodage typé, péremption~~ (`HA-01` → `HA-15`,
      2026-11-05).
- [x] ~~`clock_homeassistant` : `esp_websocket_client`, réassemblage borné 2 Ko,
      ping applicatif ; météo Open-Meteo en repli~~ (2026-11-05). Reste :
      HW-HA-01..04 avec le Home Assistant de la maison (jeton à créer par
      Nicolas), aperçu « en direct » d'une entité dans le wizard (étape 3 :
      pour l'instant on enregistre puis on lit la tuile).
- [x] ~~`homeassistant::mqtt` : repris de ThermoSync, 10 entités, `null` pour
      l'invalide, `MqttPublisher` pur~~ (`MQ-01` → `MQ-10`, 2026-11-05).
- [x] ~~`clock_homeassistant` : tâche `mqtt` sur `esp-mqtt` (testament,
      découverte QoS 1 retenue, état QoS 0, renvoi sur `<prefixe>/status`) ;
      page `/mqtt` avec wizard~~ (2026-11-05, vérifié sur le broker de banc
      `test/bench/mini_broker.py`). Reste : HW-MQ-01..03 sur le **vrai**
      Home Assistant + Mosquitto (utilisateur dédié à créer par Nicolas),
      HW-MQ-02 au démarrage à froid.
- [ ] `campagne_ha_docker.py` + `ha_docker/` : Home Assistant de **test** en
      Docker **avec Mosquitto** (WebSocket **et** MQTT). **On ne pilote jamais
      celui de la maison depuis les tests.**
- [x] ~~`alerts` pur : règles, file, heures silencieuses ; rendu par
      priorité ; canal entrant partagé~~ (2026-11-05). Phase 2 : la porte
      d'entrée du hub (`commande notifier`).
- [ ] Maquette web autonome dans `documentation/maquette-web/index.html` (base de
      design, pas la page du firmware).
- [ ] Campagne de banc, puis les vérifications `HW-*` du plan de test.

## Phase 2 — hub

- [ ] Déposer l'entrée dans `../data_logger_website/documentation/retours-horloge.md`
      (brouillon prêt : `spec-liaison-hub.md` §5). **Seulement quand la phase 2
      démarre**, et on ne modifie rien d'autre dans le dépôt du hub.
- [ ] Attendre la réponse du hub sur la question unique (automatisation capable
      de pousser `commande notifier` sur condition).
- [ ] `liaison_hub` pur + `horloge_hub` : enrôlement HTTP, WSS, enveloppe,
      compteur d'`id` **persisté en NVS**, spool borné, code d'appairage affiché
      sur la dalle.
- [ ] `campagne_hub.py` + `hub_docker/` sur le modèle de ThermoSync.

## Phase 3 — confort

- [ ] Entités de **commande** MQTT (luminosité `number`, extinction `switch`,
      scène `select`, message `text`) sur `horloge/<id>/<réglage>/set`, ajoutées
      à la découverte déjà en place en phase 1.
- [ ] **`wss`** vers Home Assistant avec certificat auto-signé accepté
      explicitement (phase 1 n'accepte que les certificats du paquet d'ESP-IDF).
- [ ] Mot de passe d'administration sur la page web **et sur l'OTA** (dette D2).
- [ ] Phrase de passe du point d'accès **réglable** au lieu de dérivée de la MAC
      (dette D1, héritée de la borne et de ThermoSync).
- [ ] Police **accentuée** sur 8 lignes, ou interligne réduit (dette D4).
- [ ] Résolution **mDNS** pour saisir l'hôte Home Assistant en `.local`
      (dette D8, partagée avec ThermoSync).
- [ ] RTC DS3231 (dette D5), ou exploiter la capacité `heure` du hub.

## Dette à surveiller

- [ ] **D6** — `render_template` ne rend que sur changement : une valeur stable
      est indiscernable d'une liaison morte. La péremption ne s'applique que si
      la liaison est tombée. **À figer à l'implémentation**, test `HA-12`.
- [ ] **D7** — médiane sur 5 : deux pics consécutifs passent. Accepté, mais à
      revoir si un éclairage clignotant pose problème en vrai.
- [ ] Constante `kCourantCanalPleinUa` (20 mA) à **confronter à la pince** sur le
      lot de LEDs réel (vérification 2 de la fiche carte). Un écart > 20 % se
      corrige dans la constante, pas dans le budget.
- [ ] Taille de la page gzippée à relever **à chaque build** (budget 60 Ko).
- [ ] CPU à **160 MHz** par défaut (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`) : passer
      à 240 MHz si le rendu + WiFi + HTTP le demandent ; à mesurer, pas à
      supposer.
- [ ] **D10** — auto-échauffement du module air : décider du déport (Q10) et
      relever l'écart réel (HW-AIR-05). Un décalage qui dépend de la scène
      affichée ne se corrige pas en logiciel.
- [ ] **Q11** — grandeur pilote du niveau d'air (AQI / eCO2 / TVOC) à
      reconsidérer après quelques jours de relevés.
