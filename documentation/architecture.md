# Architecture — Horloge

> État au **2026-11-05** : squelette ESP-IDF, `clock_board`, `display` (pur :
> tampon, correspondance, gamma, plafond de courant, rendu, polices, texte,
> scènes Heure/Date, défilement, ordonnanceur, motifs), `brightness` (pur :
> filtres, courbe, contrôleur, wizard), `network` (pur : identifiants, DNS
> captif, superviseur avec grâce et bail), `alerts` (pur, décodeur de
> notification seulement), `timekeeping` (pur : règles POSIX, heure locale avec
> DST, validité — 99,1 % couvert ensemble), `clock_display` (vérifié sur la
> dalle), `clock_hal` (ADC1 de la LDR), `clock_web` (Wi-Fi, portail, HTTP,
> mDNS, canal de notification) et `clock_time` (SNTP) existent ; **le reste est
> spécifié, pas écrit**. Ce document décrit la
> cible et les choix arrêtés. Il se met à jour dès que la structure du dépôt bouge.

---

## 1. Vue d'ensemble

```
                      ┌──────────────────── ESP32-WROOM-32 ─────────────────────┐
                      │                                                         │
  LDR ──▶ GPIO33 ──▶ clock_hal ──▶ brightness (pure) ──┐                       │
                      │                                 │                       │
  ENS160+AHT21 ─ I²C ▶ clock_hal ──▶ air_quality (pure) ──▶ 5 grandeurs + niveau ──┤   │
                      │                                 │                       │
  SNTP ◀── WiFi ──▶ clock_time ──▶ {heure, sure} ─────┤                       │
                      │                                 ▼                       │
  box ◀── WiFi ──▶ network (pure) ──▶ plan ──▶ clock_web ┤                       │
                      │                   (portail, page, API)                  │
                      │                                 │                       │
  Home Assistant ◀══ wss ══▶ clock_homeassistant ──▶ homeassistant (pure) ──▶ 8 valeurs │
  Home Assistant ◀══ mqtt ══ clock_homeassistant ◀── homeassistant::mqtt ◀── air    │
                      │                                 │                       │
                      │          POST /api/notification ─┤                       │
                      │                                 ▼                       │
                      │                        alerts (pure) ──▶ notifications  │
                      │                                 │                       │
                      │                                 ▼                       │
                      │                        display (pure)                  │
                      │                      (scènes, tampon 8×32,              │
                      │                       gamma, plafond courant)            │
                      │                                 │                       │
                      │                        clock_display ──▶ GPIO13 ──▶ dalle WS2812B
                      │                                                         │
                      │  settings (pure) ◀──── NVS ────▶ tous les domaines       │
                      └─────────────────────────────────────────────────────────┘
```

Le sens de lecture : **tout converge vers `display`**, qui est le seul module
qui écrit sur la dalle, et **rien ne bloque sa boucle**.

---

## 2. Séparation logique pure / matériel

C'est le choix structurant, repris de ThermoSync : la logique métier ne dépend
**jamais** d'ESP-IDF. Elle reçoit des mesures (avec drapeau de validité) et rend
des décisions ; les dépendances actives passent par une **interface injectée**,
implémentée par un composant `clock_*` avec ESP-IDF et par une **doublure**
dans `firmware/test/`.

| Composant pur | Ne dépend que de | Testé sur host |
|---|---|---|
| `display` | `<array>`, `<cstdint>` | oui, ≥ 95 % |
| `brightness` | idem | oui |
| `network` | idem (repris de ThermoSync) | oui |
| `homeassistant` (`ws_client` + `mqtt`) | idem | oui, transports moqués ; `mqtt` repris de ThermoSync |
| `alerts` | idem | oui |
| `air_quality` | idem, bus I²C moqué | oui |
| `settings` | idem | oui |

Vérifié par le build host : ces composants compilent avec un **g++ nu**. Un
`#include "esp_log.h"` qui s'y glisse casse les tests, pas la cible — c'est
volontaire.

