/**
 * @file store.cpp
 * @brief Settings persistence (NVS) and sharing.
 */
#include "clock_settings/store.hpp"

#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace clock_settings {

namespace {

constexpr char kTag[] = "settings";
constexpr char kNamespace[] = "clk_cfg";
constexpr char kKeyDisplay[] = "display";
constexpr char kKeyBrightness[] = "bright";
constexpr char kKeySystem[] = "system";
constexpr char kKeyAir[] = "air";
constexpr char kKeyHa[] = "ha";
constexpr char kKeyMqtt[] = "mqtt";
constexpr char kKeyAlerts[] = "alerts";
constexpr char kKeyColors[] = "colors";
/// Wi-Fi credentials live in their own namespace (clock_web); erased by the factory reset too.
constexpr char kWifiNamespace[] = "clk_wifi";

constexpr uint8_t kDisplayVersion = 1U;
constexpr uint8_t kBrightnessVersion = 1U;
constexpr uint8_t kSystemVersion = 1U;
constexpr uint8_t kAirVersion = 2U;  // v2 (2026-11-16): heater cycle fields
constexpr uint8_t kHaVersion = 1U;
constexpr uint8_t kMqttVersion = 1U;
constexpr uint8_t kColorsVersion = 1U;
constexpr uint8_t kAlertsVersion = 1U;

SemaphoreHandle_t s_mutex = nullptr;
settings::Settings s_current{};
uint32_t s_version = 0U;
bool s_replaced = false;

bool lock() {
    return (s_mutex != nullptr) && (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE);
}

void unlock() {
    (void)xSemaphoreGive(s_mutex);
}

/// One blob buffer for every type (the largest is HaSettings): the store's callers are serialised,
/// boot on the main task, then saves on the web server's single task.
constexpr std::size_t kBlobMax = sizeof(settings::HaSettings) + 1U;
uint8_t s_blob[kBlobMax];

/// Reads a versioned blob; false if absent, wrong version or wrong size (@p out untouched then).
template <typename T>
bool read_blob(nvs_handle_t h, const char* key, uint8_t version, T& out) {
    static_assert(sizeof(T) + 1U <= kBlobMax, "blob larger than the shared buffer");
    constexpr std::size_t kExpected = sizeof(T) + 1U;
    size_t size = kExpected;
    if (nvs_get_blob(h, key, s_blob, &size) != ESP_OK) {
        return false;
    }
    if ((size != kExpected) || (s_blob[0] != version)) {
        ESP_LOGW(kTag, "%s: stored version %u / size %u, expected %u / %u: defaults", key,
                 static_cast<unsigned>(s_blob[0]), static_cast<unsigned>(size), static_cast<unsigned>(version),
                 static_cast<unsigned>(kExpected));
        return false;
    }
    std::memcpy(&out, s_blob + 1, sizeof(T));
    return true;
}

template <typename T>
bool write_blob(const char* key, uint8_t version, const T& value) {
    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    static_assert(sizeof(T) + 1U <= kBlobMax, "blob larger than the shared buffer");
    s_blob[0] = version;
    std::memcpy(s_blob + 1, &value, sizeof(T));
    const bool ok = (nvs_set_blob(h, key, s_blob, sizeof(T) + 1U) == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    if (!ok) {
        ESP_LOGE(kTag, "%s: NVS write failed", key);
    }
    return ok;
}

}  // namespace

esp_err_t init() noexcept {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }
    // Loaded straight into the live copy: nothing else runs before init() returns (saves a 5 KB temporary).
    settings::Settings& loaded = s_current;
    settings::default_settings_into(loaded);
    bool replaced = false;
    nvs_handle_t h = 0;
    const esp_err_t open = nvs_open(kNamespace, NVS_READONLY, &h);
    if (open == ESP_OK) {
        settings::DisplaySettings d{};
        if (read_blob(h, kKeyDisplay, kDisplayVersion, d)) {
            if (settings::is_valid(d)) {
                loaded.display = d;
            } else {
                ESP_LOGW(kTag, "display settings invalid: defaults");
                replaced = true;
            }
        }
        brightness::Config b{};
        if (read_blob(h, kKeyBrightness, kBrightnessVersion, b)) {
            if (brightness::is_valid(b)) {
                loaded.brightness = b;
            } else {
                ESP_LOGW(kTag, "brightness settings invalid: defaults");
                replaced = true;
            }
        }
        settings::SystemSettings s{};
        if (read_blob(h, kKeySystem, kSystemVersion, s)) {
            if (settings::is_valid(s)) {
                loaded.system = s;
            } else {
                ESP_LOGW(kTag, "system settings invalid: defaults");
                replaced = true;
            }
        }
        settings::AirSettings a{};
        if (read_blob(h, kKeyAir, kAirVersion, a)) {
            if (settings::is_valid(a)) {
                loaded.air = a;
            } else {
                ESP_LOGW(kTag, "air settings invalid: defaults");
                replaced = true;
            }
        }
        // Large (token + 8 values): read in place, defaults back on an invalid blob.
        if (read_blob(h, kKeyHa, kHaVersion, loaded.ha) && !settings::is_valid(loaded.ha)) {
            settings::default_ha_settings_into(loaded.ha);
            ESP_LOGW(kTag, "home assistant settings invalid: defaults");
            replaced = true;
        }
        homeassistant::MqttConfig m{};
        if (read_blob(h, kKeyMqtt, kMqttVersion, m)) {
            if (homeassistant::is_valid(m)) {
                loaded.mqtt = m;
            } else {
                ESP_LOGW(kTag, "mqtt settings invalid: defaults");
                replaced = true;
            }
        }
        settings::ColorSettings col{};
        if (read_blob(h, kKeyColors, kColorsVersion, col)) {
            if (settings::is_valid(col)) {
                loaded.colors = col;
            } else {
                ESP_LOGW(kTag, "colour settings invalid: defaults");
                replaced = true;
            }
        }
        // 16 rules: read in place too.
        if (read_blob(h, kKeyAlerts, kAlertsVersion, loaded.alerts) && !settings::is_valid(loaded.alerts)) {
            settings::default_alert_settings_into(loaded.alerts);
            ESP_LOGW(kTag, "alert settings invalid: defaults");
            replaced = true;
        }
        nvs_close(h);
    } else if (open != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(kTag, "NVS open: %s (defaults)", esp_err_to_name(open));
    } else {
        // First boot: nothing stored yet, defaults without a flag.
    }
    if (lock()) {
        s_replaced = replaced;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "settings loaded%s: budget %u mA, mode %u, zone %s", replaced ? " (some replaced by defaults)" : "",
             static_cast<unsigned>(loaded.display.budget_ma), static_cast<unsigned>(loaded.brightness.mode),
             loaded.system.timezone);
    return ESP_OK;
}

