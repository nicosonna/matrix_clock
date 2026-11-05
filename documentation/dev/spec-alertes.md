# Spec — Canal d'alerte et moteur de règles (2026-11-05)

> **Statut : spécifié, rien d'implémenté.** Composant pur `alerts`.
> Phase 2 (règles poussées par le hub) : `spec-liaison-hub.md`.

---

## 1. Deux chemins, un seul rendu

```
   Home Assistant (événement horloge_notification)  ─┐
   POST /api/notification (HTTP local, Node-RED, hub) ├─▶  FileNotifications ─▶ affichage
   MoteurAlertes (règles locales sur les valeurs)    ─┘
```

Le **canal entrant** sert ce que la domotique sait mieux que l'horloge (« le
sèche-linge a fini », « quelqu'un sonne »). Le **moteur local** sert ce que
l'horloge doit savoir toute seule : il tourne même si Home Assistant et le hub
sont tombés (EX-43). Les deux débouchent sur la **même file** et le même rendu.

---

## 2. Moteur de règles

Au plus **16 règles** (`kReglesMax = 16`), `std::array`, aucune allocation.

### Sources

```cpp
enum class Source : uint8_t {
    Valeur,            ///< Une des 8 valeurs Home Assistant (index 0–7).
    HeureNonSure,      ///< SNTP jamais abouti, ou synchro périmée.
    LiaisonHaPerdue,   ///< WebSocket Home Assistant hors service.
    JetonRefuse,       ///< auth_invalid : jeton à refaire.
    ReseauPerdu,       ///< Pas d'IP.
    BailAbsent,        ///< Associé sans bail DHCP (spec-reseau-portail.md).
    DonneePerimee,     ///< Une valeur suivie est invalide ou périmée (index 0–7).
    CapteurLumDefaut,  ///< Photorésistance en défaut.
    EcretageCourant,   ///< Le plafond de courant écrête (budget trop serré).
    // Qualité de l'air (spec-qualite-air.md §6), locales, sans réseau :
    AirAqi,            ///< Indice UBA 1–5.
    AirEco2,           ///< ppm (équivalent calculé).
    AirTvoc,           ///< ppb.
    AirTemperature,    ///< centi-°C, décalage appliqué.
    AirHumidite,       ///< pour mille.
    AirCapteurDefaut,          ///< ENS160 absent / sortie invalide, ou AHT21 en défaut.
    AirPrechauffage,           ///< ENS160 en préchauffage ou première mise en service.
    AirCompensationDefaut,     ///< AHT21 en défaut : compensation de repli en cours.
};
```

Les sources **internes** sont ce qui rend le système honnête : une donnée absente
ne déclenche pas la règle qui la surveille (EX-44), c'est `DonneePerimee` qui le
dit. Sans elles, « pas d'alerte de gel » voudrait dire soit « il ne gèle pas »,
soit « je n'ai plus de thermomètre », et on ne saurait pas lequel.

### Champs d'une règle

| Champ | Type | Bornes | Note |
|---|---|---|---|
| `active` | booléen | | — |
| `nom` | texte | 16 car. | Libellé sur la page. |
| `source` | énuméré | | §2 |
| `index` | 0–7 | | Pour `Valeur` et `DonneePerimee`. |
| `comparaison` | `<` `<=` `>` `>=` `==` `!=` `change` `vrai` `faux` | | `vrai` / `faux` pour les sources booléennes et internes. |
| `seuil` | entier | ±2 000 000 000 | **Même virgule fixe que la source** (vérifié à la validation : une règle sur une valeur à 1 décimale a un seuil en dixièmes). |
| `hysteresis` | entier ≥ 0 | | Écart à repasser pour **retomber** (§2.1). |
| `confirmation_s` | 0–3600 | | La condition doit tenir **sans interruption** avant de lever l'alerte. |
| `priorite` | 0–3 | | §3 |
| `texte` | gabarit, 64 car. | | `{n}` nom, `{v}` valeur, `{u}` unité. Ex. `GEL {v}{u}`. |
| `couleur` | `#RRGGBB` | | Défaut : couleur de la priorité. |
| `icone` | énuméré | | — |
| `duree_s` | 1–3600 | 30 | Durée d'affichage de **chaque** passage. |
| `rappel_s` | 10–3600 | 300 | Intervalle entre deux passages **tant que la condition tient**. |
| `outrepasse_silence` | booléen | | Voir §4. |

