# Plan de test — Horloge

> **À jour en permanence** (garde-fou central). Matrice test ↔ code couvert ↔
> procédure ↔ attendu ↔ statut/date. Couvre les **tests host** (sur PC) **et** les
> **vérifications matérielles** (étape séparée, hors CI).
>
> Statut au **2026-11-05** : specs seules, **aucun test écrit**. Tous les tests
> sont `À ÉCRIRE`. Lancement prévu : `bash firmware/test/run_host_tests.sh`
> (couverture : `--coverage`, objectif ≥ 95 %, échec en dessous).

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
| AFF-16 | zone d'icônes | Alternance 3 s, priorité alerte > logo air > liaison, glyphes du logo air | À ÉCRIRE |
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
| HA-01 | `ClientHa::step()` | `auth_required` → `auth_ok` → abonnements → `EnMarche` | À ÉCRIRE |
| HA-02 | idem | `auth_invalid` ⇒ `JetonRefuse`, délai 5 min | À ÉCRIRE |
| HA-03 | `construire_gabarit()` | Entité, entité+attribut, gabarit expert, échappement JSON | À ÉCRIRE |
| HA-04 | `ClientHa::step()` | `id` croissants dès 1, repartent à 1, tous les abonnements rejoués | À ÉCRIRE |
| HA-05 | `traiter_event()` | Valeur initiale puis changements, association `id` → valeur | À ÉCRIRE |
| HA-06 | `decoder_nombre()` | 0/1/2 décimales, négatif, non numérique, hors bornes, `unavailable`, `unknown`, `null`, vide | À ÉCRIRE |
| HA-07 | `decoder_booleen()`, `decoder_texte()` | Alias `home`, `open`, `closed` | À ÉCRIRE |
| HA-08 | `traiter_event()` | Erreur de gabarit bornée à 96 car., valeur reste invalide | À ÉCRIRE |
| HA-09 | `Reassembleur` | Trame fragmentée recollée ; > 2048 octets abandonnée, **non** interprétée | À ÉCRIRE |
| HA-10 | `json_reader` | JSON malformé, tronqué, profondeur excessive, clé absente ⇒ ignoré | À ÉCRIRE |
| HA-11 | `ClientHa::step()` | Pas de `pong` ⇒ reconnexion ; délai 5/10/20/40/60 s plafonné | À ÉCRIRE |
| HA-12 | `Valeur::fraiche()` | Invalidée si **liaison tombée** depuis `peremption_s` ; **non** invalidée si la liaison vit et la valeur est stable (dette D6) | À ÉCRIRE |
| HA-13 | `decoder_notification()` | Champs valides, hors bornes ⇒ défaut, `cle` absente ⇒ rejet, `effacer` cible la bonne clé | À ÉCRIRE |
| HA-14 | `open_meteo::decoder()` | Réponse réelle (`test/fixtures/`), amputée, valeur absente | À ÉCRIRE |
| HA-15 | `settings::valider_ha()` | Hôte, port, jeton, entité, gabarit, lat/lon ⇒ repli | À ÉCRIRE |

