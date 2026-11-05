# Spec — Qualité de l'air (ENS160 + AHT21) (2026-11-05)

> **Statut : spécifié, rien d'implémenté.** Composant pur `air_quality`,
> adaptateur `clock_hal` (I²C).
>
> ⚠️ **Registres à confronter à la fiche technique** à l'implémentation : les
> adresses et valeurs ci-dessous viennent de la documentation ScioSense ENS160 et
> ASAIR AHT21 telle que connue, elles n'ont **pas encore** été vérifiées sur le
> module en main. Première tâche du composant : lire `PART_ID` et confirmer.

---

## 1. Ce que les deux capteurs apportent

Module combiné **ENS160 + AHT21** sur **I²C** (le module courant porte les deux
puces sur la même carte).

| Puce | Adresse I²C | Grandeurs |
|---|---|---|
| **ENS160** | `0x53` (`ADDR` haut) ou `0x52` (`ADDR` bas) | **AQI** (indice UBA 1–5), **TVOC** (ppb), **eCO2** (ppm) |
| **AHT21** | `0x38` | **Température** (°C), **Humidité relative** (%) |

Les deux ne sont pas indépendants : **l'ENS160 a besoin de la température et de
l'humidité** pour compenser ses mesures, et c'est l'AHT21 qui les lui fournit.
C'est la raison d'être du module combiné, et ça impose l'ordre de la boucle (§3).

Ces cinq grandeurs s'ajoutent aux 8 valeurs Home Assistant : elles sont
affichables, utilisables comme **sources de règles d'alerte**, **publiées vers
Home Assistant par MQTT** avec découverte (`spec-home-assistant.md` §6 bis —
`null` pendant le préchauffage, jamais une valeur inventée), et remontables au
hub en phase 2. Elles sont **locales**, donc disponibles **même sans réseau** —
une alarme de qualité d'air qui dépendrait du WiFi n'aurait pas de sens.

---

## 2. ENS160 — registres et validité

| Registre | Adresse | Contenu |
|---|---|---|
| `PART_ID` | `0x00` (16 bits LE) | **`0x0160`** — identification, vérifiée au démarrage |
| `OPMODE` | `0x10` | `0x00` sommeil, `0x01` repos, `0x02` **standard**, `0xF0` réinitialisation |
| `CONFIG` | `0x11` | Interruptions (non utilisées : on scrute) |
| `TEMP_IN` | `0x13` (16 bits) | Compensation : **Kelvin × 64** |
| `RH_IN` | `0x15` (16 bits) | Compensation : **% × 512** |
| `DEVICE_STATUS` | `0x20` | Drapeaux, dont **`VALIDITY_FLAG`** (bits 3:2) |
| `DATA_AQI` | `0x21` | Indice UBA **1–5** |
| `DATA_TVOC` | `0x22` (16 bits) | ppb |
| `DATA_ECO2` | `0x24` (16 bits) | ppm |

### Le drapeau de validité est le cœur de cette spec

`VALIDITY_FLAG` vaut :

| Valeur | Sens | Durée | Ce qu'on affiche |
|---|---|---|---|
| `0` | **Fonctionnement normal** | — | la valeur |
| `1` | **Préchauffage** | ~3 min après la mise sous tension | `--` + indicateur de préchauffage |
| `2` | **Première mise en service** | ~1 h cumulée au tout premier usage | `--` + indicateur de préchauffage |
| `3` | **Sortie invalide** | — | `--` + capteur en défaut |

C'est exactement la règle d'honnêteté du projet (`gardefous-dev.md` §4 bis)
appliquée à un capteur qui la documente lui-même : **pendant les trois premières
minutes après chaque coupure de courant, l'ENS160 dit lui-même que sa mesure ne
vaut rien.** On ne l'affiche pas, on n'alerte pas dessus, et on dit pourquoi.
Sans ça, chaque retour de courant afficherait un AQI inventé — et déclencherait
peut-être une alarme « AERER » en pleine nuit.

Les bits `NEWDAT` (nouvelle donnée) et `NEWGPR` sont scrutés pour ne lire que des
mesures fraîches. Un `PART_ID` qui ne vaut pas `0x0160` ⇒ capteur **absent**, et
tout ce qui dépend de lui est invalide, sans message d'erreur répété.

