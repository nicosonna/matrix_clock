#pragma once
/**
 * @file air_logo.hpp
 * @brief The "air" glyph (three strokes of moving air) in 6x6 and 8x8.
 *
 * Drawn in the colour of the air-quality level; grey when unknown. The same
 * motif exists as an inline SVG on the page.
 */

#include <cstdint>

#include "display/color.hpp"
#include "display/frame_buffer.hpp"

namespace display {

/// Draws the 6x6 logo with its top-left corner at (x, y).
void draw_air_logo_6(FrameBuffer& fb, int16_t x, int16_t y, Rgb color) noexcept;

/// Draws the 8x8 logo with its top-left corner at (x, y).
void draw_air_logo_8(FrameBuffer& fb, int16_t x, int16_t y, Rgb color) noexcept;

/// Number of lit pixels of each glyph (for tests and current estimates).
inline constexpr uint8_t kAirLogo6Pixels = 15U;
inline constexpr uint8_t kAirLogo8Pixels = 23U;

}  // namespace display
