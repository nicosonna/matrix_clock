/**
 * @file test_patterns.cpp
 * @brief Commissioning patterns and physical chaser.
 */

#include "display/test_patterns.hpp"

#include "display/geometry.hpp"

namespace display {

void draw_test_pattern(FrameBuffer& fb, TestPattern pattern) noexcept {
    fb.clear();
    switch (pattern) {
        case TestPattern::Pixel:
            fb.set(0U, 0U, kWhite);
            break;
        case TestPattern::Column:
            for (uint8_t y = 0U; y < kHeight; ++y) {
                fb.set(0U, y, Rgb{0U, 0U, 255U});
            }
            break;
        case TestPattern::Diagonal:
            for (uint8_t y = 0U; y < kHeight; ++y) {
                for (uint8_t k = 0U; k < 4U; ++k) {
                    fb.set(static_cast<uint8_t>(y * 4U + k), y, Rgb{0U, 255U, 0U});
                }
            }
            break;
        case TestPattern::Full:
            fb.fill_rect(0U, 0U, kWidth, kHeight, kWhite);
            break;
        case TestPattern::None:
        default:
            break;
    }
}

void chaser_frame(uint16_t head, PhysicalFrame& frame) noexcept {
    for (Rgb& p : frame) {
        p = kBlack;
    }
    const uint16_t h = static_cast<uint16_t>(head % kLedCount);
    for (uint8_t t = 0U; t < kChaserTail; ++t) {
        if (h >= t) {
            const uint8_t v = static_cast<uint8_t>(kChaserHead >> t);  // 48, 24, 12, 6
            frame[static_cast<uint16_t>(h - t)] = Rgb{v, v, v};
        }
    }
    frame[0] = kChaserFirstMarker;
    frame[kLedCount - 1U] = kChaserLastMarker;
}

}  // namespace display
