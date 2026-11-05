# Plan de test final — Horloge, phase 1 (2026-11-05)

> **Objet.** Le plan de test **consolidé** de la phase 1 : toutes les specs,
> tous les tests host, toutes les vérifications matérielles, les garde-fous
> transverses, la campagne de banc qui reste à jouer et les critères de sortie.
> La **matrice détaillée** (test ↔ code ↔ procédure ↔ attendu ↔ statut) reste
> `dev/test-plan-hw.md`, mise à jour à chaque modification ; ce document en est
> la vue d'ensemble et la feuille de route de la campagne finale.
>
> **Règle du projet** (CLAUDE.md) : tout se teste ; un test périmé ou une
> fonction sans test est un défaut ; les tests host tournent **sur le PC**,
> jamais sur la cible ; une dépendance matérielle se moque derrière une
> interface injectée.

---

## 1. Comment tout lancer

| Quoi | Commande / procédure | Attendu |
|---|---|---|
| **Tests host** (logique pure, 10 composants) | `bash firmware/test/run_host_tests.sh` | `100% tests passed`, nombre de vérifications affiché, code de retour 0 |
| **Couverture** (objectif ≥ 95 %, échec en dessous) | `bash firmware/test/run_host_tests.sh --coverage` | ligne `TOTAL … (target >= 95 %)` |
| **Build firmware** (PowerShell, jamais Git Bash) | `. "$HOME\esp\esp-idf\export.ps1"` puis `idf.py -C firmware build` | `matrix_clock.bin binary size …`, aucun `warning:` dans `main/` |
| **Flash série** (première fois, ou après une boucle de redémarrage) | `idf.py -C firmware -p COM<n> flash` — **COM21** le 2026-11-05 ; **jamais COM15** (ThermoSync) | démarrage, `OTA image confirmed` |
| **Mise à jour OTA** (courante) | `curl.exe -H "Expect:" --data-binary @firmware/build/matrix_clock.bin http://<ip>/api/ota` | `Image écrite…`, redémarrage sur l'autre emplacement, `/api/info` répond avec la nouvelle version |
| **Journal série** | `System.IO.Ports.SerialPort` sur le port, 115200 8N1, `DtrEnable`/`RtsEnable` à **false** (sinon reset) | lignes `I (…) main: …` |
| **Broker MQTT de banc** | `python -I firmware/test/bench/mini_broker.py 1883 broker.log inject.txt 240` puis `/mqtt` ou `POST /api/mqtt` vers l'IP du PC | `CONNECT`, `SUBSCRIBE homeassistant/status`, 10 `…/config` retenus, `…/etat` périodique ; `echo "homeassistant/status\|online" >> inject.txt` ⇒ découverte renvoyée |
| **Canal d'alerte entrant** | `curl.exe -d "cle=test&titre=T&texte=Bonjour&priorite=1&duree_s=12" http://<ip>/api/notification` ; `-d "cle=test&effacer=1"` pour retirer une tenue | `Notification acceptée`, scène `Alerte` dans `/api/etat`, retour à `Heure` |
| **Diagnostics** | `GET /api/info` (version, emplacement OTA, cause du redémarrage, tas libre / minimum, taille de la page), `GET /api/systeme/taches` (charge par tâche) ; onglet Système → Diagnostics | cause `volontaire` après un redémarrage demandé, `chien_de_garde_tache` après HW-SYS-02 |
| **Rollback OTA** (HW-WEB-03) | image corrompue ⇒ `400 Image invalide`, rien ne redémarre ; image de test `build-norollback` (`-DCLOCK_OTA_NEVER_CONFIRM=1`) ⇒ démarre, ne se confirme pas, le redémarrage suivant **revient** à l'image précédente | `/api/info` : `version` et `emplacement` reviennent |
| **État en direct** | `GET /api/etat` (heure, luminosité, courant, réseau, air, ha, mqtt, alertes, météo, scène) | JSON ; toute grandeur invalide est `false`/`--`/absente, jamais inventée |

