# Spec — Luminosité automatique (2026-11-05)

> **Statut : spécifié, rien d'implémenté.** Composant pur `brightness`,
> adaptateur `clock_hal`.

---

## 1. Chaîne de mesure

Photorésistance en diviseur de tension sur **GPIO33 = ADC1_CH5**.

> **ADC1 et pas ADC2** : sur ESP32 classique, ADC2 est indisponible dès que le
> WiFi tourne. GPIO33 est bien sur ADC1, c'est le bon choix.

Deux câblages sont possibles et on ne suppose rien :

```
(a) 3V3 ──[LDR]──┬── GPIO33        clair → tension haute
                 └──[R 10k]── GND

(b) 3V3 ──[R 10k]──┬── GPIO33      clair → tension basse
                   └──[LDR]── GND
```

La **courbe de correspondance** (§3) peut donc être **croissante ou
décroissante** : la validation vérifie que l'axe des mesures est monotone, pas
le sens de la luminosité. Le wizard d'étalonnage détermine le sens tout seul.

### Acquisition (`clock_hal`)

- `adc_oneshot`, 12 bits, atténuation `ADC_ATTEN_DB_12` (plage utile ~0–3,1 V).
- **Suréchantillonnage 64 mesures** moyennées à chaque acquisition : l'ADC de
  l'ESP32 classique est bruité et non linéaire.
- Étalonnage par `adc_cali` (Vref en eFuse si présente, schéma deux points
  sinon). Si l'étalonnage n'est pas disponible, on reste en **valeurs brutes** :
  la courbe est de toute façon étalonnée à la main, les volts ne nous servent pas.
- Cadence d'acquisition **1 Hz**. L'échec de lecture rend `{ valeur, valide=false }` ;
  on ne renvoie **jamais** une valeur inventée ni la précédente présentée comme
  fraîche.

---

## 2. Filtrage (pur)

Dans l'ordre :

1. **Médiane sur 5 mesures** — élimine le pic isolé (lampe de poche, flash,
   parasite ADC). Fenêtre glissante statique, tri par réseau de comparaisons
   (pas de boucle de tri, pas de récursion).
2. **Moyenne exponentielle asymétrique** — en virgule fixe 1/256 :
   `y += (x − y) · alpha / 256`.

| Réglage | Défaut | Bornes | Pourquoi |
|---|---|---|---|
| `montee_s` (vers plus clair) | 10 s | 1–300 | On allume la lumière du salon : l'horloge doit suivre en quelques secondes. |
| `descente_s` (vers plus sombre) | 60 s | 1–600 | Un nuage, une ombre qui passe : on ne veut pas que l'horloge pompe. |

L'asymétrie est le remède au **pompage**, qui est le défaut principal de ce genre
d'asservissement : l'horloge s'assombrit, donc le capteur voit moins de lumière,
donc elle s'assombrit encore.

