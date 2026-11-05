/**
 * @file app_main.cpp
 * @brief Entry point: wires the hardware adapters to the pure logic.
 *
 *  - a **display task** on core 1 at 25 fps, above every network task (EX-61):
 *    scene scheduler, brightness control at 1 Hz, notifications, test patterns
 *    for the wizards, OTA progress bar, settings re-applied without reboot;
 *  - the network, the settings store, the HTTP server and the time source,
 *    started from app_main on core 0; a failure there never stops the display;
 *  - the network status text (access point + password, or the IP address) is
 *    shown **once** when the state changes — and every minute in portal mode,
 *    so a missed password can still be read.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "air_quality/levels.hpp"
#include "air_quality/sensor.hpp"
#include "alerts/notification.hpp"
#include "alerts/queue.hpp"
#include "alerts/rules.hpp"
#include "brightness/brightness.hpp"
#include "clock_board/board.hpp"
#include "clock_display/led_panel.hpp"
#include "clock_hal/i2c_bus.hpp"
#include "clock_homeassistant/ha_link.hpp"
#include "clock_homeassistant/mqtt_link.hpp"
#include "clock_hal/ldr_adc.hpp"
#include "clock_settings/store.hpp"
#include "clock_time/time_source.hpp"
#include "clock_web/web.hpp"
#include "display/air_logo.hpp"
#include "display/value_icons.hpp"
#include "display/clock_scene.hpp"
#include "display/color.hpp"
#include "display/current_limit.hpp"
#include "display/frame_buffer.hpp"
#include "display/icon_zone.hpp"
#include "display/panel_map.hpp"
#include "display/render.hpp"
#include "display/scheduler.hpp"
#include "display/scroll.hpp"
#include "display/test_patterns.hpp"
#include "display/text.hpp"
#include "settings/forms.hpp"
#include "settings/json_writer.hpp"
#include "settings/model.hpp"
#include "timekeeping/tz.hpp"

namespace {

constexpr char kTag[] = "main";

/// pdMS_TO_TICKS() expands to C-style casts that trip -Wold-style-cast; same arithmetic.
constexpr TickType_t ticks_from_ms(uint32_t ms) noexcept {
    return static_cast<TickType_t>((ms * static_cast<uint32_t>(CONFIG_FREERTOS_HZ)) / 1000U);
}

constexpr uint32_t kFramePeriodMs = 40U;        ///< 25 frames per second.
constexpr uint8_t kInitialBrightness = 48U;     ///< Before the first sensor reading.
constexpr uint32_t kSensorPeriodMs = 1000U;     ///< The brightness controller is tuned for 1 Hz.
constexpr uint32_t kColonHalfPeriodMs = 500U;   ///< Colon blink, 1 Hz.
constexpr uint32_t kStatusPeriodMs = 5000U;     ///< Diagnostics log period.
constexpr uint32_t kPortalRepeatMs = 60000U;    ///< Portal info shown again every minute.
constexpr uint32_t kTestPatternTimeoutMs = 120000U;  ///< A forgotten test pattern ends by itself.
constexpr uint32_t kChaserStepMs = 40U;
constexpr UBaseType_t kDisplayTaskPriority = 6U;
constexpr uint32_t kDisplayTaskStack = 8192U;
constexpr BaseType_t kDisplayCore = 1;
constexpr UBaseType_t kAirTaskPriority = 3U;
constexpr uint32_t kAirTaskStack = 4096U;
constexpr BaseType_t kAirCore = 0;
constexpr uint32_t kAirStepMs = 20U;          ///< The sensor state machine is polled this often.
constexpr uint32_t kAirLevelPeriodMs = 1000U; ///< The level tracker runs at 1 Hz.
constexpr uint8_t kAirLogoX = 26U;            ///< 6x6 logo next to a left-aligned clock (25 px).
constexpr uint8_t kAirLogoY = 1U;

// Colours (a theme, until the page lets the user choose them).
constexpr display::ClockColors kClockColors{display::Rgb{255U, 255U, 255U}, display::Rgb{255U, 160U, 40U},
                                            display::Rgb{40U, 80U, 200U}};
constexpr display::DateColors kDateColors{display::Rgb{255U, 160U, 40U}, display::Rgb{255U, 255U, 255U}};
constexpr display::Rgb kStatusColor{90U, 160U, 255U};
constexpr display::Rgb kOtaColor{255U, 255U, 255U};
constexpr display::Rgb kValueIconColor{90U, 160U, 255U};
constexpr display::Rgb kDimColor{90U, 90U, 90U};
constexpr uint8_t kNightScale = 48U;  ///< Night scene brightness factor (/255).
/// Notification colour by priority: info, notable, important, critical.
constexpr display::Rgb kPriorityColor[4] = {
    display::Rgb{90U, 160U, 255U}, display::Rgb{255U, 190U, 40U}, display::Rgb{255U, 110U, 0U},
    display::Rgb{255U, 30U, 30U}};

uint32_t now_ms() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void confirm_ota_image() noexcept {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if ((running != nullptr) && (esp_ota_get_state_partition(running, &state) == ESP_OK) &&
        (state == ESP_OTA_IMG_PENDING_VERIFY)) {
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(kTag, "OTA image confirmed (%s)", esp_err_to_name(err));
    }
}

display::TimeInfo time_info_from(const clock_time::Snapshot& s) noexcept {
    display::TimeInfo t{};
    t.sure = s.sure;
    t.hour = s.local.hour;
    t.minute = s.local.minute;
    t.second = s.local.second;
    t.day = s.local.day;
    t.month = s.local.month;
    t.weekday = s.local.weekday;
    return t;
}

// ---------------------------------------------------------------------------
// Cross-task hand-over: one slot each, under a critical section. The real
// notification queue comes with the `alerts` engine.
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
/// Alerts: every entry point (HTTP, Home Assistant) pushes into one queue; the display task
/// runs the rule engine at 1 Hz and plays the passes. Both under one mutex (never on the frame path for long).
SemaphoreHandle_t s_alerts_mutex = nullptr;
alerts::NotificationQueue s_queue;
alerts::RuleEngine s_engine;
bool s_quiet = false;            ///< Quiet hours active (1 Hz).

// FreeRTOS macros carry C casts; the three calls below are the only ones in this file.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
bool alerts_lock() {
    return (s_alerts_mutex != nullptr) && (xSemaphoreTake(s_alerts_mutex, portMAX_DELAY) == pdTRUE);
}

void alerts_unlock() {
    (void)xSemaphoreGive(s_alerts_mutex);
}

SemaphoreHandle_t create_alerts_mutex() {
    return xSemaphoreCreateMutex();
}
#pragma GCC diagnostic pop
char s_test_pattern[16] = {};
bool s_test_pattern_set = false;
uint8_t s_ota_percent = 0U;
bool s_ota_active = false;
/// Latest sensor reading and status, for the wizard and the dashboard.
struct Shared {
    uint16_t ldr_raw;
    bool ldr_valid;
    int ldr_mv;
    uint16_t ldr_filtered;
    uint8_t level;
    bool ldr_fault;
    uint32_t estimated_ma;
    uint16_t budget_ma;
    bool limited;
    char scene[12];
    brightness::Mode mode;
};
Shared s_shared{};
/// Air-quality snapshot published by the air task.
struct AirShared {
    bool enabled;
    air_quality::AirReading reading;
    air_quality::AirLevel level;
    int16_t raw_temperature_centi;  ///< Before the offset, for the wizard.
    bool raw_temperature_valid;
    char state[16];
};
AirShared s_air{};

bool on_air_raw_temperature(int16_t& centi) {
    taskENTER_CRITICAL(&s_mux);
    const bool valid = s_air.raw_temperature_valid;
    centi = s_air.raw_temperature_centi;
    taskEXIT_CRITICAL(&s_mux);
    return valid;
}

bool on_notification(const alerts::Notification& n) {
    if (!alerts_lock()) {
        return false;
    }
    const alerts::PushStatus st = s_queue.push(n, now_ms());
    alerts_unlock();
    ESP_LOGI(kTag, "notification \"%s\": priority %u, %u s%s%s -> %s", n.key, static_cast<unsigned>(n.priority),
             static_cast<unsigned>(n.duration_s), n.persistent ? ", held" : "", n.clear ? ", clear" : "",
             (st == alerts::PushStatus::Rejected) ? "REJECTED (queue full)" : "queued");
    return st != alerts::PushStatus::Rejected;
}



bool on_test_pattern(const char* name) {
    const char* const known[] = {"aucun", "pixel", "colonne", "diagonale", "plein", "chenillard"};
    bool ok = false;
    for (const char* k : known) {
        ok = ok || (std::strcmp(k, name) == 0);
    }
    if (!ok) {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    std::strncpy(s_test_pattern, name, sizeof(s_test_pattern) - 1U);
    s_test_pattern[sizeof(s_test_pattern) - 1U] = '\0';
    s_test_pattern_set = true;
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(kTag, "test pattern: %s", name);
    return true;
}

bool on_ldr_raw(uint16_t& raw, bool& valid, int& mv) {
    taskENTER_CRITICAL(&s_mux);
    raw = s_shared.ldr_raw;
    valid = s_shared.ldr_valid;
    mv = s_shared.ldr_mv;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

void on_ota_progress(uint8_t percent, bool active) {
    taskENTER_CRITICAL(&s_mux);
    s_ota_percent = percent;
    s_ota_active = active;
    taskEXIT_CRITICAL(&s_mux);
}

/// What the MQTT task publishes: the latest air, light and time state (called from the mqtt task).
void fill_mqtt_state(homeassistant::MqttState& out) {
    taskENTER_CRITICAL(&s_mux);
    out.air_enabled = s_air.enabled;
    out.air = s_air.reading;
    out.level = s_air.level;
    out.ldr_valid = s_shared.ldr_valid && !s_shared.ldr_fault;
    out.ldr_raw = s_shared.ldr_raw;
    taskEXIT_CRITICAL(&s_mux);
    out.time_sure = clock_time::now().sure;
}

void on_mqtt_discovery() {
    clock_homeassistant::mqtt_request_discovery();
}

/// `/api/etat` for the dashboard.
std::size_t status_json(char* out, std::size_t size) {
    const clock_time::Snapshot ts = clock_time::now();
    const clock_web::NetworkInfo net = clock_web::info();
    Shared sh{};
    taskENTER_CRITICAL(&s_mux);
    sh = s_shared;
    taskEXIT_CRITICAL(&s_mux);
    char hhmmss[16];
    char date[24];
    (void)std::snprintf(hhmmss, sizeof(hhmmss), "%02u:%02u:%02u", static_cast<unsigned>(ts.local.hour),
                        static_cast<unsigned>(ts.local.minute), static_cast<unsigned>(ts.local.second));
    (void)std::snprintf(date, sizeof(date), "%04ld-%02u-%02u", static_cast<long>(ts.local.year),
                        static_cast<unsigned>(ts.local.month), static_cast<unsigned>(ts.local.day));
    settings::JsonWriter w(out, size);
    bool ok = w.begin_object();
    ok = ok && w.key("heure") && w.begin_object() && w.field("sure", ts.sure) && w.field("texte", hhmmss) &&
         w.field("date", date) && w.field("dst", ts.local.is_dst) &&
         w.field("age_s", static_cast<int64_t>((ts.age_ms == UINT32_MAX) ? -1 : static_cast<int64_t>(ts.age_ms / 1000U))) &&
         w.end_object();
    ok = ok && w.key("luminosite") && w.begin_object() && w.field("brut", static_cast<int64_t>(sh.ldr_raw)) &&
         w.field("valide", sh.ldr_valid) && w.field("filtre", static_cast<int64_t>(sh.ldr_filtered)) &&
         w.field("niveau", static_cast<int64_t>(sh.level)) && w.field("defaut", sh.ldr_fault) &&
         w.field("mode", settings::brightness_mode_name(sh.mode)) && w.end_object();
    ok = ok && w.key("courant") && w.begin_object() && w.field("estime_ma", static_cast<int64_t>(sh.estimated_ma)) &&
         w.field("budget_ma", static_cast<int64_t>(sh.budget_ma)) && w.field("ecretage", sh.limited) && w.end_object();
    ok = ok && w.key("reseau") && w.begin_object() && w.field("etat", net.state) && w.field("ip", net.ip) &&
         w.field("portail", net.portal) && w.field("ap", net.ap_ssid) && w.end_object();
    AirShared air{};
    taskENTER_CRITICAL(&s_mux);
    air = s_air;
    taskEXIT_CRITICAL(&s_mux);
    const air_quality::AirReading& r = air.reading;
    const settings::AirSettings air_cfg = clock_settings::air();
    ok = ok && w.key("air") && w.begin_object() && w.field("actif", air.enabled) && w.field("present", r.present) &&
         w.field("defaut", r.fault) && w.field("prechauffage", r.warming_up) &&
         w.field("premiere_mise_en_service", r.initial_start) && w.field("air_valide", r.air_valid) &&
         w.field("aqi", static_cast<int64_t>(r.aqi)) && w.field("eco2", static_cast<int64_t>(r.eco2_ppm)) &&
         w.field("tvoc", static_cast<int64_t>(r.tvoc_ppb)) && w.field("climat_valide", r.climate_valid) &&
         w.field("temperature_centi", static_cast<int64_t>(r.temperature_centi)) &&
         w.field("humidite_pct10", static_cast<int64_t>(r.humidity_pct10)) &&
         w.field("compensation_par_defaut", r.compensation_default) &&
         w.field("niveau", air_quality::level_name(air.level)) && w.field("etat", air.state) &&
         w.field("reinit_bus", static_cast<int64_t>(r.bus_resets)) &&
         w.field("statut_brut", static_cast<int64_t>(r.status_raw)) && w.field("opmode_brut", static_cast<int64_t>(r.opmode_raw)) &&
         w.field("adresse", (air_cfg.ens160_address == air_quality::kEns160AddressHigh) ? "0x53" : "0x52") &&
         w.field("age_s", r.present ? static_cast<int64_t>((now_ms() - r.updated_ms) / 1000U) : static_cast<int64_t>(-1)) && w.end_object();
    static clock_homeassistant::LinkSnapshot ha;
    clock_homeassistant::snapshot(ha);
    ok = ok && w.key("ha") && w.begin_object() && w.field("actif", ha.enabled) && w.field("etat", ha.state_name) &&
         w.field("erreur", ha.error) && w.field("connexions", static_cast<int64_t>(ha.stats.connections)) &&
         w.field("coupures", static_cast<int64_t>(ha.stats.disconnections)) &&
         w.field("pongs_manques", static_cast<int64_t>(ha.stats.pong_misses)) &&
         w.field("evenements", static_cast<int64_t>(ha.stats.events)) &&
         w.field("messages_ignores", static_cast<int64_t>(ha.stats.messages_ignored)) &&
         w.field("trames_abandonnees", static_cast<int64_t>(ha.frames_dropped)) &&
         w.field("notifications", static_cast<int64_t>(ha.stats.notifications)) &&
         w.field("notifications_refusees", static_cast<int64_t>(ha.stats.notifications_rejected)) &&
         w.key("valeurs") && w.begin_array();
    for (std::size_t i = 0U; ok && (i < homeassistant::kValuesMax); ++i) {
        const clock_homeassistant::ValueSnapshot& v = ha.values[i];
        ok = w.begin_object() && w.field("actif", v.enabled) && w.field("nom", v.name) && w.field("valide", v.valid) &&
             w.field("texte", v.text) && w.field("meteo", v.from_weather) && w.field("erreur", v.error) && w.end_object();
    }
    ok = ok && w.end_array() && w.end_object();
    clock_homeassistant::MqttSnapshot mq{};
    clock_homeassistant::mqtt_snapshot(mq);
    ok = ok && w.key("mqtt") && w.begin_object() && w.field("actif", mq.enabled) && w.field("etat", mq.state_name) &&
         w.field("id", mq.device_id) && w.field("connexions", static_cast<int64_t>(mq.stats.connections)) &&
         w.field("coupures", static_cast<int64_t>(mq.stats.disconnections)) &&
         w.field("decouvertes", static_cast<int64_t>(mq.stats.discoveries)) &&
         w.field("etats", static_cast<int64_t>(mq.stats.states)) &&
         w.field("echecs", static_cast<int64_t>(mq.publish_failures)) && w.field("erreur", mq.last_error) && w.end_object();
    ok = ok && w.key("meteo") && w.begin_object() && w.field("actif", ha.weather_enabled) &&
         w.field("valide", ha.weather_valid) && w.field("temperature_deci", static_cast<int64_t>(ha.weather_deci)) &&
         w.field("age_s", static_cast<int64_t>(ha.weather_age_s)) && w.end_object();
    // Alerts: rules with their live state, the queue as it is (listed, never acknowledged).
    if (ok && alerts_lock()) {
        ok = w.key("alertes") && w.begin_object() && w.field("silence", s_quiet) &&
             w.field("levees", static_cast<int64_t>(s_engine.raised_count())) &&
             w.field("tenues", s_queue.has_held()) && w.field("rejets", static_cast<int64_t>(s_queue.rejected())) &&
             w.key("regles") && w.begin_array();
        for (std::size_t i = 0U; ok && (i < alerts::kRulesMax); ++i) {
            alerts::RuleView v{};
            (void)s_engine.view(i, v);
            const alerts::Rule& rule = s_engine.rules()[i];
            char value[20] = "";
            if (v.sample_valid) {
                (void)settings::fixed_to_text(v.sample, rule.decimals, value, sizeof(value));
            }
            ok = w.begin_object() && w.field("nom", rule.name) && w.field("actif", rule.enabled) &&
                 w.field("etat", alerts::rule_state_name(v.state)) && w.field("valeur", value) && w.field("texte", v.text) &&
                 w.field("priorite", static_cast<int64_t>(rule.priority)) && w.end_object();
        }
        ok = ok && w.end_array() && w.key("file") && w.begin_array();
        const uint32_t now = now_ms();
        for (std::size_t i = 0U; ok && (i < alerts::kQueueMax); ++i) {
            const alerts::Entry* e = s_queue.at(i);
            if ((e == nullptr) || !e->used) {
                continue;
            }
            ok = w.begin_object() && w.field("cle", e->key) && w.field("titre", e->title) && w.field("texte", e->text) &&
                 w.field("priorite", static_cast<int64_t>(e->priority)) && w.field("tenue", e->held) &&
                 w.field("age_s", static_cast<int64_t>((now - e->arrival_ms) / 1000U)) &&
                 w.field("passages", e->passed) && w.end_object();
        }
        ok = ok && w.end_array() && w.end_object();
        alerts_unlock();
    }
    ok = ok && w.field("scene", sh.scene) && w.field("reglages_remplaces", clock_settings::replaced_on_boot()) &&
         w.field("fonctionnement_s", static_cast<int64_t>(esp_timer_get_time() / 1000000)) && w.end_object();
    return ok ? w.length() : 0U;
}

/// Status text: how to reach the clock.
void build_status_text(char* out, std::size_t out_size, const clock_web::NetworkInfo& net) {
    char raw[display::kTextMax + 1U];
    if (net.portal) {
        (void)std::snprintf(raw, sizeof(raw), "WIFI %s  MDP %s  http://192.168.4.1", net.ap_ssid, net.ap_password);
    } else if (net.connected) {
        (void)std::snprintf(raw, sizeof(raw), "http://%s  horloge.local", net.ip);
    } else {
        (void)std::snprintf(raw, sizeof(raw), "WIFI %s", net.state);
    }
    (void)display::transliterate(raw, out, out_size);
}

/// Milliseconds a text needs to cross the panel once at @p speed.
uint32_t scroll_duration_ms(uint16_t width, uint16_t speed) {
    return ((static_cast<uint32_t>(display::kWidth) + width) * 1000U) / speed + 300U;
}

display::TestPattern pattern_from_name(const char* name) {
    if (std::strcmp(name, "pixel") == 0) {
        return display::TestPattern::Pixel;
    }
    if (std::strcmp(name, "colonne") == 0) {
        return display::TestPattern::Column;
    }
    if (std::strcmp(name, "diagonale") == 0) {
        return display::TestPattern::Diagonal;
    }
    if (std::strcmp(name, "plein") == 0) {
        return display::TestPattern::Full;
    }
    return display::TestPattern::None;
}

/// Air-quality task: polls the sensor state machine, tracks the level, publishes.
void air_task(void* /*arg*/) {
    static clock_hal::I2cBus bus;
    if (bus.init() != ESP_OK) {
        ESP_LOGE(kTag, "I2C bus unavailable: no air quality");
    }
    settings::AirSettings cfg = clock_settings::air();
    static air_quality::AirSensor sensor(bus, settings::sensor_config(cfg));
    static air_quality::LevelTracker tracker;
    uint32_t applied_version = clock_settings::version();
    uint32_t last_level = now_ms();
    bool last_present = false;
    bool last_fault = false;
    bool last_air_valid = false;
    air_quality::AirLevel last_level_value = air_quality::AirLevel::Unknown;
    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t version = clock_settings::version();
        if (version != applied_version) {
            applied_version = version;
            cfg = clock_settings::air();
            sensor.configure(settings::sensor_config(cfg));
        }
        air_quality::AirReading reading{};
        if (cfg.enabled) {
            sensor.step(now);
            reading = sensor.reading();
        }
        air_quality::AirLevel level = tracker.current();
        if ((now - last_level) >= kAirLevelPeriodMs) {
            last_level = now;
            level = tracker.step(cfg.levels, reading, now);
            if ((reading.present != last_present) || (reading.fault != last_fault) ||
                (reading.air_valid != last_air_valid) || (level != last_level_value)) {
                ESP_LOGI(kTag, "air: %s%s%s%s | AQI %u, eCO2 %u ppm, TVOC %u ppb | %d.%02d C, %u.%u %% | level %s (%s)",
                         reading.present ? "present" : "absent", reading.fault ? ", FAULT" : "",
                         reading.warming_up ? ", warming up" : "", reading.air_valid ? "" : ", air invalid",
                         static_cast<unsigned>(reading.aqi), static_cast<unsigned>(reading.eco2_ppm),
                         static_cast<unsigned>(reading.tvoc_ppb), static_cast<int>(reading.temperature_centi / 100),
                         static_cast<int>((reading.temperature_centi < 0 ? -reading.temperature_centi : reading.temperature_centi) % 100),
                         static_cast<unsigned>(reading.humidity_pct10 / 10U), static_cast<unsigned>(reading.humidity_pct10 % 10U),
                         air_quality::level_name(level), sensor.state_name());
                last_present = reading.present;
                last_fault = reading.fault;
                last_air_valid = reading.air_valid;
                last_level_value = level;
            }
        }
        taskENTER_CRITICAL(&s_mux);
        s_air.enabled = cfg.enabled;
        s_air.reading = reading;
        s_air.level = level;
        s_air.raw_temperature_valid = reading.climate_valid;
        s_air.raw_temperature_centi = static_cast<int16_t>(reading.temperature_centi + cfg.temperature_offset_centi);
        std::strncpy(s_air.state, sensor.state_name(), sizeof(s_air.state) - 1U);
        taskEXIT_CRITICAL(&s_mux);
        vTaskDelay(ticks_from_ms(kAirStepMs));
    }
}

