#pragma once
/**
 * @file weather.hpp
 * @brief Open-Meteo fallback: request URL and response decoding (EX-35).
 *
 * The only Internet access of the firmware, off by default. A failed or
 * incomplete answer leaves the value invalid, never the previous one shown as
 * fresh.
 */

#include <cstddef>
#include <cstdint>

namespace homeassistant {

inline constexpr int32_t kLatitudeMax = 900000;    ///< 1/10000 degree.
inline constexpr int32_t kLongitudeMax = 1800000;
inline constexpr uint16_t kWeatherPeriodMinS = 300U;
inline constexpr uint16_t kWeatherPeriodMaxS = 3600U;
inline constexpr uint16_t kWeatherPeriodDefaultS = 600U;

struct WeatherConfig {
    bool enabled;
    int32_t latitude_e4;   ///< Degrees × 10000.
    int32_t longitude_e4;
    uint16_t period_s;
};

WeatherConfig default_weather_config() noexcept;
bool is_valid(const WeatherConfig& c) noexcept;

/// Builds `https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..&current=temperature_2m`.
std::size_t weather_url(const WeatherConfig& c, char* out, std::size_t size) noexcept;

/**
 * @brief Decodes `current.temperature_2m` into deci-degrees.
 * @return false if absent, null, malformed or out of -100..+70 °C.
 */
bool decode_weather(const char* json, std::size_t len, int16_t& temperature_deci) noexcept;

}  // namespace homeassistant
