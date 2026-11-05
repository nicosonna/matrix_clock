#pragma once
/**
 * @file board_horloge_v1.hpp
 * @brief Pin profile of the horloge-v1 board (ESP32-WROOM-32 devkit, 4 MB, no PSRAM).
 *
 * Source of truth: documentation/cartes/horloge-v1/README.md. Change the sheet
 * first, then this file.
 */

#include <cstdint>

namespace clock_board {

/// Human-readable board name (logs, web page, Home Assistant device model).
inline constexpr char kName[] = "horloge-v1";

/// WS2812B data line. SPI2 MOSI by default; the SPI backend routes through the
/// GPIO matrix, so any free output pin would do. Series resistor 330-470 ohm,
/// 74AHCT125 level shifter recommended (3.3 V output vs 3.5 V Vih at 5 V).
inline constexpr uint8_t kLedDataGpio = 13U;

/// Photoresistor divider: ADC1_CH5. ADC1 on purpose — ADC2 is unusable while
/// Wi-Fi is active on the ESP32.
inline constexpr uint8_t kLdrAdcGpio = 33U;

/// I2C bus shared by the ENS160 + AHT21 module (0x53 / 0x38) and, in phase 3,
/// a DS3231 RTC (0x68). Wired on the bench on GPIO27 / GPIO26 (2026-11-05);
/// both are ADC2 pins, which is irrelevant for a digital bus.
inline constexpr uint8_t kI2cSdaGpio = 27U;
inline constexpr uint8_t kI2cSclGpio = 26U;

/// Optional push button (open question Q1 of the functional scope, not decided).
/// GPIO25: no strapping role, no boot-time output, free once I2C took 26/27.
inline constexpr uint8_t kButtonGpio = 25U;

}  // namespace clock_board
