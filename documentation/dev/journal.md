# Journal de développement — Horloge

> Récent en haut. Une entrée par modification non triviale : date, quoi,
> pourquoi, état.

---

## 2026-11-17 — Régime de DRAM : 46 Ko de statique rendus, tas minimum 5,5 → 63 Ko

**Pourquoi.** Hier soir le tas tombait à **5,5 Ko** avec la liaison Home
Assistant montée, le serveur HTTP mourait sur l'image précédente (connexions
acceptées puis réinitialisées : lwIP n'avait plus de quoi créer la
`netconn`), et une reconnexion WebSocket sur deux échouait. Nicolas a validé
le régime.

**Serveur web : un jeu de tampons partagé.** `esp_http_server` exécute les
handlers **un à la fois sur sa seule tâche** : les copies statiques par
handler (`HaSettings` × 7, `AlertSettings` × 7, corps de formulaire × 7,
instantané de recherche, bloc OTA, liste des tâches) deviennent **une**
structure `Scratch` (`text` 4 Ko partagé par les corps, le scan WiFi, les
blocs OTA et `/api/systeme/taches` ; `ha_current` / `ha_next` ;
`alerts_current` / `alerts_next` ; `search`), avec `static_assert` sur les
tailles. Aucun handler n'appelle un autre handler : pas de recouvrement.

**Magasin de réglages.** Un seul tampon de blob NVS (`s_blob`, taille du
plus gros type, `static_assert` par type) au lieu d'un par type ; `init()`
charge **directement** dans `s_current` (rien d'autre ne tourne avant son
retour) et lit les blobs Home Assistant et alertes **en place**, défauts
remis par `default_ha_settings_into` / `default_alert_settings_into` si le
blob est invalide : plus de temporaires de 5 + 2,7 + 1,9 Ko.

**Réglages ESP-IDF** (`sdkconfig.defaults`, et `sdkconfig` généré, non
suivi, corrigé à la main) : tampons WiFi RX statiques 10 → **6**, RX/TX
dynamiques 32 → **16**, fenêtre BA 6 → **4** ; **IPv6 désactivé** (réseau de
la maison en IPv4, mDNS reste) ; `LWIP_MAX_SOCKETS` aligné à **10** (la
valeur réellement générée : HTTP 2 + 4 sessions, WebSocket, MQTT, météo).

**Confirmation OTA avec échéance.** L'image n'est confirmée qu'une fois le
réseau monté (hier) ; désormais, si elle est encore `PENDING_VERIFY` au
bout de **10 min** sans réseau (`kOtaConfirmDeadlineMs`), elle redémarre et
le bootloader revient à l'image précédente. Une image déjà confirmée ne
redémarre jamais (`ota_image_pending()` lu au démarrage). Non rejoué sur
matériel (HW-WEB-03 à compléter).

**Mesures.** DRAM statique **152 128 → 105 840 octets** (`.bss` 135 520 →
89 248), image 1 302 528 → 1 267 520 octets. Sur l'horloge, après l'OTA
(10,5 s malgré les tampons WiFi réduits) : tas libre **25,5 → 79 Ko**,
minimum **5,5 → 63,3 Ko**, plus gros bloc **20 → 74 Ko** sur 2 min ;
liaison Home Assistant `running` à la **première** connexion (9,7 °C),
mDNS `horloge.local` OK, recherche d'entités OK. HW-SYS-04 ajouté au plan.

**Tests.** Host inchangés (12 660 vérifications, 96,7 %) : le serveur web et
le magasin sont des adaptateurs, vérifiés sur la carte.

---

## 2026-11-16 (suite 7) — « HOME ASSISTANT HS » trop prompt, plafond de routes HTTP, horloge muette

**Demande de Nicolas.** L'horloge affiche parfois « Home Assistant pas
connecté » (vu deux fois) : mettre un délai beaucoup plus long.

**Ce qui était.** La règle interne `DOMOTIQUE` levait `HOME ASSISTANT HS`
après **300 s** de liaison WebSocket perdue, y compris quand c'est le WiFi
lui-même qui manquait ; le client retente au plus toutes les 60 s
(`kBackoffMaxMs`), un redémarrage de Home Assistant ou une mise à jour
dépasse facilement 5 min.

**Changement.** `kHaLinkLostConfirmationS = 1800` (30 min, règle par
défaut ; modifiable sur la page Alertes, champ *confirmation*), et la source
`LiaisonHaPerdue` est **indisponible** tant que `network_up` est faux : une
coupure WiFi relève de `ReseauPerdu` / `BailAbsent`, et le compte des 30 min
repart de zéro au retour du réseau. Test ALR-19 (patience, retombée seule,
reconnexion qui remet à zéro, coupure WiFi), ALR-05 complété. ⚠️ Les règles
vivent en NVS : sur l'horloge installée, la règle 3 garde 300 s tant qu'on
ne la repousse pas (`POST /api/alertes/regle/3`, à faire dès que l'horloge
répond).

**Plafond de routes.** `start_http()` enregistrait **62** routes avec
`max_uri_handlers = 59` : les trois dernières (sondes de portail captif)
manquaient en silence et `start()` rendait `HttpError`. Le plafond suit
désormais `sizeof(kRoutes)`, et une route refusée est tracée.

**Horloge : tas épuisé.** Depuis l'OTA de l'image « recherche d'entités »
(16:21), l'horloge répondait au ping mais **toute connexion HTTP était
acceptée puis réinitialisée**, même après deux coupures d'alimentation.
Cause trouvée par la fenêtre de démarrage : un guetteur à 0,3 s a obtenu
`/api/info` à **3 s** de fonctionnement (tas libre 25 Ko, **minimum 10,8 Ko**,
plus gros bloc 12,8 Ko) et a enchaîné l'OTA de l'image allégée (11 s, 1,3 Mo).
Une fois WiFi, liaison Home Assistant et MQTT montés, le tas ne suffisait
plus à lwIP pour accepter une connexion (netconn refusé ⇒ RST). Sur la
nouvelle image (152 Ko de DRAM statique, −6 Ko) : tas libre ~24,6 Ko en
régime, **minimum 5,5 Ko** pendant la deuxième connexion WebSocket (la
première est tombée : `connexions 2, coupures 2`). C'est très
probablement aussi l'origine des « Home Assistant pas connecté » vus par
Nicolas : une reconnexion qui n'a pas la mémoire pour aboutir.

Pistes relevées (`nm --size-sort`) : le serveur web garde **une copie
statique par handler** des réglages (`HaSettings` 2,7 Ko × 7,
`AlertSettings` 1,9 Ko × 7, `Settings` 5 Ko × 3, instantané HA 1,5 Ko × 4)
alors que les handlers tournent sur le seul fil du serveur : un tampon
partagé par type rendrait ~25 Ko ; `s_json` 8 Ko ; tampons WiFi
(`WIFI_STATIC_RX_BUFFER_NUM`) et fenêtres TCP à revoir. À faire en priorité.

**Remis en place par l'API.** `mode=Auto`, `lum_manuelle=96`,
`periode_cycle_min=15`, règle 3 `DOMOTIQUE` à 1800 s (vérifié en relecture).
Liaison Home Assistant `running`, valeur « Extérieur » 9,7 °C.
**HW-HA-05 (API)** : `POST /api/ha/recherche q=temp` sur le vrai Home
Assistant ⇒ `pret` en moins de 3 s, 12 entités (id, nom, état, unité) ;
la page et le clic restent à essayer par Nicolas. `/api/couleurs` répond.
Confirmation OTA différée jusqu'au réseau : `connecte` vrai à 13 s,
`emplacement ota_1`, image confirmée (pas de rollback).

