# Cadrage fonctionnel — Horloge (2026-11-05)

> **Statut : phase 1 implémentée le 2026-11-05** (étapes 1 à 10 ci-dessous,
> vérifiées sur host et sur la carte ; l'étape 11 — campagne matérielle — est
> décrite dans `plan-de-test-final.md`). Ce fichier reste la référence
> fonctionnelle : vision, exigences, affectation du matériel, phases, questions
> ouvertes. **À lire avant toute implémentation**, et à mettre à jour dès qu'un
> choix est arrêté.

---

## 1. Vision

Une **horloge matricielle de salon** qui affiche l'heure en grand, s'assombrit
toute seule la nuit, et sert de **tableau d'information discret** pour la maison :
températures et autres capteurs lus dans **Home Assistant**, et **notifications**
poussées par la domotique (porte restée ouverte, sèche-linge fini, gel annoncé).

Elle se met en service **sans outil** : à la première mise sous tension elle
ouvre son propre point d'accès WiFi et un **portail captif**, exactement comme la
borne de recharge et ThermoSync. Tout le reste se règle ensuite depuis une **page
web** servie par la carte, avec un **wizard** par domaine.

Elle est **autonome** : une fois réglée, elle affiche l'heure et évalue ses règles
d'alerte même si Home Assistant est éteint, et se remet en marche seule après une
coupure de courant, y compris quand la box Internet redémarre plus lentement
qu'elle.

### Non-buts

- **Pas d'écran tactile, pas de son.** L'horloge affiche ; elle ne pilote rien
  dans la maison et ne porte **aucune sécurité**.
- **Pas de pilotage de Home Assistant** par l'horloge (aucun service appelé) en
  phase 1 : la liaison WebSocket est en **lecture seule**, plus un canal
  d'événements entrant. L'horloge **publie** ses propres capteurs par MQTT, elle
  n'agit sur rien.
- **Pas de lecture d'entités par MQTT** (choix du 2026-11-05 : WebSocket pour
  lire). MQTT ne sert qu'à **publier** les capteurs locaux vers Home Assistant
  (découverte, comme la borne et ThermoSync) — et en phase 1 sans entité de
  **commande** : voir `spec-home-assistant.md` §6 bis et §7.
- **Pas de Cloud, pas de compte.** Une seule sortie Internet optionnelle : la
  météo de repli (Open-Meteo, sans clé), désactivable.
- **Pas d'affichage graphique riche** (images, animations complexes) : 8 lignes
  de 32 pixels, on fait du texte, des icônes 8×8 et des bandeaux de couleur.

---

## 2. Exigences

Identifiants `EX-nn`, repris par le plan de test.

### Affichage