> **Pas d'acquittement** (décision du 2026-11-05). Une alerte levée par une règle
> **revient toutes les `rappel_s`** tant que la règle est `Levee`, et disparaît
> **d'elle-même** quand la condition retombe (hystérésis et confirmation
> comprises). Ni bouton, ni page, ni commande ne la fait taire : c'est le
> **système concerné** qui la fait tomber — en aérant, en fermant la porte, en
> refaisant le jeton. Une alerte qu'on pourrait faire taire d'un geste est une
> alerte qu'on oublie.

La clé de dédoublonnage est **générée** : `regle_<index>`. Une règle ne peut donc
pas noyer la file avec ses propres répétitions.

### 2.1 Hystérésis et confirmation

Deux garde-fous distincts contre le papillonnement :

- **Hystérésis** sur la valeur : une règle `< 0` avec `hysteresis = 5` (0,5 °C)
  se lève à `< 0` et ne retombe qu'à `≥ 0,5`. Sans elle, une température qui
  oscille autour de zéro lève et retombe toutes les deux minutes.
- **Confirmation** dans le temps : la condition doit tenir `confirmation_s`
  d'affilée. Une coupure, même d'un pas de temps, **remet le compteur à zéro**.
  C'est ce qui rend utilisable une règle du type « porte ouverte depuis 10 min ».

États d'une règle : `Inactive → Surveille → EnAttenteConfirmation → Levee → Surveille`,
plus `Indisponible` quand la source est invalide. Depuis `Indisponible`, le retour
à `Surveille` **remet le compteur de confirmation à zéro** : on ne valide pas
10 minutes de condition sur des données qu'on n'avait pas.

### Exemples de règles par défaut (proposées, désactivées sauf mention)

| Nom | Source | Condition | Priorité | Texte |
|---|---|---|---|---|
| `GEL` | `Valeur` extérieur | `< 0,0` °C, hystérésis 0,5, confirmation 300 s | 1 | `GEL {v}{u}` |
| `CANICULE` | `Valeur` extérieur | `> 30,0` °C, hystérésis 1,0, confirmation 600 s | 1 | `{v}{u}` |
| `HEURE` | `HeureNonSure` | `vrai`, confirmation 120 s | 2 | `HEURE NON SURE` — **active** |
| `DOMOTIQUE` | `LiaisonHaPerdue` | `vrai`, confirmation 300 s | 1 | `HOME ASSISTANT HS` — **active** |
| `JETON` | `JetonRefuse` | `vrai` | 2 | `JETON A REFAIRE` — **active** |
| `DHCP` | `BailAbsent` | `vrai`, confirmation 1800 s | 2 | `PAS D ADRESSE IP` — **active** |
| `COURANT` | `EcretageCourant` | `vrai`, confirmation 60 s | 0 | `BUDGET COURANT SERRE` — **active** |
| `AERER` | `AirAqi` | `>= 4`, confirmation 120 s | 1 | `AERER` — **active** |
| `AIR MAUVAIS` | `AirAqi` | `>= 5`, confirmation 60 s | 2 | `AIR MAUVAIS` — **active** |
| `AIR CAPTEUR` | `AirCapteurDefaut` | `vrai`, confirmation 300 s | 1 | `CAPTEUR AIR HS` — **active** |
| `CO2`, `HUMIDITE` | `AirEco2`, `AirHumidite` | voir `spec-qualite-air.md` §6 | 1, 0 | désactivées |

Une règle sur `AirAqi` / `AirEco2` / `AirTvoc` pendant le **préchauffage** de
l'ENS160 (3 min après chaque mise sous tension) est `Indisponible` : pas de
« AERER » au retour du courant à 3 h du matin.

---

## 3. File de notifications