void display_task(void* /*arg*/) {
    static clock_display::LedPanel panel;
    const esp_err_t init_err = panel.init();
    if (init_err != ESP_OK) {
        ESP_LOGE(kTag, "LED panel init failed: %s — nothing will be displayed", esp_err_to_name(init_err));
        for (;;) {
            vTaskDelay(ticks_from_ms(1000U));
        }
    }
    (void)panel.clear();
    confirm_ota_image();

    static clock_hal::LdrAdc ldr;
    if (ldr.init() != ESP_OK) {
        ESP_LOGW(kTag, "LDR unavailable: fallback brightness");
    }

    static display::FrameBuffer fb;
    static display::PhysicalFrame frame;
    static display::Scroller status_scroller;
    static display::Scroller notif_scroller;
    static display::Scroller value_scroller;
    static char value_scroll_text[display::kTextMax + 1U];
    static char status_text[display::kTextMax + 1U];
    static char notif_text[display::kTextMax + 1U];
    static alerts::Pass pass{};
    uint16_t pass_width = 0U;
    static settings::Settings cfg;
    settings::default_settings_into(cfg);
    static brightness::Controller brightness_ctl(cfg.brightness);
    static display::Scheduler scheduler(cfg.display.scenes, now_ms());

    uint32_t applied_version = 0U;
    display::TimeInfo time_info{false, 0U, 0U, 0U, 1U, 1U, 0U};
    bool was_sure = false;
    bool notif_shown = false;      ///< A notification display is in progress (status or inbound).
    bool scroller_running = false; ///< Its scroller has been started.
    bool real_queued = false;      ///< An inbound notification is queued or on screen.
    bool status_pending = false;   ///< A status pass is due (text ready, not handed to the scheduler yet).
    bool status_queued = false;    ///< The status pass is queued or on screen.
    bool showing_status = false;   ///< The notification on screen is the status text.
    bool last_portal = false;
    bool last_connected = false;
    uint32_t last_portal_shown = 0U;
    display::TestPattern pattern = display::TestPattern::None;
    bool chaser = false;
    bool scene_finished = false;
    bool value_scroll_running = false;
    uint16_t chaser_head = 0U;
    uint32_t pattern_since = 0U;
    uint32_t last_chaser = 0U;
    uint32_t last_frame = now_ms();
    uint32_t last_status_log = last_frame;
    uint32_t last_sensor = last_frame - kSensorPeriodMs;
    uint8_t applied = kInitialBrightness;
    brightness::Sample sample{0U, false};
    brightness::Output lum{};
    display::RenderStats stats{};
    static clock_homeassistant::LinkSnapshot ha;
    AirShared air{};

    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t dt = now - last_frame;
        last_frame = now;

        // Settings changed (page): re-apply without reboot.
        const uint32_t version = clock_settings::version();
        if (version != applied_version) {
            applied_version = version;
            clock_settings::get(cfg);
            brightness_ctl = brightness::Controller(cfg.brightness);
            scheduler = display::Scheduler(cfg.display.scenes, now);
            if (alerts_lock()) {
                s_engine.configure(cfg.alerts.rules);
                alerts_unlock();
            }
            const char* posix = timekeeping::posix_for_zone(cfg.system.timezone);
            if ((posix != nullptr) && !clock_time::set_timezone(posix)) {
                ESP_LOGW(kTag, "time zone %s rejected", cfg.system.timezone);
            }
            ESP_LOGI(kTag, "settings applied (version %lu): budget %u mA, %u scenes, zone %s",
                     static_cast<unsigned long>(version), static_cast<unsigned>(cfg.display.budget_ma),
                     static_cast<unsigned>(cfg.display.scenes.count), cfg.system.timezone);
        }

        const clock_time::Snapshot ts = clock_time::now();
        time_info = time_info_from(ts);
        if (ts.sure != was_sure) {
            was_sure = ts.sure;
            ESP_LOGI(kTag, "time is now %s (%04ld-%02u-%02u %02u:%02u:%02u%s)", ts.sure ? "SURE" : "NOT sure",
                     static_cast<long>(ts.local.year), static_cast<unsigned>(ts.local.month),
                     static_cast<unsigned>(ts.local.day), static_cast<unsigned>(ts.local.hour),
                     static_cast<unsigned>(ts.local.minute), static_cast<unsigned>(ts.local.second),
                     ts.local.is_dst ? " DST" : "");
        }

        clock_homeassistant::snapshot(ha);
        taskENTER_CRITICAL(&s_mux);
        air = s_air;
        taskEXIT_CRITICAL(&s_mux);

        // Sensor and brightness at 1 Hz.
        if ((now - last_sensor) >= kSensorPeriodMs) {
            last_sensor = now;
            sample = ldr.read();
            const uint16_t minute_of_day = static_cast<uint16_t>(ts.local.hour * 60U + ts.local.minute);
            lum = brightness_ctl.step(sample, applied, time_info.sure, minute_of_day);
            applied = lum.level;

            // Network status: once on each change, every minute in portal mode.
            const clock_web::NetworkInfo net = clock_web::info();

            // Alert rules on what the clock knows right now (invalid = unavailable, never a stale value).
            alerts::Inputs in{};
            for (std::size_t i = 0U; i < homeassistant::kValuesMax; ++i) {
                const clock_homeassistant::ValueSnapshot& v = ha.values[i];
                in.value_enabled[i] = v.enabled;
                in.value_decimals[i] = v.decimals;
                std::strncpy(in.value_units[i], v.unit, alerts::kUnitMax);
                if (v.type == homeassistant::ValueType::Number) {
                    in.values[i] = alerts::Sample{v.valid, v.number};
                } else if (v.type == homeassistant::ValueType::Boolean) {
                    in.values[i] = alerts::Sample{v.valid, v.boolean ? 1 : 0};
                } else {
                    in.values[i] = alerts::Sample{false, 0};  // a text has no number to compare
                }
            }
            in.time_sure = ts.sure;
            in.ha_enabled = ha.enabled;
            in.ha_link_up = ha.state == homeassistant::LinkState::Running;
            in.token_rejected = ha.state == homeassistant::LinkState::TokenRejected;
            in.network_up = net.connected;
            in.lease_missing = (std::strcmp(net.state, "waiting_lease") == 0) || (std::strcmp(net.state, "lease_desperate") == 0);
            in.ldr_fault = lum.sensor_fault;
            in.current_limited = stats.limited;
            in.air_enabled = air.enabled;
            in.air_present = air.reading.present;
            in.air_fault = air.reading.fault;
            in.air_warming_up = air.reading.warming_up || air.reading.initial_start;
            in.air_compensation_default = air.reading.compensation_default;
            const bool air_ok = air.reading.air_valid;
            in.air_aqi = alerts::Sample{air_ok, static_cast<int32_t>(air.reading.aqi)};
            in.air_eco2 = alerts::Sample{air_ok, static_cast<int32_t>(air.reading.eco2_ppm)};
            in.air_tvoc = alerts::Sample{air_ok, static_cast<int32_t>(air.reading.tvoc_ppb)};
            in.air_temperature = alerts::Sample{air.reading.climate_valid, static_cast<int32_t>(air.reading.temperature_centi)};
            in.air_humidity = alerts::Sample{air.reading.climate_valid, static_cast<int32_t>(air.reading.humidity_pct10)};
            if (alerts_lock()) {
                s_quiet = alerts::quiet_now(cfg.alerts.quiet, ts.sure, minute_of_day);
                s_engine.step(in, now, s_queue);
                s_queue.step(now);
                alerts_unlock();
            }
            const bool portal_changed = net.portal != last_portal;
            const bool connected_changed = net.connected != last_connected;
            if ((net.connected && connected_changed) || (net.portal && portal_changed) ||
                (net.portal && ((now - last_portal_shown) >= kPortalRepeatMs))) {
                build_status_text(status_text, sizeof(status_text), net);
                status_pending = true;
                last_portal_shown = now;
            }
            last_portal = net.portal;
            last_connected = net.connected;
        }

        // Next pass of the queue (priority then age; suspended during quiet hours). It wins over a
        // status pass, which is re-armed for later. One pass at a time: the next one is picked when it ends.
        if (!real_queued && alerts_lock()) {
            alerts::Pass next{};
            const bool got = s_queue.next_pass(now, s_quiet, cfg.alerts.quiet.p3_overrides, next);
            alerts_unlock();
            if (got) {
                pass = next;
                (void)display::transliterate(pass.text, notif_text, sizeof(notif_text));
                pass_width = display::text_width(display::kFont5x7, notif_text);
                const bool on_screen_now =
                    scheduler.notify(pass.priority, static_cast<uint32_t>(pass.duration_s) * 1000U, now);
                real_queued = true;
                if (status_queued || showing_status) {
                    status_queued = false;
                    showing_status = false;
                    status_pending = true;
                }
                if (on_screen_now) {
                    notif_shown = false;  // restart the scroller with this text
                    scroller_running = false;
                }
                ESP_LOGI(kTag, "alert pass \"%s\": P%u, %u s, %s", pass.key, static_cast<unsigned>(pass.priority),
                         static_cast<unsigned>(pass.duration_s), pass.held ? "held" : "one-shot");
            }
        }
        // Status text: a priority-0 pass that waits for the end of the scene.
        if (status_pending && !real_queued && !status_queued) {
            const uint16_t width = display::text_width(display::kFont5x7, status_text);
            (void)scheduler.notify(0U, scroll_duration_ms(width, cfg.display.scroll_speed_px_s), now);
            status_pending = false;
            status_queued = true;
        }

        // Test pattern requests (wizards).
        if (s_test_pattern_set) {
            char name[16];
            taskENTER_CRITICAL(&s_mux);
            std::strncpy(name, s_test_pattern, sizeof(name));
            s_test_pattern_set = false;
            taskEXIT_CRITICAL(&s_mux);
            chaser = (std::strcmp(name, "chenillard") == 0);
            pattern = chaser ? display::TestPattern::None : pattern_from_name(name);
            pattern_since = now;
            chaser_head = 0U;
        }
        const bool testing = chaser || (pattern != display::TestPattern::None);
        if (testing && ((now - pattern_since) >= kTestPatternTimeoutMs)) {
            chaser = false;
            pattern = display::TestPattern::None;
            ESP_LOGI(kTag, "test pattern timed out");
        }

        uint8_t ota_percent = 0U;
        bool ota_active = false;
        taskENTER_CRITICAL(&s_mux);
        ota_percent = s_ota_percent;
        ota_active = s_ota_active;
        taskEXIT_CRITICAL(&s_mux);

        const air_quality::LevelColor lc = air_quality::level_color(air.level);
        const display::Rgb air_color{lc.r, lc.g, lc.b};
        const bool air_shown = air.enabled && air.reading.present;
        display::SceneAvailability avail{};
        avail.date = time_info.sure;
        avail.scroll = false;
        avail.air = air_shown && (cfg.air.logo == settings::AirLogoMode::Scene);
        for (std::size_t i = 0U; i < homeassistant::kValuesMax; ++i) {
            avail.values[i] = ha.values[i].enabled && ha.values[i].valid;
        }
        avail.scroll = avail.values[0] || avail.values[1] || avail.values[2] || avail.values[3] || avail.values[4] ||
                       avail.values[5] || avail.values[6] || avail.values[7];
        const display::Shown shown = scheduler.step(now, avail, scene_finished);
        scene_finished = false;

        const char* scene_name = "Heure";
        uint8_t frame_level = applied;
        fb.clear();
        if (ota_active) {
            // Progress bar: the only feedback if the browser is gone.
            const uint8_t len = static_cast<uint8_t>((static_cast<uint16_t>(ota_percent) * display::kWidth) / 100U);
            fb.fill_rect(0U, 3U, len, 2U, kOtaColor);
            scene_name = "OTA";
        } else if (chaser) {
            if ((now - last_chaser) >= kChaserStepMs) {
                last_chaser = now;
                chaser_head = static_cast<uint16_t>((chaser_head + 1U) % display::kLedCount);
            }
            display::chaser_frame(chaser_head, frame);
            scene_name = "Test";
        } else if (pattern != display::TestPattern::None) {
            display::draw_test_pattern(fb, pattern);
            scene_name = "Test";
        } else if (shown.notification) {
            if (!notif_shown) {
                // A notification display starts: the status pass only if nothing inbound is queued.
                showing_status = status_queued && !real_queued;
                notif_shown = true;
            }
            if (showing_status) {
                if (!scroller_running) {
                    status_scroller.start(status_text, display::kFont5x7, cfg.display.scroll_speed_px_s, 1U);
                    scroller_running = true;
                } else {
                    status_scroller.step(dt);
                }
                status_scroller.draw(fb, 0, kStatusColor);
                scene_name = "Etat";
            } else {
                // Alert pass: the queue decided the rendering (priority, quiet hours); here we only draw it.
                const alerts::Rendering& rd = pass.rendering;
                const display::Rgb color{pass.color[0], pass.color[1], pass.color[2]};
                const uint32_t half_ms = (rd.blink_hz == 2U) ? 250U : 500U;
                const bool blink_off = rd.blink && (((now / half_ms) % 2U) == 1U);
                const display::Icon icon = static_cast<display::Icon>(pass.icon);
                const int16_t x = (icon != display::Icon::None) ? 10 : 0;
                const bool scrolling = rd.scroll && (pass_width > static_cast<uint16_t>(display::kWidth - x));
                if (scrolling) {
                    if (!scroller_running) {
                        notif_scroller.start(notif_text, display::kFont5x7, cfg.display.scroll_speed_px_s, 0U);
                        scroller_running = true;
                    } else {
                        notif_scroller.step(dt);
                    }
                }
                if (!blink_off) {
                    if (scrolling) {
                        notif_scroller.draw(fb, 0, color);
                    } else {
                        if (icon != display::Icon::None) {
                            display::draw_icon(fb, icon, 0, 0, color);
                        }
                        display::draw_text(fb, display::kFont5x7, x, 0, notif_text, color);
                    }
                }
                if (rd.raise_floor && (frame_level < alerts::kCriticalFloor)) {
                    frame_level = alerts::kCriticalFloor;
                }
                scene_name = "Alerte";
            }
        } else {
            if (notif_shown) {
                // The notification on screen just ended.
                if (showing_status) {
                    status_queued = false;
                } else {
                    real_queued = false;
                }
                showing_status = false;
                notif_shown = false;
                scroller_running = false;
            }
            display::SceneKind kind = display::SceneKind::Clock;
            if (shown.scene != display::kSyntheticClock) {
                kind = cfg.display.scenes.entries[shown.scene].kind;
            }
            scene_name = settings::scene_kind_name(kind);
            if (kind != display::SceneKind::Scroll) {
                value_scroll_running = false;
            }
            switch (kind) {
                case display::SceneKind::Date:
                    display::draw_date(fb, time_info, 1U, 1U, kDateColors);
                    break;
                case display::SceneKind::Value: {
                    const clock_homeassistant::ValueSnapshot& v = ha.values[shown.scene != display::kSyntheticClock ? cfg.display.scenes.entries[shown.scene].value_index : 0U];
                    const display::Icon icon = static_cast<display::Icon>(v.icon);
                    int16_t x = 0;
                    if (icon != display::Icon::None) {
                        display::draw_icon(fb, icon, 0, 0, kValueIconColor);
                        x = 10;
                    }
                    char text[display::kTextMax + 1U];
                    (void)display::transliterate(v.valid ? v.text : "--", text, sizeof(text));
                    const uint16_t room = static_cast<uint16_t>(display::kWidth - x);
                    const bool big = display::text_width(display::kFont5x7, text) <= room;
                    display::draw_text(fb, big ? display::kFont5x7 : display::kFont3x5, x, big ? 0 : 1, text,
                                       v.valid ? kClockColors.digits : kDimColor);
                    break;
                }
                case display::SceneKind::TwoLines: {
                    // Values 1 and 2 on two rows of 3x4.
                    char text[display::kTextMax + 1U];
                    for (uint8_t row = 0U; row < 2U; ++row) {
                        const clock_homeassistant::ValueSnapshot& v = ha.values[row];
                        (void)display::transliterate((v.enabled && v.valid) ? v.text : "--", text, sizeof(text));
                        display::draw_text(fb, display::kFont3x4, 0, static_cast<int16_t>(row * 4U), text,
                                           (row == 0U) ? kClockColors.digits : kDateColors.weekday);
                    }
                    break;
                }
                case display::SceneKind::Scroll: {
                    // All valid values, "NAME value", scrolled once; the scene ends with the text.
                    if (!value_scroll_running) {
                        char all[display::kTextMax + 1U] = "";
                        std::size_t w = 0U;
                        for (std::size_t i = 0U; i < homeassistant::kValuesMax; ++i) {
                            const clock_homeassistant::ValueSnapshot& v = ha.values[i];
                            if (!v.enabled || !v.valid) {
                                continue;
                            }
                            const int n = std::snprintf(all + w, sizeof(all) - w, "%s%s %s", (w > 0U) ? "   " : "", v.name, v.text);
                            if ((n <= 0) || (static_cast<std::size_t>(n) >= (sizeof(all) - w))) {
                                break;
                            }
                            w += static_cast<std::size_t>(n);
                        }
                        (void)display::transliterate(all, value_scroll_text, sizeof(value_scroll_text));
                        value_scroller.start(value_scroll_text, display::kFont5x7, cfg.display.scroll_speed_px_s, 1U);
                        value_scroll_running = true;
                    } else {
                        value_scroller.step(dt);
                    }
                    value_scroller.draw(fb, 0, kStatusColor);
                    scene_finished = value_scroller.finished();
                    break;
                }
                case display::SceneKind::Night: {
                    // Dim, steady clock (no blink, no bar): for a bedroom.
                    display::ClockColors dim{display::scale(kClockColors.digits, kNightScale), display::scale(kClockColors.colon, kNightScale),
                                             display::scale(kClockColors.seconds_bar, kNightScale)};
                    display::draw_clock(fb, time_info, display::kClockCentredX, true, false, dim);
                    break;
                }
                case display::SceneKind::Air: {
                    // 8x8 logo in the colour of the level, then the pilot value (or -- while invalid).
                    display::draw_air_logo_8(fb, 0, 0, air_color);
                    const air_quality::PilotValue pv = air_quality::pilot_value(cfg.air.levels, air.reading);
                    char text[16];
                    if (!pv.valid) {
                        (void)std::snprintf(text, sizeof(text), "--");
                    } else if (cfg.air.levels.pilot == air_quality::PilotQuantity::Aqi) {
                        (void)std::snprintf(text, sizeof(text), "AQI %u", static_cast<unsigned>(pv.value));
                    } else {
                        (void)std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(pv.value));
                    }
                    const display::Font& font = (display::text_width(display::kFont5x7, text) <= 22U) ? display::kFont5x7 : display::kFont3x5;
                    display::draw_text(fb, font, 10, (&font == &display::kFont5x7) ? 0 : 1, text, pv.valid ? kClockColors.digits : air_color);
                    break;
                }
                case display::SceneKind::Clock:
                default: {
                    const bool colon_on = !cfg.display.colon_blink || (((now / kColonHalfPeriodMs) % 2U) == 0U);
                    // Icon zone: a held alert first (the "!" stays between two passes), else the air logo.
                    uint8_t held_priority = 0U;
                    bool held = false;
                    if (alerts_lock()) {
                        held = s_queue.has_held();
                        held_priority = s_queue.held_priority();
                        alerts_unlock();
                    }
                    const display::IconZone zone =
                        display::choose_icon_zone(held, air_shown && (cfg.air.logo == settings::AirLogoMode::IconZone));
                    display::draw_clock(fb, time_info, (zone != display::IconZone::None) ? 0U : display::kClockCentredX, colon_on,
                                        cfg.display.seconds_bar, kClockColors);
                    if (zone == display::IconZone::Alert) {
                        display::draw_alert_mark(fb, kAirLogoX, kAirLogoY, kPriorityColor[(held_priority > 3U) ? 3U : held_priority]);
                    } else if (zone == display::IconZone::Air) {
                        display::draw_air_logo_6(fb, kAirLogoX, kAirLogoY, air_color);
                    } else {
                        // centred clock, nothing in the zone
                    }
                    break;
                }
            }
        }

        if (!chaser) {
            (void)display::render(fb, frame_level, cfg.display.budget_ma, cfg.display.map, frame, stats);
        }
        const esp_err_t show_err = panel.show(frame);
        if (show_err != ESP_OK) {
            ESP_LOGW(kTag, "panel refresh failed: %s", esp_err_to_name(show_err));
        }

        taskENTER_CRITICAL(&s_mux);
        s_shared.ldr_raw = sample.raw;
        s_shared.ldr_valid = sample.valid;
        s_shared.ldr_mv = ldr.last_mv();
        s_shared.ldr_filtered = lum.filtered_raw;
        s_shared.level = applied;
        s_shared.ldr_fault = lum.sensor_fault;
        s_shared.estimated_ma = stats.estimated_ua / 1000U;
        s_shared.budget_ma = cfg.display.budget_ma;
        s_shared.limited = stats.limited;
        s_shared.mode = cfg.brightness.mode;
        std::strncpy(s_shared.scene, scene_name, sizeof(s_shared.scene) - 1U);
        taskEXIT_CRITICAL(&s_mux);

        if ((now - last_status_log) >= kStatusPeriodMs) {
            last_status_log = now;
            ESP_LOGI(kTag, "%s: %lu mA, limited=%d | ldr %u -> level %u fault=%d | net %s | time %s %02u:%02u:%02u",
                     scene_name, static_cast<unsigned long>(stats.estimated_ua / 1000U), stats.limited ? 1 : 0,
                     static_cast<unsigned>(sample.raw), static_cast<unsigned>(applied), lum.sensor_fault ? 1 : 0,
                     clock_web::info().state, ts.sure ? "sure" : "unsure", static_cast<unsigned>(ts.local.hour),
                     static_cast<unsigned>(ts.local.minute), static_cast<unsigned>(ts.local.second));
        }
        vTaskDelay(ticks_from_ms(kFramePeriodMs));
    }
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "matrix_clock, board %s, LED data GPIO%u, LDR GPIO%u", clock_board::kName,
             static_cast<unsigned>(clock_board::kLedDataGpio), static_cast<unsigned>(clock_board::kLdrAdcGpio));

    s_alerts_mutex = create_alerts_mutex();
    if (s_alerts_mutex == nullptr) {
        ESP_LOGE(kTag, "alert mutex not created: notifications disabled");
    }
    if (xTaskCreatePinnedToCore(display_task, "display", kDisplayTaskStack, nullptr, kDisplayTaskPriority, nullptr,
                                kDisplayCore) != pdPASS) {
        ESP_LOGE(kTag, "display task not created");
    }

    clock_web::Hooks hooks{};
    hooks.on_notification = on_notification;
    hooks.status_json = status_json;
    hooks.test_pattern = on_test_pattern;
    hooks.ldr_raw = on_ldr_raw;
    hooks.ota_progress = on_ota_progress;
    hooks.air_raw_temperature = on_air_raw_temperature;
    hooks.mqtt_discovery = on_mqtt_discovery;
    const clock_web::WebStatus web = clock_web::start(hooks);
    switch (web) {
        case clock_web::WebStatus::Ok:
            ESP_LOGI(kTag, "network and web server started");
            break;
        case clock_web::WebStatus::NvsError:
            ESP_LOGE(kTag, "NVS unavailable: no network");
            break;
        case clock_web::WebStatus::NetError:
            ESP_LOGE(kTag, "network failed: no web page");
            break;
        case clock_web::WebStatus::HttpError:
        default:
            ESP_LOGE(kTag, "HTTP server failed");
            break;
    }

    if (xTaskCreatePinnedToCore(air_task, "air", kAirTaskStack, nullptr, kAirTaskPriority, nullptr, kAirCore) != pdPASS) {
        ESP_LOGE(kTag, "air task not created");
    }
    if ((web != clock_web::WebStatus::NvsError) && (web != clock_web::WebStatus::NetError)) {
        const esp_err_t ha_err = clock_homeassistant::start(on_notification);
        if (ha_err != ESP_OK) {
            ESP_LOGE(kTag, "home assistant link not started: %s", esp_err_to_name(ha_err));
        }
        const esp_err_t mqtt_err = clock_homeassistant::mqtt_start(fill_mqtt_state);
        if (mqtt_err != ESP_OK) {
            ESP_LOGE(kTag, "mqtt publication not started: %s", esp_err_to_name(mqtt_err));
        }
    }

    if ((web != clock_web::WebStatus::NvsError) && (web != clock_web::WebStatus::NetError)) {
        const settings::SystemSettings sys = clock_settings::system();
        const char* posix = timekeeping::posix_for_zone(sys.timezone);
        const esp_err_t time_err = clock_time::start(sys.ntp_server, (posix != nullptr) ? posix : timekeeping::kEuropeZurich);
        if (time_err != ESP_OK) {
            ESP_LOGW(kTag, "time source: %s", esp_err_to_name(time_err));
        }
    }
}