### 1.5 `alerts` — `dev/spec-alertes.md` §6

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| ALR-01 | `Regle::evaluer()` | Chaque comparaison | À ÉCRIRE |
| ALR-02 | idem | Hystérésis : oscillation dans la bande ⇒ **un seul** déclenchement | À ÉCRIRE |
| ALR-03 | idem | Confirmation 300 s ; coupure d'un pas ⇒ compteur à zéro | À ÉCRIRE |
| ALR-04 | idem | Source invalide ⇒ `Indisponible`, aucune levée ; retour ⇒ compteur à zéro | À ÉCRIRE |
| ALR-05 | `MoteurAlertes::step()` | Les 7 sources internes | À ÉCRIRE |
| ALR-06 | idem | Passages : `duree_s` à l'arrivée puis toutes les `rappel_s` tant que `Levee` ; icône maintenue entre deux | À ÉCRIRE |
| ALR-07 | idem | Retombée ⇒ notification tenue retirée d'elle-même ; **aucune** API ne la retire autrement | À ÉCRIRE |
| ALR-08 | `valider_regle()` | Seuil dans la virgule fixe de la source, index, gabarit, priorité | À ÉCRIRE |
| ALR-09 | `rendre_gabarit()` | `{n}`, `{v}`, `{u}`, accolade orpheline, inconnu, débordement | À ÉCRIRE |
| ALR-10 | `FileNotifications::pousser()` | Dédoublonnage par clé, rang conservé, expiration repoussée | À ÉCRIRE |
| ALR-11 | idem | File pleine : éviction de la plus basse priorité la plus ancienne ; rejet sinon | À ÉCRIRE |
| ALR-12 | `FileNotifications::courante()` | Priorité puis ancienneté | À ÉCRIRE |
| ALR-13 | idem | Ponctuelle expire à `duree_s` ; tenue externe retirée seulement par `effacer` de la même `cle` | À ÉCRIRE |
| ALR-14 | `silence()` | Franchit minuit ; clignotement et défilement supprimés ; passages suspendus, tenues conservées et **reprises à la fin** ; P3 outrepasse | À ÉCRIRE |
| ALR-15 | `alerts::decode_form()` | Clé obligatoire `[a-z0-9_]` ≤ 24, champs hors bornes ⇒ défaut + compteur, booléens `1/true/oui/on`, troncature texte/titre comptée, clé absente / vide / majuscules / mal encodée ⇒ rejet, `nullptr` ; aucune allocation. Partagé avec HA-13 et WEB-06. | **PASSE** 2026-11-05 (décodeur ; le reste du composant `alerts` à écrire) |
| ALR-16 | tout `alerts` | 16 règles actives, **aucune allocation** (`operator new` piégé) | À ÉCRIRE |
| ALR-17 | idem | Débordement 32 bits des compteurs ms | À ÉCRIRE |
| ALR-18 | modèle ⇄ wizard | Aucun champ orphelin (règle des wizards) | À ÉCRIRE |

### 1.6 `air_quality` — `dev/spec-qualite-air.md` §10

Bus I²C moqué par une doublure qui rejoue des trames (y compris CRC faux, NACK,
bus bloqué).

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| AIR-01 | `Ens160::detecter()` | `PART_ID` `0x0160` ⇒ présent ; autre / NACK ⇒ absent, grandeurs invalides, pas de message répété | À ÉCRIRE |
| AIR-02 | `Ens160::lire()` | `VALIDITY_FLAG` 1, 2 ⇒ invalides + préchauffage ; 3 ⇒ défaut ; 0 ⇒ publiées | À ÉCRIRE |
| AIR-03 | idem | `NEWDAT` à 0 ⇒ pas de republication comme fraîche | À ÉCRIRE |
| AIR-04 | `Ens160::decoder()` | AQI / TVOC / eCO2, LE, extrêmes | À ÉCRIRE |
| AIR-05 | `Ens160::compenser()` | `TEMP_IN` K×64, `RH_IN` %×512, bornes, arrondi | À ÉCRIRE |
| AIR-06 | `Aht21::decoder()` | Trames réelles (`test/fixtures/`), bornes | À ÉCRIRE |
| AIR-07 | `Aht21::lire()` | CRC8 bon accepté, faux **rejeté** ; 3 rejets ⇒ défaut ; 1 bonne lecture rétablit | À ÉCRIRE |
| AIR-08 | `Aht21::initialiser()` | Non calibré au status ⇒ relance | À ÉCRIRE |
| AIR-09 | `Air::step()` | AHT21 en défaut ⇒ compensation de repli 25 °C / 50 %, drapeau, ENS160 toujours exploité | À ÉCRIRE |
| AIR-10 | `Air::step()` | Compensation écrite **avant** la lecture des données | À ÉCRIRE |
| AIR-11 | `Niveau::calculer()` | 5 paliers × 3 grandeurs pilotes, seuils exacts | À ÉCRIRE |
| AIR-12 | `Niveau::step()` | Hystérésis : oscillation ⇒ **un seul** changement | À ÉCRIRE |
| AIR-13 | `Niveau::step()` | Confirmation 59 s ⇒ rien ; 60 s ⇒ changement ; coupure ⇒ zéro | À ÉCRIRE |
| AIR-14 | `Niveau::step()` | Montée immédiate / descente freinée | À ÉCRIRE |
| AIR-15 | `valider_air()` | Seuils non monotones, couleurs mal formées ⇒ repli | À ÉCRIRE |
| AIR-16 | `Air::step()` | `offset_temperature_centi` appliqué à la mesure **et** à la compensation | À ÉCRIRE |
| AIR-17 | `construire_offset()` | Décalage du wizard ; avertissement à 2,0 °C | À ÉCRIRE |
| AIR-18 | `couleur_logo()` | Grise en préchauffage et en défaut, **jamais verte** | À ÉCRIRE |
| AIR-19 | `display::logo_air()` | `Aucun` / `ZoneIcone` / `Scene` ⇒ tampon attendu | À ÉCRIRE |
| AIR-20 | `MoteurAlertes::step()` | Source air en préchauffage / défaut ⇒ aucune levée ; `AirCapteurDefaut` lève | À ÉCRIRE |
| AIR-21 | `BusI2c` (doublure) | NACK / bus bloqué ⇒ réinitialisation, compteur, aucune attente bloquante | À ÉCRIRE |
| AIR-22 | modèle ⇄ wizard `/air` | Aucun paramètre orphelin | À ÉCRIRE |

