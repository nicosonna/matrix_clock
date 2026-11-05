# Garde-fous & standard de dev — Horloge

> Canevas repris des garde-fous éprouvés sur *ThermoSync*, *Regulation_chauffage*,
> *Pigrow*, *mavlink_rc_transponder* et *solar_house*, adapté à un firmware
> **ESP-IDF pur** d'affichage connecté. `CLAUDE.md` en est le résumé
> opérationnel ; ce fichier est la référence.

---

## 0. Contexte du projet

- **Nom / but** : Horloge — horloge matricielle WS2812B 8×32 connectée :
  luminosité automatique, heure SNTP, capteurs lus dans Home Assistant,
  notifications et règles d'alerte, page web à wizards, portail captif.
  Détail : `dev/cadrage-fonctionnel.md`.
- **Cible d'exécution** : devkit **ESP32-WROOM-32** (4 Mo, sans PSRAM), carte
  `horloge-v1`, sous **ESP-IDF v5.3** (CMake, `idf.py`), C++17, FreeRTOS. Pas de
  PlatformIO, pas d'Arduino.
- **Machine de dev** : PC Windows. Build/flash en PowerShell (`export.ps1`) ;
  **logique pure testée en natif sur PC** (g++ MinGW), matériel **moqué**.
- **Contraintes fortes** : MISRA C++ 2023, CERT C++ ; aucune allocation dynamique
  après init ; pas d'exceptions/RTTI ; **plafond de courant** obligatoire (une
  dalle de 256 LEDs tire 15,6 A en blanc plein).
- **Non-buts** : piloter quoi que ce soit dans la maison ; porter une sécurité ;
  dépendre de Home Assistant ou du hub pour afficher l'heure ; **lire** Home
  Assistant par MQTT (MQTT ne sert qu'à **publier** les capteurs locaux, comme
  la borne) ; Cloud ou compte en ligne.

> Règle d'or : dès qu'une décision structurante est prise (module créé/renommé,
> matériel choisi, contrainte arrêtée), **mettre à jour ce contexte, `CLAUDE.md`,
> `architecture.md`, le tableau des décisions de `dev/cadrage-fonctionnel.md`,
> et cocher/retirer la tâche de `dev/todo.md`**.

---

## 1. Architecture — logique pure isolée du matériel

- **La logique ne dépend jamais du matériel ni d'ESP-IDF.** Elle reçoit des
  mesures (avec drapeau de validité) et rend des décisions. Toute dépendance
  active (ADC, dalle, NVS, horloge, WebSocket, HTTP) passe par une **interface
  injectée** : un composant `clock_*` l'implémente avec ESP-IDF, les tests avec
  une **doublure** dans `firmware/test/`.
- Les composants purs (`display`, `brightness`, `network`, `homeassistant`,
  `alerts`, `settings`) ne contiennent **aucun** `#include` ESP-IDF/FreeRTOS :
  ils compilent avec un g++ nu. C'est vérifié par le build host.
- **Un fichier = une responsabilité.** Pas de sur-ingénierie.
- **Frontière d'affichage** : seul `display` écrit sur la dalle. Les couches
  réseau ne font que **publier** des données, que `display` affiche ou
  **refuse d'afficher** si elles sont invalides.
- **Brochage** : tout dans `clock_board`, `constexpr`, sélectionné par Kconfig.
  Jamais un numéro de GPIO dans la logique ou le HAL.
- ⚠️ **Noms de composants** : ne jamais nommer un composant comme un composant
  cœur d'ESP-IDF (`hal`, `json`, `log`, `heap`, `console`…). Un homonyme projet
  le **remplace silencieusement** et tout IDF cesse de compiler. Le lecteur JSON
  vit **dans** `homeassistant`.

## 2. Configuration — zéro valeur magique

- Toute constante de comportement est **nommée** avec son unité dans le nom ou
  son type (`kBudgetDefautMa`, `kCourantCanalPleinUa`, `millis_t`, `centi_degc_t`).
- Le paramétrable à l'exécution va en **NVS**, par espace et par domaine, avec un
  **numéro de version de schéma** et des défauts fournis. Version inconnue ou
  validation en échec ⇒ **défauts** + drapeau `config_remplacee` visible sur la
  page. On ne rattrape pas une config corrompue.