16 emplacements (`kNotifsMax = 16`), `std::array`.

| Champ | Taille |
|---|---|
| `cle` | 24 car. |
| `titre` | 16 car. |
| `texte` | 128 car. |
| `priorite` | 0–3 |
| `couleur`, `icone` | — |
| `arrivee_ms`, `dernier_passage_ms` | `uint32_t` |
| `tenue` | booléen — tenue par une règle `Levee`, ou par une source externe jusqu'à son `effacer` |
| `duree_s`, `rappel_s` | — |

Deux sortes de notifications, **aucune ne s'acquitte** :

| Sorte | Vit tant que | Disparaît quand |
|---|---|---|
| **Tenue** (règle locale ; externe avec `persistante: true`) | la règle est `Levee`, ou la source n'a pas envoyé `effacer` | la condition retombe, ou la source efface |
| **Ponctuelle** (externe, par défaut) | `duree_s` | expiration |

Règles :

- **Dédoublonnage par clé** : une notification de clé déjà présente **remplace**
  son contenu et repousse son expiration, **sans changer son rang d'arrivée**.
  Une porte qui envoie son état toutes les 30 s n'occupe qu'un emplacement.
- **File pleine** : on évince la notification de **plus basse priorité et la plus
  ancienne**, à condition que sa priorité soit **strictement inférieure** à celle
  de l'arrivante. Sinon la nouvelle est **rejetée** et comptée dans les
  diagnostics. Une alerte critique n'est jamais évincée par du bavardage.
- **Choix de l'affichage** : priorité la plus haute, puis la plus ancienne.
- **Passages** : une notification tenue est jouée `duree_s` à son arrivée, puis
  **rejouée toutes les `rappel_s`** ; entre deux passages, la zone d'icônes de la
  scène `Heure` garde l'icône d'alerte. Une ponctuelle est jouée une fois.
- **Retrait** : une tenue est retirée **par sa règle** (retombée) ou **par sa
  source** (`effacer` avec la même `cle`) ; une ponctuelle expire. Il n'existe
  **aucun** chemin d'acquittement manuel — ni sur la page, ni par le bouton
  physique, ni par commande. La page web **liste** les alertes en cours, c'est
  tout.
- Plusieurs tenues en même temps : à chaque passage, c'est la plus prioritaire
  puis la plus ancienne qui est jouée ; les autres suivent à leur `rappel_s`.

### Rendu par priorité

| P | Nom | Interruption | Clignotement | Couleur par défaut |
|---|---|---|---|---|
| 0 | information | non, attend la fin de la scène | non | bleu pâle |
| 1 | à savoir | oui | non | ambre |
| 2 | important | oui | 1 Hz | orange |
| 3 | critique | oui | 2 Hz, **plancher de luminosité relevé à 64** | rouge |

Texte plus long que 32 px ⇒ scène `Defilement` ; plus court ⇒ bandeau statique
avec l'icône (`spec-affichage.md` §4).

---

## 4. Heures silencieuses (EX-45)

| Réglage | Défaut | Note |
|---|---|---|
| `silence_actif` | oui | — |
| `silence_debut` / `silence_fin` | 22:30 / 07:00 | En minutes ; franchit minuit. |
| `silence_outrepasse_par_p3` | oui | Une alerte critique passe quand même. |

Pendant les heures silencieuses : **pas de clignotement**, **pas de défilement**
(le texte est tronqué en bandeau statique), et le plancher de luminosité de
`spec-luminosite.md` s'applique sans relèvement. Une alerte de priorité 3 avec
`outrepasse_silence` passe en rendu normal si `silence_outrepasse_par_p3`.

