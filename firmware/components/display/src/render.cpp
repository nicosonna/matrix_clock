/**
 * @file render.cpp
 * @brief Rendering pipeline implementation.
 */

#include "display/render.hpp"

#include "display/gamma.hpp"

namespace display {

bool render(const FrameBuffer& fb, uint8_t brightness, uint16_t budget_ma,
            const PanelMapConfig& map, PhysicalFrame& out, RenderStats& stats) noexcept {
    stats = RenderStats{};
    if (!is_valid(map)) {
        for (Rgb& p : out) {
            p = kBlack;
        }
        return false;
    }

    // 1-2. Brightness then gamma, still in logical order.
    LinearFrame linear{};
    const FrameBuffer::Pixels& src = fb.pixels();
    for (uint16_t i = 0U; i < kLedCount; ++i) {
        const Rgb s = scale(src[i], brightness);
        linear[i] = Rgb{gamma_correct(s.r), gamma_correct(s.g), gamma_correct(s.b)};
    }

    // 3. Current limit on the real duty cycles.
    const CurrentLimitResult limit = apply_current_limit(linear, budget_ma);
    stats.estimated_ua = limit.estimated_ua;
    stats.applied_ua = limit.applied_ua;
    stats.scale_q8 = limit.scale_q8;
    stats.limited = limit.limited;

    // 4. Logical -> physical order.
    for (uint8_t y = 0U; y < kHeight; ++y) {
        for (uint8_t x = 0U; x < kWidth; ++x) {
            uint16_t phys = 0U;
            // Cannot fail: (x, y) is in bounds and the map was validated above.
            (void)led_index(map, x, y, phys);
            out[phys] = linear[FrameBuffer::logical_index(x, y)];
        }
    }
    return true;
}

}  // namespace display
