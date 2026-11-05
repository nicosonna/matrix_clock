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
/// Wi-Fi credentials live in their own namespace (clock_web); erased by the factory reset too.
constexpr char kWifiNamespace[] = "clk_wifi";

constexpr uint8_t kDisplayVersion = 1U;
constexpr uint8_t kBrightnessVersion = 1U;
constexpr uint8_t kSystemVersion = 1U;
constexpr uint8_t kAirVersion = 1U;

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

/// Reads a versioned blob; false if absent, wrong version or wrong size.
template <typename T>
bool read_blob(nvs_handle_t h, const char* key, uint8_t version, T& out) {
    uint8_t buf[sizeof(T) + 1U];
    size_t size = sizeof(buf);
    if (nvs_get_blob(h, key, buf, &size) != ESP_OK) {
        return false;
    }
    if ((size != sizeof(buf)) || (buf[0] != version)) {
        ESP_LOGW(kTag, "%s: stored version %u / size %u, expected %u / %u: defaults", key,
                 static_cast<unsigned>(buf[0]), static_cast<unsigned>(size), static_cast<unsigned>(version),
                 static_cast<unsigned>(sizeof(buf)));
        return false;
    }
    std::memcpy(&out, buf + 1, sizeof(T));
    return true;
}

template <typename T>
bool write_blob(const char* key, uint8_t version, const T& value) {
    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    uint8_t buf[sizeof(T) + 1U];
    buf[0] = version;
    std::memcpy(buf + 1, &value, sizeof(T));
    const bool ok = (nvs_set_blob(h, key, buf, sizeof(buf)) == ESP_OK) && (nvs_commit(h) == ESP_OK);
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
    settings::Settings loaded = settings::default_settings();
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
        nvs_close(h);
    } else if (open != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(kTag, "NVS open: %s (defaults)", esp_err_to_name(open));
    } else {
        // First boot: nothing stored yet, defaults without a flag.
    }
    if (lock()) {
        s_current = loaded;
        s_replaced = replaced;
        ++s_version;
        unlock();
    }
    ESP_LOGI(kTag, "settings loaded%s: budget %u mA, mode %u, zone %s", replaced ? " (some replaced by defaults)" : "",
             static_cast<unsigned>(loaded.display.budget_ma), static_cast<unsigned>(loaded.brightness.mode),
             loaded.system.timezone);
    return ESP_OK;
}

settings::Settings get() noexcept {
    settings::Settings copy{};
    if (lock()) {
        copy = s_current;
        unlock();
    }
    return copy;
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