Les notifications **retenues** par le silence ne sont pas perdues : les tenues
restent dans la file (l'icône reste, les passages sont suspendus), et
**reprennent leurs passages à la fin du silence**. Une alerte dont la condition
tient encore à 7 h se voit au réveil ; une qui est retombée pendant la nuit a
disparu, et c'est juste.

---

## 5. Wizard de règle

Cinq étapes, bornes lues sur `/api/alertes/modele` :

1. **Quoi surveiller** — liste des 8 valeurs configurées (avec leur valeur
   **actuelle** affichée) et des sources internes.
2. **Condition** — comparaison et seuil, dans l'unité de la source ; l'aperçu dit
   « avec la valeur actuelle de 3,4 °C, cette règle **ne** se lèverait **pas** ».
3. **Stabilité** — hystérésis et durée de confirmation, avec un texte explicatif.
4. **Rendu** — priorité, texte (aperçu du gabarit **rendu avec la valeur réelle**),
   couleur, icône, durée d'un passage, intervalle de rappel.
5. **Récapitulatif** — phrase en français : « Si *Température extérieure* reste
   **sous 0,0 °C** pendant **5 min**, afficher *GEL 3,4 °C* en **orange,
   clignotant**, 30 s toutes les 5 min, **tant que ça gèle**. »

Un test vérifie que le wizard couvre **exactement** les champs du modèle de
règle, ni plus ni moins (règle des wizards du `CLAUDE.md`).

---

## 6. Tests

Tests host du composant pur `alerts` :

| Id | Vérifie |
|---|---|
| ALR-01 | Franchissement simple de seuil pour chaque comparaison. |
| ALR-02 | Hystérésis : lève à `< 0`, ne retombe qu'à `≥ 0,5` ; oscillation dans la bande ⇒ **un seul** déclenchement. |
| ALR-03 | Confirmation : condition tenue 299 s ⇒ rien ; 300 s ⇒ levée ; une coupure d'un pas remet le compteur à zéro. |
| ALR-04 | Source invalide ⇒ `Indisponible`, **aucune** levée ; retour à la validité ⇒ compteur de confirmation remis à zéro. |
| ALR-05 | Règles internes : `HeureNonSure`, `LiaisonHaPerdue`, `JetonRefuse`, `BailAbsent`, `DonneePerimee`, `CapteurLumDefaut`, `EcretageCourant`. |
| ALR-06 | Passages : joué `duree_s` à l'arrivée, rejoué toutes les `rappel_s` **tant que** la règle est `Levee` ; icône maintenue entre deux passages. |
| ALR-07 | Retombée d'une règle ⇒ sa notification tenue est retirée **d'elle-même** ; aucune API, aucun champ ne permet de la retirer autrement. |
| ALR-08 | Validation : seuil dans la virgule fixe de la source, index hors bornes, gabarit trop long, priorité hors bornes ⇒ refus et repli. |
| ALR-09 | Gabarit : `{n}`, `{v}`, `{u}`, accolade orpheline, placeholder inconnu, dépassement du tampon. |
| ALR-10 | File : dédoublonnage par clé, rang d'arrivée conservé, expiration repoussée. |
| ALR-11 | File pleine : éviction de la plus basse priorité la plus ancienne ; rejet si toutes sont de priorité ≥ , compteur incrémenté. |
| ALR-12 | Ordre de présentation : priorité puis ancienneté. |
| ALR-13 | Ponctuelle : expire à `duree_s`. Tenue externe (`persistante`) : n'expire pas, retirée seulement par `effacer` de la même `cle`. |
| ALR-14 | Heures silencieuses franchissant minuit : clignotement et défilement supprimés, passages suspendus mais tenues conservées et **reprises à la fin du silence**, priorité 3 qui outrepasse. |
| ALR-15 | Notification entrante hors bornes : champ remplacé par son défaut, `cle` invalide ⇒ rejet, `effacer` sur une clé absente ⇒ sans effet, pas d'erreur. |
| ALR-16 | 16 règles actives simultanément, aucune allocation (vérifié par un `operator new` qui fait échouer le test). |
| ALR-17 | Débordement 32 bits des compteurs de millisecondes. |
| ALR-18 | Cohérence wizard ⇄ modèle de règle : aucun champ orphelin d'un côté ou de l'autre. |

Vérifications matérielles : règle de gel déclenchée en refroidissant la sonde,
notification poussée par une automatisation réelle, alerte tenue qui disparaît
d'elle-même à la retombée (et jamais autrement)
depuis la page, comportement de nuit observé une nuit complète.
