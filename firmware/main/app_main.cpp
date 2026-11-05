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
#include "freertos/task.h"

#include "alerts/notification.hpp"
#include "brightness/brightness.hpp"
#include "clock_board/board.hpp"
#include "clock_display/led_panel.hpp"
#include "clock_hal/ldr_adc.hpp"
#include "clock_settings/store.hpp"
#include "clock_time/time_source.hpp"
#include "clock_web/web.hpp"
#include "display/clock_scene.hpp"
#include "display/color.hpp"
#include "display/current_limit.hpp"
#include "display/frame_buffer.hpp"
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

// Colours (a theme, until the page lets the user choose them).
constexpr display::ClockColors kClockColors{display::Rgb{255U, 255U, 255U}, display::Rgb{255U, 160U, 40U},
                                            display::Rgb{40U, 80U, 200U}};
constexpr display::DateColors kDateColors{display::Rgb{255U, 160U, 40U}, display::Rgb{255U, 255U, 255U}};
constexpr display::Rgb kStatusColor{90U, 160U, 255U};
constexpr display::Rgb kOtaColor{255U, 255U, 255U};
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
alerts::Notification s_pending_notification{};
bool s_pending_valid = false;
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

bool on_notification(const alerts::Notification& n) {
    taskENTER_CRITICAL(&s_mux);
    s_pending_notification = n;
    s_pending_valid = true;
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(kTag, "notification \"%s\": priority %u, %u s%s%s", n.key, static_cast<unsigned>(n.priority),
             static_cast<unsigned>(n.duration_s), n.persistent ? ", held" : "", n.clear ? ", clear" : "");
    return true;
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
    static char status_text[display::kTextMax + 1U];
    static char notif_text[display::kTextMax + 1U];
    static alerts::Notification notif{};
    static settings::Settings cfg = settings::default_settings();
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
    uint16_t chaser_head = 0U;
    uint32_t pattern_since = 0U;
    uint32_t last_chaser = 0U;
    uint32_t last_frame = now_ms();
    uint32_t last_status_log = last_frame;
    uint32_t last_sensor = last_frame - kSensorPeriodMs;
    uint8_t applied = kInitialBrightness;
    brightness::Sample sample{0U, false};
    brightness::Output lum{};

    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t dt = now - last_frame;
        last_frame = now;

        // Settings changed (page): re-apply without reboot.
        const uint32_t version = clock_settings::version();
        if (version != applied_version) {
            applied_version = version;
            cfg = clock_settings::get();
            brightness_ctl = brightness::Controller(cfg.brightness);
            scheduler = display::Scheduler(cfg.display.scenes, now);
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

        // Sensor and brightness at 1 Hz.
        if ((now - last_sensor) >= kSensorPeriodMs) {
            last_sensor = now;
            sample = ldr.read();
            const uint16_t minute_of_day = static_cast<uint16_t>(ts.local.hour * 60U + ts.local.minute);
            lum = brightness_ctl.step(sample, applied, time_info.sure, minute_of_day);
            applied = lum.level;

            // Network status: once on each change, every minute in portal mode.
            const clock_web::NetworkInfo net = clock_web::info();
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

        // Inbound notification (one slot). It wins over a status pass, which is re-armed for later.
        if (s_pending_valid) {
            taskENTER_CRITICAL(&s_mux);
            notif = s_pending_notification;
            s_pending_valid = false;
            taskEXIT_CRITICAL(&s_mux);
            if (!notif.clear) {
                (void)display::transliterate((notif.text[0] != '\0') ? notif.text : notif.title, notif_text, sizeof(notif_text));
                const bool on_screen_now =
                    scheduler.notify(notif.priority, static_cast<uint32_t>(notif.duration_s) * 1000U, now);
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

        display::SceneAvailability avail{};
        avail.date = time_info.sure;
        avail.scroll = false;
        const display::Shown shown = scheduler.step(now, avail, false);

        const char* scene_name = "Heure";
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
            display::Scroller& sc = showing_status ? status_scroller : notif_scroller;
            if (!scroller_running) {
                sc.start(showing_status ? status_text : notif_text, display::kFont5x7, cfg.display.scroll_speed_px_s,
                         showing_status ? 1U : 0U);
                scroller_running = true;
            } else {
                sc.step(dt);
            }
            const uint8_t p = (notif.priority > 3U) ? 3U : notif.priority;
            sc.draw(fb, 0, showing_status ? kStatusColor : kPriorityColor[p]);
            scene_name = showing_status ? "Etat" : "Notification";
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
            switch (kind) {
                case display::SceneKind::Date:
                    display::draw_date(fb, time_info, 1U, 1U, kDateColors);
                    break;
                case display::SceneKind::Clock:
                default: {
                    const bool colon_on = !cfg.display.colon_blink || (((now / kColonHalfPeriodMs) % 2U) == 0U);
                    display::draw_clock(fb, time_info, display::kClockCentredX, colon_on, cfg.display.seconds_bar,
                                        kClockColors);
                    break;
                }
            }
        }

        display::RenderStats stats{};
        if (!chaser) {
            (void)display::render(fb, applied, cfg.display.budget_ma, cfg.display.map, frame, stats);
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

    if ((web != clock_web::WebStatus::NvsError) && (web != clock_web::WebStatus::NetError)) {
        const settings::SystemSettings sys = clock_settings::get().system;
        const char* posix = timekeeping::posix_for_zone(sys.timezone);
        const esp_err_t time_err = clock_time::start(sys.ntp_server, (posix != nullptr) ? posix : timekeeping::kEuropeZurich);
        if (time_err != ESP_OK) {
            ESP_LOGW(kTag, "time source: %s", esp_err_to_name(time_err));
        }
    }
}
