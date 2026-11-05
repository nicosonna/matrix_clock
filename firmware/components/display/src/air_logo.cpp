/**
 * @file air_logo.cpp
 * @brief Air glyphs.
 */
#include "display/air_logo.hpp"

namespace display {

namespace {

// Rows as bit masks, MSB = left column.
constexpr uint8_t kLogo6[6] = {
    0b011110U,  // .####.
    0b000001U,  // .....#
    0b111110U,  // #####.
    0b000000U,  // ......
    0b011110U,  // .####.
    0b000001U,  // .....#
};

constexpr uint8_t kLogo8[8] = {
    0b00111100U,  // ..####..
    0b00000010U,  // ......#.
    0b11111100U,  // ######..
    0b00000000U,  // ........
    0b01111110U,  // .######.
    0b00000001U,  // .......#
    0b00111100U,  // ..####..
    0b01000000U,  // .#......
};

void draw_rows(FrameBuffer& fb, const uint8_t* rows, uint8_t size, int16_t x, int16_t y, Rgb color) noexcept {
    for (uint8_t r = 0U; r < size; ++r) {
        for (uint8_t c = 0U; c < size; ++c) {
            const uint8_t bit = static_cast<uint8_t>(size - 1U - c);
            if (((rows[r] >> bit) & 1U) != 0U) {
                const int16_t px = static_cast<int16_t>(x + c);
                const int16_t py = static_cast<int16_t>(y + r);
                if ((px >= 0) && (py >= 0) && (px < static_cast<int16_t>(kWidth)) && (py < static_cast<int16_t>(kHeight))) {
                    fb.set(static_cast<uint8_t>(px), static_cast<uint8_t>(py), color);
                }
            }
        }
    }
}

}  // namespace

void draw_air_logo_6(FrameBuffer& fb, int16_t x, int16_t y, Rgb color) noexcept {
    draw_rows(fb, kLogo6, 6U, x, y, color);
}

void draw_air_logo_8(FrameBuffer& fb, int16_t x, int16_t y, Rgb color) noexcept {
    draw_rows(fb, kLogo8, 8U, x, y, color);
}

}  // namespace display