3. **Compensation d'auto-éclairage** — la dalle éclaire le capteur. Mesurée par le
   wizard (§4) en **comptes ADC à luminosité 255**, **signée** (un câblage (b)
   voit la mesure *baisser* quand la dalle s'allume), et soustraite
   proportionnellement à la luminosité **appliquée** :
   `mesure_corrigee = mesure − compensation_comptes · lum_appliquee / 255`
   (bornée à 0..4095). Avec un capteur bien placé la compensation est nulle ;
   avec un capteur en façade elle évite la boucle de pompage.
   *(Implémenté le 2026-11-05 : `compensation_counts`, ±4095 — l'unité « ‰ »
   initialement prévue a été remplacée, plus simple à mesurer et à tester.)*

---

## 3. Courbe mesure → luminosité

Jusqu'à **6 points** `{ mesure, luminosite }`, `std::array`, interpolation
**linéaire entière** entre les points, extrapolation plate aux extrémités.

Validation (`is_valid()`), avec repli sur la courbe par défaut si elle échoue :
- au moins 2 points ;
- axe des mesures **strictement monotone croissant** ;
- chaque `brightness` dans [`lum_min`, `lum_max`] ;
- `lum_min ≤ lum_max`.

| Réglage | Défaut | Bornes | Note |
|---|---|---|---|
| `mode` | `Auto` | `Auto` / `Manuel` / `Eteint` | — |
| `lum_manuelle` | 96 | 0–255 | Utilisée en `Manuel`. |
| `lum_min` | **8** | 0–255 | Plancher de nuit : l'horloge reste lisible dans le noir sans éclairer la chambre. 0 = extinction totale autorisée. |
| `lum_max` | 255 | 0–255 | — |
| `pas_lum` | 4 | 1–32 | Quantification de la sortie (§3.1). |
| `extinction_active` | non | — | Plage horaire d'extinction complète. |
| `extinction_debut` / `extinction_fin` | 23:30 / 06:30 | minutes | Franchit minuit (début > fin accepté). |
| `compensation_counts` | 0 | −4095–4095 | Comptes ADC ajoutés (ou retirés) par la dalle à 255. Déterminée par le wizard. |

### 3.1 Zone morte et quantification

La luminosité appliquée ne bouge que si la valeur calculée s'en écarte d'au
moins **un pas** (`pas_lum`, zone morte) ; elle saute alors à la valeur
calculée **arrondie au multiple du pas**, bornée par plancher et plafond. Un
bruit de ±1 LSB, et même de ±20 comptes, ne fait donc jamais clignoter la
dalle (LUM-07), et les niveaux appliqués sont des multiples du pas (LUM-08).

### 3.2 Repli (EX-13)

| Situation | Comportement |
|---|---|
| Lecture ADC en échec (≥ 5 lectures consécutives) | Luminosité `kLumRepli = 64`, drapeau `capteur_en_defaut`, log `ESP_LOGW`, signalé sur la page et comme source de règle interne. |
| Mesure hors plage (0 ou 4095 collés) | Même traitement : un capteur en court-circuit ou débranché ne se présente pas comme « nuit noire ». |
| Courbe invalide en NVS | Courbe par défaut, drapeau `config_remplacee`. |

**Jamais 0 par accident** (horloge éteinte sans qu'on sache pourquoi), **jamais
255 par accident** (256 LEDs à fond la nuit).

---

## 4. Wizard d'étalonnage (EX-14)

Quatre étapes, la page lit les mesures brutes en direct sur `/api/luminosite/brut` :

| Étape | Consigne à l'écran | Mesure retenue |
|---|---|---|
| 1 | « Fais le noir dans la pièce. La dalle va s'éteindre. » | `brut_nuit` (dalle à 0) |
| 2 | « Ne bouge rien, la dalle va s'allumer au maximum. » | `brut_nuit_allume` ⇒ `compensation_counts = brut_nuit_allume − brut_nuit`, **signée** (négative pour un câblage (b)) |
| 3 | « Allume l'éclairage du soir. » | `brut_soir` |
| 4 | « Reviens en plein jour. » | `brut_jour` |

La courbe est construite sur les **trois** niveaux d'ambiance (nuit, soir,
jour), triée par mesure croissante, chaque point gardant sa luminosité
(`lum_min`, 40, `lum_max`) — ce qui donne d'office une courbe décroissante pour
un câblage (b). Deux mesures égales ⇒ le wizard refuse (LUM-14). L'étape 2
**mesure réellement** l'auto-éclairage au lieu de le supposer nul — c'est le
piège de ce montage.

Le wizard affiche le résultat (« capteur bien isolé : compensation 0 ‰ » ou
« le capteur voit la dalle : +600 comptes à pleine luminosité, envisage de le
déporter » — seuil `kCompensationWarnCounts = 600`) et demande confirmation
avant d'écrire en NVS.

---

## 5. Tests

Tests host du composant pur `brightness` :

| Id | Vérifie |
|---|---|
| LUM-01 | Médiane : un pic isolé dans 5 mesures est rejeté ; 2 pics consécutifs passent (limite assumée, documentée). |
| LUM-02 | Moyenne exponentielle : constante de temps respectée à ±10 % en montée et en descente, asymétrie effective. |
| LUM-03 | Pas de pompage : entrée constante + compensation non nulle ⇒ la sortie converge et ne cycle pas (100 itérations, amplitude finale ≤ `pas_lum`). |
| LUM-04 | Interpolation : valeurs aux points exacts, entre deux points, extrapolation plate hors bornes. |
| LUM-05 | Courbe décroissante (câblage (b)) correctement interprétée. |
| LUM-06 | Validation : < 2 points, axe non monotone, luminosité hors bornes ⇒ refus et repli sur la courbe par défaut. |
| LUM-07 | Hystérésis : un bruit de ±1 LSB autour d'un seuil ne change pas la sortie. |
| LUM-08 | Quantification par `pas_lum`. |
| LUM-09 | Plancher et plafond respectés, y compris `lum_min = 0`. |
| LUM-10 | 5 lectures invalides consécutives ⇒ `kLumRepli` + drapeau ; une lecture valide rétablit. |
| LUM-11 | Mesure collée à 0 ou à 4095 traitée comme capteur en défaut. |
| LUM-12 | Plage d'extinction qui franchit minuit (23:30 → 06:30) : dedans et dehors. |
| LUM-13 | Mode `Manuel` ignore la mesure ; mode `Eteint` donne 0. |
| LUM-14 | Construction de la courbe par le wizard à partir des 4 mesures, y compris quand le câblage inverse l'ordre. |

Vérifications matérielles : relevés jour / soir / nuit noire confrontés à la
courbe, absence de scintillement sur une journée complète, lisibilité au plancher
de nuit, et mesure de l'auto-éclairage réel du capteur en place.
