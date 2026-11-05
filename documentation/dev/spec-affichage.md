# Spec — Affichage (2026-11-05)

> **Statut : spécifié, rien d'implémenté.** Composant pur `display`,
> adaptateur `clock_display`.

---

## 1. Matériel et contrainte dimensionnante

Dalle **WS2812B 8 lignes × 32 colonnes**, 256 LEDs, protocole 800 kHz, ordre des
octets **GRB**.

Le courant est la contrainte qui cadre tout :

| Grandeur | Valeur | Constante |
|---|---|---|
| Courant d'un canal à pleine valeur | 20 mA | `kCourantCanalPleinUa = 20000` |
| Courant de repos d'une LED (contrôleur) | 1 mA | `kCourantReposUa = 1000` |
| **Blanc plein, 256 LEDs** | **15,6 A** | — |
| Repos seul, 256 LEDs | 256 mA | — |
| Budget par défaut | **2000 mA** | `kBudgetDefautMa = 2000` |

Conséquence : le firmware **doit** estimer et plafonner le courant. Ce n'est pas
une option de confort, c'est ce qui protège l'alimentation et le câblage.

---

## 2. Chaîne de rendu

L'ordre est imposé, et chaque étape est une fonction pure testée :

```
couleur logique (RGB8)
   │  1. échelle de luminosité globale (0–255)
   ▼
   │  2. correction gamma (table constexpr, γ = 2,2)
   ▼  → on est maintenant sur les rapports cycliques réels
   │  3. plafond de courant (écrêtage proportionnel des 3 canaux)
   ▼
   │  4. correspondance (x, y) → index de LED
   ▼
tampon d'octets GRB → pilote led_strip
```

Le gamma vient **après** la luminosité (pour que l'assombrissement reste
progressif à l'œil) et le plafond **après** le gamma (le courant est
proportionnel au rapport cyclique réel, pas à la couleur logique).

### 2.1 Tampon d'image

`TamponImage` : `std::array<Couleur, 256>` (`Couleur` = 3 × `uint8_t`), soit
**768 octets** statiques. Deux tampons (travail / émis) pour que la composition
ne soit jamais vue à moitié : **1536 octets**. Aucune allocation.

API : `effacer()`, `pixel(x, y, c)`, `rect(x, y, w, h, c)`, `glyphe(...)`,
`texte(...)`, `copier_depuis(...)`. Tout écart hors 0 ≤ x < 32, 0 ≤ y < 8 est
**ignoré silencieusement** (écrire hors dalle est un défaut de scène, pas une
raison de faire tomber l'affichage) et compté dans un compteur de diagnostic.

### 2.2 Gamma

Table `constexpr std::array<uint8_t, 256>`, `sortie = round(255 · (i/255)^2,2)`,
calculée à la compilation. Propriétés garanties par test : monotone croissante,
`table[0] == 0`, `table[255] == 255`.

### 2.3 Plafond de courant

```
courant_ua = 256 · kCourantReposUa
           + Σ(r + g + b après gamma) · kCourantCanalPleinUa / 255
```

Si `courant_ua > budget_ua` : facteur `f = (budget_ua − repos_ua) · 256 / somme_ua`
en virgule fixe 8 bits, puis `canal = canal · f / 256` sur les trois canaux de
toutes les LEDs. **Aucun flottant.**

- Le courant de repos (256 mA) **n'est pas écrêtable** : si le budget est
  inférieur, le firmware refuse le réglage à la validation (`budget_ma ≥ 400`).
- L'écrêtage est **proportionnel** : les couleurs sont conservées, seule la
  luminosité globale baisse. Pas de saturation par canal (qui virerait les teintes).
- Le facteur appliqué est publié dans les diagnostics : la page web affiche
  « courant estimé 1,8 A / budget 2,0 A, écrêtage 0 % », c'est ce qui permet de
  régler le budget en connaissance de cause.

### 2.4 Correspondance panneau

Les dalles 8×32 du commerce sont câblées de façons différentes (un seul ruban en
serpentin, ou 4 modules 8×8 chaînés). On ne devine pas, on règle :

| Réglage | Valeurs | Défaut |
|---|---|---|
| `axe_premier` | `Colonne` (le ruban avance en colonnes de 8) / `Ligne` | `Colonne` |
| `serpentin` | oui / non | oui |
| `origine` | `HautGauche` / `HautDroite` / `BasGauche` / `BasDroite` | `HautGauche` |
| `largeur_module` | 0 (dalle entière) ou 8 (4 modules chaînés) | 0 |

`index(x, y) → uint16_t` est pure, sans branche coûteuse, et **testée sur les
8 combinaisons** d'origine × axe × serpentin, plus le cas des modules chaînés.
Propriété vérifiée par test : la correspondance est une **bijection** de
{0..31}×{0..7} vers {0..255} — aucun index en double, aucun manquant.

**Wizard de correspondance** (règle des wizards) : la page allume le pixel
(0, 0) seul, puis la première ligne, puis une diagonale, et demande à chaque
étape « c'est bien en haut à gauche ? » ; les réponses déterminent les quatre
réglages sans que l'utilisateur ait à savoir ce qu'est un serpentin.

---

## 3. Polices

Stockées en `constexpr`, colonne par colonne, un octet par colonne (bit 0 = ligne
du haut). Glyphe absent ⇒ rendu d'un rectangle vide (jamais un plantage, jamais
un glyphe au hasard).

