#pragma once
/**
 * @file frame_buffer.hpp
 * @brief Logical 8x32 frame buffer: 256 RGB pixels, 768 bytes, static.
 *
 * Writes outside the panel are **ignored and counted**: a scene that draws off
 * the edge is a scene bug, not a reason to crash the display. The counter is
 * published in the diagnostics.
 */

#include <array>
#include <cstdint>

#include "display/color.hpp"
#include "display/geometry.hpp"

namespace display {

class FrameBuffer {
public:
    using Pixels = std::array<Rgb, kLedCount>;

    /// Starts black.
    FrameBuffer() noexcept;

    /// Sets every pixel to black (does not reset the out-of-bounds counter).
    void clear() noexcept;

    /**
     * @brief Writes one pixel.
     * @param x Column 0..31; @param y Row 0..7; @param c Colour.
     * @note Out of bounds: ignored, counter incremented.
     */
    void set(uint8_t x, uint8_t y, Rgb c) noexcept;

    /// @return The pixel, or black when out of bounds (counted too).
    Rgb get(uint8_t x, uint8_t y) const noexcept;

    /**
     * @brief Fills a rectangle, clipped to the panel.
     * @note Each clipped pixel counts as one out-of-bounds write.
     */
    void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, Rgb c) noexcept;

    /// Pixels in logical order: index = y * kWidth + x.
    const Pixels& pixels() const noexcept { return px_; }

    /// Number of out-of-bounds accesses since construction (diagnostic).
    uint32_t out_of_bounds_count() const noexcept { return oob_; }

    /// Logical index of (x, y); the caller guarantees in_bounds(x, y).
    static constexpr uint16_t logical_index(uint8_t x, uint8_t y) noexcept {
        return static_cast<uint16_t>(static_cast<uint16_t>(y) * kWidth + x);
    }

private:
    Pixels px_;
    mutable uint32_t oob_;
};

}  // namespace display
