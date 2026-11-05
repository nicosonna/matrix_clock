# Carte `horloge-v1` — fiche matériel

> État au **2026-11-05** : montage sur **devkit**, pas de carte dédiée. Les
> valeurs marquées *(à mesurer)* sont des calculs, pas des relevés.

---

## 0. Exemplaire en main — relevé `esptool` du 2026-11-05

| Lu par `esptool flash_id` | Valeur | Conséquence |
|---|---|---|
| Puce | **ESP32-D0WD-V3, révision v3.0** | Cible du projet. Révision 3 : pas de contournement du bug de cache PSRAM à activer (et pas de PSRAM de toute façon). |
| Cœurs / horloge | 2 × 240 MHz, quartz 40 MHz | Confortable pour WiFi + SPI/DMA + HTTP. |
| Flash | **4 Mo** (GigaDevice, JEDEC `c8 4016` = GD25Q32), tension 3,3 V | La table de partitions de `spec-serveur-web.md` §5 (2 × 1920 Ko OTA + NVS 48 Ko) tient **exactement**. Pas de place pour une partition de données en plus : la page web vit dans l'image. |
| eFuse | **étalonnage Vref présent** | `adc_cali` disposera du schéma « ligne » : l'ADC de la photorésistance sera étalonné sans intervention. |
| PSRAM | aucune (module WROOM-32) | Attendu. Jamais appliquer un `sdkconfig` S3/PSRAM à cette carte. |
| Pont USB-série | **Silicon Labs CP210x**, vu sur **COM21** ce jour (désigné par Nicolas) | L'énumération COM bouge sous Windows : revérifier avant de flasher. Un **autre** WROOM-32 (CH340, rév. 3.1, flash Puya) a été vu plus tôt sur COM6 : compatible aussi, mais ce n'est pas la carte de l'horloge. **COM15 est la carte ThermoSync (ESP32-S3, 16 Mo) : ne jamais la flasher avec ce projet.** |