| Composant ESP-IDF | Rôle | Exclu de la couverture |
|---|---|---|
| `clock_board` | brochage `constexpr`, Kconfig | oui (constantes) |
| `clock_display` | `espressif/led_strip`, SPI/DMA | oui (matériel) |
| `clock_hal` | ADC1 oneshot + étalonnage ; bus I²C `i2c_master` derrière `air_quality::I2cBus` (délai 50 ms, réinitialisation d'un bus bloqué par 9 coups d'horloge) | oui (matériel) |
| `clock_time` | SNTP ; la conversion et la validité sont dans `timekeeping` (pur) | oui (transport) |
| `clock_homeassistant` | tâche `ha` (cœur 0, 100 ms) : `esp_websocket_client` sans reconnexion automatique (le client pur décide), réassemblage, envoi des messages demandés, météo Open-Meteo en HTTPS (paquet de certificats), snapshot sous mutex ; tâche `mqtt` (cœur 0, 250 ms) : `esp-mqtt` (testament `offline` retenu, keepalive 30 s, reconnexion par le client) exécutant les actions du `MqttPublisher` pur — découverte QoS 1 retenue, état QoS 0 retenu, renvoi sur `<prefixe>/status` | oui (transport) |
| `clock_web` | `esp_http_server`, page embarquée, DNS captif, mDNS | oui (transport) |
| `clock_settings` | blobs NVS versionnés par domaine, copie partagée sous mutex, compteur de version scruté par la tâche d'affichage (application à chaud) | oui (NVS, FreeRTOS) |
| `main/app_main.cpp` | câblage | oui |

---

## 3. Tâches et cadences

| Tâche | Priorité | Cadence | Ce qu'elle fait |
|---|---|---|---|
| `display` | **la plus haute** des tâches applicatives | 25 Hz (40 ms) | Compose la scène depuis le **dernier état publié**, applique gamma et plafond de courant, pousse la trame. **Aucune attente réseau, aucun verrou long.** |
| `periodique` | moyenne | 1 Hz | Lit l'ADC et le bus I²C (AHT21 puis ENS160, ~100 ms), fait avancer `brightness`, `air_quality`, `network`, `alerts`, publie l'état. L'I²C ne vit **jamais** dans la tâche d'affichage. |
| `homeassistant` | basse | événementielle | WebSocket Home Assistant, météo de repli. |
| `mqtt` | basse | 250 ms | Lit l'état air / lumière / heure, fait avancer `MqttPublisher`, publie par `esp-mqtt`. |
| `web` | **la plus basse** | événementielle | HTTP, DNS captif. |

