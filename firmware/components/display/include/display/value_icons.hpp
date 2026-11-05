#pragma once
/**
 * @file value_icons.hpp
 * @brief 8x8 icons of the Value scene (thermometer, drop, bolt, door, wind, sun, home).
 */

#include <cstdint>

#include "display/color.hpp"
#include "display/frame_buffer.hpp"

namespace display {

/// Mirrors homeassistant::ValueIcon (kept apart so `display` stays independent).
enum class Icon : uint8_t {
    None = 0U,
    Thermometer,
    Drop,
    Bolt,
    Door,
    Wind,
    Sun,
    Home,
    Count,
};

/// Draws the 8x8 icon with its top-left corner at (x, y); None draws nothing.
void draw_icon(FrameBuffer& fb, Icon icon, int16_t x, int16_t y, Rgb color) noexcept;

/// Lit pixels of an icon (0 for None and out-of-range values).
uint8_t icon_pixels(Icon icon) noexcept;

}  // namespace display
