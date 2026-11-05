# firmware/ — ESP-IDF project

## Build (PowerShell, not Git Bash — `idf.py` refuses MSys)

```powershell
. "$HOME\esp\esp-idf\export.ps1"
idf.py -C firmware -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.horloge-v1" build
```

The overlay `sdkconfig.defaults.horloge-v1` carries the per-board settings
(flash size, board profile). `sdkconfig` itself is generated and git-ignored.

## Flash and monitor

Several boards are usually plugged in (COM15 is the ThermoSync ESP32-S3: never
flash it from here). **Always pass the port**:

```powershell
idf.py -C firmware -p COM21 flash monitor
```

Check the port first — Windows renumbers them:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

## Host tests (Git Bash)

```bash
bash firmware/test/run_host_tests.sh
bash firmware/test/run_host_tests.sh --coverage   # fails below 95 %
```

## Layout

| Path | Role |
|---|---|
| `components/display/` | pure display logic (frame buffer, panel mapping, gamma, current limit, render) — no ESP-IDF |
| `components/clock_board/` | pin profiles (`constexpr`), chosen by Kconfig |
| `components/clock_display/` | WS2812B driver on `espressif/led_strip` (SPI/DMA, RMT fallback) |
| `main/app_main.cpp` | wiring; today: commissioning test patterns |
| `test/` | host harness (CMake + g++), coverage script |
| `tools/gen_gamma.py` | regenerates the gamma table in `display/gamma.hpp` |