### 1.7 `homeassistant::mqtt` — `dev/spec-home-assistant.md` §6 bis et §9

Repris de ThermoSync (ses HA-01..12 du banc MQTT), adapté aux entités de
l'horloge.

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| MQ-01 | `mqtt_ha::topic()` | `disponible`, `etat`, `config` par entité, `<prefixe>/status`, `id` MAC ; débordement refusé | À ÉCRIRE |
| MQ-02 | `mqtt_ha::decouverte()` | Chaque entité : plateforme, classe, unité, `state_class`, `value_template`, `availability`, bloc `device` ; JSON valide | À ÉCRIRE |
| MQ-03 | `mqtt_ha::entites` ⇄ `etat()` | Chaque `value_template` lit une clé publiée, chaque clé publiée est lue | À ÉCRIRE |
| MQ-04 | `mqtt_ha::etat()` | Décimales correctes ; invalide / préchauffage / absent ⇒ **`null`** | À ÉCRIRE |
| MQ-05 | `mqtt_ha::etat()` | `niveau` : 5 valeurs énumérées, `null` en préchauffage | À ÉCRIRE |
| MQ-06 | `mqtt_ha::doit_publier()` | À `periode_s` **et** sur changement de niveau, pas à chaque mesure | À ÉCRIRE |
| MQ-07 | `mqtt_ha::sur_status()` | `online` ⇒ découverte renvoyée | À ÉCRIRE |
| MQ-08 | `MqttConfig::valider()` | Tous les `MqttStatus`, repli sur les défauts | À ÉCRIRE |
| MQ-09 | modèle ⇄ `MqttConfig` | Aucun champ orphelin | À ÉCRIRE |
| MQ-10 | `mqtt_ha::desactiver()` | `offline` publié avant la déconnexion | À ÉCRIRE |

### 1.8 `settings` / web — `dev/spec-serveur-web.md` §7

| Id | Code couvert | Attendu | Statut |
|---|---|---|---|
| WEB-01 | `form_field()` | Couvert aussi par RES-10, appliqué aux 6 formulaires | À ÉCRIRE |
| WEB-02 | `appliquer_<domaine>()` | Tout-ou-rien : un champ fautif ⇒ 400, **rien** modifié | À ÉCRIRE |
| WEB-03 | idem | Champ nommé, raison en français, bornes exactes | À ÉCRIRE |
| WEB-04 | `modele_json()` | `/api/<domaine>/modele` ⇄ modèle de config : aucun paramètre orphelin, **8 domaines** | À ÉCRIRE |
| WEB-05 | `json_ecrivain` | Échappement, troncature propre avec `incomplet`, jamais de JSON invalide | À ÉCRIRE |
| WEB-06 | `decoder_notification()` | Mêmes bornes et rejets que HA-13 | À ÉCRIRE |
| WEB-07 | `usine()` | Refus sans `confirme=OUI` | À ÉCRIRE |
| WEB-08 | `motif_test()` | Chaque motif produit le tampon attendu | À ÉCRIRE |