void get(settings::Settings& out) noexcept {
    if (lock()) {
        out = s_current;
        unlock();
    }
}

settings::DisplaySettings display() noexcept {
    settings::DisplaySettings d{};
    if (lock()) {
        d = s_current.display;
        unlock();
    }
    return d;
}

brightness::Config brightness() noexcept {
    brightness::Config b{};
    if (lock()) {
        b = s_current.brightness;
        unlock();
    }
    return b;
}

settings::SystemSettings system() noexcept {
    settings::SystemSettings sys{};
    if (lock()) {
        sys = s_current.system;
        unlock();
    }
    return sys;
}

settings::AirSettings air() noexcept {
    settings::AirSettings a{};
    if (lock()) {
        a = s_current.air;
        unlock();
    }
    return a;
}

void ha(settings::HaSettings& out) noexcept {
    if (lock()) {
        out = s_current.ha;
        unlock();
    }
}

void alerts(settings::AlertSettings& out) noexcept {
    if (lock()) {
        out = s_current.alerts;
        unlock();
    }
}

homeassistant::MqttConfig mqtt() noexcept {
    homeassistant::MqttConfig m = homeassistant::default_mqtt_config();
    if (lock()) {
        m = s_current.mqtt;
        unlock();
    }
    return m;
}

settings::ColorSettings colors() noexcept {
    settings::ColorSettings c = settings::default_color_settings();
    if (lock()) {
        c = s_current.colors;
        unlock();
    }
    return c;
}

uint32_t version() noexcept {
    uint32_t v = 0U;
    if (lock()) {
        v = s_version;
        unlock();
    }
    return v;
}

bool replaced_on_boot() noexcept {
    bool r = false;
    if (lock()) {
        r = s_replaced;
        unlock();
    }
    return r;
}

bool save_display(const settings::DisplaySettings& s) noexcept {
    if (!settings::is_valid(s) || !write_blob(kKeyDisplay, kDisplayVersion, s)) {
        return false;
    }
    if (lock()) {
        s_current.display = s;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "display settings saved");
    return true;
}

bool save_brightness(const brightness::Config& c) noexcept {
    if (!brightness::is_valid(c) || !write_blob(kKeyBrightness, kBrightnessVersion, c)) {
        return false;
    }
    if (lock()) {
        s_current.brightness = c;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "brightness settings saved");
    return true;
}

bool save_system(const settings::SystemSettings& s) noexcept {
    if (!settings::is_valid(s) || !write_blob(kKeySystem, kSystemVersion, s)) {
        return false;
    }
    if (lock()) {
        s_current.system = s;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "system settings saved");
    return true;
}

bool save_air(const settings::AirSettings& s) noexcept {
    if (!settings::is_valid(s) || !write_blob(kKeyAir, kAirVersion, s)) {
        return false;
    }
    if (lock()) {
        s_current.air = s;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "air settings saved");
    return true;
}

bool save_ha(const settings::HaSettings& s) noexcept {
    if (!settings::is_valid(s) || !write_blob(kKeyHa, kHaVersion, s)) {
        return false;
    }
    if (lock()) {
        s_current.ha = s;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "home assistant settings saved");
    return true;
}

bool save_mqtt(const homeassistant::MqttConfig& c) noexcept {
    if (!homeassistant::is_valid(c) || !write_blob(kKeyMqtt, kMqttVersion, c)) {
        return false;
    }
    if (lock()) {
        s_current.mqtt = c;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "mqtt settings saved");
    return true;
}

bool save_colors(const settings::ColorSettings& c) noexcept {
    if (!settings::is_valid(c) || !write_blob(kKeyColors, kColorsVersion, c)) {
        return false;
    }
    if (lock()) {
        s_current.colors = c;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "colour settings saved");
    return true;
}

bool save_alerts(const settings::AlertSettings& a) noexcept {
    if (!settings::is_valid(a) || !write_blob(kKeyAlerts, kAlertsVersion, a)) {
        return false;
    }
    if (lock()) {
        s_current.alerts = a;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "alert settings saved");
    return true;
}

bool factory_reset() noexcept {
    bool ok = true;
    const char* const namespaces[] = {kNamespace, kWifiNamespace};
    for (const char* ns : namespaces) {
        nvs_handle_t h = 0;
        if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
            ok = (nvs_erase_all(h) == ESP_OK) && (nvs_commit(h) == ESP_OK) && ok;
            nvs_close(h);
        }
    }
    ESP_LOGW(kTag, "factory reset %s", ok ? "done: rebooting" : "FAILED");
    return ok;
}

}  // namespace clock_settings