**Tests.** 12 660 vérifications, couverture 96,7 % (5 330 lignes). Pas
encore commité (avec le figeage du climat et la recherche d'entités).

---

## 2026-11-16 (suite 6) — Recherche d'entités Home Assistant, règles d'alerte par nom

**Demande de Nicolas.** Trouver les entités sans taper leur identifiant
(barre de recherche), et choisir la valeur d'une règle d'alerte par son nom
plutôt que par un numéro.

**Comment.** La recherche se fait par Home Assistant lui-même : le client
WebSocket envoie un `render_template` dont le gabarit Jinja parcourt
`states` côté serveur et renvoie au plus 20 entités (id, nom, état, unité)
contenant le mot ; une réponse suffit, le client se désabonne. Rien ne
transite par le navigateur avec le jeton, rien de gros n'est gardé sur
l'horloge (réponse 2,5 Ko). API `POST`/`GET /api/ha/recherche`, hooks
`ha_search` / `ha_search_get`, `clock_homeassistant::search()` /
`search_snapshot()`. Requête épurée (lettres, chiffres, espace, `_ . -`,
32 max) : elle entre dans une chaîne Jinja, rien d'autre n'y entre.
Tampons : `kMessageMax` 2 048 → 3 072, `kOutgoingMax` 512 → 768 (le test du
réassembleur devient relatif à la constante).

**Page.** Widget « Chercher une entité » dans chaque carte de valeur et dans
l'étape 3 du wizard Home Assistant ; un clic remplit entité, nom, unité,
mode, actif. Règles d'alerte : le champ « Valeur n° » devient une liste
« Valeur n — nom » (noms lus sur `/api/ha`), idem dans le wizard d'alerte.

**Tests.** HA-16 (dialogue complet simulé), WEB-04 (littéraux de la page),
réassembleur ; 12 620 vérifications, couverture 96,7 % sur 5 328 lignes.

**Reste.** HW-HA-05 sur le vrai Home Assistant (Nicolas) ; la page tourne
sur l'horloge mais la liaison Home Assistant n'y est pas configurée.

---

## 2026-11-16 (suite 5) — Luminosité : câblage (b) et auto-éclairage sur l'horloge installée

**Symptôme (Nicolas).** Luminosité « toujours à fond ou très variable », il
soupçonne la dalle d'influencer le capteur.

**Relevé.** 40 s en auto : la mesure brute saute entre ~1260 et ~2060 au gré
du contenu de la dalle (941 → 1385 mA), le niveau erre de 89 à 152. Dalle
éteinte : **2630** ; dalle à 255 : **1071**. La mesure **baisse** quand la
lumière monte : **câblage (b)**, pas (a) comme noté le 2026-11-05 (fiche
corrigée), et la dalle éclaire fortement la LDR (−1559 comptes à 255). Avec
la courbe croissante par défaut, c'était aussi la vraie cause du « sombre le
jour, clair la nuit » du matin (la correction des butées restait juste, mais
n'était pas la cause principale).

**Correction (réglages, pas de code).** `POST /api/luminosite` : courbe
décroissante `600:255,1500:160,2600:60,3800:8`, `compensation_counts =
-1559`. Résultat : mesure corrigée stable (2600–2690) alors que la brute
varie de 300, niveau posé à 56–61, plus de pompage (HW-LUM-02 PASSE,
HW-LUM-01/03 en cours). À faire par Nicolas : vérifier le sens en couvrant
la LDR (la dalle doit **s'assombrir**), jouer le wizard une nuit pour une
courbe mesurée, et si possible abriter la LDR de la dalle.

**Chauffe cyclique, suite.** Observation détaillée d'un réveil : le capteur
sort de veille en « normal » mais ne lève **jamais** NEWDAT (statut `0x01`),
4 min durant, deux cycles de suite ; le passage IDLE → STANDARD seul ne
relance pas l'algorithme. Réveil désormais par **reset logiciel** (`OPMODE`
0xF0, 20 ms) puis STANDARD : préchauffage de 3 min attendu, dans le plafond
de 4 min. Test AIR-24 compte un reset par réveil (aucun au premier).
Bonne nouvelle au passage : 4 min de chauffe n'ont fait monter l'AHT21 que
de quelques centièmes (figeage vérifié : valeur constante, `climat_age_s`
qui monte).

**Suite du même jour.** Le reset seul n'a marché qu'une fois sur deux
(réveil suivant muet, `0x01`). Réveil désormais par la **séquence complète**
des pilotes de référence (reset, idle, effacement GPR, standard, 20 ms
entre chaque), au démarrage aussi en mode cyclique : deux réveils de suite
observés valides (préchauffage `0x87` ~3 min, puis `0x83`, AQI 3 puis 2).
Trouvé en passant : juste après le reset le capteur peut annoncer une
« nouvelle donnée » sur des registres effacés (AQI 0), ce qui effaçait la
dernière mesure ; en mode cyclique une donnée hors 1..5 ne remplace plus la
précédente. AIR-24 étendu (un reset par réveil, donnée effacée ignorée).

---

## 2026-11-16 (suite 3) — Chauffe cyclique de l'ENS160

**Pourquoi.** Nicolas trouve le module tiède et l'AHT21 lit 26 °C : ce n'est
ni un défaut ni la fréquence des lectures I²C, c'est la plaque chauffante de
l'ENS160, allumée en permanence en mode standard. Il a choisi de la couper
entre deux mesures (conséquences expliquées : rafraîchissement de l'air une
fois par cycle, préchauffage à chaque réveil).

**Quoi.** `SensorConfig::heater_cycle` / `cycle_period_ms` ; dans la machine
d'état : réveil (`SetMode`, `heater_on`), capture d'une lecture valide ⇒
`SetIdle` (`OPMODE` idle), AHT21 lu pendant la veille sans toucher l'ENS160,
réveil à la période, préchauffage du réveil qui **garde** la dernière mesure,
plafond `kCycleWarmupMaxMs` (4 min), retour au mode continu ⇒ chauffe
immédiate. Réglages `cycle_chauffe` (défaut non) et `periode_cycle_min`
(5–60, défaut **15** : à 5 min la plaque chauffe encore 60 % du temps),
blob NVS `air` **v2** (les réglages air reviennent aux défauts une fois),
`/api/etat` → `air.chauffe` / `air.cycle`, tuile « Chauffe ENS160 » et
mention « dernière mesure » sur la page `/air`.

**Tests.** AIR-24 (cycle complet simulé : 10 min de machine d'état à 10 ms),
AIR-22 à 18 champs, formulaire air (bornes 5–60, `cycle_chauffe=2` refusé).
Mon doublon d'identifiant AIR-21 d'hier (textes de la dalle) renommé AIR-23.
Couverture 96,8 % sur 5 210 lignes.

**Sur la carte (HW-AIR-07).** Option activée à 15 min. Après un redémarrage
logiciel le capteur se déclare valide tout de suite : capture, `OPMODE` idle
en 30 s, puis la température descend de **26,6 à 23,7 °C en 4 min** de
veille, AQI conservé. La plaque pèse donc ~3 °C sur ce montage.

**Figeage pendant la chauffe (demande de Nicolas, suite 4).** Pendant les
minutes de chauffe du réveil, la température et l'humidité publiées restent
celles de la dernière lecture plaque froide (`climate_cold`, `climate_ms`,
`climat_plaque_froide` / `climat_age_s` sur `/api/etat`, phrase sur la page
`/air`) ; l'ENS160 reçoit la lecture vive pour sa compensation ; un AHT21 en
échec pendant la chauffe ne fait pas tomber la valeur figée ; en veille la
lecture vive reprend, en mode continu rien ne change. Test AIR-25.

**Reste.** Observer le figeage sur un réveil réel (réveil suivant à 15 min).

---

## 2026-11-16 (suite 2) — Domaine `couleurs`, éditeur de scènes réparé

**Couleurs personnalisables** (maquette approuvée par Nicolas, puis firmware) :

- `settings::ColorSettings` : 9 rôles (chiffres, deux-points, barre des
  secondes, jour, date, icône et texte des valeurs, grandeur de l'air, heure
  de nuit), défauts = anciennes constantes de `app_main`, qui disparaissent.
  `kColorFields`, `apply_color_form` (tout-ou-rien, `#RRGGBB`),
  `color_to_text`, `color_field`, `color_model_json` (champs `couleur` +
  **palettes** Classique / Ambre / Glace / Forêt / Braise, constantes du
  firmware), `color_json`. Blob NVS `colors` v1, `clock_settings::colors()` /
  `save_colors()`.
- Rendu : `app_main` lit `cfg.colors` à chaque trame ; un **essai** de 10 s
  (`POST /api/couleurs/essai`, hook `colors_try`) remplace les couleurs sans
  rien enregistrer, puis les enregistrées reviennent. La scène `Nuit` utilise
  `nuit_heure` (`#303030` par défaut) au lieu d'un facteur d'atténuation.
- Page : onglet **Couleurs** (`/couleurs`) avec aperçu simulé 8×32 en direct
  (polices 5×7 / 3×5 et icônes réduites, en JS), palettes lues du modèle,
  sélecteur de couleur natif + champ `#RRGGBB` pour tout champ `couleur`
  ayant un défaut (les règles d'alerte gardent le champ texte : vide =
  couleur de la priorité), boutons « Essayer 10 s » et « Valeurs d'usine »,
  **wizard** en 4 étapes (palette → heure et date → valeurs → essai et
  enregistrement). `chargerDomaine()` rend maintenant sa promesse.
- Tests : COL-01, COL-02, WEB-04 étendu (domaine `couleurs`, palettes lues du
  modèle, aucun libellé de palette recopié). 5 142 lignes couvertes à 96,8 %.

**Éditeur de scènes réparé** (signalé par Nicolas : impossible de choisir une
scène). `editeurScenes()` lisait `modele.types_scene` dans le modèle **du
champ**, alors que le firmware publiait la liste à la racine du modèle :
listes déroulantes vides depuis le début, jamais vues à l'œil (vérifications
faites par l'API). La liste voyage désormais dans le champ `scenes`
(`display_model_json`), test WEB-04 de non-régression (position de
`types_scene` dans le champ, lecture `modele.types_scene` dans la page).

**Au passage.** Le test lisait la page dans 64 Ko : la page (78 Ko bruts,
20 Ko gzippés) dépassait et les littéraux du wizard de correspondance
n'étaient plus trouvés ; tampon de 128 Ko avec garde anti-troncature.

**Sur la carte.** OTA, `/api/couleurs/modele` (9 champs, 5 palettes), essai
accepté sans toucher l'enregistré, refus nommé, palette Ambre enregistrée et
relue, défauts restaurés ; page `/couleurs` vue dans le navigateur
(HW-COL-01). Couleurs à l'œil sur la dalle : Nicolas.

**Reste.** Rien côté code ; vérification à l'œil.

---

## 2026-11-16 (suite) — Scènes `Temperature` et `Humidite` du capteur local

**Quoi.** Deux nouveaux types de scène, lus directement sur l'AHT21 de
l'horloge (pas par le détour Home Assistant) : icône thermomètre + `27.6°`
(une décimale, après le décalage d'auto-échauffement), icône goutte + `46%`.
`SceneAvailability::climate` (capteur présent et climat valide) : la scène
est **sautée** tant que la mesure n'est pas valide, quittée si elle le
devient en cours (règle d'honnêteté : jamais `--` pendant 6 s). Texte formaté
par deux fonctions pures `air_quality::temperature_text()` /
`humidity_text()` (AIR-21). Modèle `types_scene`, noms `Temperature` /
`Humidite`, libellés de la page ; l'éditeur de scènes et le wizard
d'affichage les listent automatiquement depuis le modèle. Test WEB-04 étendu :
chaque type de scène du firmware doit avoir un libellé sur la page.

**Tests.** AFF-12 (ordonnanceur), AIR-21 (textes), WEB-04 (libellés),
aller-retour du texte de scènes dans `test_alert_form` ; tout `PASSE`,
couverture 96,8 % sur 5 050 lignes.

**Attention.** Le capteur de l'horloge installée lit ~27,6 °C : c'est
l'auto-échauffement (spec qualité d'air §8), le décalage est encore à 0.
Avant d'activer la scène, mesurer l'écart avec un thermomètre de référence
(wizard Qualité d'air) ou déporter le capteur.

**Sur la carte.** OTA (dalle au minimum pendant l'envoi), `types_scene`
liste les deux nouveaux types, rotation `Heure → Date → Temperature →
Humidite` observée sur `/api/etat` (HW-AFF-10) ; les deux scènes sont
laissées actives sur l'horloge (6 s chacune).

**Couleurs personnalisables.** Maquette simulée
`documentation/maquette-web/couleurs.html` (aperçu 8×32 en direct, 9 rôles
de couleur par scène, 5 palettes, essai 10 s, assistant en 4 étapes) soumise
à Nicolas avant toute implémentation.

**Reste.** Rendu des scènes à l'œil ; décalage de température ; couleurs
après approbation de la maquette.

---

## 2026-11-16 — Luminosité « inversée » : les butées de l'ADC ne sont plus un défaut capteur

**Symptôme (Nicolas).** Plus lumineuse la nuit que le jour.

**Cause.** `brightness::Controller::step()` traitait une mesure brute à 0 ou à
4095 comme un capteur en défaut (ancienne LUM-11) et appliquait le repli 64.
Or, avec le diviseur de 10 kΩ et le câblage (a) de la carte, l'ADC est
**saturé à 4095 en plein jour** et **à 0 dans une pièce noire** : la dalle
passait donc à 64 le jour (au lieu du plafond 255) et à 64 la nuit noire (au
lieu du plancher 8), et n'était « juste » que le soir sous la lampe (~2000
comptes ⇒ ~160). Vu de la pièce : inversé.

**Correction.** Une butée est une **saturation**, pas un défaut : la mesure
est valide et les extrémités plates de la courbe donnent plafond / plancher,
dans les deux câblages. Seule une lecture ADC en échec (`valid == false`)
compte pour le défaut capteur (LUM-10 inchangé). Une LDR débranchée donne le
plancher de nuit et `brut = 0` sur la page : limite assumée, documentée
(`spec-luminosite.md` §1).

**Tests.** LUM-11 réécrit (échouait sur l'ancien code : 64 attendu 8 / 255,
dans les deux sens), 12 180 vérifications, 0 échec. Fiche carte, plan de test
(HW-LUM-01 à rejouer : `brut` 4095 en plein jour ⇒ plafond, jamais `defaut`),
plan final, todo.

**Sur la carte (plus tard le même jour, horloge installée, OTA seulement).**

- Réglages de luminosité lus avant la mise à jour : **ceux par défaut**
  (courbe croissante, câblage (a)) — le sens de la courbe n'était pas en
  cause, seule la règle « butée = défaut » l'était.
- **Baisse de tension à l'emplacement d'installation.** Le premier envoi OTA
  (dalle en auto, niveau ~176, ~1,6 A estimés) a fait redémarrer la carte en
  `baisse_tension` ; remise en manuel 8, l'OTA passe (`ota_1`, redémarrage
  `volontaire`). Retour en auto (niveau 186–204) ⇒ **boucle de redémarrages
  `baisse_tension`** toutes les 3–4 s ; en manuel 24 (~270 mA) la carte tient.
  Mitigation appliquée dans les réglages (NVS, pas dans le code) :
  **`budget_ma` 2000 → 800** ; essai de 150 s en auto (niveau 80–99,
  460–700 mA) sans redémarrage. Le plafond de 800 mA n'a pas encore été
  exercé en pleine lumière (la pièce s'assombrissait). L'alimentation ou le
  câble 5 V de l'installation est à revoir : fiche carte §3, `todo.md`.
- Une image servant à ce diagnostic a été ajoutée dans la foulée : la page et
  `/api/etat` donnent maintenant le **courant appliqué** (`applique_ma`, après
  écrêtage) à côté de l'estimation (`estime_ma`, avant) — la tuile affichait
  « 2328 mA (écrêté) » alors que 800 étaient envoyés. Log `%lu mA (applied
  %lu)`. Installée par OTA sur `ota_0` après retour au manuel 24 le temps de
  l'envoi.

**Reste.** HW-LUM-01 en plein jour (`brut` 4095 ⇒ plafond, jamais `defaut`) ;
alimentation de l'installation (le budget de 800 mA est un pansement) ;
rejouer l'écrêtage à 800 mA en pleine lumière.

---

## 2026-11-06 — Diagnostics, page gzippée, rollback OTA et chien de garde vérifiés, charge mesurée

**Quoi.**

- `/api/info` : cause du dernier redémarrage (`esp_reset_reason`, noms
  français comme ThermoSync), tas libre / minimum / plus grand bloc,
  fréquence CPU, emplacement OTA courant, taille de la page ;
  `GET /api/systeme/taches` : statistiques d'exécution FreeRTOS
  (`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`) et liste des tâches ; carte
  **Diagnostics** de l'onglet Système.
- Page `app.html` **servie gzippée** : compressée au build par le Python
  d'ESP-IDF (`add_custom_command` + `target_add_binary_data`), envoyée avec
  `Content-Encoding: gzip` — 67 Ko bruts, **17 Ko** sur le fil ; le budget de
  60 Ko (EX-52) est vérifié par `run_host_tests.sh` à chaque exécution.
- Deux **interrupteurs de build de test**, jamais pour une version :
  `-DCLOCK_OTA_NEVER_CONFIRM=1` (l'image ne se confirme pas ⇒ rollback au
  redémarrage suivant, HW-WEB-03) et `-DCLOCK_TEST_LOCKUP_S=<s>` (la tâche
  `display` se bloque ⇒ chien de garde, HW-SYS-02) ; `PROJECT_VER`
  surchargeable pour les distinguer sur `/api/info`.

**Résultats sur la carte.**

- HW-SYS-01 : fuseau `UTC` appliqué à chaud (23:58 → 22:58), retour idem,
  aucun redémarrage.
- HW-WEB-03 : image **corrompue** (64 octets inversés au milieu) ⇒
  `400 Image invalide`, l'horloge continue ; **PASSE** 2026-11-06 : image de test `0.1.0-norollback` (ne se confirme pas) ⇒ démarre sur `ota_1`, 45 s plus tard toujours là ; `POST /api/systeme/redemarrer` ⇒ le bootloader **revient** à `0.1.0` sur `ota_0` (rollback), réglages conservés
- HW-SYS-02 (chien de garde) : **PASSE** 2026-11-06 : image `0.1.0-wdt`, blocage de `display` à 120 s ⇒ `Task watchdog got triggered` (IDLE1 affamé), panique, redémarrage ; `/api/info` : `redemarrage: chien_de_garde_tache`, uptime 33 s ; image de production restaurée par OTA
- HW-SYS-03 (charge) : **PASSE** 2026-11-06 : `display` 3 %, `wifi` 4 %, `httpd`/`mqtt`/`ha`/`air` < 1 % chacun, IDLE0 93 % / IDLE1 95 % à 160 MHz, dalle allumée, WiFi, MQTT et HA actifs ; 240 MHz inutile
- Page gzippée reçue et décodée par `curl --compressed` (11 onglets
  trouvés), `page_octets` 17 158.

**Reste.** La campagne du plan final §4 (ce qui demande Nicolas ou du temps).

---

## 2026-11-05 (suite 15) — Zone d'icône testable (AFF-16) et plan de test final

**Quoi.**

- `display/icon_zone` : le choix de la zone d'icône devient une fonction pure
  (`choose_icon_zone`) : **alerte tenue > logo air > rien**, et la marque `!`
  (10 pixels) a son dessin et son test. Décision : pas d'alternance de 3 s
  (une chose à la fois, la plus importante), la spec affichage §4 est
  amendée par le plan de test.
- `dev/plan-de-test-final.md` : vue consolidée de toute la phase 1 — comment
  tout lancer (host, couverture, build, flash, OTA, série, broker de banc,
  canal d'alerte), état par spec (130 tests host, 43 vérifications
  matérielles), garde-fous du projet ↔ tests, **campagne matérielle** à jouer
  dans l'ordre (établi, vrai Home Assistant, durée), trous connus (watchdog,
  charge CPU, heure d'été réelle), critères de sortie de phase 1.
- `test-plan-hw.md` (en-tête à jour, AFF-16 `PASSE`), `CLAUDE.md` (état du
  dépôt : phase 1 implémentée), `cadrage-fonctionnel.md` (statut, étapes 1 à
  10 faites), `README.md` (statut, layout), `todo.md`.

**Résultats.** 12 175 vérifications, 0 échec, couverture 96,8 % (5 022
lignes). Image flashée par OTA, démarrage propre, scène `Etat` à la
connexion, aucune alerte levée (air redescendu à AQI 3), MQTT désactivé.

**Reste.** La campagne du §4 du plan final ; pousser le dépôt quand Nicolas
le demande ; licence.

---

## 2026-11-05 (suite 14) — Moteur de règles d'alerte, file de notifications, heures silencieuses, page `/alertes`

**Quoi.** Étape 10 de la phase 1 (`spec-alertes.md`), composant pur `alerts` :

- `queue.hpp/.cpp` : **file de 16** dédoublonnée par clé (remplacement sans
  changer le rang, expiration repoussée), éviction de la plus basse priorité
  la plus ancienne **seulement** si strictement inférieure, sinon rejet
  compté ; tenues (règle levée, externe `persistante`) rejouées toutes les
  `rappel_s`, ponctuelles jouées une fois ; **heures silencieuses** (fenêtre
  franchissant minuit, jamais silencieux sans heure sûre) qui suspendent les
  passages sans rien perdre, P3 `outrepasse_silence` exempte ; rendu par
  priorité (P0 attend la scène, P2 clignote 1 Hz, P3 2 Hz + plancher 64).
- `rules.hpp/.cpp` : **17 sources** (8 valeurs HA, heure, liaison, jeton,
  réseau, bail, donnée périmée, LDR, écrêtage, 5 grandeurs d'air, capteur
  en défaut, préchauffage, compensation), 9 comparaisons, hystérésis selon
  le sens, confirmation remise à zéro à la moindre coupure, `Unavailable`
  dès que la source ne vaut rien (liaison désactivée ⇒ `LiaisonHaPerdue`
  indisponible, pas « levée »), gabarit `{n}` `{v}` `{u}`, synchronisation
  avec la file : la règle **seule** pousse et retire sa notification
  `regle_<i>` (un `clear` extérieur est annulé au pas suivant). Règles par
  défaut de la spec (8 internes actives, GEL / CANICULE / CO2 / HUMIDITE
  prêtes mais inactives).
- `settings` : domaine **alertes** (silence + 16 règles par
  `POST /api/alertes/regle/<i>`), seuil et hystérésis saisis en **décimal
  de l'unité de la source** (refusés s'ils ont plus de décimales qu'elle,
  remis à zéro quand la source change), couleur `#RRGGBB` ou vide, blob NVS
  `alerts` v1. Tampon JSON porté à 8 Ko et **partagé** entre tous les GET
  (13 × 4 Ko de statiques en moins).
- `app_main` : file + moteur sous un **mutex** (portes HTTP et WebSocket),
  pas de 1 Hz avec les entrées réelles, passage suivant choisi par la file
  (priorité, âge, silence), rendu : couleur, clignotement, défilement ou
  bandeau avec icône, plancher relevé ; **`!` dans la zone d'icône** tant
  qu'une alerte est tenue (prend le pas sur le logo air) ; `/api/etat.alertes`
  (règles avec état et valeur, file, silence, rejets).
- Page : onglet **Alertes** (file **listée, sans bouton**, règles en direct,
  silence, 16 cartes depuis `modele.regle`, widgets `decimal` et `couleur`),
  **wizard en 5 étapes** (source avec valeur actuelle → condition avec
  aperçu « se lèverait / ne se lèverait pas » → stabilité → rendu avec
  aperçu du texte → récapitulatif en français et choix de l'emplacement).
  Le tableau de bord compte les alertes en cours.

**Résultats.** Tests host : **12 100+ vérifications, 0 échec, couverture
96,7 %** (5 012 lignes ; `rules.cpp` 95,3 %, `queue.cpp` 98,9 %). Image
1 334 Ko (32 % libre). Sur la carte (OTA) : à 23 h le silence est actif,
`AERER` en `confirmation` à AQI 4 puis `surveille` à AQI 3 ; silence coupé
le temps du banc : ponctuelle P1 ⇒ scène `Alerte` puis `Heure` ; tenue P2
⇒ passage puis `tenues:true` jusqu'à `effacer` par sa source ; règle
`CO2 >= 900` posée par l'API ⇒ `levee`, `CO2 906 ppm` tenu ; `seuil=0.55`
refusé par son nom ; réglages remis (règle 12 inactive, silence actif).

**Reste.** HW-ALR-01 (sonde refroidie), HW-ALR-03 (une nuit), HW-AIR-01
(aucun `AERER` au préchauffage, logique couverte par AIR-20), plan de test
final consolidé.

---

## 2026-11-05 (suite 13) — Publication MQTT vers Home Assistant : découverte, état `null`, page `/mqtt`, broker de banc

**Quoi.** Étape 9 bis de la phase 1 (`spec-home-assistant.md` §6 bis) :

- `homeassistant/mqtt.hpp/.cpp` (pur, repris de ThermoSync
  `domotique/home_assistant.hpp` + `mqtt_config.hpp`, traduit) :
  `MqttConfig` + `validate()` (9 statuts, raisons pour la page),
  `DeviceIdentity` (MAC, version, carte, URL locale), topics
  `horloge/<id>/disponible` et `/etat`, **10 entités** générées depuis une
  table (température, humidité, eCO2, TVOC, AQI, niveau `enum`, capteur en
  défaut, préchauffage, lumière brute, heure sûre), **un seul** état JSON où
  tout ce qui est invalide (préchauffage, première heure, défaut, absent,
  capteur désactivé, LDR, CRC) est **`null`** — un niveau confirmé mais
  dont l'air est invalide est `null` aussi. `MqttPublisher` : machine de
  décision (connect / online / découverte / état / offline / disconnect)
  avec période, publication immédiate sur changement de niveau, renvoi sur
  `<prefixe>/status` = `online` ou sur bouton, `offline` **avant** tout arrêt.
- `settings` : domaine **mqtt** (8 champs, mot de passe écrit seulement,
  `effacer_mdp`, préfixe vide = défaut), modèle `/api/mqtt/modele`, blob NVS
  `mqtt` v1 dans `clk_cfg`.
- `clock_homeassistant/mqtt_link` : tâche `mqtt` (cœur 0, 250 ms, 4 Ko) sur
  `esp-mqtt` — testament `offline` retenu QoS 1, keepalive 30 s, reconnexion
  par le client, découverte QoS 1 retenue, état QoS 0 retenu, abonnement
  unique à `<prefixe>/status`. `app_main` fournit l'état (air, LDR, heure)
  et `/api/etat.mqtt` ; hook `mqtt_discovery` pour `POST /api/mqtt/decouverte`.
- Page : onglet **MQTT** (tuiles, bouton « renvoyer la découverte »,
  formulaires broker / avancés) et **wizard** en 3 étapes (broker →
  utilisateur et mot de passe avec test de connexion qui suit
  `/api/etat.mqtt.etat` → découverte).
- Banc : `test/bench/mini_broker.py`, broker MQTT 3.1.1 minimal (CONNACK,
  SUBACK, PUBACK, PINGRESP, journal, injection de messages) pour vérifier
  l'horloge **sans toucher au Home Assistant de la maison**.

**Résultats.** Tests host : **11 395 vérifications, 0 échec, couverture
96,7 %** (4 121 lignes ; `mqtt.cpp` 97,4 %). Image 1 302 Ko (34 % libre). Sur
la carte, OTA puis broker de banc sur le PC : `CONNECT` avec utilisateur,
testament, abonnement `homeassistant/status`, `online`, 10 découvertes (JSON
valides, relus par Python), état toutes les 10 s avec les vraies valeurs
(28,5 °C, eCO2 1000 ppm, TVOC 992 ppb, niveau `mauvais` — la pièce était à
aérer), `status` = `online` injecté ⇒ tout renvoyé, bouton idem,
désactivation ⇒ `offline` retenu **puis** `DISCONNECT` propre,
`/api/etat.mqtt` cohérent (1 connexion, 1 coupure, 3 découvertes, 5 états).

**Reste.** HW-MQ-01/03 sur le vrai Home Assistant + Mosquitto (Nicolas),
HW-MQ-02 (préchauffage `null`) au prochain démarrage à froid. Suite :
moteur de règles d'alerte (étape 10), plan de test final.

---

## 2026-11-05 (suite 12) — Home Assistant : lecteur JSON, client WebSocket pur, liaison, page `/ha`, scènes Valeur

**Quoi.** Étape 8 de la phase 1 :

- `components/homeassistant/` (pur) : `json_reader` (repris de
  `energie/json_path` de ThermoSync, traduit, profondeur bornée à 16, chaînes
  désechappées), `values` (configuration d'une valeur suivie, gabarit
  `states()` / `state_attr()` / expert échappé JSON, décodage `Nombre` en
  virgule fixe, `Booleen` avec alias, `Texte` ; `unknown` / `unavailable` /
  `none` ⇒ **invalide**), `reassembler` (2 Ko statiques, trame trop longue
  **abandonnée**, jamais tronquée), **`WsClient`** (auth, `render_template`
  avec `report_errors`, `subscribe_events(horloge_notification)` décodé par
  le **même** décodeur que `POST /api/notification`, ping/pong, attente
  5 → 60 s, jeton refusé 5 min, péremption appliquée **seulement liaison
  tombée** — dette D6 tranchée), `weather` (Open-Meteo, fixture réelle).
- `settings` : domaine **ha** (liaison, 8 valeurs par `POST /api/ha/valeur/<i>`,
  météo), jeton en **écriture seule** (`jeton_defini` sur l'API, vide = gardé,
  `effacer_jeton`), types de modèle `secret` et `coordonnee`.
- `clock_homeassistant` : tâche `ha`, `esp_websocket_client` (`^1.2.3`,
  reconnexion automatique **désactivée** : c'est le client pur qui décide),
  `wss` par le paquet de certificats, météo par `esp_http_client`, notifications
  remises à `app_main` comme celles du web.
- `display/value_icons` : 7 icônes 8×8 ; `app_main` : scènes **Valeur**
  (icône + valeur, `--` si invalide), **DeuxLignes** (valeurs 1 et 2 en 3×4),
  **Defilement** (toutes les valeurs valides, une passe), **Nuit** (heure
  atténuée fixe) ; disponibilité par valeur ⇒ une scène sans donnée est
  sautée ; `/api/etat.ha` et `.meteo`.
- Page : onglet **Home Assistant** (liaison, statistiques, valeurs vivantes,
  météo), **wizard** en 4 étapes (adresse/port → jeton avec test de connexion
  qui suit l'état `running` / `token_rejected` / injoignable → première
  valeur → notifications), 8 cartes de valeur construites depuis
  `modele.valeur`, météo de repli, réglages avancés.

**Défaut trouvé et corrigé.** Avec le domaine `ha`, `settings::Settings`
pèse ~4 Ko : `clock_settings::get()` par valeur et `default_settings()` dans
`init()` faisaient **déborder la pile de la tâche `main`** (3,5 Ko) avant tout
journal — boucle de redémarrage, image rejetée par le rollback (ce qui est le
comportement attendu, et une bonne nouvelle pour HW-WEB-03). Correctifs :
accès **par domaine** (`display()`, `brightness()`, `system()`, `air()`,
`ha(out&)`, `get(out&)`), défauts construits **en place**, snapshots copiés
dans des statiques, pile de `main` à 6 Ko (`sdkconfig`, pas seulement les
defaults : le fichier généré garde l'ancienne valeur).

**Résultats.** Tests host : **10 761 vérifications, 0 échec, couverture
96,7 %** (3 613 lignes). Image 1 262 Ko (36 % libre : le paquet de certificats
et le client WebSocket pèsent ~250 Ko). Sur la carte (flash série après la
boucle) : démarrage propre, liaison `disabled` par défaut, hôte fictif ⇒
`connecting` → `waiting` avec coupures comptées, valeur enregistrée, jeton
jamais relu, désactivation à chaud. **ENS160 sorti de sa première heure** :
AQI 3, eCO2 764 ppm, TVOC 261 ppb, 27,9 °C (auto-échauffement visible : le
wizard de décalage a son utilité), niveau `Moyen`.

**Reste** (phase 1) : publication **MQTT** (étape 9 bis), **moteur de règles**
d'alerte (étape 10), plan de test final consolidé, liaison avec le vrai Home
Assistant (jeton de Nicolas).

---

## 2026-11-05 (suite 11) — Qualité de l'air : ENS160 + AHT21 sur I²C, niveaux, logo, page `/air`

**Quoi.** Étape 9 de la phase 1 (« oublie pas la partie I²C ») :

- `components/air_quality/` (pur) : `I2cBus` injecté (`write`, `read`,
  `write_read`, `recover`) ; `aht21` (CRC-8 poly 0x31, décodage en centi-°C et
  dixièmes de %, statuts `Busy` / `CrcError` / `NotCalibrated`) ; `ens160`
  (registres, `PART_ID`, décodage de `DEVICE_STATUS` et du bloc AQI / TVOC /
  eCO2, encodage K×64 / %×512 écrêté) ; **`AirSensor`**, machine d'état **non
  bloquante** (une transaction par `step()`, délais par horodatage) : sonde →
  mode standard → vérification AHT21 → cycle de 1 s (déclenchement, lecture,
  compensation écrite **avant** la lecture des données, status, données si
  valides et fraîches) ; absence (nouvelle sonde / 60 s), défaut après 3
  échecs consécutifs puis `recover()`, AHT21 absent ou en défaut ⇒ repli 25 °C
  / 50 % drapeauté ; `levels` (`LevelConfig` validée, `raw_level`,
  **`LevelTracker`** avec confirmation et hystérésis asymétrique, `Unknown`
  immédiat sur lecture invalide, couleurs avec gris pour l'inconnu,
  `build_offset`).
- `display/air_logo` : glyphes 6×6 et 8×8 ; `SceneKind::Air` (ajouté **en fin
  d'énumération** pour ne pas casser les blobs NVS) + `SceneAvailability.air`.
- `settings` : domaine **air** (`AirSettings` : actif, adresse, `LevelConfig`,
  logo, décalage), formulaire tout-ou-rien, modèle JSON avec le type
  `temperature` (centi-°C stockés, °C affichés), blob NVS `air`.
- `clock_hal/i2c_bus` : `i2c_master` d'ESP-IDF 5.3, 100 kHz, pull-ups
  internes, délai 50 ms par transaction, **récupération** d'un bus bloqué (bus
  détruit, SCL cadencé 9 fois en GPIO, STOP, bus recréé).
- `app_main` : tâche `air` (cœur 0, priorité 3, pas de 20 ms, niveau à 1 Hz,
  journal au changement), snapshot partagé, `/api/etat.air` complet (dont
  `statut_brut` et `opmode_brut` pour le diagnostic), scène **Air** (logo 8×8
  + `AQI n` ou valeur, `--` si invalide), logo 6×6 à côté de l'heure
  (`ZoneIcone`, heure alignée à gauche), hook de température brute pour le
  wizard.
- `clock_web` : `/air`, `/api/air/modele`, `GET`/`POST /api/air`,
  `POST /api/air/wizard` (`reference` en centi-°C ⇒ décalage calculé sur la
  mesure brute, avertissement > 2 °C).
- Page : onglet **Air** (tuiles vivantes, explications préchauffage / première
  mise en service / compensation par défaut / défaut), **wizard** en 3 étapes
  (détection, décalage avec thermomètre de référence, logo), deux formulaires.

**Sur le module réel** (HW-AIR-08) : `PART_ID` 0x0160 à 0x53, `OPMODE` relu
0x02, AHT21 calibré à 0x38 : 24,9 °C / 37,6 %. `DEVICE_STATUS` = 0x8B : le
capteur, neuf, est dans sa **première heure** (validité 2) — la page le dit,
l'AQI reste `--`, le logo gris : la règle d'honnêteté fonctionne telle quelle.
Le wizard de décalage a été exercé par HTTP (380 centi avec une référence
fictive, remis à 0 ensuite). Dette D11 (registres) levée pour `PART_ID`,
`OPMODE`, `DEVICE_STATUS`, AHT21 ; `DATA_*` à confronter après la première
heure.

**Résultats.** Tests host : **9 955 vérifications, 0 échec, couverture 97,8 %**
(2 014 lignes ; `air_quality` : `aht21` / `ens160` / `levels` 100 %, `sensor`
97 %). Image 1 045 Ko (47 % libre). OTA ×2.

---

## 2026-11-05 (suite 10) — Réglages, page web complète, wizards, OTA, couleurs

**Quoi.** Étape 7 de la phase 1, plus deux demandes de Nicolas (« l'IP une
seule fois à la connexion », « plus de couleur ») et, pendant la nuit, « une
page plus intuitive avec de vraies heures », « choisir ce que la dalle affiche
et l'ordre » :

- `components/settings/` (pur) : `JsonWriter` borné (profondeur 8, `nullptr`
  refusé, débordement ⇒ longueur 0), modèle `DisplaySettings` /
  `SystemSettings` / `brightness::Config`, formulaires **tout-ou-rien**
  (`apply_*_form`, `FormError{champ, raison}`), texte des scènes
  `Heure:20:1,Valeur0:8:1` et de la courbe `300:8,900:40`, JSON de **modèle**
  (`type`, `libelle`, `unite`, bornes, défaut, aide ; types `heure`, `scenes`,
  `courbe` pour les widgets) et de valeurs. **Source de vérité unique** : la
  page ne recopie aucune borne ni aucun nom.
- `components/clock_settings/` : blobs NVS versionnés (`clk_cfg/display`,
  `bright`, `system`), copie partagée sous mutex, compteur de version que la
  tâche d'affichage scrute pour **appliquer à chaud** (contrôleur de luminosité
  reconstruit, ordonnanceur relancé, fuseau), `replaced_on_boot()` signalé sur
  le tableau de bord, `factory_reset()` (réglages + Wi-Fi).
- `clock_web` : `Hooks` (notification, `status_json`, motif de test, mesure
  brute, progression OTA) ; routes `/`, `/affichage`, `/luminosite`,
  `/systeme` (une page), `/api/etat`, `/api/<domaine>/modele`, `GET`/`POST
  /api/<domaine>` (400 `{"ok":false,"champ","raison"}`), `/api/affichage/test`,
  `/api/luminosite/brut`, `/api/luminosite/wizard` (4 mesures →
  `build_from_wizard`), `/api/systeme/redemarrer`, `/api/systeme/usine`
  (`confirme=OUI`), **`POST /api/ota`** (écriture par blocs de 4 Ko dans
  l'emplacement inactif, barre sur la dalle, redémarrage, rollback armé —
  l'image se confirme au premier rafraîchissement de la dalle).
- `www/app.html` (19,5 Ko, aucune ressource externe, thème auto) : tableau de
  bord, formulaires **construits depuis le modèle** avec libellés en clair,
  unités, aide et bornes du firmware ; **heures en HH:MM** (`<input
  type=time>` ↔ minutes du jour) ; **éditeur de scènes** (type, durée, active,
  monter/descendre/retirer, ≤ 8) — c'est là qu'on choisit ce que la dalle
  affiche et dans quel ordre ; éditeur de **courbe** par points ; wizard de
  correspondance (pixel, colonne, chenillard pour le serpentin, diagonale,
  plein) ; wizard de luminosité (4 mesures, avertissement « déportez le
  capteur ») ; OTA avec barre ; redémarrage ; usine avec confirmation.
- `app_main` : thème de couleurs (chiffres blancs, deux-points ambre, barre des
  secondes bleue, jour ambre, date blanche, état bleu clair, notification par
  priorité) ; le texte d'état est une passe **unique** (priorité 0, durée
  calculée sur la largeur du texte) à l'obtention de l'IP, et toutes les 60 s
  en portail ; une notification entrante prime et ré-arme l'état. Rotation par
  défaut Heure 20 s / Date 5 s (le défilement permanent est retiré).
- `clock_scene` : `ClockColors` / `DateColors` à la place d'une couleur unique.

**Défauts trouvés et corrigés.** (1) `clock_time::start()` passait à lwIP un
pointeur vers un nom de serveur NTP **sur la pile de `app_main`** : plus aucune
synchro après le passage aux réglages NVS — copie statique désormais (et
l'horloge se synchronise en 6 s). (2) La passe d'état était « oubliée » entre
sa mise en file et son affichage, et sortait avec le texte/la couleur d'une
notification : machine d'état `pending` → `queued` → `showing` explicite.
(3) `JsonWriter` déréférençait un `nullptr` (trouvé par le nouveau test).

**Résultats.** Tests host : **9 299 vérifications, 0 échec, couverture 97,9 %**
(1 865 lignes) ; `settings` 96,5 % (`forms.cpp`). Image 1 006 Ko (49 % libre).
Sur la carte : API complète vérifiée par `curl` (HW-WEB-04), **OTA par HTTP**
(HW-WEB-02 : 998 Ko en 5,6 s, image confirmée), heure SURE à 6 s, ordre des
scènes changé à chaud. **À observer par Nicolas** : la passe d'IP (HW-WEB-05),
les couleurs (HW-WEB-06), la page au téléphone (HW-WEB-01).

---

## 2026-11-05 (suite 9) — L'heure : règles POSIX en logique pure, SNTP, `--:--` devient l'heure

**Quoi.** Étape 6 de la phase 1 :

- `components/timekeeping/` (pur) : `parse_posix_tz()` (`STD offset DST[offset],Mm.w.d[/time],…`,
  offsets en minutes **est** de l'UTC, demi-heures, secondes, heures de
  bascule négatives ou > 24), `days_from_civil` / `civil_from_days` (Hinnant,
  64 bits : pas de falaise 2038), `transition_epoch()` (semaine 5 = dernier
  jour-de-semaine du mois), `is_dst()` (hémisphère nord **et** sud),
  `to_local()`, `SyncValidity` (48 h), et une table de **18 zones** IANA →
  POSIX pour la future page `/systeme`, `Europe/Zurich` par défaut.
  **Choix** : ne pas utiliser le `TZ` de la libc — celui de MinGW ne comprend
  pas les règles `M3.5.0`, donc impossible à tester sur host ; la logique
  maison est testée **à la seconde** sur les deux bascules suisses de 2026,
  celle des États-Unis et celle de Sydney (avertissement de Nicolas :
  « attention au changement d'heure été/hiver »).
- `components/clock_time/` : `esp_netif_sntp` (`pool.ntp.org`, sans attente),
  rappel de synchro → `SyncValidity::mark_synced()` sous section critique,
  `now()` = epoch UTC de `time()` converti par la règle courante ;
  `set_timezone()` à chaud pour la page de réglages.
- `app_main` : lit la source de temps à chaque trame, log au basculement
  `sure`, scène **Date** ajoutée à la rotation (Heure 6 s, Date 4 s, texte
  d'état), minute du jour fournie au contrôleur de luminosité pour la fenêtre
  d'extinction.

**Résultats.** Tests host : **9108 vérifications, 0 échec, couverture 99,1 %**
(1280 lignes). Build propre, image 907 Ko. Sur la carte : `boot_grace` →
`connected` à 23 s (la grâce en action : la box a mis 20 s), `SNTP synchronised`
à 31 s, **`time is now SURE (2026-11-05 03:33:24)`**, heure de Zurich sur la
dalle, puis `JEU 05.11` en 3×5.

**Vérifié par Nicolas** : heure juste, date en 3×5 lisible. **Non vérifié** :
la bascule réelle du 29 mars 2026 (HW-TIME-02), le réglage du fuseau sur la
page (HW-SYS-01, page à écrire). Serveur NTP et fuseau encore constants dans `app_main` (réglages
NVS à venir avec `settings`).

---

## 2026-11-05 (suite 8) — Réseau : portail captif, reprise après coupure, canal de notification

**Quoi.** Étape 5 de la phase 1, à la demande de Nicolas « rien de ThermoSync
sauf le réseau, et le hub doit pouvoir envoyer des messages » :

- `components/network/` (pur) : `credentials` (validation, formulaire borné,
  JSON, mot de passe du point d'accès), `captive_dns` et `supervisor` **repris
  de ThermoSync et traduits** ; le superviseur gagne l'**extension coupure de
  courant** : états `BootGrace` (délais 10/20/40/60/60 s, jamais de portail,
  fin anticipée après 2 échecs d'authentification), `WaitingLease` (associé
  sans IP : réessais 60 s → 5 min, jamais de portail), `LeaseDesperate` (30 min
  sans bail : portail levé **en plus**), et la demande de redémarrage de
  récupération (24 h sans IP, une fois). Entrée `LinkStatus{associated, has_ip}`
  + `FailCause` ; l'ancienne signature `step(now, link_up)` reste pour
  l'héritage.
- `components/alerts/` (pur, amorce) : `decode_form()` du canal de
  notification — `cle` obligatoire `[a-z0-9_]`, champs hors bornes remplacés
  par leur défaut et comptés, textes tronqués. Partagé par toutes les portes
  d'entrée (HTTP local aujourd'hui, Home Assistant et hub ensuite).
- `components/clock_web/` : Wi-Fi APSTA, événements → `LinkStatus`/`FailCause`
  (`NO_AP_FOUND` → `ApNotFound`, `AUTH_FAIL`/handshake → `AuthFailed`), tâche
  de supervision à 1 s qui **applique le plan**, DNS captif (tâche), identifiants
  en NVS `clk_wifi` pris en compte **sans redémarrage**, mDNS `horloge.local`,
  serveur HTTP : `/wifi` (page embarquée, 6,3 Ko), `/api/wifi/*`, `/api/info`,
  sondes de détection de portail, 404 → portail, et **`POST /api/notification`**
  qui pousse une notification à l'affichage (priorité → couleur, durée →
  interruption de scène).
- `app_main` : **tâche d'affichage dédiée** sur le cœur 1, priorité 6 (au-dessus
  du serveur HTTP et des tâches réseau, EX-61) ; le réseau démarre ensuite et
  son échec ne touche pas l'affichage. Le texte défilant montre **comment
  joindre l'horloge** : `WIFI Horloge-XXXX MDP …` en portail, l'adresse IP une
  fois connectée.
- Mot de passe du point d'accès : alphabet **chiffres + majuscules sans O ni I**
  — la police 5×7 n'a pas de minuscules, et un mot de passe lu sur la dalle doit
  se taper exactement. Puis, à la demande de Nicolas, mot de passe **fixé à
  `12345678`** dans `sdkconfig.defaults.horloge-v1` (`CLOCK_WIFI_AP_PASSWORD`) :
  il ne garde que le portail de mise en service, et un mot de passe trivial
  lisible sur la dalle vaut mieux qu'un mot de passe dérivé que personne ne
  retient. La dérivation reste le repli quand le réglage est vide.

**Résultats.** Tests host : **8901 vérifications, 0 échec, couverture 99,1 %**
(1075 lignes). Build propre, image **894 Ko** (53 % libres : le WiFi, lwIP,
mbedTLS et mDNS pèsent). Flashé sur COM21 : `access point Horloge-C995 (WPA2)`,
`captive DNS ready`, `mDNS: horloge.local`, `HTTP server ready`, affichage à
25 i/s et LDR vivants pendant que le réseau tourne.

**Ce que les tests ont attrapé.** Un test de troncature mal construit (préfixe
compté à la main à 44 au lieu de 45 caractères : le champ `texte` n'existait
pas) ; corrigé par `strlen`. Le décodeur acceptait d'abord un texte trop long
en le **refusant** au lieu de le tronquer : corrigé (tampon de travail de 1 Ko,
troncature comptée).

**Non vérifié.** Portail ouvert depuis un téléphone (HW-RES-05), connexion à la
box depuis le portail (HW-RES-06), `horloge.local` (HW-RES-07), coupure
secteur (HW-RES-01/02) : ça demande un téléphone et la box de Nicolas.

---

## 2026-11-05 (suite 7) — Luminosité automatique : `brightness` pur, ADC1, asservissement

**Quoi.** Étape 4 de la phase 1 :

- `components/brightness/` (pur) : `Median5` (réseau de comparaisons),
  `AsymmetricAverage` (Q8, montée 10 s / descente 60 s), `Config` + `Curve`
  validés, `interpolate()` entier arrondi, `in_window()` franchissant minuit,
  `Controller` (compensation d'auto-éclairage **signée en comptes à 255**,
  zone morte d'un pas + arrondi au multiple du pas, plancher/plafond, 5 ratés
  ⇒ `kFallbackLevel` 64, rails 0/4095 = défaut, fenêtre d'extinction appliquée
  **seulement si l'heure est sûre**, modes Auto/Manual/Off),
  `build_from_wizard()` (3 points triés par mesure, compensation signée).
- `components/clock_hal/` : `LdrAdc` — `adc_oneshot` sur le canal résolu depuis
  le GPIO (refus explicite si ADC2), 12 dB, 12 bits, 64 échantillons moyennés,
  étalonnage *line fitting* par l'eFuse, lecture ratée ⇒ échantillon invalide.
- `app_main` : lecture à 1 Hz, `Controller::step()`, le niveau appliqué nourrit
  `render()` ; log brut / mV / filtré / niveau / défaut toutes les 5 s.

**Écarts de spec, consignés dans `spec-luminosite.md`** : compensation en
**comptes ADC** (signée) plutôt qu'en ‰ ; courbe du wizard à **3 points**
(nuit/soir/jour) et non 4 — la 4ᵉ mesure est l'auto-éclairage, pas un niveau
d'ambiance ; « hystérésis d'un demi-pas » précisée en **zone morte d'un pas +
arrondi au pas**.

**Résultats.** Tests host : **8661 vérifications, 0 échec, couverture 98,9 %**
(629 lignes). Build propre, 241 Ko, flashé sur COM21 : `ADC1 channel 5 on
GPIO33, 12 dB, calibrated=1`, ~2086 comptes (1,9 V) sous l'éclairage du bureau
⇒ niveau 170 avec la courbe par défaut (câblage (a) supposé).

**Câblage confirmé (a)** : main sur la LDR ⇒ la dalle s'assombrit (Nicolas).
La courbe par défaut est dans le bon sens. La baisse de mesure vue dans les
logs (2086 → 1754) pendant que la dalle passait de 48 à 170 n'était pas de
l'auto-éclairage — avec ce câblage il ferait *monter* les comptes — mais
l'ambiance qui changeait (lampe allumée entre-temps, déplacements). Aucune
compensation inventée : le wizard la mesurera dans le noir, dalle éteinte puis
allumée (HW-LUM-02).

**Ce que les tests ont attrapé.** Une attente d'arrondi fausse dans LUM-05
(131,5 → 131, l'arrondi va à l'opposé de zéro sur une pente négative) ; le code
était cohérent, l'attente a été corrigée et documentée dans le test.

---

## 2026-11-05 (suite 6) — Polices, scènes, défilement, ordonnanceur ; `--:--` sur la dalle

**Quoi.** Étape 3 de la phase 1, dans `display` (pur) :

- `font.hpp` / `fonts.cpp` : trois polices colonne-majeure (bit 0 = ligne du
  haut) — **5×7** (jeu GLCD classique, `' '..'`'`), **3×5** et **3×4** dessinées
  pour la dalle ; le degré occupe le slot du backtick (`kDegreeChar`).
- `text.hpp` : `text_width()`, `draw_text()` avec **clipping propre** (un texte
  qui défile hors dalle n'incrémente pas le compteur hors-dalle), et
  `transliterate()` : UTF-8 → capitales ASCII, accents Latin-1 et Œ réduits à
  leur base, `°` → degré, inconnu → `?`, borné et toujours NUL-terminé.
- `clock_scene.hpp` : `TimeInfo{sure, …}`, `draw_clock()` (25 px, deux-points
  étroit d'un pixel, clignotement, barre des secondes) qui affiche **`--:--`
  sans barre** quand `sure` est faux, `draw_date()` (`SAM 05.11` en 3×5, 30 px,
  rien si pas sûr), abréviations françaises des jours.
- `scroll.hpp` : `Scroller` à vitesse entière exacte (position en 1/256 px +
  reste en ms : 20 px/s font 20 px/s à 40 ms comme à 33 ms), 1..n passages ou
  infini, troncature à 128 caractères.
- `scheduler.hpp` : rotation des scènes, saut des scènes sans donnée (y compris
  **en cours** si la donnée disparaît), horloge synthétique si tout est sauté,
  durée 0 = jusqu'à `finished`, notifications (P≥1 interrompt et rend la main
  **au temps restant**, P0 attend la fin de scène, remplacement par priorité ≥),
  débordement 32 bits toléré.
- `test_patterns.hpp` : les motifs de mise en service et le chenillard
  physique sortent de `app_main` pour devenir des fonctions pures (le wizard
  s'en servira tel quel).
- `app_main` : boucle à **25 i/s** pilotée par l'ordonnanceur ; sans source de
  temps, la scène Heure montre **`--:--`** avec le deux-points qui clignote, en
  alternance avec un texte défilant translittéré (« MATRIX CLOCK — ESP-IDF —
  HEURE NON SYNCHRONISEE »).

**Résultats.** Tests host : **8530 vérifications, 0 échec, couverture 99,5 %**
(422 lignes ; les deux lignes restantes sont des branches défensives
injoignables de `scheduler.cpp`). `idf.py build` propre (`esp_timer` ajouté
aux `REQUIRES` de `main`), image 230 Ko, **flashé sur COM21** : `scene 0`
(Heure) puis `scene 4` (Défilement) dans les logs, `oob=0`.

**Ce que les tests ont attrapé.** L'ordonnanceur attendait une seconde en
« horloge synthétique » avant de choisir la première scène : onze échecs d'un
coup, tous décalés de 1 s — corrigé par un `started_` qui fait choisir dès le
premier `step()`. Deux pièges de compilation notés : `"\xC3\xA9e"` est **un
seul** échappement hexadécimal invalide (le `e` est un chiffre hexa), il faut
couper la chaîne ; et le `.gcda` d'un run précédent fait râler gcov après une
recompilation (le script les efface désormais avant `ctest`).

**Vérifié sur la dalle** (Nicolas) : `--:--` clignotant puis le texte qui
défile. **Non vérifié** : la lisibilité des glyphes 3×5 / 3×4 dessinés à la
main — la date n'apparaît qu'avec une heure sûre, donc à l'étape SNTP.

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
