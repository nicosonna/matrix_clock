/**
 * @file value_icons.cpp
 * @brief 8x8 icons.
 */
#include "display/value_icons.hpp"

namespace display {

namespace {

struct Glyph {
    uint8_t rows[8];  ///< MSB = left column.
};

constexpr Glyph kGlyphs[static_cast<uint8_t>(Icon::Count)] = {
    {{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}},  // None
    {{0b00011000U, 0b00010100U, 0b00011000U, 0b00010100U, 0b00011000U, 0b00111100U, 0b00111100U, 0b00011000U}},  // Thermometer
    {{0b00001000U, 0b00001000U, 0b00011100U, 0b00111110U, 0b01111111U, 0b01111111U, 0b00111110U, 0b00011100U}},  // Drop
    {{0b00001100U, 0b00011000U, 0b00110000U, 0b01111110U, 0b00001100U, 0b00011000U, 0b00110000U, 0b00100000U}},  // Bolt
    {{0b01111110U, 0b01000010U, 0b01000010U, 0b01001010U, 0b01001010U, 0b01000010U, 0b01000010U, 0b01111110U}},  // Door
    {{0b00111100U, 0b00000010U, 0b11111100U, 0b00000000U, 0b01111110U, 0b00000001U, 0b00111100U, 0b01000000U}},  // Wind
    {{0b00011000U, 0b01011010U, 0b00111100U, 0b11111111U, 0b11111111U, 0b00111100U, 0b01011010U, 0b00011000U}},  // Sun
    {{0b00011000U, 0b00111100U, 0b01111110U, 0b11111111U, 0b01111110U, 0b01100110U, 0b01100110U, 0b01111110U}},  // Home
};

}  // namespace

void draw_icon(FrameBuffer& fb, Icon icon, int16_t x, int16_t y, Rgb color) noexcept {
    const uint8_t index = static_cast<uint8_t>(icon);
    if ((index == 0U) || (index >= static_cast<uint8_t>(Icon::Count))) {
        return;
    }
    const Glyph& g = kGlyphs[index];
    for (uint8_t r = 0U; r < 8U; ++r) {
        for (uint8_t c = 0U; c < 8U; ++c) {
            if (((g.rows[r] >> (7U - c)) & 1U) != 0U) {
                const int16_t px = static_cast<int16_t>(x + c);
                const int16_t py = static_cast<int16_t>(y + r);
                if ((px >= 0) && (py >= 0) && (px < static_cast<int16_t>(kWidth)) && (py < static_cast<int16_t>(kHeight))) {
                    fb.set(static_cast<uint8_t>(px), static_cast<uint8_t>(py), color);
                }
            }
        }
    }
}

uint8_t icon_pixels(Icon icon) noexcept {
    const uint8_t index = static_cast<uint8_t>(icon);
    if (index >= static_cast<uint8_t>(Icon::Count)) {
        return 0U;
    }
    uint8_t n = 0U;
    for (uint8_t r = 0U; r < 8U; ++r) {
        for (uint8_t c = 0U; c < 8U; ++c) {
            n = static_cast<uint8_t>(n + ((kGlyphs[index].rows[r] >> c) & 1U));
        }
    }
    return n;
}

}  // namespace display
