#pragma once
/**
 * @file model.hpp
 * @brief The settings of the clock, by domain, with defaults and validation.
 *
 * Every bound lives here (or in the component that owns the type, such as
 * brightness::Config) and nowhere else: the web page reads them from the
 * model JSON, the form decoder enforces them, NVS stores what passed them.
 */

#include <cstddef>
#include <cstdint>

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

/// Everything the clock remembers.
struct Settings {
    DisplaySettings display;
    brightness::Config brightness;
    SystemSettings system;
};

DisplaySettings default_display_settings() noexcept;
SystemSettings default_system_settings() noexcept;
Settings default_settings() noexcept;

bool is_valid(const DisplaySettings& s) noexcept;
bool is_valid(const SystemSettings& s) noexcept;

/// @return true if @p host is a plausible host name or IPv4: [A-Za-z0-9.-], no edge dot/dash, 1..kHostMax.
bool is_valid_host(const char* host) noexcept;

}  // namespace settings