- Le modèle de réglages de `settings` est la **seule source de vérité** des
  bornes : la page web les lit sur `/api/<domaine>/modele`, jamais en dur.
- Les réglages **par carte** (brochage, dorsale LED) ne vont jamais dans
  `sdkconfig.defaults` commun : surcouche `sdkconfig.defaults.<carte>`.

## 3. Sécurité & secrets

- **Aucun secret dans le dépôt** (clés WiFi, jeton Home Assistant, jeton du hub).
  NVS uniquement. Le jeton Home Assistant **s'écrit et ne se relit pas** depuis
  la page web.
- Défaillance ⇒ **état honnête explicite**, jamais une valeur silencieusement
  fausse (§4 bis).
- **Entrée non sûre** : formulaire du portail, requête DNS, message WebSocket,
  corps de `POST /api/…`, message du hub. Décodage **borné, sans allocation, sans
  récursion**, et **rejet** plutôt que réparation. Les tampons de réassemblage
  sont dimensionnés à la compilation ; un message trop long est abandonné avec un
  log, **jamais tronqué puis interprété**.

## 4. Qualité de code

- C++17, `std::array`, entiers explicites, `enum class`, `noexcept`, `constexpr`.
- **Doxygen** sur chaque API publique ; commentaires expliquant le **pourquoi**.
- **Gestion d'erreur explicite** par `Status` ; tout `esp_err_t` vérifié, jamais
  d'échec silencieux.
- Build host en **`-Werror`** avec l'arsenal d'avertissements (conversions,
  masquage, casts C). Le code firmware compile avec les mêmes.
- **Langue** : **code en anglais** (identifiants, commentaires, Doxygen, logs,
  commits) — le dépôt est public ; page web et documentation en français. Les
  composants repris de ThermoSync sont traduits à la copie.
- Cohérence stylistique : le nouveau code ressemble au code autour.

## 4 bis. Honnêteté de l'affichage (propre à ce projet)

C'est l'équivalent, pour une horloge, de l'« état sûr » d'une régulation : une
horloge qui affiche une heure fausse est **pire** qu'une horloge qui affiche
`--:--`, parce qu'on la croit.

- Heure jamais synchronisée, ou synchro de plus de 48 h ⇒ `--:--` + indicateur.
- Valeur Home Assistant absente, non numérique, `unavailable`, `unknown` ou
  périmée ⇒ `--`. **Jamais** la dernière valeur connue présentée comme fraîche.
- Mesure de luminosité invalide ⇒ luminosité de **repli nommée** (`kLumRepli`),
  signalée dans les diagnostics. Jamais 0 ni 255 par accident.
- Une règle d'alerte dont la source est invalide **ne déclenche pas** ; ce sont
  les règles **internes** (`donnee_perimee`, `liaison_perdue`, `heure_non_sure`,
  `bail_absent`) qui disent qu'on ne sait pas.
- Un pilote qui échoue éteint la dalle **explicitement** plutôt que de laisser le
  dernier contenu figé (une horloge figée se lit comme une horloge juste).

## 5. Tests & couverture (garde-fou central)

- **Tout se teste.** Si quelque chose n'est pas testable, c'est un défaut
  d'architecture : on extrait la logique et on **moque** la dépendance.
- **Les tests tournent sur PC**, jamais sur la cible. Harnais `firmware/test/`
  (CMake, g++, exécutable `test_clock`), lancé par
  `bash firmware/test/run_host_tests.sh`. Couverture ≥ **95 %** de la logique
  pure : `--coverage` (gcov agrégé, échec sous 95 %).
- **Harnais sans `assert()`** : `assert` disparaît avec `NDEBUG`, un test
  passerait en silence. On compte les vérifications (`CHECK`, `CHECK_EQ`,
  `CHECK_ENUM`) et `main` renvoie un code non nul au moindre échec.
- **Un test d'absence d'allocation** : un `operator new` global qui fait échouer
  le test, armé après l'initialisation. C'est la seule façon de faire respecter
  « aucune allocation après init » autrement qu'à la relecture.
- **Plan de test** `dev/test-plan-hw.md` **à jour en permanence** : matrice
  test ↔ code ↔ procédure ↔ attendu ↔ statut/date. Tests host **et**
  vérifications sur matériel réel (étape séparée, hors CI).