Le Home Assistant **de la maison** n'est jamais piloté par un test : les
vérifications HA/MQTT réelles se font avec un utilisateur et un jeton dédiés
créés par Nicolas, ou avec un Home Assistant de test (Docker + Mosquitto).

---

## 2. État de la couverture par spec (2026-11-05)

| Spec | Composant(s) | Tests host | Vérifications matérielles | Reste |
|---|---|---|---|---|
| `spec-affichage.md` §7 | `display` (pur), `clock_display` | AFF-01 → AFF-18 : **18 PASSE** (tampon, correspondance, gamma, plafond de courant, polices, scènes, défilement, ordonnanceur, motifs, zone d'icône) | HW-AFF-01 PASSE ; HW-AFF-02 → 05 à faire | Correspondance à l'œil sur la dalle finale, lisibilité à 3 m, charge SPI vs RMT |
| `spec-luminosite.md` §5 | `brightness` (pur), `clock_hal` ADC | LUM-01 → LUM-14 : **14 PASSE** | HW-LUM-01 en cours (ADC étalonné) ; HW-LUM-02/03 à faire | Plage nuit/jour réelle, wizard d'étalonnage joué une nuit |
| `spec-reseau-portail.md` §5 | `network` (pur, repris de ThermoSync), `clock_web` | RES-01 → RES-28 : **12 PASSE** (identifiants, portail, DNS, superviseur, coupure, bail) | HW-RES-05/06/07 PASSE ou en cours ; HW-RES-01 → 04 à faire | Coupure secteur, box éteinte 10 min, DHCP coupé, téléphone |
| `spec-home-assistant.md` §9 (WebSocket) | `homeassistant` (pur), `clock_homeassistant` | HA-01 → HA-15 : **15 PASSE** | HW-HA-01 mécanique vérifiée (hôte fictif) ; HW-HA-01 → 04 à faire avec un vrai jeton | Jeton d'un utilisateur dédié, HA redémarré, jeton révoqué, automatisation réelle |
| `spec-home-assistant.md` §6 bis (MQTT) | `homeassistant::mqtt` (pur, repris de ThermoSync), `clock_homeassistant/mqtt_link` | MQ-01 → MQ-10 : **10 PASSE** | HW-MQ-01/03 mécanique vérifiée sur le **broker de banc** ; HW-MQ-02 à observer | Mosquitto + découverte sur le vrai Home Assistant, préchauffage `null` au démarrage à froid, testament par coupure brutale |
| `spec-alertes.md` §6 | `alerts` (pur), rendu dans `app_main` | ALR-01 → ALR-18 : **18 PASSE** | HW-ALR-02 mécanique vérifiée, HW-ALR-04 PASSE ; HW-ALR-01/03 à faire | Sonde refroidie (règle `GEL`), une nuit complète en heures silencieuses |
| `spec-qualite-air.md` §10 | `air_quality` (pur), `clock_hal` I²C | AIR-01 → AIR-22 : **22 PASSE** | HW-AIR-02/05/08 vérifiés ; HW-AIR-01/03/04/06/07 à faire | Préchauffage chronométré, solvant, pièce fermée, référence de température, débranchement à chaud |
| `spec-serveur-web.md` §7 | `settings` (pur), `clock_web`, `www/app.html` | WEB-01 → WEB-07 : **7 PASSE** + WEB-07 vérifié par HTTP (7 domaines, aucun champ orphelin, wizards cohérents) | HW-WEB-02/04 PASSE ; HW-WEB-01/03/05/06 à faire | Téléphone, image cassée (rollback), couleurs à l'œil |
| heure (`timekeeping`, `clock_time`) | `timekeeping` (pur) | TIME-01 → TIME-08 : **8 PASSE** | HW-TIME-01 PASSE ; HW-SYS-01 à jouer ; HW-TIME-02 en mars | Fuseau changé à chaud sur la carte, bascule réelle d'heure d'été |
| Garde-fous transverses | tout | GEN-01 → GEN-05 : **5 PASSE** (g++ nu, `-Werror`, couverture, aucune allocation, harnais sans `assert`) | — | — |

**Totaux au 2026-11-05 :** 10 composants purs, **130 tests host** (tous
`PASSE`), **12 175 vérifications** unitaires, couverture **96,8 %** sur
5 022 lignes de logique pure ; 43 vérifications matérielles dont **8 PASSE**,
**6 « mécanique vérifiée »** sur banc, **27 à jouer**, 2 reportées
(HW-TIME-02 en mars, HW-ALR-03 une nuit).

Les tests host sont exécutés **à chaque modification** (`run_host_tests.sh`),
les vérifications matérielles sont une **campagne** (§4).

---

## 3. Ce que chaque garde-fou du projet vérifie, et où

| Règle (CLAUDE.md) | Tests |
|---|---|
| **Honnêteté de l'affichage** : `--:--` sans heure sûre, `--` valeur absente/périmée, luminosité de repli nommée, air `--` + logo gris en préchauffage, `null` vers MQTT, règle sans source ⇒ `Indisponible` | AFF-11, TIME-07, HA-05 → HA-07, HA-12, LUM-11, AIR-02, AIR-03, AIR-18, AIR-19, MQ-04, MQ-05, ALR-04, ALR-05, AIR-20 |
| **Aucun acquittement** (EX-46) : passages toutes les `rappel_s`, retrait **par la règle** ou **par la source** seulement | ALR-06, ALR-07, ALR-13, ALR-18 (aucun champ `acquitt…`), HW-ALR-02, HW-ALR-04 |
| **Entrée non sûre** : tout ce qui vient du réseau est borné, rejeté plutôt que réparé | RES-01 → RES-10 (formulaire, DNS), HA-09, HA-10 (trames, JSON), ALR-15 (notification), WEB-02, WEB-03, MQ-08, MQ-09, ALR-08 (formulaires tout-ou-rien, `%ZZ` sur chaque champ) |
| **Aucune allocation après init** | GEN-04, AFF-17, ALR-16, MQ-06 (`AllocGuard`) |
| **Wizards = modèle = firmware** (une seule source de vérité) | WEB-04 (7 domaines), AIR-22, MQ-09, ALR-18 |
| **Plafond de courant** | AFF-05 → AFF-07, HW-AFF-01 |
| **Pas de flottant, unités entières** | build host `-Wdouble-promotion -Wfloat-equal -Werror` (GEN-02) |
| **Débordement 32 bits des compteurs ms** | AFF-14, LUM-09, RES-24, HA-11, ALR-17, MQ-06 |

---

## 4. Campagne matérielle finale (à jouer, dans l'ordre)

Chaque ligne renvoie à sa procédure détaillée dans `dev/test-plan-hw.md` §2.
Cocher et dater dans la matrice, pas ici.

### 4.1 Sur l'établi, une soirée (Nicolas)

1. **HW-WEB-06, HW-WEB-05** — couleurs (chiffres blancs, deux-points ambre,
   barre bleue, jour ambre / date blanche) et la **passe d'IP** une seule fois
   à la connexion. Un simple coup d'œil.
2. **HW-WEB-01** — la page sur le téléphone (clair et sombre), chaque onglet,
   un wizard au doigt.
3. **HW-AFF-02 → 05** — wizard de correspondance sur la dalle **finale**,
   chenillard, lisibilité à 3 m, motif plein avec le plafond de courant
   (mesurer le courant à la pince).
4. ~~**HW-SYS-01**~~ — joué le 2026-11-05 par l'API (décalage sans
   redémarrage) ; à refaire d'un clic sur la page si tu veux.
