/**
 * @file model.cpp
 * @brief Defaults and validation.
 */
#include "settings/model.hpp"

#include <cstdio>
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

AirSettings default_air_settings() noexcept {
    AirSettings a{};
    a.enabled = true;
    a.ens160_address = air_quality::kEns160AddressHigh;
    a.levels = air_quality::default_level_config();
    a.logo = AirLogoMode::IconZone;
    a.temperature_offset_centi = 0;
    return a;
}

void default_ha_settings_into(HaSettings& h) noexcept {
    std::memset(&h, 0, sizeof(h));
    h.enabled = false;
    h.secure = false;
    h.host[0] = '\0';
    h.port = kHaPortDefault;
    h.token[0] = '\0';
    h.notifications = true;
    h.ping_s = homeassistant::kPingDefaultS;
    h.pong_ms = homeassistant::kPongDefaultMs;
    for (homeassistant::ValueConfig& v : h.values) {
        v = homeassistant::default_value_config();
    }
    h.weather = homeassistant::default_weather_config();
}

HaSettings default_ha_settings() noexcept {
    HaSettings h;
    default_ha_settings_into(h);
    return h;
}

void default_settings_into(Settings& s) noexcept {
    s.display = default_display_settings();
    s.brightness = brightness::default_config();
    s.system = default_system_settings();
    s.air = default_air_settings();
    default_ha_settings_into(s.ha);
}

Settings default_settings() noexcept {
    Settings s;
    default_settings_into(s);
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

bool is_valid(const AirSettings& s) noexcept {
    const bool logo_ok = (s.logo == AirLogoMode::None) || (s.logo == AirLogoMode::IconZone) || (s.logo == AirLogoMode::Scene);
    return logo_ok && air_quality::is_valid(s.levels) && air_quality::is_valid(sensor_config(s));
}

air_quality::SensorConfig sensor_config(const AirSettings& s) noexcept {
    air_quality::SensorConfig c = air_quality::default_sensor_config();
    c.ens160_address = s.ens160_address;
    c.temperature_offset_centi = s.temperature_offset_centi;
    return c;
}

homeassistant::WsConfig ws_config(const HaSettings& s) noexcept {
    homeassistant::WsConfig c = homeassistant::default_ws_config();
    c.enabled = s.enabled;
    c.token = s.token;
    c.notifications = s.notifications;
    c.ping_s = s.ping_s;
    c.pong_ms = s.pong_ms;
    return c;
}

bool is_valid(const HaSettings& s) noexcept {
    if (s.token[homeassistant::kTokenMax] != '\0') {
        return false;
    }
    if (s.port == 0U) {
        return false;
    }
    if (s.host[0] != '\0') {
        if (!is_valid_host(s.host)) {
            return false;
        }
    } else if (s.enabled) {
        return false;  // active link without a host
    } else {
        // disabled and empty: fine
    }
    return homeassistant::is_valid(ws_config(s)) && homeassistant::is_valid(s.values) &&
           homeassistant::is_valid(s.weather);
}

std::size_t ha_uri(const HaSettings& s, char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (size == 0U) || (s.host[0] == '\0')) {
        return 0U;
    }
    const int n = std::snprintf(out, size, "%s://%s:%u/api/websocket", s.secure ? "wss" : "ws", s.host,
                                static_cast<unsigned>(s.port));
    if ((n <= 0) || (static_cast<std::size_t>(n) >= size)) {
        out[0] = '\0';
        return 0U;
    }
    return static_cast<std::size_t>(n);
}

}  // namespace settings