| Police | Hauteur | Jeu | Usage |
|---|---|---|---|
| `police_5x7` | 7 | `0-9`, `:`, `.`, `-`, `+`, `°`, `%`, `A-Z`, espace | Heure en grand, textes défilants |
| `police_3x5` | 5 | `0-9`, `:`, `.`, `-`, `°`, `A-Z`, espace | Ligne secondaire, date |
| `police_3x4` | 4 | `0-9`, `:`, `.`, `-` | Scène à **deux lignes** (4 + 4 = 8) |

Largeur de l'heure en `police_5x7`, séparateur de 1 px et espacement de 1 px :

```
H  H  :  M  M
5+1+5+1+1+1+5+1+5 = 25 px   → 7 colonnes libres sur 32
```

Deux mises en page de la scène `Heure`, au choix :
- **centrée** : 25 px centrés (décalage 3 px), rien d'autre ;
- **heure + icônes** : heure à x = 0, zone d'icônes 6 px à x = 26 (état de la
  liaison, alerte en cours, cadenas du portail, et le **logo air** coloré selon
  le niveau de qualité d'air si `logo_air = ZoneIcone` —
  `spec-qualite-air.md` §5). Quand plusieurs icônes sont à montrer, elles
  **alternent** toutes les 3 s dans la zone ; le logo air a la priorité sur
  l'icône de liaison, l'alerte en cours a la priorité sur tout.

La 8ᵉ ligne (y = 7) reste libre : option **barre de secondes** (0 à 32 px),
ou séparateur de couleur.

### Accents

Une capitale accentuée demande 8 lignes de haut : il ne resterait pas
d'interligne. Les textes passent par `translitere()` — fonction **pure et
testée** — qui remplace `À Â Ä É È Ê Ë Î Ï Ô Ö Ù Û Ü Ç` par leur base et met en
capitales. **Limitation documentée**, police accentuée en phase 3 (Q7 du cadrage).

---

## 4. Scènes

Un **ordonnanceur** pur enchaîne des scènes selon la configuration. Chaque entrée :
`{ scene, duree_s, active }`, au plus 8 entrées, `std::array`.

| Scène | Contenu | Défaut |
|---|---|---|
| `Heure` | `HH:MM` en 5×7, séparateur clignotant à 1 Hz (réglable : fixe / 1 Hz / éteint) | active, 20 s |
| `Date` | `SAM 05.11` en 3×5 | active, 5 s |
| `Valeur` | Icône 8×8 + valeur en 5×7 (`21.5°`) — une entrée Home Assistant, **ou** la température / l'humidité locales de l'AHT21 | active, 8 s par valeur affichable |
| `Air` | Logo air 8×8 coloré selon le niveau + grandeur pilote (`AQI 3`, `820PPM`) ; `--` et logo gris en préchauffage | active si `logo_air = Scene`, 6 s |
| `DeuxLignes` | Deux valeurs courtes en 3×4 (ex. heure et température) | inactive |
| `Defilement` | Texte long qui défile (notification, message poussé) | déclenchée, pas périodique |
| `Nuit` | Heure seule au plancher de luminosité, sans clignotement | automatique en heures silencieuses |
| `Test` | Motif de mise en service (wizard de correspondance) | sur demande |