Relevé avec le Python de l'IDF :
`C:\Users\<user>\.espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe -m esptool --port COM21 flash_id`
(`esptool.py` n'est **pas** dans le `PATH` PowerShell même après `export.ps1`).
L'adresse MAC relevée n'est volontairement pas consignée ici : dépôt public.

**Verdict : compatible, sans réserve, avec tout ce qui est spécifié.**

## 1. Nomenclature

| Élément | Référence | Note |
|---|---|---|
| Calculateur | **ESP32-WROOM-32** (devkit) | 4 Mo de flash, **pas de PSRAM** |
| Afficheur | Dalle **WS2812B 8×32**, 256 LEDs | Pas de 10 mm typique, 25,6 × 6,4 cm |
| Capteur de lumière | **Photorésistance (LDR)** + résistance fixe 10 kΩ | Diviseur sur ADC |
| Qualité de l'air | Module **ENS160 + AHT21** | I²C, `0x53` + `0x38`, 3,3 V, **déporté** (§5 bis) |
| Adaptateur de niveau | **74AHCT125** *(recommandé, §4)* | Ou alimentation de la 1ʳᵉ LED à ~4,5 V |
| Alimentation | **5 V / 5 A** minimum | §3 |
| Découplage | 1000 µF / 10 V au plus près de la dalle | + 100 nF près de l'ESP32 |
| Protection de la ligne de données | 330–470 Ω en série | Au plus près de la sortie ESP32 |

---

## 2. Brochage

| Fonction | Broche | Périphérique | Contrainte |
|---|---|---|---|
| Données dalle WS2812B | **GPIO13** | MOSI de SPI2 (`led_strip`, dorsale SPI **avec DMA**) | Sortie uniquement ; le SPI passe par la matrice GPIO, donc une autre broche libre convient aussi |
| Photorésistance | **GPIO33** | **ADC1_CH5** | ⚠️ **ADC1 obligatoire** : ADC2 est indisponible dès que le WiFi tourne |
| **ENS160 + AHT21** | **GPIO27 (SDA) / GPIO26 (SCL)** | I²C0, 100 kHz | Câblé ainsi sur la carte (2026-11-05). Tirages 4,7 kΩ en général déjà sur le module ; câble < 30 cm. GPIO26/27 sont des broches ADC2 : sans importance pour un bus numérique. |
| Bouton *(proposition Q1, non décidé)* | GPIO25 | GPIO entrée + tirage interne | Ni strapping, ni JTAG, ni flash, ni sortie au boot |
| RTC DS3231 *(phase 3, non décidé)* | GPIO27 / GPIO26 | même bus I²C, `0x68` | Pas de conflit d'adresse avec le module air |

### Broches à ne pas utiliser sur ce module

| Broches | Pourquoi |
|---|---|
| GPIO6–11 | **Flash SPI interne.** Les câbler empêche le démarrage. |
| GPIO0, GPIO2, GPIO12, GPIO15 | **Strapping** : lues au boot, un niveau imposé change le mode de démarrage ou la tension du flash. |
| GPIO34–39 | **Entrées seules**, sans tirage interne (utilisables pour un capteur analogique, pas pour une sortie). |
| ADC2 (GPIO0, 2, 4, 12–15, 25–27) | Inutilisable en ADC dès que le WiFi est actif. |

---

## 2 bis. Câblage de la dalle — mesuré au chenillard (2026-11-05)

Le **schéma du fabricant** montrait un ruban en rangées avec l'entrée en bas à
droite : **il ne décrit pas cette dalle**. Le chenillard physique (LED 0 rouge,
LED 255 bleue, point blanc dans l'ordre du ruban) a donné la vérité :

| Observation | Conséquence |
|---|---|
| LED 0 (rouge) **en haut à gauche** | `origin = TopLeft` |
| Le point descend une **colonne de 8**, puis passe à la suivante | `first_axis = Column` |
| LED 255 (bleue) **en haut à droite** | la 32ᵉ colonne **remonte** ⇒ `serpentine = true` (sans serpentin elle serait en bas à droite) |
| Alimentation : fil rouge/blanc **au milieu** de la dalle (schéma) | point d'**injection central** prévu par le fabricant, à utiliser en plus de l'entrée |
| Longueur 320 mm | pas de 10 mm |

Correspondance par défaut du firmware (`display::default_panel_map()`) :
**`Column`, serpentin, `TopLeft`, dalle entière**. Si la dalle est montée dans
un autre sens, seule l'origine change — le wizard le règle. Leçon : **on ne se
fie qu'à ce qu'on allume**, jamais à un dessin de catalogue.

## 3. Courant — la contrainte dimensionnante

| Cas | Calcul | Courant |
|---|---|---|
| Dalle éteinte (repos des contrôleurs) | 256 × 1 mA | **256 mA** |
| Blanc plein, luminosité 255 | 256 × 3 × 20 mA | **15,4 A** (+ repos ⇒ **15,6 A**) |
| Blanc, luminosité 64 (≈ 25 %) | gamma 2,2 ⇒ ~5 % du rapport cyclique | ~1,0 A |
| Scène `Heure` typique, luminosité 96 | ~60 pixels allumés, une couleur | ~150 mA *(à mesurer)* |

**Ce que le firmware fait** : il estime ce courant à chaque trame et **écrête
proportionnellement** les trois canaux pour respecter un budget réglable
(`budget_ma`, défaut **2000 mA**). Détail et formule :
`../../dev/spec-affichage.md` §2.3.

**Ce que le matériel doit garantir** :

- Alimentation **5 V / 5 A** minimum (soit 2,5× le budget par défaut). Une
  alimentation 2 A sur cette dalle est une alimentation qui s'effondre dès qu'une
  scène claire s'affiche, et une dalle qui s'effondre affiche du bruit coloré.
- **Injection aux deux extrémités** de la dalle : 15 A ne passent pas par les
  pistes d'un ruban, et même 2 A créent une chute visible (les dernières LEDs
  virent au jaune sur du blanc).
- **Masse commune** ESP32 / dalle / alimentation, et masse de retour **épaisse**.
- L'ESP32 n'alimente **jamais** la dalle depuis son 5 V USB.
- Condensateur **1000 µF** au point d'injection : l'appel de courant au
  changement de trame est brutal.

---

## 4. ⚠️ Adaptation de niveau — le piège

Le WS2812B alimenté en 5 V attend un `Vih ≥ 0,7 × VDD = 3,5 V`. L'ESP32 sort
**3,3 V**. C'est **hors spécification**, même si le montage « marche souvent » :
ça marche à température ambiante, sur un ruban court, avec un lot de LEDs
tolérant — et ça lâche en hiver, ou après avoir rallongé le câble, par des
pixels qui scintillent ou prennent la mauvaise couleur.

Deux remèdes, au choix :

1. **74AHCT125** (ou 74HCT245) alimenté en 5 V : un tampon logique dont le seuil
   d'entrée est compatible TTL (≈ 2 V), donc piloté sans problème par 3,3 V, et
   qui sort du 5 V franc. C'est la solution propre, et elle coûte moins d'un
   euro.
2. **Alimenter la première LED à ~4,5 V** (une diode 1N4001 en série sur son
   +5 V) : son seuil descend à 3,15 V. Astuce connue, moins robuste, et elle
   impose une alimentation séparée pour cette LED.

> Un niveau logique marginal est le défaut le plus pénible à diagnostiquer sur ce
> genre de montage : il se manifeste comme un bug logiciel aléatoire. On le règle
> au matériel, pas au logiciel.

---

## 5. Diviseur de la photorésistance

```
(a) 3V3 ──[LDR]──┬── GPIO33        clair → tension haute
                 └──[10 kΩ]── GND

(b) 3V3 ──[10 kΩ]──┬── GPIO33      clair → tension basse
                   └──[LDR]── GND
```

Les deux câblages sont acceptés : la courbe d'étalonnage peut être croissante ou
décroissante (`../../dev/spec-luminosite.md` §1), et le wizard détermine le sens
tout seul.

Choix de la résistance fixe : prendre l'ordre de grandeur de la résistance du LDR
à la luminosité où l'on veut le plus de résolution — **l'éclairage du soir**,
puisque c'est là que la luminosité de la dalle doit être juste. 10 kΩ convient à
un GL5516 ; à ajuster après le premier relevé.

### Placement du capteur — compte autant que le composant

La dalle **éclaire le capteur**. Si le LDR voit la dalle, on obtient une boucle
de pompage : l'horloge s'assombrit, le capteur voit moins, elle s'assombrit
encore. Trois précautions :

1. Le déporter sur le **côté** ou l'**arrière** du boîtier, pas en façade à côté
   des LEDs.
2. Lui mettre un **œillet** ou un tube noir pour limiter son angle de vue.
3. Le mesurer : l'étape 2 du wizard d'étalonnage **chiffre** l'auto-éclairage
   (dalle éteinte puis à 255, dans le noir) et le compense. Un résultat au-delà
   de ~150 ‰ veut dire qu'il faut déplacer le capteur, pas augmenter la
   compensation.

---

## 5 bis. Module ENS160 + AHT21 — placement et pièges

Le détail logiciel est dans `../../dev/spec-qualite-air.md` ; ici, ce qui
relève du montage.

- **Il chauffe trop s'il est dedans.** 256 LEDs et le régulateur 5 V chauffent le
  boîtier de plusieurs degrés. L'AHT21 lit alors trop chaud, trop sec, et comme
  sa température **compense l'ENS160**, l'AQI dérive aussi. Le module va
  **hors du boîtier** au bout d'un câble I²C court (< 30 cm, 4 fils), ou sur la
  face **arrière/basse** avec une ouverture d'air. Le wizard mesure l'écart
  dalle éteinte / dalle à fond et refuse de « compenser » au-delà de 2 °C.
- **Il a besoin d'air.** Un capteur de COV dans un boîtier fermé mesure l'air du
  boîtier (colle, plastique chaud), pas celui de la pièce. Ouvertures en regard.
- **Il met 3 minutes à se réveiller** après chaque coupure, et **1 heure** la
  toute première fois : c'est le capteur qui le dit (`VALIDITY_FLAG`), et le
  firmware affiche `--` pendant ce temps. Ce n'est pas une panne.
- **Vérifier l'alimentation du module** : certains modules combinés sont donnés
  3,3–5 V, d'autres 3,3 V seulement. L'ENS160 est une puce 1,8 V derrière un
  régulateur de carte. On alimente en **3,3 V**, I²C en 3,3 V, aucune adaptation
  de niveau nécessaire.
- **Tirages** : s'ils sont déjà sur le module (le cas courant, 4,7 kΩ ou 10 kΩ),
  ne pas en ajouter. Deux jeux en parallèle font chuter les temps de montée ; sur
  un câble de 30 cm à 100 kHz ce n'est pas critique, mais c'est inutile.
- **`ADDR`** : le module fixe en général l'adresse de l'ENS160 à `0x53`. Si le
  balayage du wizard trouve `0x52`, c'est le réglage à changer, pas une panne.
- **Première tâche à l'allumage** : lire `PART_ID` (`0x0160` attendu) et
  confronter la table des registres de la spec à la fiche technique — elle n'a
  **pas encore été vérifiée** sur ce module (dette D11).

## 6. Points de vérification au premier allumage

| # | Vérification | Attendu |
|---|---|---|
| 1 | Tension d'alimentation **à la dalle**, scène `plein` blanc, luminosité 64 | ≥ 4,7 V. En dessous : câble trop fin ou alimentation trop juste. |
| 2 | Courant à la pince, même scène | À comparer à l'estimation du firmware (page `/`). Un écart > 20 % veut dire que `kCourantCanalPleinUa` est à réviser pour ce lot de LEDs. |
| 3 | Motif `pixel` | **Un seul** pixel allumé, en haut à gauche après le wizard. |
| 4 | Motif `diagonale` | Trait net ; brisé tous les 8 pixels ⇒ `largeur_module = 8`. |
| 5 | Scintillement | Aucun à 25 trames/s. S'il y en a : niveau logique (§4) ou masse. |
| 6 | Mesure du LDR, dalle éteinte, dans le noir puis en plein jour | Deux valeurs **nettement** distinctes. Collées ⇒ résistance fixe mal choisie. |
| 7 | Échauffement après 1 h sur une scène claire | Dalle tiède, alimentation tiède. Chaude ⇒ baisser `budget_ma`. |
| 8 | Balayage I²C depuis la page `/air` | `0x53` (ou `0x52`) et `0x38` trouvés ; `PART_ID = 0x0160`. |
| 9 | Température de l'AHT21 confrontée à un thermomètre, dalle éteinte depuis 10 min | Écart < 1 °C. Au-delà : le module est trop près de la dalle, même éteinte (régulateur). |

---

## 7. Dérivés et évolutions

- **Carte dédiée** : si elle est tirée un jour, y intégrer le 74AHCT125, le point
  d'injection double, le condensateur, et sortir le LDR sur un connecteur déporté.
- **ESP32-S3** : donnerait le RMT avec DMA, l'USB natif et plus de flash. Mais la
  numérotation `D33` du montage actuel ne correspond pas ; le brochage vit dans
  `clock_board`, un profil supplémentaire suffirait.
- **DS3231** : garderait l'heure pendant une coupure (Q2 du cadrage).
