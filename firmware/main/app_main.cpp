/**
 * @file app_main.cpp
 * @brief Entry point: wires the hardware adapters to the pure logic.
 *
 * Current state (phase 1, step 3): a 25 fps display loop driven by the scene
 * scheduler. There is no time source yet, so the clock scene honestly shows
 * `--:--`, alternating with a scrolling text. The display loop will move to its
 * own task when the network tasks arrive (EX-61).
 */

#include <cstdint>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "brightness/brightness.hpp"
#include "clock_board/board.hpp"
#include "clock_display/led_panel.hpp"
#include "clock_hal/ldr_adc.hpp"
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
/// Diagnostics log period.
constexpr uint32_t kLogPeriodMs = 5000U;

constexpr display::Rgb kClockColor{255U, 255U, 255U};
constexpr display::Rgb kScrollColor{255U, 160U, 40U};

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

/// Demo rotation until settings exist: the clock for 6 s, then one pass of text.
display::SceneList demo_scene_list() noexcept {
    display::SceneList l{};
    l.entries[0] = display::SceneEntry{display::SceneKind::Clock, 0U, 6U, true};
    l.entries[1] = display::SceneEntry{display::SceneKind::Scroll, 0U, 0U, true};
    l.count = 2U;
    return l;
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "matrix_clock, board %s, LED data GPIO%u, LDR GPIO%u", clock_board::kName,
             static_cast<unsigned>(clock_board::kLedDataGpio),
             static_cast<unsigned>(clock_board::kLdrAdcGpio));

    static clock_display::LedPanel panel;
    const esp_err_t init_err = panel.init();
    if (init_err != ESP_OK) {
        ESP_LOGE(kTag, "LED panel init failed: %s — nothing will be displayed",
                 esp_err_to_name(init_err));
        for (;;) {
            vTaskDelay(ticks_from_ms(1000U));
        }
    }
    // A panel left uninitialised shows coloured noise: clear it explicitly first.
    (void)panel.clear();
    confirm_ota_image();

    // Light sensor: a failure here is not fatal, the controller falls back on its own.
    static clock_hal::LdrAdc ldr;
    const esp_err_t ldr_err = ldr.init();
    if (ldr_err != ESP_OK) {
        ESP_LOGW(kTag, "LDR unavailable (%s): fallback brightness", esp_err_to_name(ldr_err));
    }
    static brightness::Controller brightness_ctl(brightness::default_config());

    static display::FrameBuffer fb;
    static display::PhysicalFrame frame;
    static display::Scroller scroller;
    static char scroll_text[display::kTextMax + 1U];
    // "\xC3\xA9" is split from the following 'e': a hex digit would extend the escape.
    (void)display::transliterate("Matrix clock \xE2\x80\x94 ESP-IDF \xE2\x80\x94 heure non synchronis\xC3\xA9" "e",
                                 scroll_text, sizeof(scroll_text));

    const display::PanelMapConfig map = display::default_panel_map();
    const display::SceneList scenes = demo_scene_list();
    display::Scheduler scheduler(scenes, now_ms());

    // No time source yet: the clock must say so.
    const display::TimeInfo time_info{false, 0U, 0U, 0U, 1U, 1U, 0U};

    uint8_t last_scene = display::kSyntheticClock;
    bool scroll_started = false;
    uint32_t last_frame = now_ms();
    uint32_t last_log = last_frame;
    uint32_t last_sensor = last_frame - kSensorPeriodMs;  // read on the first frame
    uint8_t applied = kInitialBrightness;
    brightness::Sample sample{0U, false};
    brightness::Output lum{};

    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t dt = now - last_frame;
        last_frame = now;

        if ((now - last_sensor) >= kSensorPeriodMs) {
            last_sensor = now;
            sample = ldr.read();
            lum = brightness_ctl.step(sample, applied, time_info.sure, 0U);
            applied = lum.level;
        }

        display::SceneAvailability avail{};
        avail.date = time_info.sure;
        avail.scroll = true;
        const bool scroll_done = scroll_started && scroller.finished();
        const display::Shown shown = scheduler.step(now, avail, scroll_done);

        display::SceneKind kind = display::SceneKind::Clock;
        if (!shown.notification && (shown.scene != display::kSyntheticClock)) {
            kind = scenes.entries[shown.scene].kind;
        }
        if (shown.scene != last_scene) {
            scroll_started = false;  // a scene restarts from its beginning
            last_scene = shown.scene;
        }

        fb.clear();
        switch (kind) {
            case display::SceneKind::Scroll:
                if (!scroll_started) {
                    scroller.start(scroll_text, display::kFont5x7, display::kScrollSpeedDefault, 1U);
                    scroll_started = true;
                } else {
                    scroller.step(dt);
                }
                scroller.draw(fb, 0, kScrollColor);
                break;
            case display::SceneKind::Clock:
            default: {
                const bool colon_on = ((now / kColonHalfPeriodMs) % 2U) == 0U;
                display::draw_clock(fb, time_info, display::kClockCentredX, colon_on, true, kClockColor);
                break;
            }
        }

        display::RenderStats stats{};
        (void)display::render(fb, applied, display::kDefaultBudgetMa, map, frame, stats);
        const esp_err_t show_err = panel.show(frame);
        if (show_err != ESP_OK) {
            ESP_LOGW(kTag, "panel refresh failed: %s", esp_err_to_name(show_err));
        }
        if ((now - last_log) >= kLogPeriodMs) {
            last_log = now;
            ESP_LOGI(kTag, "scene %u: estimated %lu mA, limited=%d, oob=%lu | ldr raw=%u (%d mV, valid=%d) filtered=%u -> level %u fault=%d",
                     static_cast<unsigned>(kind), static_cast<unsigned long>(stats.estimated_ua / 1000U),
                     stats.limited ? 1 : 0, static_cast<unsigned long>(fb.out_of_bounds_count()),
                     static_cast<unsigned>(sample.raw), ldr.last_mv(), sample.valid ? 1 : 0,
                     static_cast<unsigned>(lum.filtered_raw), static_cast<unsigned>(applied),
                     lum.sensor_fault ? 1 : 0);
        }
        vTaskDelay(ticks_from_ms(kFramePeriodMs));
    }
}
