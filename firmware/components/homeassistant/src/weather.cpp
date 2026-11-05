/**
 * @file weather.cpp
 * @brief Open-Meteo fallback.
 */
#include "homeassistant/weather.hpp"

#include <cstdio>

#include "homeassistant/json_reader.hpp"

namespace homeassistant {

namespace {
constexpr int32_t kTempMinDeci = -1000;
constexpr int32_t kTempMaxDeci = 700;
}  // namespace

WeatherConfig default_weather_config() noexcept {
    WeatherConfig c{};
    c.enabled = false;
    c.latitude_e4 = 0;
    c.longitude_e4 = 0;
    c.period_s = kWeatherPeriodDefaultS;
    return c;
}

bool is_valid(const WeatherConfig& c) noexcept {
    return (c.latitude_e4 >= -kLatitudeMax) && (c.latitude_e4 <= kLatitudeMax) && (c.longitude_e4 >= -kLongitudeMax) &&
           (c.longitude_e4 <= kLongitudeMax) && (c.period_s >= kWeatherPeriodMinS) && (c.period_s <= kWeatherPeriodMaxS);
}

std::size_t weather_url(const WeatherConfig& c, char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (size == 0U) || !is_valid(c)) {
        return 0U;
    }
    const int32_t lat_abs = (c.latitude_e4 < 0) ? -c.latitude_e4 : c.latitude_e4;
    const int32_t lon_abs = (c.longitude_e4 < 0) ? -c.longitude_e4 : c.longitude_e4;
    const int n = std::snprintf(out, size,
                                "https://api.open-meteo.com/v1/forecast?latitude=%s%ld.%04ld&longitude=%s%ld.%04ld"
                                "&current=temperature_2m",
                                (c.latitude_e4 < 0) ? "-" : "", static_cast<long>(lat_abs / 10000),
                                static_cast<long>(lat_abs % 10000), (c.longitude_e4 < 0) ? "-" : "",
                                static_cast<long>(lon_abs / 10000), static_cast<long>(lon_abs % 10000));
    if ((n <= 0) || (static_cast<std::size_t>(n) >= size)) {
        out[0] = '\0';
        return 0U;
    }
    return static_cast<std::size_t>(n);
}

bool decode_weather(const char* json, std::size_t len, int16_t& temperature_deci) noexcept {
    const char* const path[] = {"current", "temperature_2m"};
    JsonSpan v{};
    if (!json_find(json, len, path, 2U, v) || json_is_null(v)) {
        return false;
    }
    int32_t deci = 0;
    if (!json_to_fixed(v, 1U, deci) || (deci < kTempMinDeci) || (deci > kTempMaxDeci)) {
        return false;
    }
    temperature_deci = static_cast<int16_t>(deci);
    return true;
}

}  // namespace homeassistant
