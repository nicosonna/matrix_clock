/**
 * @file clock_scene.cpp
 * @brief `Heure` and `Date` scenes.
 */

#include "display/clock_scene.hpp"

#include "display/font.hpp"
#include "display/geometry.hpp"
#include "display/text.hpp"

namespace display {

namespace {

/// Glyph of a single digit, or '-' when the time is not sure.
char digit_or_dash(bool sure, uint8_t value) noexcept {
    return sure ? static_cast<char>('0' + (value % 10U)) : '-';
}

/// One-pixel-wide vertical pair of dots (rows 2 and 4), the narrow colon/dot.
void draw_narrow_colon(FrameBuffer& fb, uint8_t x, Rgb color) noexcept {
    fb.set(x, 2U, color);
    fb.set(x, 4U, color);
}

}  // namespace

const char* weekday_abbrev(uint8_t weekday) noexcept {
    switch (weekday) {
        case 0U: return "LUN";
        case 1U: return "MAR";
        case 2U: return "MER";
        case 3U: return "JEU";
        case 4U: return "VEN";
        case 5U: return "SAM";
        case 6U: return "DIM";
        default: return "???";
    }
}

void draw_clock(FrameBuffer& fb, const TimeInfo& t, uint8_t x, bool colon_on, bool seconds_bar,
                const ClockColors& colors) noexcept {
    const char hh[3] = {digit_or_dash(t.sure, t.hour / 10U), digit_or_dash(t.sure, t.hour), '\0'};
    const char mm[3] = {digit_or_dash(t.sure, t.minute / 10U), digit_or_dash(t.sure, t.minute), '\0'};
    // HH at x, colon at x + 12, MM at x + 14: 5+1+5 = 11 columns, gap, 1 column, gap.
    draw_text(fb, kFont5x7, static_cast<int16_t>(x), 0, hh, colors.digits);
    if (colon_on) {
        draw_narrow_colon(fb, static_cast<uint8_t>(x + 12U), colors.colon);
    }
    draw_text(fb, kFont5x7, static_cast<int16_t>(x + 14U), 0, mm, colors.digits);

    if (seconds_bar && t.sure) {
        // 0..59 s -> 0..32 pixels on the bottom row, growing from the left.
        const uint8_t len = static_cast<uint8_t>((static_cast<uint16_t>(t.second) * kWidth) / 60U);
        for (uint8_t i = 0U; i < len; ++i) {
            fb.set(i, static_cast<uint8_t>(kHeight - 1U), colors.seconds_bar);
        }
    }
}

void draw_date(FrameBuffer& fb, const TimeInfo& t, uint8_t x, uint8_t y, const DateColors& colors) noexcept {
    if (!t.sure) {
        return;
    }
    const char dd[3] = {static_cast<char>('0' + (t.day / 10U) % 10U), static_cast<char>('0' + t.day % 10U), '\0'};
    const char mo[3] = {static_cast<char>('0' + (t.month / 10U) % 10U), static_cast<char>('0' + t.month % 10U), '\0'};
    // "SAM" = 3*3 + 2 = 11 columns, 2 blank, "05" = 7, 1 blank, dot, 1 blank, "11" = 7.
    draw_text(fb, kFont3x5, static_cast<int16_t>(x), static_cast<int16_t>(y), weekday_abbrev(t.weekday), colors.weekday);
    draw_text(fb, kFont3x5, static_cast<int16_t>(x + 13U), static_cast<int16_t>(y), dd, colors.date);
    fb.set(static_cast<uint8_t>(x + 21U), static_cast<uint8_t>(y + 4U), colors.date);  // the dot, bottom row of the glyphs
    draw_text(fb, kFont3x5, static_cast<int16_t>(x + 23U), static_cast<int16_t>(y), mo, colors.date);
}

}  // namespace display
