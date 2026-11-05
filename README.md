# matrix_clock — connected LED matrix clock (ESP-IDF)

Repository: `github.com/nicosonna/matrix_clock` (the project is called
*Horloge* in its French documentation).

A living-room clock on a **WS2812B 8×32 LED matrix**, driven by an
**ESP32-WROOM-32** running **ESP-IDF** (no Arduino, no PlatformIO), written in
C++17 with a safety-oriented coding standard.

> **Status (2026-11-05): specifications only — no firmware code yet.**
> The documentation describes the target, not the current state. See
> `documentation/dev/journal.md` and `documentation/dev/todo.md`.

## What it does (target)

- **Big, honest time display** — `HH:MM` in a 5×7 font, SNTP-synchronised,
  automatic DST. Until the first successful sync it shows `--:--`, never a wrong
  time.
- **Automatic brightness** from a photoresistor, with slow asymmetric filtering
  (no pumping), a night floor, and a calibration wizard that measures how much
  the matrix itself illuminates the sensor.
- **Current limiting in firmware** — 256 LEDs at full white draw ~15 A; the
  frame buffer is scaled to a configurable current budget on every frame.
- **Home Assistant, two ways** — reads up to 8 entities over the **WebSocket
  API** (`render_template` subscriptions: initial value + push on change), and
  **publishes its own sensors over MQTT** with discovery, so the clock shows up
  as a device.
- **Air quality** — ENS160 + AHT21 (I²C): AQI, TVOC, eCO₂, temperature,
  humidity. The ENS160 validity flag is honoured: during warm-up the values are
  *unknown*, not invented. Optional air icon that changes colour with the level;
  "AERER" / "AIR MAUVAIS" alerts.
- **Alerts** — a local rule engine (threshold, hysteresis, confirmation time,
  priority, quiet hours) plus an inbound notification channel (Home Assistant
  event or local HTTP POST). **No acknowledgement**: an alert keeps coming back
  as long as its condition holds and clears itself when it no longer does.
- **Captive-portal provisioning** — first boot opens an access point and a
  captive portal; afterwards the clock reconnects on its own after a power cut,
  with a start-up grace window so a slow-booting router doesn't trigger the
  portal, and DHCP-less association never does.
- **Self-contained web UI** (no CDN, light/dark, a step-by-step wizard per
  settings domain), OTA with rollback.

## Hardware

| Function | Pin | Notes |
|---|---|---|
| WS2812B data | GPIO13 | `led_strip` SPI backend (DMA); level shifter recommended |
| Photoresistor | GPIO33 | ADC1_CH5 — ADC1 works with Wi-Fi on |
| ENS160 + AHT21 | GPIO27 (SDA) / GPIO26 (SCL) | I²C, `0x53` + `0x38`, mount it away from the LED heat |

Power: 5 V / 5 A minimum, injected at both ends of the panel. Details, pitfalls
and the current budget are in `documentation/cartes/horloge-v1/README.md`.

## Repository layout

```
CLAUDE.md                     project rules (French)
documentation/
  architecture.md             modules, flows, decisions, known debt
  gardefous-dev.md            development standard
  cartes/horloge-v1/          hardware sheet
  dev/
    cadrage-fonctionnel.md    requirements (EX-nn), phases, open questions
    spec-*.md                 one spec per domain
    test-plan-hw.md           traceability matrix (host tests + hardware checks)
    plan-de-test-final.md     final test plan: per-spec coverage, bench campaign, exit criteria
    journal.md, todo.md       living history and backlog
firmware/                     ESP-IDF project (components/, main/, test/ host tests and bench tools)
```

## Languages

Code, comments, Doxygen, logs and commit messages are in **English**.
The embedded web UI and the design documentation are in **French**.

## Building and testing

```
. $HOME/esp/esp-idf/export.ps1      # ESP-IDF v5.3.x, PowerShell on Windows
idf.py -C firmware build
bash firmware/test/run_host_tests.sh --coverage   # pure logic, on the PC, ≥ 95 %
```

Status (2026-11-05): phase 1 implemented and running on the board — display,
automatic brightness, captive portal with power-cut recovery, SNTP with local
DST rules, settings pages with wizards, OTA, ENS160 + AHT21 air quality, Home
Assistant WebSocket link, MQTT discovery publication, local alert rules with a
bounded notification queue and quiet hours. 130 host tests pass, 96.8 %
coverage. The remaining hardware campaign is listed in
`documentation/dev/plan-de-test-final.md`.

## Engineering rules in one paragraph

Pure logic (display, brightness, network supervisor, Home Assistant client,
alerts, air quality, settings) has **no ESP-IDF dependency** and is tested on
the host with ≥ 95 % coverage. No dynamic allocation after init, no exceptions,
no RTTI, no recursion, `std::array` only, explicit integer units (centi-°C, mA,
ms), every `esp_err_t` checked. Anything coming from the network is untrusted
input: bounded, allocation-free decoding, rejected rather than repaired. The
display never shows a value it cannot vouch for.

## License

To be chosen before the first public release.
