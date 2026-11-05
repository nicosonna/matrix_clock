#pragma once
/**
 * @file aht21.hpp
 * @brief AHT21 (ASAIR) temperature / humidity sensor: commands and decoding.
 *
 * Integer units only: temperature in centi-degrees Celsius, relative humidity
 * in tenths of a percent. A frame with a wrong CRC is **rejected**, never
 * repaired (AIR-07).
 */

#include <cstddef>
#include <cstdint>

namespace air_quality {

/// 7-bit address of the AHT21.
inline constexpr uint8_t kAht21Address = 0x38U;
/// Status request (one byte read back).
inline constexpr uint8_t kAht21StatusCommand = 0x71U;
/// Initialisation / calibration command.
inline constexpr uint8_t kAht21InitCommand[3] = {0xBEU, 0x08U, 0x00U};
/// Trigger a measurement; the result is ready after kAht21MeasureDelayMs.
inline constexpr uint8_t kAht21MeasureCommand[3] = {0xACU, 0x33U, 0x00U};
inline constexpr uint32_t kAht21MeasureDelayMs = 80U;
inline constexpr uint32_t kAht21InitDelayMs = 10U;
/// Measurement frame: status, humidity (20 bits), temperature (20 bits), CRC8.
inline constexpr std::size_t kAht21FrameSize = 7U;
/// Status bit 7: measurement still running.
inline constexpr uint8_t kAht21StatusBusy = 0x80U;
/// Status bit 3: calibrated.
inline constexpr uint8_t kAht21StatusCalibrated = 0x08U;

/// A decoded measurement.
struct Aht21Reading {
    int16_t temperature_centi;  ///< -5000..15000 (centi-degrees Celsius).
    uint16_t humidity_pct10;    ///< 0..1000 (tenths of a percent).
};

enum class Aht21Status : uint8_t {
    Ok = 0U,
    Busy,           ///< Measurement not finished: read again later.
    CrcError,       ///< Frame corrupted: rejected.
    NotCalibrated,  ///< Calibration bit clear: send the init command again.
};

/**
 * @brief CRC-8 of the AHT21 (polynomial 0x31, init 0xFF, no reflection).
 * @param data Bytes to check.
 * @param len Number of bytes.
 * @return The CRC.
 */
uint8_t aht21_crc8(const uint8_t* data, std::size_t len) noexcept;

/**
 * @brief Decodes a 7-byte frame.
 * @param frame The bytes read after the measurement delay.
 * @param out Filled when the status is Ok.
 * @return The status of the frame.
 */
Aht21Status aht21_decode(const uint8_t frame[kAht21FrameSize], Aht21Reading& out) noexcept;

}  // namespace air_quality
