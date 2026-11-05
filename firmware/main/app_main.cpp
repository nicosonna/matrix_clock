/**
 * @file app_main.cpp
 * @brief Entry point: wires the hardware adapters to the pure logic.
 *
 * Current state (phase 1, step 5):
 *  - a **display task** on core 1 at 25 fps, above every network task (EX-61):
 *    scene scheduler, brightness control at 1 Hz, inbound notifications;
 *  - the network (Wi-Fi station / captive portal) and the HTTP server started
 *    from app_main on core 0; a failure there never stops the display.
 * No time source yet: the clock scene honestly shows `--:--`.
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
#include "clock_time/time_source.hpp"
#include "clock_web/web.hpp"
#include "timekeeping/tz.hpp"
#include "display/clock_scene.hpp"
#include "display/color.hpp"
#include "display/current_limit.hpp"
#include "display/frame_buffer.hpp"
#include "display/panel_map.hpp"
#include "display/render.hpp"
#include "display/scheduler.hpp"
#include "display/scroll.hpp"
#include "display/text.hpp"

namespace {

constexpr char kTag[] = "main";

/// pdMS_TO_TICKS() expands to C-style casts that trip -Wold-style-cast; same arithmetic.
constexpr TickType_t ticks_from_ms(uint32_t ms) noexcept {
    return static_cast<TickType_t>((ms * static_cast<uint32_t>(CONFIG_FREERTOS_HZ)) / 1000U);
}

/// 25 frames per second.
constexpr uint32_t kFramePeriodMs = 40U;
/// Brightness before the first sensor reading: visible, not blinding.
constexpr uint8_t kInitialBrightness = 48U;
/// Sensor sampling period: the brightness controller is tuned for 1 Hz.
constexpr uint32_t kSensorPeriodMs = 1000U;
/// Colon blink: 1 Hz, half period.
constexpr uint32_t kColonHalfPeriodMs = 500U;
/// Diagnostics log and network status refresh period.
constexpr uint32_t kStatusPeriodMs = 5000U;
/// Display task: core 1, above the HTTP server (5) and the network tasks (2-3).
constexpr UBaseType_t kDisplayTaskPriority = 6U;
constexpr uint32_t kDisplayTaskStack = 6144U;
constexpr BaseType_t kDisplayCore = 1;

constexpr display::Rgb kClockColor{255U, 255U, 255U};
constexpr display::Rgb kScrollColor{255U, 160U, 40U};
/// Notification colour by priority: info, notable, important, critical.
constexpr display::Rgb kPriorityColor[4] = {
    display::Rgb{90U, 160U, 255U}, display::Rgb{255U, 190U, 40U}, display::Rgb{255U, 110U, 0U},
    display::Rgb{255U, 30U, 30U}};

/// Milliseconds since boot, 32-bit wrap tolerated by every consumer.
uint32_t now_ms() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

/// Confirms this image to the bootloader (no-op when not pending verification).
void confirm_ota_image() noexcept {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if ((running != nullptr) && (esp_ota_get_state_partition(running, &state) == ESP_OK) &&
        (state == ESP_OTA_IMG_PENDING_VERIFY)) {
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(kTag, "OTA image confirmed (%s)", esp_err_to_name(err));
    }
}

/// NTP server until the settings page exists.
constexpr char kNtpServer[] = "pool.ntp.org";

/// Demo rotation until settings exist: clock 6 s, date 4 s, one pass of the status text.
display::SceneList demo_scene_list() noexcept {
    display::SceneList l{};
    l.entries[0] = display::SceneEntry{display::SceneKind::Clock, 0U, 6U, true};
    l.entries[1] = display::SceneEntry{display::SceneKind::Date, 0U, 4U, true};
    l.entries[2] = display::SceneEntry{display::SceneKind::Scroll, 0U, 0U, true};
    l.count = 3U;
    return l;
}

/// Maps the time source's snapshot onto the display's model.
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
// Inbound notifications: handed over from the HTTP task to the display task
// through one slot under a critical section (the real queue comes with `alerts`).
portMUX_TYPE s_notif_mux = portMUX_INITIALIZER_UNLOCKED;
alerts::Notification s_pending_notification{};
bool s_pending_valid = false;

bool on_notification(const alerts::Notification& n) {
    taskENTER_CRITICAL(&s_notif_mux);
    s_pending_notification = n;
    s_pending_valid = true;
    taskEXIT_CRITICAL(&s_notif_mux);
    ESP_LOGI(kTag, "notification \"%s\": priority %u, %u s%s%s", n.key, static_cast<unsigned>(n.priority),
             static_cast<unsigned>(n.duration_s), n.persistent ? ", held" : "", n.clear ? ", clear" : "");
    return true;
}

bool take_notification(alerts::Notification& out) {
    bool got = false;
    taskENTER_CRITICAL(&s_notif_mux);
    if (s_pending_valid) {
        out = s_pending_notification;
        s_pending_valid = false;
        got = true;
    }
    taskEXIT_CRITICAL(&s_notif_mux);
    return got;
}

/// Status text for the scrolling scene: how to reach the clock.
void build_status_text(char* out, std::size_t out_size) {
    const clock_web::NetworkInfo net = clock_web::info();
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

void display_task(void* /*arg*/) {
    static clock_display::LedPanel panel;
    const esp_err_t init_err = panel.init();
    if (init_err != ESP_OK) {
        ESP_LOGE(kTag, "LED panel init failed: %s — nothing will be displayed", esp_err_to_name(init_err));
        for (;;) {
            vTaskDelay(ticks_from_ms(1000U));
        }
    }
    (void)panel.clear();  // an uninitialised panel shows coloured noise
    confirm_ota_image();

    static clock_hal::LdrAdc ldr;
    const esp_err_t ldr_err = ldr.init();
    if (ldr_err != ESP_OK) {
        ESP_LOGW(kTag, "LDR unavailable (%s): fallback brightness", esp_err_to_name(ldr_err));
    }
    static brightness::Controller brightness_ctl(brightness::default_config());

    static display::FrameBuffer fb;
    static display::PhysicalFrame frame;
    static display::Scroller scroller;
    static display::Scroller notif_scroller;
    static char scroll_text[display::kTextMax + 1U];
    static char notif_text[display::kTextMax + 1U];
    static alerts::Notification notif{};
    build_status_text(scroll_text, sizeof(scroll_text));

    const display::PanelMapConfig map = display::default_panel_map();
    const display::SceneList scenes = demo_scene_list();
    display::Scheduler scheduler(scenes, now_ms());
    display::TimeInfo time_info{false, 0U, 0U, 0U, 1U, 1U, 0U};
    bool was_sure = false;

    uint8_t last_scene = display::kSyntheticClock;
    bool scroll_started = false;
    bool notif_shown = false;
    uint32_t last_frame = now_ms();
    uint32_t last_status = last_frame;
    uint32_t last_sensor = last_frame - kSensorPeriodMs;
    uint8_t applied = kInitialBrightness;
    brightness::Sample sample{0U, false};
    brightness::Output lum{};

    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t dt = now - last_frame;
        last_frame = now;

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

        if ((now - last_sensor) >= kSensorPeriodMs) {
            last_sensor = now;
            sample = ldr.read();
            const uint16_t minute_of_day = static_cast<uint16_t>(ts.local.hour * 60U + ts.local.minute);
            lum = brightness_ctl.step(sample, applied, time_info.sure, minute_of_day);
            applied = lum.level;
        }

        if (take_notification(notif) && !notif.clear) {
            (void)display::transliterate(notif.text[0] != '\0' ? notif.text : notif.title, notif_text, sizeof(notif_text));
            (void)scheduler.notify(notif.priority, static_cast<uint32_t>(notif.duration_s) * 1000U, now);
            notif_shown = false;
        }

        display::SceneAvailability avail{};
        avail.date = time_info.sure;
        avail.scroll = true;
        const bool scroll_done = scroll_started && scroller.finished();
        const display::Shown shown = scheduler.step(now, avail, scroll_done);

        fb.clear();
        if (shown.notification) {
            if (!notif_shown) {
                notif_scroller.start(notif_text, display::kFont5x7, display::kScrollSpeedDefault, 0U);
                notif_shown = true;
            } else {
                notif_scroller.step(dt);
            }
            const uint8_t p = (notif.priority > 3U) ? 3U : notif.priority;
            notif_scroller.draw(fb, 0, kPriorityColor[p]);
        } else {
            notif_shown = false;
            display::SceneKind kind = display::SceneKind::Clock;
            if (shown.scene != display::kSyntheticClock) {
                kind = scenes.entries[shown.scene].kind;
            }
            if (shown.scene != last_scene) {
                scroll_started = false;  // a scene restarts from its beginning
                last_scene = shown.scene;
            }
            switch (kind) {
                case display::SceneKind::Scroll:
                    if (!scroll_started) {
                        build_status_text(scroll_text, sizeof(scroll_text));
                        scroller.start(scroll_text, display::kFont5x7, display::kScrollSpeedDefault, 1U);
                        scroll_started = true;
                    } else {
                        scroller.step(dt);
                    }
                    scroller.draw(fb, 0, kScrollColor);
                    break;
                case display::SceneKind::Date:
                    display::draw_date(fb, time_info, 1U, 1U, kClockColor);
                    break;
                case display::SceneKind::Clock:
                default: {
                    const bool colon_on = ((now / kColonHalfPeriodMs) % 2U) == 0U;
                    display::draw_clock(fb, time_info, display::kClockCentredX, colon_on, true, kClockColor);
                    break;
                }
            }
        }

        display::RenderStats stats{};
        (void)display::render(fb, applied, display::kDefaultBudgetMa, map, frame, stats);
        const esp_err_t show_err = panel.show(frame);
        if (show_err != ESP_OK) {
            ESP_LOGW(kTag, "panel refresh failed: %s", esp_err_to_name(show_err));
        }
        if ((now - last_status) >= kStatusPeriodMs) {
            last_status = now;
            ESP_LOGI(kTag, "estimated %lu mA, limited=%d, oob=%lu | ldr raw=%u filtered=%u -> level %u fault=%d | net %s | time %s %02u:%02u:%02u",
                     static_cast<unsigned long>(stats.estimated_ua / 1000U), stats.limited ? 1 : 0,
                     static_cast<unsigned long>(fb.out_of_bounds_count()), static_cast<unsigned>(sample.raw),
                     static_cast<unsigned>(lum.filtered_raw), static_cast<unsigned>(applied), lum.sensor_fault ? 1 : 0,
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

    // The display first: whatever happens to the network, the clock shows something.
    if (xTaskCreatePinnedToCore(display_task, "display", kDisplayTaskStack, nullptr, kDisplayTaskPriority, nullptr,
                                kDisplayCore) != pdPASS) {
        ESP_LOGE(kTag, "display task not created");
    }

    const clock_web::WebStatus web = clock_web::start(on_notification);
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

    // SNTP needs the network interfaces; without them the clock keeps showing --:--.
    if (web != clock_web::WebStatus::NvsError && web != clock_web::WebStatus::NetError) {
        const esp_err_t time_err = clock_time::start(kNtpServer, timekeeping::kEuropeZurich);
        if (time_err != ESP_OK) {
            ESP_LOGW(kTag, "time source: %s", esp_err_to_name(time_err));
        }
    }
}
