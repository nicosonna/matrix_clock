# Backlog — Horloge

> Tâches identifiées et décisions en suspens. Coché quand c'est fait **et testé
> et documenté**. Retiré quand ça devient sans objet.

---

## Décisions en attente (bloquent peu, mais à trancher)

- [ ] **Q1 — bouton physique** sur GPIO27 ? Sans lui : aucun portail forcé
      hors page web. Proposition : court = scène suivante, long 5 s = portail,
      très long 15 s = usine. (Pas d'acquittement : EX-46.)
- [x] ~~Q3 — fuseau horaire par défaut~~ : `Europe/Zurich`, réglable sur la page
      (tranché le 2026-11-05).
- [ ] **Q4 — entités Home Assistant** à afficher au départ (température
      extérieure + une pièce ?), pour les scènes par défaut.
- [ ] **Q2 — RTC DS3231** : utile seulement si les coupures sans réseau sont
      fréquentes. Phase 3 par défaut.
- [x] ~~Mettre le dépôt sous Git~~ : `git init` + `.gitignore` + `README.md`
      anglais, commit initial le 2026-11-05.
- [ ] Choisir une **licence** avant de publier (MIT ? GPL-3.0 ? — c'est un choix
      de Nicolas, pas un défaut).
- [ ] Table **fuseaux IANA → règles POSIX** compilée dans l'image (liste courte :
      Europe, Amérique du Nord, UTC), sélection sur `/systeme`, application à
      chaud (`setenv("TZ")`, `tzset()`).

## Phase 1 — à faire, dans l'ordre

- [ ] Squelette ESP-IDF : `firmware/CMakeLists.txt`, `main/`, `partitions.csv`
      (table de `spec-serveur-web.md` §5), `sdkconfig.defaults`,
      `sdkconfig.defaults.horloge-v1`, build propre.
- [ ] `clock_board` : brochage `constexpr` + Kconfig, profils `horloge-v1` et
      `devkit`.
- [ ] Harnais de test host : `firmware/test/` (CMake + g++),
      `run_host_tests.sh`, macros `CHECK*` **sans `assert`**, agrégation gcov,
      échec sous 95 %, **et le test `operator new`** qui interdit l'allocation
      après init.
- [ ] `display` pur : tampon, correspondance, gamma, plafond de courant
      (`AFF-01` → `AFF-07`).
- [ ] `clock_display` : `espressif/led_strip`, dorsale **SPI/DMA**,
      effacement explicite au boot, motifs de test. **Mesurer** la charge
      processeur SPI vs RMT et consigner au journal.
- [ ] Polices 5×7, 3×5, 3×4 + `translitere()` + scènes + défilement
      (`AFF-08` → `AFF-14`).
- [ ] `brightness` pur (`LUM-01` → `LUM-14`) ; `clock_hal` ADC1 oneshot +
      étalonnage ; asservissement.
- [ ] **Lire `PART_ID` sur le module ENS160 + AHT21 réel** et confronter la
      table des registres de `spec-qualite-air.md` §2 à la fiche technique
      (dette D11) — **avant** d'écrire le décodage.
- [ ] `air_quality` pur : `BusI2c` injecté, ENS160 (validité, compensation, décodage),
      AHT21 (CRC8), niveaux (hystérésis, confirmation), décalages
      (`AIR-01` → `AIR-22`) ; trames réelles en `test/fixtures/`.
- [ ] `clock_hal` I²C : pilote, réinitialisation d'un bus bloqué, aucune
      attente bloquante ; boucle de mesure dans la tâche `periodique`.
- [ ] Logo air (glyphes 6×6 et 8×8) + scène `Air` + alternance de la zone
      d'icônes (`AFF-16`).
- [ ] `network` : **copier** le composant de ThermoSync, puis ajouter `EtatLien`,
      `CauseEchec`, grâce au démarrage et `AttenteBail` / `BailDesespere`.
      Vérifier que `RES-01` → `RES-11` passent **avant** d'ajouter quoi que ce
      soit, puis `RES-20` → `RES-29`.
- [ ] `clock_web` : serveur HTTP, DNS captif, sondes de détection de portail,
      mDNS, page `/wifi`, affichage du SSID et de la phrase de passe **sur la
      dalle**.
- [ ] `clock_time` : SNTP, fuseau, **validité** de l'heure (extraire la partie
      testable de la validité).
- [ ] `settings` pur + `/api/<domaine>/modele` + `POST` tout-ou-rien
      (`WEB-01` → `WEB-08`), puis les pages et les wizards.
- [ ] OTA + **rollback** (`esp_ota_mark_app_valid_cancel_rollback` après dalle
      allumée **et** serveur en écoute) + barre de progression sur la dalle.
- [ ] `homeassistant` pur : machine d'état WebSocket, `render_template`, lecteur JSON
      sans allocation, décodage typé, péremption (`HA-01` → `HA-15`).
- [ ] `clock_homeassistant` : `esp_websocket_client`, réassemblage borné 2 Ko,
      ping applicatif ; météo Open-Meteo en repli.
- [ ] `homeassistant::mqtt` : **copier** `home_assistant.hpp/.cpp` et
      `mqtt_config.hpp/.cpp` de ThermoSync, remplacer `thermosync` par
      `horloge`, remplacer les entités par les 10 de `spec-home-assistant.md`
      §6 bis, générer la liste depuis le modèle (`MQ-01` → `MQ-10`).
- [ ] `clock_homeassistant` : client `esp-mqtt` (testament, découverte QoS 1
      retenue, état QoS 0, renvoi sur `<prefixe>/status`) ; page `/mqtt` reprise
      de ThermoSync avec son wizard.
- [ ] `campagne_ha_docker.py` + `ha_docker/` : Home Assistant de **test** en
      Docker **avec Mosquitto** (WebSocket **et** MQTT). **On ne pilote jamais
      celui de la maison depuis les tests.**
- [ ] `alerts` pur : règles, file, heures silencieuses (`ALR-01` → `ALR-18`) ;
      rendu par priorité dans `display` ; canal entrant partagé par les trois
      portes d'entrée.
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
- [ ] **D10** — auto-échauffement du module air : décider du déport (Q10) et
      relever l'écart réel (HW-AIR-05). Un décalage qui dépend de la scène
      affichée ne se corrige pas en logiciel.
- [ ] **Q11** — grandeur pilote du niveau d'air (AQI / eCO2 / TVOC) à
      reconsidérer après quelques jours de relevés.