Les états partagés sont publiés par **copie atomique d'une structure simple**
(double tampon + indice, pas de mutex sur le chemin d'affichage) : une requête
HTTP lente ou une reconnexion WebSocket ne doit jamais faire sauter une trame
(EX-61).

---

## 4. Persistance (NVS)

| Espace | Contenu | Écrit par |
|---|---|---|
| `clk_wifi` | SSID, phrase de passe | portail |
| `clk_display` | correspondance, scènes, budget, séparateur | `/affichage` |
| `clk_bright` | mode, courbe, bornes, compensation, extinction | `/luminosite` + wizard |
| `clk_ha` | schéma, hôte, port, **jeton**, 8 valeurs, météo | `/ha` |
| `clk_alerts` | 16 règles, heures silencieuses | `/alertes` |
| `clk_air` | adresse ENS160, grandeur pilote, seuils, couleurs, textes, logo, décalages | `/air` + wizard |
| `clk_mqtt` | broker, port, utilisateur, **mot de passe**, préfixe, période (`MqttConfig` de ThermoSync) | `/mqtt` |
| `clk_sys` | fuseau, serveurs SNTP, `maj_auto` | `/systeme` |
| `clk_hub` | *(phase 2)* `appareil_id`, jeton, **compteur d'`id` monotone** | liaison hub |

Chaque espace porte un **numéro de version de schéma**. Une version inconnue ou
une validation en échec ⇒ **valeurs par défaut** + drapeau `config_remplacee`
visible sur la page. On ne tente pas de rattraper une config corrompue.

**Aucun secret dans le dépôt.** Le jeton Home Assistant s'écrit et ne se relit
pas (EX-34).

---

## 5. Choix et leurs raisons

| Choix | Raison | Alternative écartée |
|---|---|---|
| WebSocket Home Assistant, abonnements `render_template` | Valeur initiale **et** poussée sur changement, messages minuscules, zéro configuration côté Home Assistant | `get_states` (plusieurs centaines de Ko, impossible sans PSRAM) ; REST (scrutation) ; MQTT (demande `mqtt_statestream`) |
| Règles d'alerte **dans** l'horloge | Elle doit alerter même si Home Assistant et le hub sont tombés | Tout décider à l'extérieur (écran muet sans réseau) |
| Dorsale **SPI avec DMA** pour les LEDs | Le RMT de l'ESP32 classique n'a pas de DMA : 7,7 ms par trame entièrement en interruptions, en concurrence avec le WiFi | RMT (reste sélectionnable par Kconfig) |
| **Plafond de courant** dans le firmware | 256 LEDs en blanc plein = 15,6 A. Ce n'est pas du confort, c'est ce qui protège l'alimentation | Faire confiance au réglage de luminosité |
| Correspondance du panneau **réglable** + wizard | Les dalles 8×32 du commerce ne sont pas câblées pareil, et on ne le saura qu'en allumant | Coder un câblage en dur |
| **Fenêtre de grâce** au démarrage | La box redémarre plus lentement que la carte : sans grâce, l'horloge ouvre son portail après chaque coupure de courant | Comportement ThermoSync tel quel |
| `BailAbsent` ne mène pas au portail | S'associer **prouve** que les identifiants sont bons : le portail ne servirait à rien | Compter un échec DHCP comme un échec de réseau |
| Composant `network` **repris**, pas réécrit | Déjà pur, déjà testé, comportement déjà éprouvé sur la borne | Réécriture |
| Lecteur JSON **dans** `homeassistant` | ESP-IDF a déjà un composant `json` (cJSON) qu'un homonyme projet remplacerait silencieusement — même piège que `hal` | Composant `json` séparé |
| MQTT **pour publier seulement** (capteurs locaux, découverte), WebSocket pour lire | Comme la borne ; le composant pur existe déjà dans ThermoSync ; l'horloge devient un appareil dans Home Assistant | Lire aussi par MQTT (`mqtt_statestream`) ; ne rien publier (décision du matin, révisée à la demande de Nicolas) |

---

## 6. Dette connue et limitations assumées

| # | Limitation | Conséquence | Suite |
|---|---|---|---|
| D1 | Phrase de passe du point d'accès **dérivée de la MAC** | Devinable par qui connaît l'algorithme et lit le BSSID | La rendre réglable (todo, Q5) |
| D2 | **Aucune authentification** sur la page web et sur l'OTA | Quiconque sur le réseau local peut tout régler et flasher | Mot de passe d'administration en phase 3 (todo, Q6) |
| D3 | Jeton Home Assistant **en clair** en `ws` | Lisible sur le réseau local | `wss` (déjà prévu), utilisateur Home Assistant dédié |
| D4 | **Pas d'accents** sur 8 lignes | Textes translittérés et en capitales | Police dédiée en phase 3 (Q7) |
| D5 | **Pas de RTC** | Après une coupure sans réseau : `--:--` | DS3231 en phase 3 (Q2) ; la capacité `heure` du hub est une autre voie |
| D6 | `render_template` ne rend que sur **changement** | Une valeur stable est indiscernable d'une liaison morte ⇒ la péremption ne s'applique que si la **liaison** est tombée | Figer à l'implémentation (HA-12) |
| D7 | Médiane sur 5 : **deux** pics consécutifs passent | Un flash prolongé peut faire monter la luminosité | Accepté, documenté (LUM-01) |
| D8 | **Pas de résolution mDNS** | L'hôte Home Assistant doit être donné en **IP**, pas en `.local` | Comme ThermoSync ; todo |
| D9 | Pas de bouton physique **décidé** | Aucun portail forcé hors page web (il ne servirait pas à acquitter : EX-46) | Q1 du cadrage |
| D10 | **Auto-échauffement** du module ENS160 + AHT21 par la dalle | Température trop haute, humidité trop basse, et l'erreur se propage à l'AQI par la compensation | Déporter le capteur (Q10), décalage mesuré par le wizard, visible sur la page |
| D11 | Table des registres ENS160 / AHT21 **non vérifiée** sur le module | Risque de décodage faux | Lire `PART_ID` et confronter la fiche technique en première tâche du composant `air_quality` |

---

## 7. Flux en une phrase chacun

- **Heure** : SNTP → `{heure, sure}` → scène `Heure`, ou `--:--` si `sure` est faux.
- **Luminosité** : ADC1 → médiane → moyenne asymétrique → compensation → courbe
  → hystérésis → échelle globale de `display`.
- **Qualité de l'air** : AHT21 (CRC8) → compensation écrite dans l'ENS160 →
  `VALIDITY_FLAG` → 5 grandeurs valides ou `--` → niveau (hystérésis,
  confirmation) → logo coloré dans la zone d'icônes, et source de règle.
- **Publication MQTT** : grandeurs de `air_quality` (+ lumière, heure sûre) → état JSON
  unique toutes les 30 s et sur changement de niveau, `null` pour l'invalide →
  broker → entités découvertes dans Home Assistant.
- **Valeur Home Assistant** : `event render_template` → décodage typé → valeur +
  fraîcheur → scène `Valeur` **ou** `--`, et source de règle.
- **Notification** : événement Home Assistant / `POST /api/notification` /
  `commande notifier` *(phase 2)* → **même décodeur** → `FileNotifications` →
  interruption de scène selon la priorité.
- **Alerte locale** : valeur + règle (hystérésis, confirmation) → notification de
  clé `regle_<n>` → même file.
- **Réseau** : événements WiFi → `EtatLien` + `CauseEchec` → `network` → plan
  (point d'accès, portail, station) → appliqué par `clock_web`.
