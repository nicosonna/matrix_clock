# Journal de développement — Horloge

> Récent en haut. Une entrée par modification non triviale : date, quoi,
> pourquoi, état.

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
