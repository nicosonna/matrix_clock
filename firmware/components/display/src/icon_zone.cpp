/**
 * @file icon_zone.cpp
 * @brief Icon zone choice and alert mark (see icon_zone.hpp).
 */
#include "display/icon_zone.hpp"

namespace display {

namespace {
constexpr uint8_t kMarkColumn = 2U;   ///< Offset of the 2-px wide bar inside the 6x6 zone.
constexpr uint8_t kMarkWidth = 2U;
constexpr uint8_t kBarHeight = 4U;
constexpr uint8_t kDotRow = 5U;
}  // namespace

IconZone choose_icon_zone(bool alert_held, bool air_logo) noexcept {
    if (alert_held) {
        return IconZone::Alert;
    }
    return air_logo ? IconZone::Air : IconZone::None;
}

void draw_alert_mark(FrameBuffer& fb, uint8_t x, uint8_t y, Rgb color) noexcept {
    fb.fill_rect(static_cast<uint8_t>(x + kMarkColumn), y, kMarkWidth, kBarHeight, color);
    fb.fill_rect(static_cast<uint8_t>(x + kMarkColumn), static_cast<uint8_t>(y + kDotRow), kMarkWidth, 1U, color);
}

uint8_t alert_mark_pixels() noexcept {
    return static_cast<uint8_t>(kMarkWidth * (kBarHeight + 1U));
}

}  // namespace display