| Id | Exigence |
|---|---|
| EX-01 | L'heure est affichée en `HH:MM` lisible depuis l'autre bout de la pièce : police 5×7, 4 chiffres + séparateur, centrée sur la dalle 8×32. |
| EX-02 | Tant que l'heure n'a **jamais** été synchronisée depuis le démarrage, l'affichage montre `--:--` et un indicateur de défaut ; **jamais** une heure fausse. |
| EX-03 | Le fuseau horaire est **`Europe/Zurich` par défaut** et **réglable depuis la page web** (liste de fuseaux IANA → règle POSIX compilée dans l'image, appliquée **sans redémarrage**). Le passage heure d'hiver / heure d'été est automatique. |
| EX-04 | L'affichage enchaîne des **scènes** configurables (heure, date, valeur de capteur, texte défilant), chacune avec sa durée et son activation. |
| EX-05 | Le **courant total** de la dalle est estimé par le firmware et **plafonné** à un budget réglable en mA, par écrêtage proportionnel des trois canaux. |
| EX-06 | La **correspondance** entre coordonnées (x, y) et index de LED est un réglage (serpentin/progressif, coin d'origine, sens), vérifiable par un wizard visuel. |
| EX-07 | Une **correction gamma** est appliquée pour que les basses luminosités restent progressives. |

### Luminosité

| Id | Exigence |
|---|---|
| EX-10 | La luminosité suit la lumière ambiante mesurée par la photorésistance de GPIO33, de façon **lente et sans pompage** (pas de variation visible d'une seconde à l'autre). |
| EX-11 | La correspondance mesure → luminosité est une **courbe réglable** (points monotones), avec un **plancher de nuit** et un plafond de jour. |
| EX-12 | Un mode **manuel** fixe la luminosité ; un mode **extinction** éteint la dalle (plage horaire ou commande). |
| EX-13 | Une mesure invalide (ADC en erreur, valeur hors plage) ⇒ luminosité de **repli** nommée, signalée dans les diagnostics ; jamais 0 ni 255 par accident. |
| EX-14 | Un wizard d'étalonnage construit la courbe à partir de mesures prises **dalle éteinte puis allumée**, pour quantifier l'auto-éclairage du capteur. |

### Réseau et mise en service

| Id | Exigence |
|---|---|
| EX-20 | Sans réseau configuré : point d'accès `Horloge-XXXX`, **portail captif**, page `/wifi`. Comportement repris de la borne et de ThermoSync. |
| EX-21 | Réseau configuré : essais de connexion **point d'accès éteint** ; en repli, point d'accès + portail et **station au repos** (une station qui réessaie en fond fait sauter le canal radio et déconnecte les clients du point d'accès — constaté sur la borne). |
| EX-22 | Après une **coupure de courant**, l'horloge se reconnecte seule. Pendant une **fenêtre de grâce** au démarrage (défaut 5 min), les échecs n'ouvrent **pas** le portail : on réessaie avec un délai croissant, parce que la box redémarre plus lentement que la carte. |
| EX-23 | **Pas de bail DHCP** est distingué de **point d'accès introuvable** : associé sans IP ⇒ on attend `dhcp_ms` puis on relance, sans compter un échec de réseau. |
| EX-24 | Les identifiants sont en **NVS** et pris en compte **sans redémarrage**. Aucun identifiant dans le dépôt. |
| EX-25 | La page est joignable par mDNS (`horloge.local`) en plus de son adresse IP. |

### Home Assistant

| Id | Exigence |
|---|---|
| EX-30 | L'horloge lit jusqu'à **8 valeurs** de Home Assistant, chacune désignée par une entité et un gabarit, en **WebSocket** avec un jeton longue durée. |
| EX-31 | Les valeurs arrivent **en poussée** (pas de scrutation) et la valeur **initiale** est obtenue dès l'abonnement. |
| EX-32 | Une valeur non reçue depuis `peremption_s` (défaut 300 s) devient **invalide** : l'affichage montre `--`. Jamais une valeur périmée présentée comme fraîche. |
| EX-33 | La liaison se **rétablit seule** (délai croissant plafonné), et son état est visible sur la page et comme source de règle d'alerte. |
| EX-34 | Le **jeton** est stocké en NVS, s'écrit mais **ne se relit pas** depuis la page web. |
| EX-35 | Une **météo de repli** (Open-Meteo, sans clé, lat/lon réglables) fournit la température extérieure quand Home Assistant est absent ou l'entité non configurée ; désactivable. |
| EX-36 | Les grandeurs **locales** (température, humidité, eCO2, TVOC, AQI, niveau d'air, état du capteur, lumière ambiante, heure sûre) sont **publiées vers Home Assistant par MQTT** avec **découverte** : aucun YAML côté Home Assistant, l'horloge apparaît comme un **appareil**. |
| EX-37 | Une grandeur invalide (préchauffage, capteur absent, CRC faux) est publiée **`null`** — entité *inconnue* — jamais une valeur inventée ni périmée. |
| EX-38 | La disponibilité est publiée avec un **testament** (`offline` à la disparition de l'horloge) et la découverte est **renvoyée** au redémarrage de Home Assistant. Réglages du broker en NVS, mot de passe en écriture seule. |

### Alertes et notifications

| Id | Exigence |
|---|---|
| EX-40 | Un **canal d'alerte entrant** accepte des notifications poussées : événement Home Assistant `horloge_notification` (WebSocket) et `POST /api/notification` (HTTP local). |
| EX-41 | Une notification porte titre, texte, **priorité** (0–3), durée, couleur et icône ; elle est rendue selon sa priorité (bandeau, défilement, clignotement). |
| EX-42 | La **file de notifications** est bornée (16), ordonnée par priorité puis arrivée, **dédoublonnée** par clé ; une ponctuelle expire, une **tenue** reste tant que sa règle est levée ou que sa source ne l'a pas effacée. |
| EX-46 | **Aucun acquittement.** Une alerte **revient** (passage toutes les `rappel_s`) tant que sa condition tient, et tombe **d'elle-même** quand le système concerné revient à la normale. Ni bouton, ni page, ni commande ne la fait taire. |
| EX-43 | Un **moteur de règles** local évalue jusqu'à 16 règles (source, comparaison, seuil, durée de confirmation, hystérésis, priorité, plage silencieuse) et lève des notifications : l'horloge alerte **même si Home Assistant est tombé**. |
| EX-44 | Une règle dont la **source est invalide ne déclenche pas**. Des règles **internes** (`donnee_perimee`, `liaison_perdue`, `heure_non_sure`) signalent le problème à la place. |
| EX-45 | Les **heures silencieuses** suppriment clignotement et défilement et imposent le plancher de luminosité ; la priorité 3 peut les outrepasser (réglable). |

### Qualité de l'air (ajout du 2026-11-05, ENS160 + AHT21)

| Id | Exigence |
|---|---|
| EX-70 | Le module **ENS160 + AHT21** (I²C) fournit AQI, TVOC, eCO2, température et humidité **locales**, disponibles **sans réseau**, affichables et utilisables comme sources de règles. |
| EX-71 | Le **drapeau de validité** de l'ENS160 est respecté : préchauffage (3 min après chaque mise sous tension), première mise en service (1 h) et sortie invalide ⇒ valeurs **invalides**, logo **gris**, aucune alerte. Jamais un AQI inventé. |
| EX-72 | L'ENS160 est **compensé** par la température et l'humidité de l'AHT21 à chaque cycle ; AHT21 en défaut ⇒ compensation de repli nommée et **signalée**, ENS160 toujours exploité. |
| EX-73 | Lectures AHT21 vérifiées par **CRC8** ; un CRC faux est **rejeté**, pas réparé. |
| EX-74 | Un **niveau** de qualité d'air (5 paliers, seuils réglables, grandeur pilote AQI / eCO2 / TVOC) est calculé avec **hystérésis** et **durée de confirmation** : le logo ne clignote pas entre deux couleurs. |
| EX-75 | **Option logo air** : aucun / logo permanent dans la zone d'icônes de l'heure / scène dédiée — **coloré selon le niveau**. |
| EX-76 | **Alarme qualité d'air** par le moteur de règles : air pas bon ⇒ `AERER` ; air mauvais ⇒ `AIR MAUVAIS` puis `AERER`. Textes et seuils réglables. |
| EX-77 | L'**auto-échauffement** du boîtier est mesuré par un wizard et compensé par un décalage **visible** ; au-delà de 2 °C le wizard demande de déporter le capteur. |

### Page web

| Id | Exigence |
|---|---|
| EX-50 | Paramétrage **complet** depuis la page : réseau, affichage, luminosité, Home Assistant, alertes, système. |
| EX-51 | Chaque domaine a un **wizard** pas-à-pas, dont les bornes et défauts sont lus sur `/api/<domaine>/modele` : **le firmware est la seule source de vérité**. |
| EX-52 | Page **autonome** (aucun CDN, aucune ressource externe), embarquée dans l'image, thème clair/sombre automatique, **aucun emoji**, icônes SVG inline. |
| EX-53 | Mise à jour **OTA** depuis la page, deux emplacements, retour à l'ancienne image si la nouvelle ne démarre pas. |

### Robustesse

| Id | Exigence |
|---|---|
| EX-60 | Watchdog de tâche actif ; une dépendance absente (réseau, Home Assistant, ADC) dégrade **proprement** avec un log, sans bloquer l'affichage. |
| EX-61 | L'affichage de l'heure n'est **jamais** interrompu par le réseau : la boucle d'affichage et les tâches réseau sont découplées (dernier état publié, pas d'attente). |
| EX-62 | Aucune allocation dynamique après l'initialisation. |

---

## 3. Affectation du matériel

Détail et pièges : `../cartes/horloge-v1/README.md`.

| Fonction | Broche | Note |
|---|---|---|
| Données de la dalle WS2812B | GPIO13 | MOSI de SPI2, dorsale SPI **avec DMA** du composant `led_strip`. Résistance série 330–470 Ω. |
| Photorésistance | **GPIO33** | ADC1_CH5. ADC1 **fonctionne avec le WiFi actif** (ADC2 non). Diviseur avec une résistance fixe. |
| **ENS160 + AHT21** | **GPIO27 (SDA) / GPIO26 (SCL)** | I²C0, adresses `0x53` (ENS160) et `0x38` (AHT21). Câblage relevé le 2026-11-05. Module **déporté** hors du flux de chaleur de la dalle. |
| Bouton (proposition, §5) | GPIO25 | Changer de scène, forcer le portail, réglages d'usine. **Pas d'acquittement** (EX-46). Non décidé. |
| RTC DS3231 (phase 3) | GPIO27/26 | Même bus I²C (`0x68`, pas de conflit). Garderait l'heure pendant une coupure. Non décidé. |

Alimentation : **5 V / 5 A minimum**, injection aux deux extrémités de la dalle,
condensateur de 1000 µF au plus près, masse commune avec l'ESP32.

---

## 4. Phases

### Phase 1 — l'horloge autonome (cœur)

Affichage, luminosité automatique, SNTP, portail captif avec reprise après
coupure, page web de paramétrage, moteur de règles local et canal d'alerte
entrant, liaison Home Assistant en lecture.

Ordre d'implémentation — chaque étape finit par ses tests host à jour. **Étapes 1 à 10 faites le 2026-11-05** (journal `suite 1` à `suite 14`) ; l'étape 11 est en cours :

1. Squelette ESP-IDF, `clock_board`, partitions, build propre.
2. `display` pur (tampon, correspondance, gamma, plafond de courant) + tests ;
   `clock_display` et un motif de test à l'écran.
3. Polices, scènes, défilement + tests ; `--:--` tant que l'heure n'est pas sûre.
4. `brightness` pur + tests ; `clock_hal` ADC ; boucle d'asservissement.
5. `air_quality` pur (ENS160 + AHT21 sur bus I²C moqué, validité, CRC, niveaux) +
   tests ; `clock_hal` I²C ; logo air dans `display`. **Première tâche :
   lire `PART_ID` sur le module réel et confronter la table des registres.**
6. `network` repris de ThermoSync + **extension coupure** + tests ; `clock_web`
   avec le portail et la page `/wifi`.
7. `clock_time` : SNTP, fuseau, validité.
8. `settings` + `clock_web` complet : pages, API, wizards, OTA.
9. `homeassistant` pur (machine d'état WebSocket, `render_template`, lecteur JSON,
   péremption) + tests ; `clock_homeassistant` ; météo de repli.
9 bis. `homeassistant::mqtt` **repris de ThermoSync** (découverte, état, `null`,
   `MqttConfig`) + tests ; client `esp-mqtt` ; page `/mqtt` ; Home Assistant de
   test en Docker pour la vérification.
10. `alerts` pur (règles, file) + tests ; rendu dans `display` ; canal
    entrant ; règles air par défaut.
11. Campagne de banc, puis vérifications matérielles du plan de test —
    **en cours** : broker MQTT de banc, canal d'alerte, OTA et rollback,
    capteur d'air réel vérifiés ; le reste (vrai Home Assistant, nuit,
    coupures) listé dans `plan-de-test-final.md` §4.

### Phase 2 — liaison au hub `app-house`

Enrôlement, descripteur généré, alertes poussées par le hub, règles
paramétrables depuis le hub. **Proposition à valider** côté hub :
`spec-liaison-hub.md`. Rien ne commence avant que le hub ait répondu dans
`../data_logger_website/documentation/retours-horloge.md`.

### Phase 3 — confort, si ça en vaut la peine

Entités de **commande** MQTT (luminosité, scène, extinction, message) ajoutées
à la découverte déjà en place en phase 1, RTC de secours, bouton physique,
TLS sur la liaison Home Assistant, police accentuée sur 8 lignes.

---

## 5. Questions ouvertes

| # | Question | Pourquoi ça compte | Proposition |
|---|---|---|---|
| Q1 | **Un bouton physique** sur la carte ? | Sans lui, aucun moyen de forcer le portail sans la page web. Un appui long « j'ouvre le portail » sauve une mise en service ratée. *(Il ne servirait **pas** à acquitter : EX-46.)* | Oui, un bouton sur GPIO25 : appui court = scène suivante, appui long 5 s = portail forcé, appui très long 15 s = réglages d'usine. |
| Q2 | **RTC de secours** (DS3231) ? | Après une coupure sans réseau, l'horloge affiche `--:--` jusqu'à ce que la box revienne. Avec un DS3231 elle affiche l'heure tout de suite. | Phase 3, sauf si la coupure sans réseau est un cas fréquent chez toi. |
| ~~Q3~~ | ~~Fuseau horaire par défaut ?~~ | **Tranché le 2026-11-05** : `Europe/Zurich` par défaut, réglable sur la page (EX-03). | — |
| Q4 | **Quelles entités** Home Assistant afficher au départ ? | Dimensionne le nombre de valeurs suivies et les scènes par défaut. | Température extérieure + température d'une pièce, le reste réglable. **À préciser.** |
| Q5 | **Mot de passe du point d'accès** dérivé de la MAC ? | Repris de la borne : déterministe donc **devinable** par qui connaît l'algorithme et lit le BSSID. Acceptable pour un portail de mise en service. | Garder pour la phase 1, le rendre réglable ensuite (dette connue, déjà notée sur ThermoSync). |
| Q6 | **Authentification de la page web** ? | Sans elle, n'importe qui sur le réseau local peut pousser une notification ou changer les réglages. ThermoSync fait ce choix aussi. | Phase 1 sans authentification, comme ThermoSync ; mot de passe d'administration optionnel en phase 3. Documenté comme risque accepté. |
| Q7 | **Accents** dans les textes défilants ? | Une capitale accentuée demande 8 lignes de haut, il n'en reste pas pour l'interligne. | Translittération (`É`→`E`) par une fonction pure testée, limitation documentée. Police accentuée en phase 3. |
| Q8 | **Deux lignes de texte** ? | 8 lignes ne permettent pas deux lignes de police 3×5 (10 px). Il faut une micro-police 3×4. | Scène `DeuxLignes` en micro-police 3×4, réservée aux valeurs courtes (`21.5` / `12:34`). |
| Q9 | **Dalle en un seul ruban ou 4 modules 8×8 chaînés ?** | Change la correspondance (x, y) → index. On ne le saura qu'en allumant. | Correspondance réglable + wizard visuel (EX-06) : on n'a pas besoin de trancher avant d'avoir la dalle en main. |
| Q10 | **Où va le module ENS160 + AHT21 ?** | Dans le boîtier, il lit la chaleur des LEDs et du régulateur, et l'erreur se propage à l'AQI. | **Déporté** au bout d'un câble I²C court (< 30 cm), ou sur la face arrière avec une ouverture d'air. Le wizard mesure l'écart et le dit (EX-77). |
| Q11 | **Grandeur pilote** du niveau d'air : AQI UBA, eCO2 ou TVOC ? | L'AQI est synthétique mais opaque ; l'eCO2 parle à tout le monde (« 1200 ppm ») mais c'est un **équivalent** calculé, pas une mesure de CO₂. | AQI par défaut, réglable. À reconsidérer après quelques jours de relevés. |

---

## 6. Décisions arrêtées

| Date | Décision | Raison |
|---|---|---|
| 2026-11-05 | **ESP32-WROOM-32** classique (4 Mo, sans PSRAM), carte `horloge-v1`. | Cohérent avec le brochage existant (photorésistance sur GPIO33 = ADC1_CH5). |
| 2026-11-05 | Photorésistance **analogique** sur ADC1, pas de capteur I²C. | C'est le montage en place. Conséquence : pas de lux absolus, une courbe à étalonner. |
| 2026-11-05 | Lecture de Home Assistant en **WebSocket**, abonnements `render_template`. | Poussée sans scrutation, valeur initiale immédiate, charge mémoire minuscule — alors que `get_states` renverrait tout l'état de Home Assistant, impossible sans PSRAM. |
| 2026-11-05 | MQTT **pour publier seulement** : les capteurs locaux partent vers Home Assistant par découverte MQTT (composant `domotique` de ThermoSync repris) ; **pas de lecture** par MQTT. *(Revient sur « pas de MQTT en phase 1 » pris le matin même.)* | Demande de Nicolas : comme la borne. Le composant existe déjà, pur et testé ; et l'horloge devient un appareil dans Home Assistant, ce qui rend les entités de commande de la phase 3 triviales. |
| 2026-11-05 | Règles d'alerte **dans l'horloge** (phase 1) + liaison hub **spécifiée puis validée** (phase 2). | L'horloge doit alerter même si Home Assistant et le hub sont tombés. |
| 2026-11-05 | Composant `network` **repris de ThermoSync** plutôt que réécrit, avec l'extension « coupure de courant ». | Logique déjà pure, déjà testée sur host, comportement déjà éprouvé sur la borne. |
| 2026-11-05 | Dorsale **SPI avec DMA** pour `led_strip`, RMT en repli. | Sur ESP32 classique le RMT n'a pas de DMA : 256 LEDs = 7,7 ms d'émission par trame, en interruptions. Le SPI décharge le processeur. À mesurer. |
| 2026-11-05 | **Aucun acquittement** des alertes : passages répétés tant que la condition tient, retrait automatique à la retombée (EX-46). | Demande de Nicolas. Une alerte qu'on peut faire taire d'un geste est une alerte qu'on oublie ; c'est le système concerné qui doit revenir à la normale. |
| 2026-11-05 | Fuseau **`Europe/Zurich`** par défaut, **réglable** sur la page, appliqué à chaud. | Demande de Nicolas. |
| 2026-11-05 | **Code en anglais** (identifiants, commentaires, Doxygen, logs, commits) ; page web en français ; documentation en français + `README.md` anglais. Composants repris de ThermoSync **traduits à la copie**. Dépôt **public** sur Git. | Demande de Nicolas. Une base publique se lit en anglais ; l'interface et la doc de travail restent dans la langue de la maison. |
| 2026-11-05 | Module **ENS160 + AHT21** sur I²C (GPIO21/22), composant pur `air_quality`, alarme par le **moteur de règles existant**, logo air en option dans la zone d'icônes. | Qualité d'air locale, disponible sans réseau. Un seul mécanisme d'alerte plutôt que deux. Le drapeau de validité de l'ENS160 est respecté : pas d'AQI inventé pendant le préchauffage. |