### 1.9 Garde-fous transverses

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
| HW-RES-07 | `horloge.local` depuis PC et téléphone | Résolu | À FAIRE |
| HW-HA-01 | Liaison au Home Assistant **de la maison**, en lecture | Valeur affichée = valeur de l'interface | À FAIRE |
| HW-HA-02 | Home Assistant redémarré | Reconnexion seule, valeurs retrouvées | À FAIRE |
| HW-HA-03 | Jeton révoqué | `JetonRefuse` visible sur la page, pas de martèlement | À FAIRE |
| HW-HA-04 | Automatisation réelle qui déclenche `horloge_notification` | Notification affichée au bon rendu | À FAIRE |
| HW-ALR-01 | Sonde refroidie sous le seuil de la règle `GEL` | Alerte après la durée de confirmation, pas avant | À FAIRE |
| HW-ALR-02 | Règle `AERER` levée, puis fenêtre ouverte | Passages toutes les `rappel_s` tant que l'air est mauvais ; disparition **seule** quand le niveau retombe ; aucun bouton sur la page | À FAIRE |
| HW-SYS-01 | Fuseau changé sur `/systeme` (`Europe/Zurich` → `UTC`) | Heure décalée **sans redémarrage** ; défaut `Europe/Zurich` après réglages d'usine | À FAIRE |
| HW-ALR-03 | Une nuit complète en heures silencieuses | Aucun clignotement, alerte conservée et visible au réveil | À FAIRE |
| HW-AIR-01 | Mise sous tension, chronomètre | `--` + préchauffage ~3 min, puis valeurs ; **aucune** alerte « AERER » pendant ce temps | À FAIRE |
| HW-AIR-02 | Premier usage du capteur neuf | Drapeau 2 pendant ~1 h, signalé sur la page | À FAIRE |
| HW-AIR-03 | Solvant vaporisé à proximité | AQI, TVOC, eCO2 montent ; niveau confirmé après 60 s | À FAIRE |
| HW-AIR-04 | Pièce fermée plusieurs heures puis fenêtre ouverte | Montée puis retour, logo qui change **sans clignoter** entre deux niveaux | À FAIRE |
| HW-AIR-05 | Wizard étape 3 : dalle éteinte 10 min, puis 255 pendant 20 min | Écart chiffré ; > 2 °C ⇒ avertissement de déport | À FAIRE |
| HW-AIR-06 | Température / humidité confrontées à une référence | Après compensation : < 1 °C, < 5 % HR | À FAIRE |
| HW-AIR-07 | Capteur débranché à chaud puis rebranché | Grandeurs invalides, logo gris, `CAPTEUR AIR HS` ; bus rétabli **sans redémarrage** | À FAIRE |
| HW-AIR-08 | Lecture de `PART_ID` et des registres sur le module réel | Table de `spec-qualite-air.md` §2 confirmée ou **corrigée** (dette D11) | À FAIRE |
| HW-MQ-01 | Home Assistant **de test en Docker** + Mosquitto, liaison activée | Appareil « Horloge » et ses 10 entités découverts sans YAML ; valeurs = page `/air` | À FAIRE |
| HW-MQ-02 | Mise sous tension de l'horloge, pendant les 3 min de préchauffage | Entités d'air *inconnues* (`null`), puis valeurs ; `prechauffage` à `on` puis `off` | À FAIRE |
| HW-MQ-03 | Horloge débranchée ; puis Home Assistant redémarré | `disponible` passe `offline` (testament) ; après redémarrage de HA, entités retrouvées (découverte renvoyée) | À FAIRE |
| HW-WEB-01 | Page sur iPhone, Android, PC, thèmes clair et sombre | Lisible, utilisable au doigt | À FAIRE |
| HW-WEB-02 | OTA d'une image valide | Barre de progression sur la dalle, redémarrage sur la nouvelle image | À FAIRE |
| HW-WEB-03 | OTA d'une image **volontairement cassée** | **Rollback** automatique sur l'ancienne image | À FAIRE |
| HW-HUB-01…| *(phase 2)* hub de test en Docker, puis vraie carte | — | PHASE 2 |

---

## 3. Couverture des exigences

Chaque `EX-nn` de `dev/cadrage-fonctionnel.md` doit avoir **au moins un** test.

| Exigence | Tests |
|---|---|
| EX-01, EX-04 | AFF-10, AFF-12, HW-AFF-04 |
| EX-02 | AFF-11 |
| EX-03 | HW-SYS-01, WEB-04 (le fuseau est dans le modèle `systeme`) ; changement d'heure réel à vérifier en mars |
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
