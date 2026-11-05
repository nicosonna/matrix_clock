#pragma once
/**
 * @file model.hpp
 * @brief Configuration model: every setting the clock remembers, validated and bounded.
 *
 * This is the **single source of truth** of the bounds exposed to the web page:
 * the model JSON (`json.hpp`) is generated from these types and the wizards read
 * it instead of copying numbers by hand.
 */

#include <cstddef>
#include <cstdint>

#include "air_quality/levels.hpp"
#include "air_quality/sensor.hpp"
#include "brightness/brightness.hpp"
#include "display/current_limit.hpp"
#include "display/panel_map.hpp"
#include "display/scheduler.hpp"
#include "display/scroll.hpp"

namespace settings {

/// Display domain (page /affichage).
struct DisplaySettings {
    display::PanelMapConfig map;
    uint16_t budget_ma;
    display::SceneList scenes;
    bool colon_blink;
    bool seconds_bar;
    uint16_t scroll_speed_px_s;
};

inline constexpr std::size_t kZoneNameMax = 31U;
inline constexpr std::size_t kHostMax = 63U;

/// System domain (page /systeme).
struct SystemSettings {
    char timezone[kZoneNameMax + 1U];  ///< IANA name from timekeeping::named_zones().
    char ntp_server[kHostMax + 1U];    ///< Host name or IPv4, [A-Za-z0-9.-].
};

/// How the air-quality logo is shown (spec-qualite-air.md §5).
enum class AirLogoMode : uint8_t {
    None = 0U,      ///< Nothing on the panel (page and rules only).
    IconZone,       ///< 6x6 logo next to the clock, coloured by level.
    Scene,          ///< Dedicated scene: 8x8 logo + pilot value.
};

/// Air-quality domain (page /air).
struct AirSettings {
    bool enabled;                      ///< false = sensor ignored even if present.
    uint8_t ens160_address;            ///< 0x52 or 0x53.
    air_quality::LevelConfig levels;   ///< Pilot quantity, thresholds, hysteresis, confirmation.
    AirLogoMode logo;
    int16_t temperature_offset_centi;  ///< Self-heating offset subtracted from the AHT21 (wizard).
};

/// Everything the clock remembers.
struct Settings {
    DisplaySettings display;
    brightness::Config brightness;
    SystemSettings system;
    AirSettings air;
};

DisplaySettings default_display_settings() noexcept;
SystemSettings default_system_settings() noexcept;
AirSettings default_air_settings() noexcept;
Settings default_settings() noexcept;

bool is_valid(const DisplaySettings& s) noexcept;
bool is_valid(const SystemSettings& s) noexcept;
bool is_valid(const AirSettings& s) noexcept;

/// @return true if @p host is a plausible host name or IPv4: [A-Za-z0-9.-], no edge dot/dash, 1..kHostMax.
bool is_valid_host(const char* host) noexcept;

/// The sensor loop configuration derived from the air settings.
air_quality::SensorConfig sensor_config(const AirSettings& s) noexcept;

}  // namespace settings