Règles de l'ordonnanceur :
- Une scène dont la **donnée est invalide est sautée** (pas de scène `Valeur`
  figée sur `--` pendant 8 s si la valeur n'arrive jamais) — sauf si aucune autre
  scène n'est active, cas où `Heure` est toujours jouée.
- Une **notification** de priorité ≥ 1 **interrompt** la scène courante, la joue,
  puis rend la main là où on en était. La priorité 0 attend la fin de la scène.
- Transition : coupure franche par défaut, **glissement** latéral de 8 px en
  option (réglable, 0 = coupure).

### Défilement

`Defilement` avance de `vitesse_px_s` (défaut 20 px/s, bornes 5–60) ; mise à
l'échelle entière sur la cadence de trame pour éviter le tremblement
(accumulateur de sous-pixels en 1/256ᵉ, pas de flottant). Le texte est **tronqué**
à 128 caractères à l'entrée, jamais alloué. Mode `boucles` : 1 à 5 passages, ou
`tenu` : rejoué à chaque passage tant que la notification est tenue
(`spec-alertes.md` §3 — rien ne s'acquitte).

---

## 5. Validité de l'heure (EX-02)

`clock_time` publie `{ heure_locale, sure }`. `sure` est faux tant que SNTP
n'a **jamais** abouti depuis le démarrage, et redevient faux si la dernière
synchronisation a plus de `peremption_heure_h` (défaut 48 h, au-delà la dérive de
l'oscillateur devient visible).

- `sure == false` ⇒ la scène `Heure` affiche `--:--` et la zone d'icônes montre
  l'indicateur « heure non sûre ». **Jamais** une heure fausse, jamais l'heure
  d'un compteur depuis le démarrage présentée comme l'heure du jour.
- Une règle d'alerte interne `heure_non_sure` peut être activée pour le signaler
  explicitement (`spec-alertes.md`).

---

## 6. Pilote (`clock_display`)

- Composant managé **`espressif/led_strip`**.
- **Dorsale SPI avec DMA** préférée : sur ESP32 classique le RMT n'a pas de DMA,
  et 256 LEDs représentent 256 × 24 × 1,25 µs = **7,7 ms** d'émission par trame,
  entièrement en interruptions de remplissage. À 25 trames/s cela ferait ~19 %
  d'un cœur en interruptions, en concurrence avec le WiFi. Le SPI décharge le
  processeur. **À mesurer sur la carte** et à consigner dans le journal ; RMT
  reste sélectionnable par Kconfig si le SPI pose un problème.
- Cadence nominale **25 trames/s** (40 ms). La boucle d'affichage est une tâche
  dédiée qui ne fait **jamais** d'attente réseau (EX-61) : elle lit un dernier
  état publié par les autres tâches.
- Au démarrage, avant toute initialisation réseau : dalle **effacée
  explicitement** (une bande de LEDs non initialisée affiche du bruit coloré).

---

## 7. Tests

Tests host du composant pur `display` (identifiants du plan de test) :

| Id | Vérifie |
|---|---|
| AFF-01 | `index(x, y)` est une bijection, pour chaque combinaison origine × axe × serpentin. |
| AFF-02 | Modules 8×8 chaînés : correspondance correcte sur les 4 modules. |
| AFF-03 | Écriture hors dalle ignorée, compteur de diagnostic incrémenté. |
| AFF-04 | Table gamma monotone, bornes exactes. |
| AFF-05 | Estimation de courant : blanc plein = 15,6 A ± 1 %, dalle éteinte = 256 mA. |
| AFF-06 | Écrêtage : budget 2000 mA sur du blanc plein ⇒ courant estimé ≤ budget, teintes conservées (rapports r:g:b inchangés). |
| AFF-07 | Budget inférieur au courant de repos refusé à la validation. |
| AFF-08 | Largeur des glyphes, glyphe absent ⇒ rectangle vide. |
| AFF-09 | `translitere()` : accents, minuscules, caractère inconnu. |
| AFF-10 | `HH:MM` en 5×7 mesure 25 px et tient centré sur 32. |
| AFF-11 | Heure non sûre ⇒ `--:--` (et pas 00:00 ni l'heure depuis le démarrage). |
| AFF-12 | Ordonnanceur : rotation, durées, scène à donnée invalide sautée, `Heure` jouée si tout est sauté. |
| AFF-13 | Notification de priorité ≥ 1 interrompt et rend la main à la scène interrompue. |
| AFF-14 | Défilement : vitesse respectée sans tremblement (accumulateur), troncature à 128 caractères, fin de boucles. |
| AFF-15 | Débordement 32 bits du compteur de millisecondes sans saut d'affichage. |
| AFF-16 | Zone d'icônes : alternance toutes les 3 s, priorité alerte > logo air > liaison ; glyphes 6×6 et 8×8 du logo air dans les bornes. (Couleur et modes du logo : AIR-18, AIR-19.) |

Vérifications matérielles (étape séparée, plan de test) : motif de test,
correspondance validée à l'œil par le wizard, **courant mesuré à la pince** sur
du blanc plein pour confronter l'estimation, lisibilité à 4 m, absence de
scintillement à 25 trames/s.
