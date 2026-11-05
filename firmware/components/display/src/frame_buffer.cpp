/**
 * @file frame_buffer.cpp
 * @brief Logical frame buffer implementation.
 */

#include "display/frame_buffer.hpp"

namespace display {

FrameBuffer::FrameBuffer() noexcept : px_{}, oob_(0U) {
    clear();
}

void FrameBuffer::clear() noexcept {
    for (Rgb& p : px_) {
        p = kBlack;
    }
}

void FrameBuffer::set(uint8_t x, uint8_t y, Rgb c) noexcept {
    if (!in_bounds(x, y)) {
        ++oob_;
        return;
    }
    px_[logical_index(x, y)] = c;
}

Rgb FrameBuffer::get(uint8_t x, uint8_t y) const noexcept {
    if (!in_bounds(x, y)) {
        ++oob_;
        return kBlack;
    }
    return px_[logical_index(x, y)];
}

void FrameBuffer::fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, Rgb c) noexcept {
    // Coordinates are widened so that x + w cannot wrap around in uint8_t.
    const uint16_t x_end = static_cast<uint16_t>(static_cast<uint16_t>(x) + w);
    const uint16_t y_end = static_cast<uint16_t>(static_cast<uint16_t>(y) + h);
    for (uint16_t yy = y; yy < y_end; ++yy) {
        for (uint16_t xx = x; xx < x_end; ++xx) {
            // Values above 255 are out of bounds by definition; clamp keeps set()'s contract.
            const uint8_t cx = (xx > 255U) ? 255U : static_cast<uint8_t>(xx);
            const uint8_t cy = (yy > 255U) ? 255U : static_cast<uint8_t>(yy);
            set(cx, cy, c);
        }
    }
}

}  // namespace display