- À chaque modif de code OU ajout de test, **automatiquement** : rejouer, mettre
  à jour le statut, retirer l'obsolète, ajouter pour toute nouvelle feature.
  **Aucune feature sans test.** Un nouveau test doit d'abord **échouer** sur le
  défaut qu'il vise.
- **Bancs en Docker** : Home Assistant de test (`campagne_ha_docker.py`) et, en
  phase 2, hub de test (`campagne_hub.py`). On ne pilote **jamais** le Home
  Assistant de la maison depuis les tests.
- Exclusions de couverture légitimes uniquement : accès matériel réel, transports,
  `app_main`, boucles infinies. Jamais de logique testable.

## 6. Documentation vivante (garde-fou central)

- **Avant** de modifier : lire `dev/journal.md`, `dev/todo.md`,
  `dev/cadrage-fonctionnel.md`, `architecture.md`.
- **Après** toute modif non triviale : entrée en tête de `dev/journal.md` (date,
  quoi, pourquoi, état), mise à jour de `dev/todo.md`, de `architecture.md` si la
  structure change, de `dev/test-plan-hw.md`, et de la spec du domaine touché.
- Fiche matériel dans `cartes/horloge-v1/` (brochage, alimentation, pièges).

## 7. Normes UI

- **Aucun emoji.** Icônes = **SVG inline** style ligne, centralisées en `<symbol>`.
- **Aucune ressource externe / CDN** : page autonome servie par l'ESP32,
  **≤ 60 Ko gzippés**, taille relevée à chaque build.
- Look pro et sobre, thème clair/sombre auto (`prefers-color-scheme`), utilisable
  au doigt sur téléphone.
- **Wizard par domaine**, bornes lues sur `/api/<domaine>/modele`. Un wizard
  périmé est un défaut.

## 8. Cohérence / robustesse

- Watchdog de tâche actif, reprise propre après reset, pas de blocage ; une
  dépendance absente dégrade **proprement** avec un log.
- **La boucle d'affichage ne bloque jamais** : états partagés publiés par copie,
  priorité d'affichage supérieure aux tâches réseau, aucune attente réseau sur
  son chemin.
- Rollback OTA actif : une image qui ne confirme pas son démarrage est annulée.
- Valeurs dupliquées synchronisées (config ⇄ doc ⇄ plan de test).

## 9. Conventions Git

- **Ne commiter/pusher que si demandé.** Sur `main`, créer une branche d'abord.
- Ne jamais re-tracker un fichier volontairement git-ignoré (`sdkconfig`,
  `build/`, `managed_components/`).
- Messages clairs, **en anglais**, ligne de co-auteur nommant le modèle
  réellement utilisé. Dépôt **public** : relire chaque commit pour un secret, une
  adresse de la maison, un numéro de série.
- Avant une opération destructive, vérifier la cible ; préférer le réversible.

---

## 10. Checklist « avant de dire que c'est fini »

- [ ] Logique pure sans include ESP-IDF ; dépendances derrière une interface injectée, moquées en test.
- [ ] Aucune valeur magique ; constantes nommées avec unité ; brochage dans `clock_board` ; aucun nom de composant en conflit avec ESP-IDF.
- [ ] Pas d'allocation dynamique (test `operator new`), pas d'exception/RTTI/récursion ; `std::array`.
- [ ] Doxygen + messages d'erreur explicites ; tous les `esp_err_t` vérifiés.
- [ ] **Honnêteté** : aucun chemin où une heure, une valeur ou une luminosité fausse peut s'afficher.
- [ ] **Plafond de courant** respecté et vérifié ; écrêtage visible sur la page.
- [ ] Wizard du domaine touché mis à jour, test de cohérence wizard ⇄ modèle passé.
- [ ] Tests ajoutés/mis à jour, **rejoués** (`run_host_tests.sh`), couverture ≥ 95 %.
- [ ] Build host en `-Werror` propre ; `idf.py build` propre ; taille de la page gzippée relevée.
- [ ] `dev/journal.md` **et** `dev/todo.md` **et** `dev/test-plan-hw.md` **et** `architecture.md` **et** la spec du domaine mis à jour.
- [ ] Résultats rapportés **fidèlement** (tests qui échouent = on le dit).
