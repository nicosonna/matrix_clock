#pragma once
/**
 * @file test_patterns.hpp
 * @brief Commissioning patterns (mapping wizard) and the physical chaser.
 *
 * The patterns are drawn in logical coordinates and go through the normal
 * pipeline; the chaser writes a *physical* frame directly, bypassing the
 * mapping, to reveal the real wiring (LED 0 red, LED 255 blue, white runner).
 */

#include <cstdint>

#include "display/frame_buffer.hpp"
#include "display/render.hpp"

namespace display {

/// Commissioning patterns, in the order the wizard asks about them.
enum class TestPattern : uint8_t {
    None = 0U,   ///< Everything off.
    Pixel,       ///< (0, 0) only: which corner?
    Column,      ///< x = 0, all rows: a column or a row?
    Diagonal,    ///< Four pixels per row: straight, or broken every 8?
    Full,        ///< All white: current estimate and limiter.
};

/// Draws @p pattern into @p fb (cleared first).
void draw_test_pattern(FrameBuffer& fb, TestPattern pattern) noexcept;

/// Fixed marker on LED 0 (red) and LED 255 (blue) of the chaser.
inline constexpr Rgb kChaserFirstMarker{40U, 0U, 0U};
inline constexpr Rgb kChaserLastMarker{0U, 0U, 40U};
/// Brightness of the runner's head; each of the 4 tail LEDs halves it.
inline constexpr uint8_t kChaserHead = 48U;
inline constexpr uint8_t kChaserTail = 4U;

/**
 * @brief Fills a physical frame with the chaser at position @p head (0..255).
 * @note Dim linear values on purpose: no gamma, no limiter needed.
 */
void chaser_frame(uint16_t head, PhysicalFrame& frame) noexcept;

}  // namespace display