---

## 3. AHT21 et boucle de mesure

### AHT21

| Étape | Trame |
|---|---|
| Initialisation | `0xBE 0x08 0x00`, puis vérifier le bit *calibré* du status |
| Déclenchement | `0xAC 0x33 0x00`, puis **attendre 80 ms** |
| Lecture | 7 octets : status, humidité (20 bits), température (20 bits), **CRC8** |

- `temperature_centi_degc = brut · 20000 / 1048576 − 5000`
- `humidite_pour_mille = brut · 1000 / 1048576` *(stocké en ‰ pour garder une
  décimale sans flottant)*
- **CRC8** (polynôme `0x31`) vérifié : un CRC faux ⇒ lecture **rejetée**, pas
  réparée. Trois rejets consécutifs ⇒ capteur en défaut.

### Ordre de la boucle, toutes les secondes

```
1. AHT21 : déclencher, attendre 80 ms, lire, vérifier le CRC
2. ENS160 : écrire TEMP_IN / RH_IN avec ce qu'on vient de lire
3. ENS160 : lire DEVICE_STATUS
4. si VALIDITY_FLAG == 0 et NEWDAT : lire AQI, TVOC, eCO2
5. publier les 5 grandeurs avec leur drapeau de validité
```

**Si l'AHT21 est en défaut**, on écrit quand même la compensation avec des valeurs
de repli nommées (`kTempComp = 25,00 °C`, `kHumComp = 50,0 %`) et on lève le
drapeau `compensation_par_defaut` : les mesures de l'ENS160 restent exploitables
mais moins précises, et la page web le dit. On ne fait **pas** semblant d'avoir
une compensation réelle, et on ne jette **pas** l'ENS160 pour autant.

L'I²C passe par une **interface injectée** (`BusI2c` : `ecrire()`, `lire()`,
`attendre_ms()`), donc tout le §2 et le §3 sont **testables sur host** avec une
doublure qui rejoue des trames — y compris les cas tordus (CRC faux, flag 3,
`PART_ID` inconnu, NACK).

---

## 4. Niveaux de qualité d'air

Trois grandeurs peuvent décider du niveau. Par défaut on suit l'**AQI UBA**, et
l'eCO2 et le TVOC sont disponibles comme sources de règles en plus.

| Niveau | AQI UBA | eCO2 (ppm) | TVOC (ppb) | Couleur par défaut | Message par défaut |
|---|---|---|---|---|---|
| `Excellent` | 1 | < 600 | < 220 | vert | — |
| `Bon` | 2 | 600–800 | 220–660 | vert-jaune | — |
| `Moyen` | 3 | 800–1000 | 660–1430 | jaune | — *(ou `AERER`, réglable)* |
| `Mauvais` | 4 | 1000–1500 | 1430–2200 | orange | **`AERER`** |
| `Tres mauvais` | 5 | > 1500 | > 2200 | rouge | **`AIR MAUVAIS`** puis **`AERER`** |

- Les **seuils sont réglables** (bornes validées, monotones) : les valeurs
  ci-dessus sont les repères usuels de l'indice UBA, pas une vérité.
- Les textes sont des **gabarits réglables** (`{v}` = valeur, `{u}` = unité) et
  passent par `translitere()` : l'affichage rend `AERER`, pas `AÉRER` — une
  capitale accentuée ne tient pas sur 7 lignes (dette D4).
- `grandeur_pilote` (réglage) : `Aqi` (défaut), `Eco2` ou `Tvoc` — c'est elle qui
  décide du niveau, du logo et du message.

### Stabilité du niveau

L'AQI de l'ENS160 saute d'un pas à l'autre. Sans précaution, le logo
clignoterait entre jaune et orange toute la soirée. Deux garde-fous, les mêmes
que pour les règles d'alerte :

| Réglage | Défaut | Bornes | Rôle |
|---|---|---|---|
| `confirmation_s` | **60** | 0–3600 | Un nouveau niveau doit tenir sans interruption avant d'être affiché. |
| `hysteresis_eco2` / `hysteresis_tvoc` | 50 ppm / 100 ppb | ≥ 0 | Écart à repasser pour **redescendre** d'un niveau. |

