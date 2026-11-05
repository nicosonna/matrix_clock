#pragma once
/**
 * @file ens160.hpp
 * @brief ENS160 (ScioSense) air-quality sensor: registers, encoding, decoding.
 *
 * The register map is the one of `dev/spec-qualite-air.md` §2, to be confirmed
 * on the real module (debt D11): `PART_ID` is checked at start, a mismatch
 * means "absent", never "probably fine".
 */

#include <cstddef>
#include <cstdint>

namespace air_quality {

inline constexpr uint8_t kEns160AddressHigh = 0x53U;  ///< ADDR pin high (module default).
inline constexpr uint8_t kEns160AddressLow = 0x52U;   ///< ADDR pin low.
inline constexpr uint16_t kEns160PartId = 0x0160U;

/// Registers.
inline constexpr uint8_t kEns160RegPartId = 0x00U;        ///< 16 bits LE.
inline constexpr uint8_t kEns160RegOpMode = 0x10U;
inline constexpr uint8_t kEns160RegConfig = 0x11U;
inline constexpr uint8_t kEns160RegTempIn = 0x13U;        ///< 16 bits LE, Kelvin x 64.
inline constexpr uint8_t kEns160RegRhIn = 0x15U;          ///< 16 bits LE, % x 512.
inline constexpr uint8_t kEns160RegDeviceStatus = 0x20U;
inline constexpr uint8_t kEns160RegDataAqi = 0x21U;
inline constexpr uint8_t kEns160RegDataTvoc = 0x22U;      ///< 16 bits LE, ppb.
inline constexpr uint8_t kEns160RegDataEco2 = 0x24U;      ///< 16 bits LE, ppm.

/// Operating modes.
inline constexpr uint8_t kEns160OpModeSleep = 0x00U;
inline constexpr uint8_t kEns160OpModeIdle = 0x01U;
inline constexpr uint8_t kEns160OpModeStandard = 0x02U;
inline constexpr uint8_t kEns160OpModeReset = 0xF0U;

/// DEVICE_STATUS bits.
inline constexpr uint8_t kEns160StatusError = 0x40U;    ///< STATER.
inline constexpr uint8_t kEns160StatusNewData = 0x02U;  ///< NEWDAT.
inline constexpr uint8_t kEns160StatusValidityShift = 2U;
inline constexpr uint8_t kEns160StatusValidityMask = 0x0CU;

/// Meaning of VALIDITY_FLAG — the heart of the honesty rule for this sensor.
enum class Ens160Validity : uint8_t {
    Normal = 0U,       ///< Values are usable.
    WarmUp = 1U,       ///< ~3 min after power-up: values **invalid**.
    InitialStart = 2U, ///< ~1 h cumulated at first use: values **invalid**.
    Invalid = 3U,      ///< Sensor fault.
};

/// Decoded DEVICE_STATUS.
struct Ens160Status {
    Ens160Validity validity;
    bool new_data;
    bool error;
};

/// Decoded data block (AQI, TVOC, eCO2: 5 bytes from DATA_AQI).
struct Ens160Data {
    uint8_t aqi;        ///< UBA index 1..5 (0 if the sensor returned 0).
    uint16_t tvoc_ppb;
    uint16_t eco2_ppm;
};

/// Size of the data block read in one go from DATA_AQI.
inline constexpr std::size_t kEns160DataSize = 5U;
/// Size of the compensation block written in one go from TEMP_IN (register + 4 bytes).
inline constexpr std::size_t kEns160CompensationSize = 5U;

/// Compensation used when the AHT21 is in fault (named fallback, flagged).
inline constexpr int16_t kFallbackTemperatureCenti = 2500;
inline constexpr uint16_t kFallbackHumidityPct10 = 500U;

/// @return true if the two PART_ID bytes (LE) identify an ENS160.
bool ens160_part_id_ok(const uint8_t bytes[2]) noexcept;

/// Decodes DEVICE_STATUS.
Ens160Status ens160_decode_status(uint8_t status) noexcept;

/// Decodes the 5-byte data block (LE words).
Ens160Data ens160_decode_data(const uint8_t bytes[kEns160DataSize]) noexcept;

/**
 * @brief Encodes the compensation write: register address then TEMP_IN and RH_IN (LE).
 * @param temperature_centi Clamped to -4000..8500 before encoding (sensor range).
 * @param humidity_pct10 Clamped to 0..1000.
 * @param out 5 bytes: {0x13, t_lo, t_hi, rh_lo, rh_hi}.
 */
void ens160_encode_compensation(int16_t temperature_centi, uint16_t humidity_pct10,
                                uint8_t out[kEns160CompensationSize]) noexcept;

}  // namespace air_quality