5. ~~**HW-WEB-03**~~ — joué le 2026-11-06 (image corrompue refusée, image
   de test non confirmée ⇒ retour à la précédente).
6. **HW-AIR-07** — capteur débranché à chaud : `--`, logo gris, alerte
   `CAPTEUR AIR HS` après 300 s ; rebranché : bus rétabli **sans redémarrage**.
7. **HW-AIR-01** — mise sous tension chronométrée : `--` pendant ~3 min,
   **aucun** `AERER` (règle `Indisponible` sur `/alertes`), puis valeurs ;
   **HW-MQ-02** en même temps avec le broker de banc (`null` puis valeurs,
   `prechauffage` ON puis OFF).
8. **HW-ALR-01** — règle `GEL` activée sur la valeur 1 (ou sur
   `AirTemperature` avec un seuil au-dessus de la pièce pour simuler) : levée
   **après** la confirmation, pas avant ; retombée seule avec l'hystérésis.

### 4.2 Avec le Home Assistant de la maison (Nicolas crée les accès)

9. **HW-HA-01** — utilisateur dédié, jeton longue durée, wizard `/ha` :
   `running`, valeur 1 = entité réelle ; **HW-HA-04** : une automatisation
   lance `horloge_notification` ⇒ rendu selon la priorité.