L'hystérésis est **asymétrique par construction** : on monte d'un niveau dès le
seuil franchi (l'air se dégrade, on le dit vite), on ne redescend qu'après avoir
repassé le seuil moins l'hystérésis et tenu la confirmation.

---

## 5. Affichage : le logo air

Réglage `logo_air`, trois valeurs — c'est l'« option logo air ou pas » :

| Valeur | Rendu |
|---|---|
| `Aucun` | Rien. La qualité d'air reste lisible sur la page web et utilisable en règle d'alerte. |
| `ZoneIcone` *(défaut)* | **Logo 6×6 permanent** dans la zone d'icônes de la scène `Heure` (`spec-affichage.md` §3), **coloré selon le niveau**. L'heure reste en grand, le logo informe en permanence du coin de l'œil. |
| `Scene` | Scène `Air` dédiée dans la rotation : logo 8×8 + valeur de la grandeur pilote (`AQI 3`, `820PPM`), colorée selon le niveau. |

Le logo est une **icône SVG style ligne** côté page web et un **glyphe 6×6 / 8×8**
côté dalle, dessiné dans `display` : trois traits de courant d'air, lisible à
cette taille. Pas d'emoji, ni ici ni sur la page.

**Couleur** : celle du niveau (§4), réglable par niveau en `#RRGGBB`. Pendant le
préchauffage ou en défaut, le logo est affiché en **gris** — jamais en vert
(« tout va bien ») alors qu'on ne sait pas.

En heures silencieuses, le logo ne clignote pas, quel que soit le niveau, sauf
alerte de priorité 3 qui outrepasse (`spec-alertes.md` §4).

---

## 6. Alarme qualité d'air

L'alarme passe par le **moteur de règles existant** : pas de deuxième mécanisme
d'alerte, donc un seul rendu, une seule file, une seule logique de silence et
de retombée (rien ne s'acquitte, EX-46). Les nouvelles sources :

```cpp
AirAqi, AirEco2, AirTvoc, AirTemperature, AirHumidite,  // mesures
AirCapteurDefaut, AirPrechauffage, AirCompensationDefaut // internes
```

### Règles par défaut

| Nom | Source | Condition | Priorité | Texte | Actif |
|---|---|---|---|---|---|
| `AERER` | `AirAqi` | `>= 4`, confirmation 120 s | 1 | `AERER` | **oui** |
| `AIR MAUVAIS` | `AirAqi` | `>= 5`, confirmation 60 s | 2 | `AIR MAUVAIS` | **oui** |
| `CO2` | `AirEco2` | `> 1200` ppm, hystérésis 100, confirmation 300 s | 1 | `CO2 {v}{u}` | non |
| `AIR CAPTEUR` | `AirCapteurDefaut` | `vrai`, confirmation 300 s | 1 | `CAPTEUR AIR HS` | **oui** |
| `HUMIDITE` | `AirHumidite` | `> 70` %, hystérésis 3, confirmation 1800 s | 0 | `HUMIDITE {v}{u}` | non |

- `AirPrechauffage` **ne lève pas d'alerte** par défaut : c'est un état normal de
  3 minutes après chaque mise sous tension, pas un problème. La règle existe pour
  qui veut le savoir.
- Une règle sur une grandeur en préchauffage ou en défaut **ne déclenche pas**
  (EX-44) : c'est `AirCapteurDefaut` qui parle. Une coupure de courant à 3 h du
  matin ne réveille donc personne avec un faux « AERER ».
- La règle `AERER` de priorité 1 n'interrompt pas la nuit : les heures
  silencieuses suspendent ses passages, et elle **revient d'elle-même** au
  réveil si l'air est toujours mauvais — et seulement dans ce cas. On ne
  l'acquitte pas : on aère (EX-46).

---

## 7. Réglages (page `/air`)

Bornes lues sur `/api/air/modele`.

| Réglage | Défaut | Bornes |
|---|---|---|
| Capteur actif | **auto** | Détecté par `PART_ID` ; forçable à « absent » |
| Adresse ENS160 | `0x53` | `0x52` ou `0x53` |
| `grandeur_pilote` | `Aqi` | `Aqi` / `Eco2` / `Tvoc` |
| Seuils des 5 niveaux | §4 | monotones, validés |
| Couleurs des 5 niveaux | §4 | `#RRGGBB` |
| Textes des niveaux | §4 | gabarit 32 car. |
| `logo_air` | `ZoneIcone` | `Aucun` / `ZoneIcone` / `Scene` |
| `confirmation_s` | 60 | 0–3600 |
| `hysteresis_eco2` / `hysteresis_tvoc` | 50 / 100 | ≥ 0 |
| `offset_temperature_centi` | **0** | ±1000 (±10 °C) — §8 |
| `offset_humidite_pour_mille` | 0 | ±200 |
| `periode_s` | 1 | 1–60 |

**Wizard en 4 étapes** : détection (`PART_ID`, adresses trouvées sur le bus) →
attente du préchauffage avec **compte à rebours réel** lu dans le drapeau de
validité → compensation de l'auto-échauffement (§8) → niveaux, couleurs, textes
et logo, avec un aperçu du rendu sur la dalle.

---

## 8. ⚠️ Auto-échauffement — le piège de ce montage

256 LEDs et un régulateur 5 V dans le même boîtier chauffent. Un AHT21 à côté lit
**plusieurs degrés de trop** et, l'humidité relative dépendant de la température,
une humidité **plusieurs points de trop bas**. Et comme cette température sert à
**compenser l'ENS160**, l'erreur se propage à l'AQI.

Donc, dans l'ordre :

1. **Placement** : capteur déporté hors du boîtier, ou au moins sur la face
   **basse/arrière**, loin de la dalle et du régulateur, avec une ouverture
   d'air. Comme la photorésistance, c'est un problème de mécanique avant d'être
   un problème de logiciel.
2. **Mesure de l'écart** : étape 3 du wizard — dalle éteinte 10 min, relever ;
   dalle à 255 pendant 20 min, relever. L'écart est affiché et proposé comme
   `offset_temperature_centi`.
3. **Le wizard dit la vérité** : au-delà de **2,0 °C** d'écart, il affiche
   « le capteur chauffe trop pour être compensé proprement : déporte-le ». Un
   décalage constant se corrige ; un décalage qui dépend de la scène affichée ne
   se corrige pas.

Le décalage est appliqué **à la mesure publiée et à la compensation de l'ENS160**,
et le fait qu'un décalage non nul soit actif est **visible sur la page** : une
température corrigée à la main qui se présenterait comme une mesure brute serait
exactement le genre de valeur silencieusement fausse qu'on refuse.

---

## 9. Matériel

| Fonction | Broche | Note |
|---|---|---|
| `SDA` | **GPIO27** | I²C0, 100 kHz (400 kHz possible). Câblage relevé le 2026-11-05. |
| `SCL` | **GPIO26** | Broches ADC2 toutes les deux : sans importance pour un bus numérique. |

- Tirages de 4,7 kΩ : en général **déjà sur le module**, à vérifier avant d'en
  ajouter (deux jeux en parallèle font chuter les temps de montée).
- Alimentation **3,3 V** (le module combiné accepte en général 3,3–5 V, à
  vérifier : l'ENS160 est une puce 1,8 V avec régulateur sur carte).
- Le bus est **partagé** avec un éventuel DS3231 (`0x68`) en phase 3 : pas de
  conflit d'adresse.
- Un NACK, un bus bloqué (SDA tenu bas) ⇒ **réinitialisation du bus** (impulsions
  d'horloge) et nouvelle tentative, comptées dans les diagnostics. Pas de boucle
  d'attente infinie : l'I²C ne doit jamais retarder la boucle d'affichage
  (EX-61), donc la boucle de mesure vit dans la tâche `periodique`, jamais dans
  celle d'affichage.

---

## 10. Tests

Tests host du composant pur `air_quality` (bus I²C moqué par une doublure qui rejoue des
trames) :

| Id | Vérifie |
|---|---|
| AIR-01 | `PART_ID` `0x0160` ⇒ capteur présent ; autre valeur ou NACK ⇒ **absent**, toutes ses grandeurs invalides, pas de message répété. |
| AIR-02 | `VALIDITY_FLAG` 1 et 2 ⇒ grandeurs **invalides** + drapeau de préchauffage ; 3 ⇒ capteur en défaut ; 0 ⇒ valeurs publiées. |
| AIR-03 | `NEWDAT` à 0 ⇒ on ne republie pas une mesure comme fraîche. |
| AIR-04 | Décodage AQI / TVOC / eCO2, octets de poids faible en premier, valeurs extrêmes. |
| AIR-05 | Encodage de la compensation : `TEMP_IN` en K×64, `RH_IN` en %×512, bornes et arrondi. |
| AIR-06 | AHT21 : décodage température et humidité sur des trames réelles (`test/fixtures/`), bornes. |
| AIR-07 | AHT21 : **CRC8** correct accepté, CRC faux **rejeté** ; 3 rejets ⇒ capteur en défaut ; une bonne lecture rétablit. |
| AIR-08 | AHT21 non calibré au status ⇒ relance de l'initialisation. |
| AIR-09 | AHT21 en défaut ⇒ compensation de repli (25 °C / 50 %) écrite, drapeau `compensation_par_defaut`, ENS160 toujours exploité. |
| AIR-10 | Ordre de la boucle : compensation écrite **avant** la lecture des données. |
| AIR-11 | Niveaux : les 5 paliers pour chaque `grandeur_pilote`, aux seuils exacts. |
| AIR-12 | Hystérésis : oscillation autour d'un seuil ⇒ **un seul** changement de niveau. |
| AIR-13 | Confirmation : nouveau niveau tenu 59 s ⇒ pas de changement ; 60 s ⇒ changement ; coupure ⇒ compteur à zéro. |
| AIR-14 | Montée immédiate / descente freinée (asymétrie). |
| AIR-15 | Validation des seuils : non monotones, inversés, couleurs mal formées ⇒ refus et repli. |
| AIR-16 | `offset_temperature_centi` appliqué à la mesure **et** à la compensation ; bornes. |
| AIR-17 | `construire_offset()` du wizard, et seuil d'avertissement à 2,0 °C. |
| AIR-18 | Couleur du logo : grise en préchauffage et en défaut, **jamais verte**. |
| AIR-19 | `logo_air` : les trois modes produisent le tampon attendu. |
| AIR-20 | Règles sur une source en préchauffage ou en défaut ⇒ **aucune levée** ; `AirCapteurDefaut` lève à sa place. |
| AIR-21 | Bus bloqué / NACK ⇒ réinitialisation, compteur, aucune attente bloquante. |
| AIR-22 | Modèle ⇄ wizard `/air` : aucun paramètre orphelin. |

Vérifications matérielles :

| Id | Procédure | Attendu |
|---|---|---|
| HW-AIR-01 | Mise sous tension, chronomètre | `--` + préchauffage pendant ~3 min, puis valeurs ; **aucune** alerte « AERER » pendant ce temps |
| HW-AIR-02 | Premier usage du capteur neuf | Drapeau 2 pendant ~1 h, signalé sur la page |
| HW-AIR-03 | Souffler / vaporiser un solvant à proximité | AQI, TVOC et eCO2 montent, niveau confirmé après 60 s |
| HW-AIR-04 | Pièce fermée plusieurs heures puis fenêtre ouverte | Montée puis retour, logo qui change de couleur **sans clignoter** entre deux niveaux |
| HW-AIR-05 | Wizard, étape 3 (dalle éteinte 10 min puis 255 pendant 20 min) | Écart chiffré ; > 2 °C ⇒ avertissement de déport |
| HW-AIR-06 | Température et humidité confrontées à un thermomètre de référence | Après compensation, écart < 1 °C et < 5 % HR |
| HW-AIR-07 | Capteur débranché à chaud | Grandeurs invalides, logo gris, alerte `CAPTEUR AIR HS`, bus rétabli au rebranchement sans redémarrage |
