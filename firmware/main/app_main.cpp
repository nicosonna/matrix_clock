/**
 * @file app_main.cpp
 * @brief Entry point: wires the hardware adapters to the pure logic.
 *
 * Current state (phase 1, step 2): brings the LED panel up and cycles the
 * commissioning test patterns (single pixel, first column, diagonal, full
 * white) so the wiring and the panel mapping can be checked by eye. The
 * display loop will move to its own task once the scene scheduler exists.
 */

#include <cstdint>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clock_board/board.hpp"
#include "clock_display/led_panel.hpp"
#include "display/color.hpp"
#include "display/current_limit.hpp"
#include "display/frame_buffer.hpp"
#include "display/panel_map.hpp"
#include "display/render.hpp"

namespace {

constexpr char kTag[] = "main";

/// pdMS_TO_TICKS() expands to C-style casts that trip -Wold-style-cast; same arithmetic.
constexpr TickType_t ticks_from_ms(uint32_t ms) noexcept {
    return static_cast<TickType_t>((ms * static_cast<uint32_t>(CONFIG_FREERTOS_HZ)) / 1000U);
}

/// Brightness used for the commissioning patterns: visible, not blinding.
constexpr uint8_t kTestBrightness = 48U;
/// Time each pattern stays on screen.
constexpr uint32_t kPatternPeriodMs = 3000U;
/// Number of commissioning patterns.
constexpr uint8_t kPatternCount = 4U;

/// Draws commissioning pattern @p index into @p fb (see spec-affichage.md, mapping wizard).
void draw_pattern(uint8_t index, display::FrameBuffer& fb) noexcept {
    fb.clear();
    switch (index) {
        case 0U:  // one pixel: where is (0, 0)?
            fb.set(0U, 0U, display::kWhite);
            break;
        case 1U:  // first eight LEDs of the strip: a column or a row?
            for (uint8_t y = 0U; y < display::kHeight; ++y) {
                fb.set(0U, y, display::Rgb{0U, 0U, 255U});
            }
            break;
        case 2U:  // full diagonal: straight, or broken every 8 pixels?
            for (uint8_t y = 0U; y < display::kHeight; ++y) {
                for (uint8_t k = 0U; k < 4U; ++k) {
                    fb.set(static_cast<uint8_t>(y * 4U + k), y, display::Rgb{0U, 255U, 0U});
                }
            }
            break;
        default:  // full white: current estimate and limiter at work
            fb.fill_rect(0U, 0U, display::kWidth, display::kHeight, display::kWhite);
            break;
    }
}

/// Chaser step period: 256 LEDs x 40 ms = about 10 s per sweep.
constexpr uint32_t kChaserStepMs = 40U;
/// Sweeps of the physical chaser before the logical patterns are shown again.
constexpr uint8_t kChaserSweeps = 2U;
/// Dim linear values: the chaser bypasses gamma and the limiter on purpose.
constexpr display::Rgb kMarkerFirst{40U, 0U, 0U};   ///< LED 0: red, fixed.
constexpr display::Rgb kMarkerLast{0U, 0U, 40U};    ///< LED 255: blue, fixed.
constexpr uint8_t kChaserTail = 4U;
constexpr uint8_t kChaserHead = 48U;

/**
 * @brief Runs a white dot along the strip in **physical** order, mapping ignored.
 *
 * Reveals the real wiring: where LED 0 sits, which way the strip runs, where it
 * turns. LED 0 stays red and LED 255 stays blue so the two ends are obvious.
 */
void run_physical_chaser(clock_display::LedPanel& panel, display::PhysicalFrame& frame) noexcept {
    for (uint8_t sweep = 0U; sweep < kChaserSweeps; ++sweep) {
        for (uint16_t head = 0U; head < display::kLedCount; ++head) {
            for (display::Rgb& p : frame) {
                p = display::kBlack;
            }
            for (uint8_t t = 0U; t < kChaserTail; ++t) {
                if (head >= t) {
                    const uint8_t v = static_cast<uint8_t>(kChaserHead >> t);  // 48, 24, 12, 6
                    frame[static_cast<uint16_t>(head - t)] = display::Rgb{v, v, v};
                }
            }
            frame[0] = kMarkerFirst;
            frame[display::kLedCount - 1U] = kMarkerLast;
            (void)panel.show(frame);
            vTaskDelay(ticks_from_ms(kChaserStepMs));
        }
    }
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

    static display::FrameBuffer fb;
    static display::PhysicalFrame frame;
    const display::PanelMapConfig map = display::default_panel_map();
    uint8_t pattern = 0U;

    for (;;) {
        if (pattern == 0U) {
            ESP_LOGI(kTag, "physical chaser: LED 0 red, LED 255 blue, white dot in strip order");
            run_physical_chaser(panel, frame);
        }
        draw_pattern(pattern, fb);
        display::RenderStats stats{};
        (void)display::render(fb, kTestBrightness, display::kDefaultBudgetMa, map, frame, stats);
        const esp_err_t show_err = panel.show(frame);
        if (show_err != ESP_OK) {
            ESP_LOGW(kTag, "panel refresh failed: %s", esp_err_to_name(show_err));
        }
        ESP_LOGI(kTag, "pattern %u: estimated %lu mA, applied %lu mA, limited=%d, scale=%u/256",
                 static_cast<unsigned>(pattern), static_cast<unsigned long>(stats.estimated_ua / 1000U),
                 static_cast<unsigned long>(stats.applied_ua / 1000U), stats.limited ? 1 : 0,
                 static_cast<unsigned>(stats.scale_q8));
        pattern = static_cast<uint8_t>((pattern + 1U) % kPatternCount);
        vTaskDelay(ticks_from_ms(kPatternPeriodMs));
    }
}
