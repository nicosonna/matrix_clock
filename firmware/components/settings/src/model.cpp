/**
 * @file model.cpp
 * @brief Defaults and validation.
 */
#include "settings/model.hpp"

#include <cstring>

#include "timekeeping/tz.hpp"

namespace settings {

DisplaySettings default_display_settings() noexcept {
    DisplaySettings d{};
    d.map = display::default_panel_map();
    d.budget_ma = display::kDefaultBudgetMa;
    d.scenes = display::default_scene_list();
    d.colon_blink = true;
    d.seconds_bar = true;
    d.scroll_speed_px_s = display::kScrollSpeedDefault;
    return d;
}

SystemSettings default_system_settings() noexcept {
    SystemSettings s{};
    std::strncpy(s.timezone, timekeeping::kDefaultZoneName, kZoneNameMax);
    std::strncpy(s.ntp_server, "pool.ntp.org", kHostMax);
    return s;
}

Settings default_settings() noexcept {
    Settings s{};
    s.display = default_display_settings();
    s.brightness = brightness::default_config();
    s.system = default_system_settings();
    return s;
}

bool is_valid(const DisplaySettings& s) noexcept {
    return display::is_valid(s.map) && display::is_valid_budget_ma(s.budget_ma) && display::is_valid(s.scenes) &&
           (s.scroll_speed_px_s >= display::kScrollSpeedMin) && (s.scroll_speed_px_s <= display::kScrollSpeedMax);
}

bool is_valid_host(const char* host) noexcept {
    if (host == nullptr) {
        return false;
    }
    std::size_t n = 0U;
    for (; host[n] != '\0'; ++n) {
        const char c = host[n];
        const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) ||
                        (c == '.') || (c == '-');
        if (!ok || (n >= kHostMax)) {
            return false;
        }
    }
    if (n == 0U) {
        return false;
    }
    const char first = host[0];
    const char last = host[n - 1U];
    return (first != '.') && (first != '-') && (last != '.') && (last != '-');
}

bool is_valid(const SystemSettings& s) noexcept {
    return (timekeeping::posix_for_zone(s.timezone) != nullptr) && is_valid_host(s.ntp_server);
}

}  // namespace settings
