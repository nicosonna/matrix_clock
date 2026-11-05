# Plan de test — Horloge

> **À jour en permanence** (garde-fou central). Matrice test ↔ code couvert ↔
> procédure ↔ attendu ↔ statut/date. Couvre les **tests host** (sur PC) **et** les
> **vérifications matérielles** (étape séparée, hors CI).
>
> Statut au **2026-11-05 (soir)** : **130 tests host, tous `PASSE`**
> (12 175 vérifications, couverture 96,8 % sur 5 022 lignes), 43 vérifications
> matérielles dont 8 `PASSE`, 6 « mécanique vérifiée » sur banc, le reste à
> jouer. Lancement : `bash firmware/test/run_host_tests.sh` (couverture :
> `--coverage`, objectif ≥ 95 %, échec en dessous). Vue d'ensemble et campagne
> finale : `dev/plan-de-test-final.md`.

---

## 1. Tests host

Les colonnes *procédure* et *attendu* sont volontairement courtes : le détail de
chaque cas est dans la spec citée.

### 1.1 `display` — `dev/spec-affichage.md` §7

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| AFF-01 | `display::led_index()` | Bijection {0..31}×{0..7} → {0..255} pour les **32** combinaisons origine × axe × serpentin × modules ; positions connues des câblages usuels | **PASSE** 2026-11-05 |
| AFF-02 | `display::led_index()` | Modules 8×8 chaînés corrects sur les 4 modules ; (x, y) hors dalle et config invalide refusés | **PASSE** 2026-11-05 |
| AFF-03 | `FrameBuffer::set/get/fill_rect` | Écriture hors dalle ignorée, compteur incrémenté, rectangle clippé (pas de débordement `uint8_t`), `clear()` garde le compteur | **PASSE** 2026-11-05 |
| AFF-04 | `display::kGammaTable` | Monotone croissante, `[0]=0`, `[255]=255`, `[64]=12`, `[128]=56`, `[192]=137` (table générée par `tools/gen_gamma.py`) | **PASSE** 2026-11-05 |
| AFF-05 | `display::estimate_current_ua()` | Blanc plein = 15 616 000 µA exactement ; dalle éteinte = 256 000 µA | **PASSE** 2026-11-05 |
| AFF-06 | `display::apply_current_limit()` | Budget jamais dépassé, jamais sous-atteint de plus de 5 % (plancher d'arrondi), canaux uniformes, teinte 4:2:1 conservée à ±2, trame sombre intacte, trame noire au budget minimal | **PASSE** 2026-11-05 |
| AFF-07 | `display::is_valid_budget_ma()` | 400–15 000 mA ; budget invalide ⇒ repli sur 2000 ; minimum > courant de repos | **PASSE** 2026-11-05 |
| AFF-08 | `kFont5x7/3x5/3x4`, `text_width()`, `draw_text()` | Largeurs exactes, aucun glyphe ne déborde de sa hauteur, glyphe absent ⇒ boîte vide et curseur avancé, **clipping** gauche/droite/bas sans compteur hors-dalle | **PASSE** 2026-11-05 |
| AFF-09 | `transliterate()` | Accents Latin-1 (É à ç ÿ ß ñ ø Ì Ù Ð Ý), Œ, `°` → glyphe degré, minuscules, `€`/`×`/`©` → `?`, troncature NUL-terminée, `nullptr`, séquence UTF-8 tronquée | **PASSE** 2026-11-05 |
| AFF-10 | `draw_clock()` | `HH:MM` = 25 px dans les colonnes 3..27, deux-points étroit (col. 15, lignes 2 et 4), clignotement = −2 pixels, barre des secondes 30 s = 16 px | **PASSE** 2026-11-05 |
| AFF-11 | `draw_clock()`, `draw_date()` | `sure == false` ⇒ `--:--` (22 pixels, ligne 3 seule), **pas** de barre des secondes, différent de `00:00` ; date vide | **PASSE** 2026-11-05 |
| AFF-12 | `Scheduler::step()` | Rotation et durées, scène à donnée invalide sautée **ou quittée en cours**, horloge synthétique si tout est sauté et reprise sous 1 s, durée 0 = jusqu'à `finished`, liste invalide ⇒ défaut | **PASSE** 2026-11-05 |
| AFF-13 | `Scheduler::notify()` | P≥1 interrompt puis rend la main **au temps restant** ; P0 attend la fin de scène ; remplacement seulement par priorité ≥ | **PASSE** 2026-11-05 |
| AFF-14 | `Scroller` | 20 px/s = 20 px/s exactement à 40 ms comme à 33 ms (pas de dérive), sortie par la gauche à 3,05 s, 2 passages, infini, bornes 5–60, troncature à 128 car., texte vide fini d'emblée | **PASSE** 2026-11-05 |
| AFF-15 | `Scheduler` | Débordement 32 bits : construction à 0xFFFFF000, transitions et notification à travers le passage à zéro | **PASSE** 2026-11-05 |
| AFF-16 | `display::choose_icon_zone()`, `draw_alert_mark()` | Priorité **alerte tenue > logo air > rien** (pas d'alternance : une chose à la fois, décision du 2026-11-05) ; marque `!` 6×6 de 10 pixels dans la couleur de la priorité, logo air (AIR-19) | **PASSE** 2026-11-05 |
| AFF-18 | `draw_test_pattern()`, `chaser_frame()` | Pixel = 1, colonne = 8, diagonale = 32 avec (4,1), plein = 256 ; chenillard : marqueurs 0/255, traîne 48/24/12/6, repli modulo 256 | **PASSE** 2026-11-05 |
| AFF-17 | `display::render()` | Chaîne complète : luminosité **avant** gamma (128 → 56), pixel (1,0) sur la LED 15, luminosité 0 ⇒ tout éteint, blanc plein écrêté même à 15 A, mapping invalide ⇒ trame noire et `false` ; **aucune allocation** (GEN-04) | **PASSE** 2026-11-05 |

### 1.2 `brightness` — `dev/spec-luminosite.md` §5

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| LUM-01 | `Median5::push()` | 1 pic rejeté ; 2 pics consécutifs passent (limite documentée D7) ; remplissage par le premier échantillon | **PASSE** 2026-11-05 |
| LUM-02 | `AsymmetricAverage::push()` | 63 % à τ ±10 % en montée, descente 6× plus lente, premier échantillon sans retard, τ = 1 s = pas de filtrage, plancher α = 1/256 | **PASSE** 2026-11-05 |
| LUM-03 | `Controller::step()` | Ambiance constante + dalle vue par le capteur (400 comptes) ⇒ convergence sur le niveau de l'ambiance vraie, amplitude finale ≤ `step` : **pas de pompage** | **PASSE** 2026-11-05 |
| LUM-04 | `interpolate()` | Points exacts, interpolation arrondie, extrapolation plate | **PASSE** 2026-11-05 |
| LUM-05 | `interpolate()` | Câblage (b) : courbe décroissante correcte | **PASSE** 2026-11-05 |
| LUM-06 | `is_valid()` | < 2 points, doublon, non monotone, > 4095, > 6 points, bornes de chaque réglage ⇒ refus ; config invalide ⇒ défauts + `config_replaced` | **PASSE** 2026-11-05 |
| LUM-07 | `Controller::auto_level()` | ±20 comptes de bruit ⇒ sortie stable (zone morte d'un pas) | **PASSE** 2026-11-05 |
| LUM-08 | idem | Changement réel ⇒ saut à un multiple de `step` | **PASSE** 2026-11-05 |
| LUM-09 | idem | Plancher et plafond, `min_level = 0` inclus | **PASSE** 2026-11-05 |
| LUM-10 | `Controller::step()` | 4 ratés = maintien ; 5 ⇒ `kFallbackLevel` (64) + drapeau ; 1 lecture valide rétablit ; défaut dès le boot ⇒ 64 | **PASSE** 2026-11-05 |
| LUM-11 | idem | Collé à 0 ou 4095 ⇒ capteur en défaut | **PASSE** 2026-11-05 |
| LUM-12 | `in_window()` | 23:30 → 06:30 franchit minuit, fenêtre même jour, fenêtre vide ; **ignorée si l'heure n'est pas sûre** | **PASSE** 2026-11-05 |
| LUM-13 | `Controller::step()` | `Manual` ignore la mesure (même invalide) ; `Off` ⇒ 0 | **PASSE** 2026-11-05 |
| LUM-14 | `build_from_wizard()` | Compensation signée, 3 points triés par mesure, câblage (b) ⇒ courbe décroissante et contrôleur dans le bon sens, mesures égales ou > 4095 refusées sans toucher la config ; aucune allocation | **PASSE** 2026-11-05 |

### 1.3 `network` — `dev/spec-reseau-portail.md` §5

`RES-01` → `RES-11` sont **repris de ThermoSync** : ils doivent passer **avant**
toute modification du composant.

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| RES-01 | `WifiSupervisor::step()` | Pas d'identifiants ⇒ AP + portail dès le premier `step()`, pour toujours | **PASSE** 2026-11-05 (repris, traduit) |
| RES-02 | idem | Identifiants ⇒ essais, AP éteint | **PASSE** 2026-11-05 |
| RES-03 | idem | 3 essais échoués hors grâce ⇒ `Fallback`, station au repos | **PASSE** 2026-11-05 |
| RES-04 | idem | Nouvel essai 30 s / 15 min, AP maintenu | **PASSE** 2026-11-05 |
| RES-05 | idem | Connexion ⇒ `Connected`, AP et portail éteints | **PASSE** 2026-11-05 |
| RES-06 | idem | Lien perdu ⇒ nouvelle série | **PASSE** 2026-11-05 |
| RES-07 | idem | `attempt_id` change à chaque essai, et seulement là | **PASSE** 2026-11-05 |
| RES-08 | idem | Débordement 32 bits sans faux déclenchement (grâce désactivée pour l'héritage) | **PASSE** 2026-11-05 |
| RES-09 | `validate()` | SSID / phrase de passe : tous les cas de `CredStatus` | **PASSE** 2026-11-05 |
| RES-10 | `form_field()` | Champ absent, `%XX`, `%` mal formé rejeté, `+`, octet nul, débordement, lecture hors corps | **PASSE** 2026-11-05 |
| RES-11 | `build_captive_response()` | A, AAAA (vide sans erreur), EDNS ignoré, tronqué, compression, 2 questions, réponse, nom > 255 | **PASSE** 2026-11-05 |
| RES-12 | `derive_ap_password()` | Déterministe, propre à chaque carte, **chiffres + majuscules sans O/I** (lisible en 5×7), valide en WPA2 | **PASSE** 2026-11-05 |
| **RES-20** | `WifiSupervisor::step()` | Grâce : 5 min d'échecs `ApNotFound` ⇒ **jamais** d'AP ni de portail, essais à 0/20/50/100/170/240 s (délais 10/20/40/60/60), station **au repos** entre deux | **PASSE** 2026-11-05 |
| **RES-21** | idem | Fin de grâce ⇒ `Connecting` puis `Fallback` à +30 s ; succès en grâce ⇒ `Connected` sans portail | **PASSE** 2026-11-05 |
| **RES-22** | idem | `boot_grace_ms = 0` ⇒ comportement ThermoSync à l'identique (RES-01…08) | **PASSE** 2026-11-05 |
| **RES-23** | idem | 2 échecs `AuthFailed` en grâce ⇒ `Fallback` immédiat ; **sauf** après une association réussie (box capricieuse, pas mauvais mot de passe) | **PASSE** 2026-11-05 |
| **RES-24** | idem | 1 seul échec `AuthFailed` ne termine pas la grâce | **PASSE** 2026-11-05 |
| **RES-25** | idem | Associé sans IP 20 s ⇒ `WaitingLease`, pas de portail, réessais à 60/180/420/720/1020 s (60 → plafond 300 s) | **PASSE** 2026-11-05 |
| **RES-26** | idem | 29 min sans portail ; 30 min ⇒ `LeaseDesperate` (AP + portail + réessais poursuivis) | **PASSE** 2026-11-05 |
| **RES-27** | idem | Bail obtenu depuis `WaitingLease` / `LeaseDesperate` ⇒ `Connected`, AP éteint | **PASSE** 2026-11-05 |
| **RES-28** | idem | Après une association, une série `Connecting` restée associée ne tombe jamais en `Fallback` ; association perdue > 10 s ⇒ nouvelle série ; nouvel essai en repli qui s'associe sans bail ⇒ `WaitingLease` | **PASSE** 2026-11-05 |
| **RES-29** | idem | `reboot_without_ip_ms` : une demande par tranche, remise à zéro par une IP, jamais si 0 ni sans identifiants ; **aucune allocation** | **PASSE** 2026-11-05 |

### 1.4 `homeassistant` — `dev/spec-home-assistant.md` §9

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| HA-01 | `WsClient` (`on_connected`, `on_message`, `next_outgoing`) | `auth_required` → `auth` avec le jeton → `auth_ok` → `render_template` × n (ids 1..n) → `subscribe_events(horloge_notification)` → résultats → `Running`, `connections` = 1 | **PASSE** 2026-11-05 |
| HA-02 | `WsClient` (`auth_invalid`) | `TokenRejected`, message gardé, fermeture demandée, **aucune** tentative pendant 5 min, puis reconnexion ; sans message ⇒ `auth_invalid` | **PASSE** 2026-11-05 |
| HA-03 | `build_template()` | Entité seule, entité + attribut (`state_attr`), gabarit expert, échappement des guillemets et antislashs, tampon trop petit ⇒ 0 | **PASSE** 2026-11-05 |
| HA-04 | `WsClient` (reconnexion) | Après coupure : attente 5 s, `id` repartent à 1, **tous** les abonnements rejoués ; abonnement refusé ⇒ erreur gardée, liaison quand même `Running` | **PASSE** 2026-11-05 |
| HA-05 | `WsClient::handle_event()` | Valeur initiale puis changements, `id` → bonne valeur, résultat texte **ou** nombre natif, `unavailable` ⇒ invalide, `id` inconnu ⇒ ignoré et compté | **PASSE** 2026-11-05 |
| HA-06 | `decode_number()` | 0/1/2 décimales, négatif, non numérique, `unavailable`, `unknown`, `null`, vide, > ±2 000 000 000 ⇒ invalide | **PASSE** 2026-11-05 |
| HA-07 | `decode_boolean()`, `decode_text()` | Alias `on/true/home/open/yes`, `off/false/not_home/closed/no` ; `maybe` invalide ; texte vide / `unknown` / `none` invalide | **PASSE** 2026-11-05 |
| HA-08 | `WsClient::handle_event()` (`report_errors`) | Erreur de gabarit bornée à **96** caractères, valeur invalide, effacée au prochain résultat | **PASSE** 2026-11-05 |
| HA-09 | `Reassembler` | Fragments recollés dans l'ordre ; > 2048 octets abandonné et compté une fois, suite ignorée ; désordre, dépassement du total, `nullptr`, total nul ⇒ rejet ; 2048 exactement accepté | **PASSE** 2026-11-05 |
| HA-10 | `json_find()` et `on_message()` | Malformé, tronqué, > 16 niveaux, clé absente, mauvais type ⇒ `false` ; messages inattendus ⇒ ignorés et comptés, état inchangé ; nombres `1.`, `.5`, `+1`, `01`, `1e50`, 23 chiffres refusés | **PASSE** 2026-11-05 |
| HA-11 | `WsClient::step()` | Ping après `ping_s`, `pong` avec le bon `id` ; sans `pong` dans `pong_ms` ⇒ fermeture, `pong_misses`, `Waiting` ; échelle 5/10/20/40/60/60 s ; connexion muette ⇒ refaite après 60 s | **PASSE** 2026-11-05 |
| HA-12 | `WsClient::value_valid()` | Liaison vivante ⇒ valeur stable **valide** (dette D6) ; liaison tombée ⇒ invalide après `peremption_s` exactement | **PASSE** 2026-11-05 |
| HA-13 | `WsClient::decode_notification()` → `alerts::decode_form()` | Événement `horloge_notification` : `cle`, `titre`, `texte`, `priorite`, `duree_s`, `persistante`, `rappel_s`, `effacer` ; hors bornes ⇒ défaut compté ; sans `cle` ⇒ rejet compté | **PASSE** 2026-11-05 |
| HA-14 | `decode_weather()`, `weather_url()` | Réponse **réelle** `test/fixtures/open_meteo.json` ⇒ 7,3 °C ; tronquée, `null`, absente, > 70 °C ⇒ invalide ; URL avec 4 décimales et signes | **PASSE** 2026-11-05 |
| HA-15 | `settings::apply_ha_form()`, `apply_ha_value_form()`, `is_valid(HaSettings)` | Hôte, port, jeton (512 car., écrit seulement, vide = gardé, `effacer_jeton`), ping/pong, latitude/longitude/période ; valeurs : entité, gabarit, type, décimales, icône, péremption ; liaison active sans hôte ou sans jeton refusée ; 23 + 17 corps fautifs tout-ou-rien ; modèle avec types `secret` et `coordonnee`, `jeton` jamais relu | **PASSE** 2026-11-05 |

### 1.5 `alerts` — `dev/spec-alertes.md` §6

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| ALR-01 | `RuleEngine::step()` (comparaisons) | `<` `<=` `>` `>=` `==` `!=` lèvent et retombent au bon côté du seuil ; `change` ⇒ une **ponctuelle** (non tenue, expire) ; `vrai` / `faux` sur une source interne ; règle inactive ⇒ `Inactive`, rien en file | **PASSE** 2026-11-05 |
| ALR-02 | idem (hystérésis) | Lève à `< 0`, oscillation dans la bande `[0, 0,5[` ⇒ **un seul** déclenchement (même `seq` en file), retombe à `≥ 0,5` ; `>` avec hystérésis ; `==` avec bande | **PASSE** 2026-11-05 |
| ALR-03 | idem (confirmation) | Condition tenue 299 s ⇒ `Confirming`, 300 s ⇒ `Raised` ; une coupure d'un pas ⇒ `Watching` et compteur à zéro (299 s après la coupure : toujours en confirmation) | **PASSE** 2026-11-05 |
| ALR-04 | idem (`Unavailable`) | Source invalide ⇒ `Unavailable`, **aucune** levée même avec une valeur périmée à -5 °C ; retour ⇒ compteur à zéro ; une règle levée dont la source disparaît **retombe** ; valeur désactivée, air en préchauffage / défaut / absent / désactivé ⇒ indisponible | **PASSE** 2026-11-05 |
| ALR-05 | `sample()`, `default_rules()` | Les 11 sources booléennes valides et fausses sur de bonnes entrées, chacune bascule sur son entrée ; `LiaisonHaPerdue` / `JetonRefuse` / `DonneePerimee` **indisponibles** liaison désactivée, air idem capteur désactivé, `AirCapteurDefaut` valide capteur absent ; décimales et unités des sources ; noms aller-retour ; règles par défaut de la spec valides, `AERER` levée après 120 s d'AQI 4, retombe seule | **PASSE** 2026-11-05 |
| ALR-06 | `NotificationQueue::next_pass()` | Passage à la levée (`duree_s`, `interrupts` P1), rien avant `rappel_s`, rejoué à `rappel_s` ; `has_held()` entre deux passages (icône) ; texte suivi de la valeur (`GEL -4.1C`, même rang) | **PASSE** 2026-11-05 |
| ALR-07 | `RuleEngine::step()` ⇄ file | Retombée ⇒ notification tenue retirée **d'elle-même** ; un `clear()` extérieur est annulé au pas suivant tant que la règle est levée ; reconfiguration ⇒ notifications des règles non levées retirées ; aucun champ d'acquittement (`Notification` n'a que `effacer`, réservé à la source) | **PASSE** 2026-11-05 |
| ALR-08 | `validate(Rule)`, `settings::apply_alert_rule_form()` | Les 13 statuts (nom, source, index selon la source, comparaison booléenne ⇄ numérique, seuil, hystérésis, décimales, confirmation, priorité, texte, icône, durée, rappel) ; règle invalide ⇒ défaut inactif ; formulaire : seuil en **virgule fixe de la source** (`-0.5` sur 1 décimale ⇒ -5, `0.55` refusé, changement de source ⇒ nombres remis à zéro), index forcé à 0 hors `Valeur`, couleur `#RRGGBB` ou vide, 22 corps fautifs tout-ou-rien, `%ZZ` sur chaque champ refusé par son nom | **PASSE** 2026-11-05 |
| ALR-09 | `render_template()` | `{n}`, `{v}` (virgule fixe, signe, `--` si invalide), `{u}` ; accolade orpheline et placeholder inconnu copiés tels quels ; gabarit vide ⇒ nom ; `{v}` d'une source booléenne ⇒ `--` ; débordement ⇒ 0 et tampon vidé ; `nullptr` | **PASSE** 2026-11-05 |
| ALR-10 | `NotificationQueue::push()` | Même clé ⇒ contenu remplacé, `seq` et arrivée conservés, expiration repoussée ; texte vide ⇒ le titre ; couleur de la priorité sauf si posée ; clé invalide rejetée et comptée | **PASSE** 2026-11-05 |
| ALR-11 | idem (pleine) | 16 tenues : une P1 évince la **plus ancienne P0** ; puis la suivante ; P1 face à 16 P1 ⇒ **rejetée** et comptée ; P3 évince la plus ancienne P1 ; remplacement d'une clé existante n'évince personne | **PASSE** 2026-11-05 |
| ALR-12 | `next_pass()` | Priorité puis ancienneté ; P0 n'interrompt pas (`interrupts` faux) ; une ponctuelle ne se rejoue pas | **PASSE** 2026-11-05 |
| ALR-13 | `step()`, `push()` (`effacer`) | Ponctuelle retirée à `duree_s` ; tenue externe (`persistante`) survit 1 h, rejouée à `rappel_s`, retirée **seulement** par `effacer` de la même clé ; `clear` d'une clé absente sans effet ni erreur | **PASSE** 2026-11-05 |
| ALR-14 | `quiet_now()`, `rendering_for()`, `next_pass()` | Fenêtre 22:30 → 07:00 franchissant minuit, même jour, désactivée, heure non sûre ⇒ jamais silencieux, fenêtre vide, invalide ; rendu P0..P3 (interruption, clignotement 1 / 2 Hz, défilement, plancher 64) coupé en silence sauf P3 exempte ; passages suspendus, tenues **conservées et reprises** à la fin, P3 `outrepasse_silence` passe (si `silence_outrepasse_par_p3`) | **PASSE** 2026-11-05 |
| ALR-15 | `alerts::decode_form()` | Clé obligatoire `[a-z0-9_]` ≤ 24, champs hors bornes ⇒ défaut + compteur, booléens `1/true/oui/on`, troncature texte/titre comptée, clé absente / vide / majuscules / mal encodée ⇒ rejet, `nullptr` ; aucune allocation. Partagé avec HA-13 et WEB-06. | **PASSE** 2026-11-05 (décodeur ; le reste du composant `alerts` à écrire) |
| ALR-16 | tout `alerts` | 16 règles actives, 2000 pas avec file et passages, `AllocGuard` ⇒ **0** allocation | **PASSE** 2026-11-05 |
| ALR-17 | idem | Confirmation et rappel à cheval sur le débordement 32 bits ; expiration d'une ponctuelle à travers le débordement | **PASSE** 2026-11-05 |
| ALR-18 | modèle ⇄ page `/alertes` ⇄ wizard | Les 4 champs de `kAlertFields` dans le groupe `alertes:`, les 15 de `kAlertRuleFields` dans `alerte_regle:` une fois, cartes construites depuis `modele.regle`, le **wizard envoie exactement les 15 champs**, types `decimal` / `couleur` / `heure` rendus, aucun champ `acquitt…` (dans WEB-04) | **PASSE** 2026-11-05 |

### 1.6 `air_quality` — `dev/spec-qualite-air.md` §10

Bus I²C moqué par une doublure qui rejoue des trames (y compris CRC faux, NACK,
bus bloqué).

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| AIR-01 | `AirSensor::step()` (état `Probe`) | `PART_ID` `0x0160` ⇒ présent ; autre valeur ou NACK au premier contact ⇒ absent, grandeurs invalides, nouvelle sonde après 60 s ; NACK **après** avoir été vu ⇒ panne de bus, pas absence | **PASSE** 2026-11-05 |
| AIR-02 | `AirSensor::step()` (`EnsStatus`) | `VALIDITY_FLAG` 1 ⇒ `warming_up`, 2 ⇒ `initial_start`, grandeurs **invalides**, climat toujours valide ; 3 ou STATER ⇒ `fault` ; 0 ⇒ publiées | **PASSE** 2026-11-05 |
| AIR-03 | idem | `NEWDAT` à 0 ⇒ aucune relecture, l'AQI précédent reste, rien de republié comme frais | **PASSE** 2026-11-05 |
| AIR-04 | `ens160_decode_data()`, `ens160_decode_status()` | AQI masqué sur 3 bits, TVOC / eCO2 LE, extrêmes 0 et 65535, bits STATER / NEWDAT / validité | **PASSE** 2026-11-05 |
| AIR-05 | `ens160_encode_compensation()` | `TEMP_IN` K×64 (25,00 °C ⇒ 19082), `RH_IN` %×512 (50,0 % ⇒ 25600), écrêtage -40..85 °C / 0..100 %, arrondi | **PASSE** 2026-11-05 |
| AIR-06 | `aht21_decode()` | Trames synthétisées à ±1 centi / ±1 dixième : 23,45 °C / 45,6 %, -40 °C / 0 %, 85 °C / 100 % ; **trames réelles encore à capturer** dans `test/fixtures/` | **PASSE** 2026-11-05 (synthétique) |
| AIR-07 | `aht21_crc8()`, `AirSensor::step()` | CRC8 (poly 0x31, init 0xFF : vide ⇒ 0xFF, {0} ⇒ 0xAC) ; CRC faux ⇒ rejeté, sortie intacte ; dès 1 rejet le climat n'est plus déclaré valide, 3 rejets ⇒ compensation de repli ; une bonne trame rétablit | **PASSE** 2026-11-05 |
| AIR-08 | `AirSensor::step()` (`AhtCheck`, `AhtRead`) | Bit calibré absent au status ⇒ `0xBE 0x08 0x00` envoyé ; trame `Busy` ⇒ 3 relances à 10 ms puis cycle sans climat | **PASSE** 2026-11-05 |
| AIR-09 | `AirSensor::step()` (`EnsCompensate`) | AHT21 en défaut ou absent ⇒ 25,00 °C / 50,0 % écrits (`19082` / `25600`), `compensation_default`, ENS160 toujours lu | **PASSE** 2026-11-05 |
| AIR-10 | idem | Ordre vérifié sur le journal du faux bus : écriture `TEMP_IN` **avant** lecture `DATA_AQI` | **PASSE** 2026-11-05 |
| AIR-11 | `raw_level()` | 5 paliers pour `Aqi` (écrêté 1..5), `Eco2` (600/800/1000/1500) et `Tvoc` (220/660/1430/2200) aux seuils exacts ; invalide ⇒ `Unknown` | **PASSE** 2026-11-05 |
| AIR-12 | `LevelTracker::step()` | Oscillation 990/1010 ppm autour de 1000 avec hystérésis 50 ⇒ **zéro** changement ; descente à 949 seulement | **PASSE** 2026-11-05 |
| AIR-13 | idem | Niveau tenu 0..59 s ⇒ rien ; 60 s révolues ⇒ changement ; interruption ⇒ compteur à zéro ; lecture invalide ⇒ `Unknown` immédiat, première lecture valide ⇒ immédiate | **PASSE** 2026-11-05 |
| AIR-14 | idem | Montée au seuil sans marge, descente sous seuil − hystérésis ; TVOC avec sa propre hystérésis ; saut de deux niveaux d'un coup | **PASSE** 2026-11-05 |
| AIR-15 | `air_quality::is_valid(LevelConfig)`, `settings::apply_air_form()` | Seuils non croissants, seuil 0, hystérésis ≥ écart entre seuils, confirmation > 3600, pilote inconnu ⇒ refus ; formulaire tout-ou-rien (15 corps fautifs) | **PASSE** 2026-11-05 |
| AIR-16 | `AirSensor` (`temperature_offset_centi`) | Offset 3,50 °C : 26,00 mesuré ⇒ 22,50 publié **et** compensation 18922 ; changement d'adresse à chaud ⇒ nouvelle sonde | **PASSE** 2026-11-05 |
| AIR-17 | `build_offset()` | mesuré − référence ; > 2,00 °C ⇒ avertissement « déporter » ; > 10 °C ⇒ refus | **PASSE** 2026-11-05 |
| AIR-18 | `level_color()` | Gris neutre pour `Unknown` (r = g = b), vert seulement pour `Excellent`, rouge pour `Tres mauvais` | **PASSE** 2026-11-05 |
| AIR-19 | `display::draw_air_logo_6/8()` | Glyphes 6×6 (15 px) et 8×8 (23 px) dans la couleur donnée, découpage hors dalle sans écriture hors bornes ; modes `Aucun` / `ZoneIcone` / `Scene` câblés dans `app_main` (vérification visuelle HW) | **PASSE** 2026-11-05 (glyphes) |
| AIR-20 | `alerts::sample()`, `RuleEngine` | Règle `AirAqi >= 4` : préchauffage (`air_aqi.valid` faux), défaut, absent, capteur désactivé ⇒ `Unavailable`, aucune levée ; air valide à 5 ⇒ levée ; `AirCapteurDefaut` valide et vrai capteur absent | **PASSE** 2026-11-05 (dans ALR-04 / ALR-05) |
| AIR-21 | `AirSensor::step()` (`Recover`) | Bus bloqué ⇒ 3 échecs consécutifs ⇒ `fault`, `recover()` appelé, compteur `bus_resets`, nouvelle sonde ; aucune attente : une transaction par `step()` | **PASSE** 2026-11-05 |
| AIR-22 | modèle ⇄ page `/air` | Les 16 champs de `kAirFields` dans exactement un groupe de la page, modèle avec `libelle` et type `temperature`, clé `reference` du wizard connue | **PASSE** 2026-11-05 (dans WEB-04) |

### 1.7 `homeassistant::mqtt` — `dev/spec-home-assistant.md` §6 bis et §9

Repris de ThermoSync (`domotique/home_assistant.hpp` + `mqtt_config.hpp`),
adapté aux entités de l'horloge. Test host : `firmware/test/test_mqtt.cpp`.
Banc : `firmware/test/bench/mini_broker.py` (broker MQTT 3.1.1 minimal qui
journalise ce que l'horloge publie et peut injecter `<prefixe>/status`).

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| MQ-01 | `build_identity()`, `mqtt_topic()`, `ha_status_topic()` | `horloge/<id>/disponible`, `horloge/<id>/etat`, `<prefixe>/status`, `id` = MAC hexadécimale, `url` depuis l'IP ; débordement ⇒ 0 et tampon vidé ; chaînes trop longues refusées | **PASSE** 2026-11-05 |
| MQ-02 | `mqtt_discovery()` | 10 entités : topic `<prefixe>/<plateforme>/horloge_<id>/<objet>/config`, JSON **analysé** (`name`, `unique_id`, `state_topic`, `value_template`, `availability`, bloc `device` avec `identifiers` et `connections`, `origin`, `configuration_url`), classes / unités / `state_class` de la table du §6 bis ; `enum` avec `options` sans unité ; binaires sans `state_class` ; autre préfixe ; index hors liste et tampons trop petits ⇒ 0 | **PASSE** 2026-11-05 |
| MQ-03 | `mqtt_entity_key()` ⇄ `mqtt_state_json()` | Chaque `value_template` lit une clé **publiée** ; chaque clé publiée est **lue** par une entité ; `object_id` uniques ; 10 clés | **PASSE** 2026-11-05 |
| MQ-04 | `mqtt_state_json()` | Température à 1 décimale arrondie (21,49 ⇒ 21.5, -0,05 ⇒ -0.1, 20,05 ⇒ 20.1), humidité en dixièmes ; préchauffage / première heure ⇒ air `null` + `prechauffage` ON ; défaut ⇒ tout `null` + `capteur_defaut` ON ; absent ⇒ `capteur_defaut` ON, `prechauffage` `null` ; capteur désactivé ⇒ `null` partout (pas « sans défaut ») ; climat invalide seul ; LDR invalide ⇒ `null` ; `heure_sure` ON/OFF ; tampon trop petit ⇒ 0 | **PASSE** 2026-11-05 |
| MQ-05 | `mqtt_level_option()`, `mqtt_state_json()` | `excellent`, `bon`, `moyen`, `mauvais`, `tres_mauvais` = les `options` de la découverte ; `Unknown` ⇒ `null` ; un niveau confirmé **avec air invalide** ⇒ `null` (honnêteté) | **PASSE** 2026-11-05 |
| MQ-06 | `MqttPublisher::update_state()` / `next_action()` | État publié à la première mesure, puis **pas avant** `periode_s` malgré une mesure par seconde, puis à la période ; **changement de niveau** ⇒ publié sans attendre ; même niveau ⇒ rien ; aucune allocation | **PASSE** 2026-11-05 |
| MQ-07 | `is_ha_online()`, `MqttPublisher::on_message()` | `<prefixe>/status` = `online` ⇒ découverte + état renvoyés ; `offline` ou autre topic ⇒ rien ; `request_discovery()` (bouton) idem | **PASSE** 2026-11-05 |
| MQ-08 | `validate(MqttConfig)`, `mqtt_status_reason()` | Les 9 statuts (`HostEmpty` seulement si active, `HostInvalid` bords et caractères, `PortInvalid`, `UserInvalid`, `PasswordInvalid`, `PasswordWithoutUser`, `PrefixInvalid` vide / bords / majuscule / joker, `PeriodInvalid` 4 et 601) ; tableaux non terminés refusés ; `configure()` invalide ⇒ défauts (désactivé) ; une raison par statut | **PASSE** 2026-11-05 |
| MQ-09 | `settings::apply_mqtt_form()`, `mqtt_model_json()`, `mqtt_json()`, page `/mqtt` | Les 8 champs de `kMqttFields` ; mot de passe vide = gardé, `effacer_mdp`, préfixe vide = défaut ; 16 corps fautifs tout-ou-rien ; modèle avec `libelle`, type `secret`, défauts 1883 / `homeassistant` ; le mot de passe n'est jamais relu (`mot_de_passe_defini`) ; page : champs dans exactement un groupe (`mqtt:`, `mqtt_avance:`), aucun `name=` recopié (WEB-04) | **PASSE** 2026-11-05 |
| MQ-10 | `MqttPublisher::configure()` / `next_action()` | Désactivation connectée ⇒ `PublishOffline` **puis** `Disconnect`, jamais `Connect` ; changement de broker connecté ⇒ offline, disconnect, connect ; en connexion ⇒ disconnect, connect ; coupure ⇒ rien en file, retour ⇒ online + découverte + état ; compteurs sans double comptage | **PASSE** 2026-11-05 |

### 1.8 `settings` / web — `dev/spec-serveur-web.md` §7

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| WEB-01 | `form_field()` | Couvert aussi par RES-10, appliqué aux formulaires d'affichage, de luminosité, de système, du wizard de luminosité, de `usine` et de `test` | **PASSE** 2026-11-05 (`test_settings.cpp`) |
| WEB-02 | `settings::apply_<domain>_form()` | Tout-ou-rien : un champ fautif ⇒ `false` + `FormError`, **rien** modifié dans `out` ; corps vide ⇒ succès sans changement ; mauvais encodage (`%ZZ`) sur chaque champ ⇒ refus nommé | **PASSE** 2026-11-05 |
| WEB-03 | idem | Champ nommé, raison en français, bornes exactes (`budget_ma` 400..15000, `vitesse_defilement` 5..60, `largeur_module` 0/8, `pas_lum` 1..32, `extinction_*` 0..1439, scènes ≤ 8, courbe 2..6 points croissants, fuseau dans la table, hôte NTP valide) | **PASSE** 2026-11-05 |
| WEB-04 | `settings::*_model_json()` ⇄ `www/app.html` | Le modèle nomme chaque champ avec `type`, `libelle`, bornes, défaut, aide (`heure` pour les minutes du jour, `scenes` et `courbe` pour les éditeurs) ; la page **construit** ses formulaires depuis `/api/<domaine>/modele` (aucun `name=` recopié), répartit chaque champ dans **exactement un** groupe, possède un widget par type émis (dont `type: 'time'` et le réordonnancement des scènes) ; les clés et valeurs codées en dur par les wizards existent dans le modèle ; aucun `https://`, aucun emoji. 7 domaines (`affichage`, `luminosite`, `systeme`, `air`, `ha`, `mqtt`, `alertes`) | **PASSE** 2026-11-05 |
| WEB-05 | `settings::JsonWriter` | Échappement, virgules, profondeur ≤ 8, `nullptr` refusé, débordement ⇒ `length()` = 0 (le serveur répond 500 `incomplet`), jamais de JSON invalide | **PASSE** 2026-11-05 |
| WEB-06 | `alerts::decode_form()` | Mêmes bornes et rejets que HA-13 | **PASSE** 2026-11-05 (= ALR-15) |
| WEB-07 | `handle_factory()` (`web_server.cpp`) | Refus sans `confirme=OUI` | Vérifié par HTTP sur la carte (400 sans confirmation) ; logique dans l'adaptateur, pas de test host |
| WEB-08 | `display::draw_test_pattern()` | Chaque motif produit le tampon attendu | **PASSE** 2026-11-05 (= AFF-06 ; `POST /api/affichage/test` vérifié sur la carte) |

### 1.9 `timekeeping` — heure locale, règles POSIX, validité

Logique pure : le `TZ` de la libc n'est pas utilisé (celui de MinGW ne
comprend pas les règles `M3.5.0`, donc intestable sur host). Epochs de
référence calculés avec Python (`datetime`, UTC).

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| TIME-01 | `parse_posix_tz()` | Zurich, UTC0, New York, Londres, Sydney, demi-heures (`IST-5:30`, Terre-Neuve), secondes acceptées, heures de bascule négatives / > 24 ; 20 chaînes invalides refusées sans toucher la sortie | **PASSE** 2026-11-05 |
| TIME-02 | `to_local()` | Zurich : 2026-11-05 12:00 UTC = 13:00 jeudi ; **bascules à la seconde** : 29 mars 00:59:59 UTC → 01:59:59, 01:00:00 UTC → 03:00:00 ; 25 octobre 00:59:59 UTC → 02:59:59, 01:00:00 UTC → 02:00:00 | **PASSE** 2026-11-05 |
| TIME-03 | `to_local()` | New York, 2ᵉ dimanche de mars à 02:00 local : 06:59:59 UTC → 01:59:59 EST, 07:00:00 UTC → 03:00:00 EDT | **PASSE** 2026-11-05 |
| TIME-04 | `civil_from_days()`, `is_leap()` | 29 février 2028 23:30 UTC = 1ᵉʳ mars 00:30 CET mercredi ; 2000 / 2100 / 2026 ; aller-retour sur ±200 000 jours | **PASSE** 2026-11-05 |
| TIME-05 | `to_local()` UTC | 2000-01-01 samedi, 2038-01-19 03:14:07 (pas de falaise 32 bits), 2100-03-01 lundi, epoch 0 jeudi, epoch −1 = 1969-12-31 23:59:59 | **PASSE** 2026-11-05 |
| TIME-06 | `is_dst()` | Hémisphère sud (Sydney) : été à cheval sur le nouvel an, janvier et novembre en DST, juin hors DST | **PASSE** 2026-11-05 |
| TIME-07 | `SyncValidity` | Jamais synchronisé ⇒ faux ; synchronisé ⇒ vrai ; 48 h ⇒ faux ; resynchro ⇒ vrai ; débordement 32 bits ; 0 = défaut | **PASSE** 2026-11-05 |
| TIME-08 | `named_zones()`, `posix_for_zone()` | 18 zones, toutes analysables, défaut `Europe/Zurich` présent, inconnue ⇒ `nullptr` ; aucune allocation | **PASSE** 2026-11-05 |

### 1.10 Garde-fous transverses

| Id | Vérifie | Statut |
|---|---|---|
| GEN-01 | Les composants purs compilent avec un **g++ nu** (aucun include ESP-IDF) | **PASSE** 2026-11-05 (`display`, g++ 15.2 MinGW) |
| GEN-02 | Build host en `-Werror` avec tout l'arsenal d'avertissements | **PASSE** 2026-11-05 |
| GEN-03 | Couverture ≥ 95 % de la logique pure, échec en dessous | **PASSE** 2026-11-05 (`display` + `brightness` : 629 lignes, **98,9 %** ; le reste = branches défensives injoignables ou `default` d'un `switch` sur `enum class`) |
| GEN-04 | `operator new` global armé après init ⇒ **aucune allocation** sur tous les chemins testés | **PASSE** 2026-11-05 (`AllocGuard` autour de `render()`, AFF-17) |
| GEN-05 | Harnais **sans `assert()`** : code de retour non nul au moindre échec, nombre de vérifications compté | **PASSE** 2026-11-05 (8530 vérifications ; les échecs initiaux de AFF-06 puis de AFF-12/13/15 ont bien fait sortir en erreur) |

---

## 2. Vérifications sur matériel réel

Étape **séparée**, jouée sur la carte, consignée ici avec sa date.

| Id | Procédure | Attendu | Statut |
|---|---|---|---|
| HW-AFF-01 | Chenillard physique (LED 0 rouge, LED 255 bleue, point blanc) puis motifs `pixel`, `ligne`, `diagonale`, `plein` | Câblage lu au chenillard, puis motifs géométriquement justes à l'œil | **PASSE** 2026-11-05 : rouge haut-gauche, parcours vertical serpentin, bleue haut-droite ⇒ (colonne, serpentin, haut-gauche) ; pixel haut-gauche, colonne verticale à gauche, diagonale confirmés par Nicolas. Dorsales **SPI/DMA et RMT** toutes deux fonctionnelles. |
| HW-AFF-02 | Blanc, luminosité 64, **pince ampèremétrique** | Courant mesuré à ±20 % de l'estimation de la page `/` | À FAIRE |
| HW-AFF-03 | Tension **à la dalle** sur scène `plein` | ≥ 4,7 V | À FAIRE |
| HW-AFF-04 | Observation à 25 trames/s, lisibilité à 4 m | Aucun scintillement, heure lisible | À FAIRE |
| HW-AFF-05 | 1 h sur une scène claire | Dalle et alimentation tièdes | À FAIRE |
| HW-LUM-01 | Relevés nuit noire / soir / plein jour | Trois valeurs nettement distinctes | **EN COURS** 2026-11-05 : ADC1_CH5 étalonné (eFuse), ~2086 comptes / 1,9 V en éclairage de bureau ; **câblage (a) confirmé** (main sur la LDR ⇒ la dalle s'assombrit). Relevés nuit/soir/jour à faire en conditions stables. |
| HW-LUM-02 | Wizard d'étalonnage, étape 2 | Auto-éclairage **chiffré** ; > 150 ‰ ⇒ déplacer le capteur | À FAIRE |
| HW-LUM-03 | Une journée complète d'observation | Aucun pompage, aucun scintillement | À FAIRE |
| HW-RES-01 | Coupure secteur maison (horloge **et** box) | Reprise seule, heure affichée, **pas** de portail | À FAIRE |
| HW-RES-02 | Box éteinte 10 min puis rallumée | Reconnexion seule, pas de portail | À FAIRE |
| HW-RES-03 | DHCP désactivé sur la box | `AttenteBail` sans portail ; portail **en plus** à 30 min + alerte | À FAIRE |
| HW-RES-04 | Phrase de passe changée sur la box | Portail en moins de 2 min | À FAIRE |
| HW-RES-05 | Portail ouvert, iPhone / Android / Windows | Fenêtre captive qui s'ouvre seule ; SSID et phrase de passe **lisibles sur la dalle** | **EN COURS** 2026-11-05 : AP `Horloge-XXXX` WPA2, DNS captif, sondes et page `/wifi` en place, SSID + mot de passe défilent sur la dalle — **essai au téléphone en attente** |
| HW-RES-06 | Identifiants saisis au portail | Connexion **sans redémarrage** | **PASSE** 2026-11-05 : box jointe depuis le portail au téléphone, état `connected` sans reset (2 min de fonctionnement dans les logs), point d'accès éteint (absent du scan Wi-Fi du PC) |
| HW-RES-07 | `horloge.local` depuis PC et téléphone | Résolu | **PASSE (PC)** 2026-11-05 : `http://horloge.local/api/info` répond depuis le PC Windows (`"ip":"10.10.0.85"`, `"reseau":"connected"`). Téléphone non testé. |
| HW-HA-01 | Liaison au Home Assistant **de la maison**, en lecture | Valeur affichée = valeur de l'interface | À FAIRE par Nicolas (jeton d'un utilisateur dédié, wizard `/ha`) ; **mécanique vérifiée** le 2026-11-05 : hôte fictif ⇒ `connecting` → `waiting` avec coupures comptées, valeur 1 enregistrée, jeton jamais renvoyé par l'API, désactivation ⇒ `disabled` |
| HW-HA-02 | Home Assistant redémarré | Reconnexion seule, valeurs retrouvées | À FAIRE |
| HW-HA-03 | Jeton révoqué | `JetonRefuse` visible sur la page, pas de martèlement | À FAIRE |
| HW-HA-04 | Automatisation réelle qui déclenche `horloge_notification` | Notification affichée au bon rendu | À FAIRE |
| HW-ALR-01 | Sonde refroidie sous le seuil de la règle `GEL` | Alerte après la durée de confirmation, pas avant | À FAIRE |
| HW-ALR-02 | Règle `AERER` levée, puis fenêtre ouverte | Passages toutes les `rappel_s` tant que l'air est mauvais ; disparition **seule** quand le niveau retombe ; aucun bouton sur la page | **MÉCANIQUE VÉRIFIÉE** 2026-11-05 : `AERER` en `confirmation` à AQI 4 puis retour `surveille` à AQI 3 ; règle `CO2 >= 900` posée par l'API ⇒ `levee`, texte `CO2 906 ppm`, notification `regle_12` tenue (`!` dans la zone d'icône) ; notification externe tenue retirée **seulement** par `effacer` de sa source ; `seuil=0.55` sur une source à 1 décimale ⇒ 400 nommé. Reste : fenêtre ouverte réelle, passages comptés sur une heure |
| HW-SYS-01 | Fuseau changé sur `/systeme` (`Europe/Zurich` → `UTC`) | Heure décalée **sans redémarrage** ; défaut `Europe/Zurich` après réglages d'usine | À FAIRE (page écrite, à jouer sur la carte) |
| HW-TIME-01 | Mise sous tension, box jointe, SNTP `pool.ntp.org` | `--:--` jusqu'à la synchro, puis l'heure locale de Zurich ; date en 3×5 | **PASSE** 2026-11-05 : `boot_grace` → `connected` à 23 s, `SNTP synchronised` à 31 s, `time is now SURE (2026-11-05 03:33:24)`, heure et date sur la dalle |
| HW-TIME-02 | Changement d'heure réel (29 mars 2026 à 02:00 → 03:00, 25 octobre 03:00 → 02:00) | La dalle bascule d'elle-même, sans redémarrage ni resynchro | À FAIRE (en mars) — couvert sur host à la seconde par TIME-02 |
| HW-ALR-03 | Une nuit complète en heures silencieuses | Aucun clignotement, alerte conservée et visible au réveil | À FAIRE |
| HW-ALR-04 | `POST /api/notification` depuis le PC (`cle=test`, priorité 2, 15 s) | 200, le texte interrompt l'heure, défile dans la couleur de la priorité, l'heure reprend | **PASSE** 2026-11-05 : ponctuelle P1 ⇒ scène `Alerte` puis retour `Heure`, `passages:true`, expirée ; tenue P2 (`persistante`) ⇒ passage, puis `tenues:true` jusqu'à `effacer` |
| HW-AIR-01 | Mise sous tension, chronomètre | `--` + préchauffage ~3 min, puis valeurs ; **aucune** alerte « AERER » pendant ce temps | À FAIRE |
| HW-AIR-02 | Premier usage du capteur neuf | Drapeau 2 pendant ~1 h, signalé sur la page | **OBSERVÉ** 2026-11-05 : `DEVICE_STATUS` = 0x8B (validité 2, NEWDAT), `premiere_mise_en_service:true`, AQI `--`, niveau `Inconnu` ; à confirmer : passage à 0 après ~1 h cumulée |
| HW-AIR-03 | Solvant vaporisé à proximité | AQI, TVOC, eCO2 montent ; niveau confirmé après 60 s | À FAIRE |
| HW-AIR-04 | Pièce fermée plusieurs heures puis fenêtre ouverte | Montée puis retour, logo qui change **sans clignoter** entre deux niveaux | À FAIRE |
| HW-AIR-05 | Wizard étape 2 : thermomètre de référence à côté du module, dalle allumée 30 min | Écart chiffré ; > 2 °C ⇒ avertissement de déport | **MÉCANIQUE VÉRIFIÉE** 2026-11-05 (`reference=2100` avec 24,80 °C mesuré ⇒ `decalage_centi:380, deporter:true`, puis remis à 0) ; écart réel à mesurer par Nicolas |
| HW-AIR-06 | Température / humidité confrontées à une référence | Après compensation : < 1 °C, < 5 % HR | À FAIRE |
| HW-AIR-07 | Capteur débranché à chaud puis rebranché | Grandeurs invalides, logo gris, `CAPTEUR AIR HS` ; bus rétabli **sans redémarrage** | À FAIRE |
| HW-AIR-08 | Lecture de `PART_ID` et des registres sur le module réel | Table de `spec-qualite-air.md` §2 confirmée ou **corrigée** (dette D11) | **PASSE** 2026-11-05 : `PART_ID` 0x0160 à 0x53, `OPMODE` relu 0x02, `DEVICE_STATUS` cohérent (STATAS, validité, NEWDAT), AHT21 à 0x38 calibré, 24,9 °C / 37,6 % plausibles. Restent à confronter : `DATA_*` (après la première heure) |
| HW-MQ-01 | Home Assistant **de test en Docker** + Mosquitto, liaison activée | Appareil « Horloge » et ses 10 entités découverts sans YAML ; valeurs = page `/air` | **MÉCANIQUE VÉRIFIÉE** 2026-11-05 sur le broker de banc (`mini_broker.py`) : `CONNECT` avec utilisateur, keepalive 30 s et testament `offline` retenu, abonnement `homeassistant/status`, `online`, **10** messages de découverte retenus QoS 1 (JSON valides), état retenu QoS 0 toutes les 10 s (28,5 °C, 40,8 %, eCO2 1000, TVOC 992, AQI 4, `mauvais`, LDR 1198) ; `/api/etat.mqtt` = `connected`. Reste : le vrai Home Assistant (Mosquitto) |
| HW-MQ-02 | Mise sous tension de l'horloge, pendant les 3 min de préchauffage | Entités d'air *inconnues* (`null`), puis valeurs ; `prechauffage` à `on` puis `off` | À FAIRE (logique couverte par MQ-04 ; à observer au prochain démarrage à froid avec le broker de banc) |
| HW-MQ-03 | Horloge débranchée ; puis Home Assistant redémarré | `disponible` passe `offline` (testament) ; après redémarrage de HA, entités retrouvées (découverte renvoyée) | **MÉCANIQUE VÉRIFIÉE** 2026-11-05 : `homeassistant/status` = `online` injecté ⇒ les 10 découvertes + l'état renvoyés ; bouton « renvoyer la découverte » idem ; désactivation ⇒ `offline` retenu **puis** `DISCONNECT` propre. Testament par coupure brutale : à faire sur Mosquitto |
| HW-WEB-01 | Page sur iPhone, Android, PC, thèmes clair et sombre | Lisible, utilisable au doigt | À FAIRE |
| HW-WEB-02 | OTA d'une image valide (`curl --data-binary @matrix_clock.bin /api/ota`) | Barre de progression sur la dalle, redémarrage sur la nouvelle image, `OTA image confirmed` | **PASSE** 2026-11-05 : 998 Ko en 5,6 s, redémarrage sur l'autre emplacement, image confirmée, réglages conservés (budget 2500 relu) |
| HW-WEB-03 | OTA d'une image **volontairement cassée** | **Rollback** automatique sur l'ancienne image | À FAIRE |
| HW-WEB-04 | `GET /api/affichage`, `POST budget_ma=99999` puis `budget_ma=2500` ; `GET /api/luminosite/brut` ; `POST extinction_debut=1380` ; `POST scenes=Date:5:1,Heure:20:1` | 400 `{"ok":false,"champ":"budget_ma","raison":…}` puis 200 ; lecture brute vivante ; heures relues ; **ordre des scènes** appliqué sans redémarrage (log `Date` puis `Heure`) | **PASSE** 2026-11-05 |
| HW-WEB-05 | Texte d'état (IP) à la connexion | Une **seule** passe `http://<ip>  horloge.local` en bleu clair à l'obtention de l'IP, jamais dans la rotation ; en portail : SSID + mot de passe toutes les 60 s | À OBSERVER (logique en place ; passe trop courte pour le log de 5 s) |
| HW-WEB-06 | Couleurs : chiffres blancs, deux-points ambre, barre des secondes bleue, jour ambre + date blanche | Lisibilité à l'œil | À CONFIRMER par Nicolas |
| HW-HUB-01…| *(phase 2)* hub de test en Docker, puis vraie carte | — | PHASE 2 |

---

## 3. Couverture des exigences

Chaque `EX-nn` de `dev/cadrage-fonctionnel.md` doit avoir **au moins un** test.

| Exigence | Tests |
|---|---|
| EX-01, EX-04 | AFF-10, AFF-12, HW-AFF-04 |
| EX-02 | AFF-11, TIME-07, HW-TIME-01 |
| EX-03 | TIME-01, TIME-02, TIME-03, TIME-06, HW-TIME-01 ; HW-SYS-01 (réglage sur la page) et HW-TIME-02 (bascule réelle, en mars) à faire |
| EX-05 | AFF-05, AFF-06, AFF-07, HW-AFF-02 |
| EX-06 | AFF-01, AFF-02, HW-AFF-01 |
| EX-07 | AFF-04 |
| EX-10, EX-11 | LUM-02, LUM-03, LUM-04, LUM-05, HW-LUM-03 |
| EX-12 | LUM-12, LUM-13 |
| EX-13 | LUM-10, LUM-11 |
| EX-14 | LUM-14, HW-LUM-02 |
| EX-20, EX-21 | RES-01 → RES-05, HW-RES-05 |
| EX-22 | RES-20 → RES-24, HW-RES-01, HW-RES-02 |
| EX-23 | RES-25 → RES-28, HW-RES-03 |
| EX-24 | HW-RES-06 |
| EX-25 | HW-RES-07 |
| EX-30, EX-31 | HA-01, HA-03, HA-04, HA-05 |
| EX-32 | HA-12 |
| EX-33 | HA-02, HA-11, HW-HA-02, HW-HA-03 |
| EX-34 | WEB-04 *(le jeton est en écriture seule dans le modèle)* |
| EX-35 | HA-14 |
| EX-36 | MQ-01, MQ-02, MQ-03, HW-MQ-01 |
| EX-37 | MQ-04, MQ-05, HW-MQ-02 |
| EX-38 | MQ-07, MQ-08, MQ-10, HW-MQ-03 |
| EX-40, EX-41 | HA-13, ALR-15, WEB-06, HW-HA-04 |
| EX-42 | ALR-10, ALR-11, ALR-12, ALR-13 |
| EX-43 | ALR-01 → ALR-07, HW-ALR-01 |
| EX-44 | ALR-04, ALR-05 |
| EX-45 | ALR-14, HW-ALR-03 |
| EX-46 | ALR-06, ALR-07, ALR-13, HW-ALR-02 |
| EX-70 | AIR-01, AIR-04, AIR-06, HW-AIR-08 |
| EX-71 | AIR-02, AIR-03, AIR-18, AIR-20, HW-AIR-01, HW-AIR-02 |
| EX-72 | AIR-05, AIR-09, AIR-10 |
| EX-73 | AIR-07, AIR-08 |
| EX-74 | AIR-11 → AIR-15, HW-AIR-03, HW-AIR-04 |
| EX-75 | AIR-19, AFF-16 |
| EX-76 | AIR-20, ALR-05 (sources air), HW-AIR-03 |
| EX-77 | AIR-16, AIR-17, HW-AIR-05, HW-AIR-06 |
| EX-50, EX-51 | WEB-02, WEB-03, WEB-04, ALR-18, AIR-22, MQ-09 |
| EX-52 | HW-WEB-01 *(+ relevé de la taille gzippée à chaque build)* |
| EX-53 | HW-WEB-02, HW-WEB-03 |
| EX-60 | *(watchdog : vérification matérielle à ajouter)* |
| EX-61 | AFF-15, et vérification matérielle de charge à ajouter après la mesure SPI/RMT |
| EX-62 | GEN-04, ALR-16 |

**Trous connus à combler** : EX-03 (changement d'heure), EX-60 (watchdog),
EX-61 (charge processeur mesurée). Notés dans `dev/todo.md`.