10. **HW-HA-02, HW-HA-03** — HA redémarré (reconnexion seule, valeurs
    retrouvées), jeton révoqué (`token_rejected`, pas de martèlement : 5 min).
11. **HW-MQ-01, HW-MQ-03** — Mosquitto + utilisateur MQTT dédié, wizard
    `/mqtt` : appareil « Horloge » et ses 10 entités **sans YAML** ; HA
    redémarré ⇒ découverte renvoyée ; horloge débranchée ⇒ `disponible`
    passe `offline` (testament).

### 4.3 Dans la durée

12. **HW-LUM-01 → 03** — wizard d'étalonnage une nuit, courbe relevée, pas de
    pompage au crépuscule.
13. **HW-ALR-03** — une nuit en heures silencieuses : aucun clignotement,
    alerte tenue visible au réveil.
14. **HW-RES-01 → 04** — coupure secteur (box + horloge), box éteinte 10 min,
    DHCP coupé, mot de passe changé : reprise seule, portail au bon moment,
    jamais de portail pendant la grâce.
15. **HW-AIR-03, 04, 05, 06** — solvant, pièce fermée puis fenêtre, écart
    d'auto-échauffement mesuré (déporter le capteur si > 2 °C), température et
    humidité confrontées à une référence.
16. **HW-TIME-02** — bascule d'heure d'été réelle (29 mars 2026).

### 4.4 Trous connus, à combler avant de parler de version 1.0

| Exigence | Manque | Proposition |
|---|---|---|
| EX-60 (watchdog) | *couvert le 2026-11-06* : HW-SYS-02, build de test `CLOCK_TEST_LOCKUP_S` | — |
| EX-61 (charge processeur) | *couvert le 2026-11-06* : HW-SYS-03, `/api/systeme/taches` | — |
| EX-03 (heure d'été réelle) | HW-TIME-02 en mars | couvert à la seconde sur host (TIME-02) |

---

## 5. Couverture des exigences

Chaque `EX-nn` de `dev/cadrage-fonctionnel.md` a **au moins un test** ; la table
exigence → tests est tenue dans `dev/test-plan-hw.md` §3 (elle cite les ids
ci-dessus). Les trois trous connus sont ceux du §4.4.

---

## 6. Critères de sortie de la phase 1

- [x] Tous les tests host `PASSE`, couverture ≥ 95 %, build host `-Werror`,
      build firmware sans avertissement dans `main/` et les composants projet.
- [x] Chaque domaine de réglage a son modèle, son formulaire tout-ou-rien, sa
      page **et** son wizard, vérifiés cohérents par un test (WEB-04).
- [x] Les règles d'honnêteté et « aucun acquittement » ont chacune leurs tests
      (§3) et ont été vues sur la carte (HW-ALR-04, HW-AIR-02, HW-MQ-01).
- [ ] Campagne §4.1 jouée et datée dans la matrice.
- [ ] Campagne §4.2 jouée avec le vrai Home Assistant.
- [ ] §4.3 au moins HW-LUM-01 → 03 et HW-RES-01 → 02 ; le reste peut suivre la
      mise en service.
- [ ] Licence choisie (backlog), dépôt poussé.

Une fois ces cases cochées, la phase 2 (liaison au hub `app-house`,
`spec-liaison-hub.md`) peut commencer ; rien n'y est implémenté.
