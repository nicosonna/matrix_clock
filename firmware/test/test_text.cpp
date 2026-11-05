/**
 * @file test_text.cpp
 * @brief Host tests: fonts, text drawing, transliteration, clock and date scenes
 *        (AFF-08, AFF-09, AFF-10, AFF-11).
 */
#include "test_support.hpp"

#include <cstring>

#include "display/clock_scene.hpp"
#include "display/font.hpp"
#include "display/frame_buffer.hpp"
#include "display/text.hpp"

namespace {

using display::FrameBuffer;
using display::kBlack;
using display::kWhite;
using display::Rgb;

uint16_t lit_count(const FrameBuffer& fb) {
    uint16_t n = 0U;
    for (const Rgb& p : fb.pixels()) {
        if (p != kBlack) {
            ++n;
        }
    }
    return n;
}

uint8_t popcount7(uint8_t v) {
    uint8_t n = 0U;
    for (uint8_t i = 0U; i < 8U; ++i) {
        if ((v & static_cast<uint8_t>(1U << i)) != 0U) {
            ++n;
        }
    }
    return n;
}

// --------------------------------------------------------------------- AFF-08

void test_fonts_and_widths() {
    CHECK_EQ(display::kFont5x7.width, 5);
    CHECK_EQ(display::kFont5x7.height, 7);
    CHECK_EQ(display::kFont3x5.height, 5);
    CHECK_EQ(display::kFont3x4.height, 4);
    CHECK(display::kFont5x7.has('A'));
    CHECK(display::kFont5x7.has(display::kDegreeChar));
    CHECK(!display::kFont5x7.has('a'));
    CHECK(!display::kFont5x7.has('~'));
    CHECK(display::kFont5x7.glyph('~') == nullptr);
    CHECK(display::kFont5x7.glyph('0') != nullptr);
    CHECK_EQ(display::kFont5x7.glyph('0')[0], 0x3E);
    CHECK(display::kFont5x7.glyph(display::kDegreeChar)[1] != 0U);  // the degree sign is drawn
    CHECK(display::kFont3x4.has('9'));
    CHECK(!display::kFont3x4.has('A'));
    // Every glyph of every font fits in its rows: no bit above `height`.
    const display::Font* fonts[] = {&display::kFont5x7, &display::kFont3x5, &display::kFont3x4};
    for (const display::Font* f : fonts) {
        bool fits = true;
        for (char c = f->first; c <= f->last; ++c) {
            const uint8_t* g = f->glyph(c);
            for (uint8_t col = 0U; col < f->width; ++col) {
                if ((g[col] >> f->height) != 0U) {
                    fits = false;
                }
            }
        }
        CHECK(fits);
    }

    CHECK_EQ(display::text_width(display::kFont5x7, ""), 0);
    CHECK_EQ(display::text_width(display::kFont5x7, "A"), 5);
    CHECK_EQ(display::text_width(display::kFont5x7, "12:34"), 29);  // generic colon is 5 wide
    CHECK_EQ(display::text_width(display::kFont3x5, "SAM"), 11);
    CHECK_EQ(display::text_width(display::kFont3x5, "~"), 3);       // unknown = blank box
}

void test_draw_text() {
    FrameBuffer fb;
    display::draw_text(fb, display::kFont5x7, 0, 0, "A", kWhite);
    const uint8_t* a = display::kFont5x7.glyph('A');
    uint16_t expected = 0U;
    for (uint8_t col = 0U; col < 5U; ++col) {
        expected = static_cast<uint16_t>(expected + popcount7(a[col]));
    }
    CHECK_EQ(lit_count(fb), expected);
    CHECK_EQ(expected, 18);

    // Unknown glyph: empty box, cursor still advances.
    fb.clear();
    display::draw_text(fb, display::kFont5x7, 0, 0, "~A", kWhite);
    CHECK_EQ(lit_count(fb), 18);
    CHECK(fb.get(6U, 1U) == kWhite);   // A's first column, second row, at x = 6
    CHECK(fb.get(0U, 1U) == kBlack);

    // Clipping, left: only columns 2..4 of A are visible; no out-of-bounds counted.
    fb.clear();
    display::draw_text(fb, display::kFont5x7, -2, 0, "A", kWhite);
    CHECK_EQ(lit_count(fb), static_cast<uint16_t>(popcount7(a[2]) + popcount7(a[3]) + popcount7(a[4])));
    CHECK_EQ(fb.out_of_bounds_count(), 0);

    // Clipping, right and bottom.
    fb.clear();
    display::draw_text(fb, display::kFont5x7, 30, 0, "A", kWhite);
    CHECK_EQ(lit_count(fb), static_cast<uint16_t>(popcount7(a[0]) + popcount7(a[1])));
    fb.clear();
    display::draw_text(fb, display::kFont5x7, 0, 5, "A", kWhite);  // rows 5, 6, 7 visible = glyph rows 0..2
    uint16_t top3 = 0U;
    for (uint8_t col = 0U; col < 5U; ++col) {
        top3 = static_cast<uint16_t>(top3 + popcount7(static_cast<uint8_t>(a[col] & 0x07U)));
    }
    CHECK_EQ(lit_count(fb), top3);
    CHECK_EQ(fb.out_of_bounds_count(), 0);

    // Entirely off-panel: nothing, no counter.
    fb.clear();
    display::draw_text(fb, display::kFont5x7, 40, 0, "ABC", kWhite);
    display::draw_text(fb, display::kFont5x7, -100, 0, "ABC", kWhite);
    CHECK_EQ(lit_count(fb), 0);
    CHECK_EQ(fb.out_of_bounds_count(), 0);
}

// --------------------------------------------------------------------- AFF-09

void test_transliterate() {
    char out[32];
    CHECK_EQ(display::transliterate("\xC3\x89t\xC3\xA9 \xC3\xA0 21\xC2\xB0" "C", out, sizeof(out)), 10);
    CHECK(std::strcmp(out, "ETE A 21`C") == 0);
    display::transliterate("\xC3\xA7" "a", out, sizeof(out));  // ça (split: 'a' is a hex digit)
    CHECK(std::strcmp(out, "CA") == 0);
    display::transliterate("hello", out, sizeof(out));
    CHECK(std::strcmp(out, "HELLO") == 0);
    display::transliterate("\xE2\x82\xAC", out, sizeof(out));  // euro sign: one '?'
    CHECK(std::strcmp(out, "?") == 0);
    display::transliterate("{}~`", out, sizeof(out));
    CHECK(std::strcmp(out, "????") == 0);
    display::transliterate("\xC5\x92uvre", out, sizeof(out));  // Œ
    CHECK(std::strcmp(out, "OUVRE") == 0);
    display::transliterate("\xC3\xBF \xC3\x9F \xC3\xB1 \xC3\xB8", out, sizeof(out));  // ÿ ß ñ ø
    CHECK(std::strcmp(out, "Y S N O") == 0);
    display::transliterate("\xC3\x97", out, sizeof(out));  // ×
    CHECK(std::strcmp(out, "?") == 0);
    display::transliterate("\xC3\x8C\xC3\x99\xC3\x90\xC3\x9D\xC3\xAE\xC3\xBC", out, sizeof(out));  // Ì Ù Ð Ý î ü
    CHECK(std::strcmp(out, "IUDYIU") == 0);
    display::transliterate("\xC2\xA9", out, sizeof(out));  // © : a C2 sign that is not the degree
    CHECK(std::strcmp(out, "?") == 0);

    // Truncation: always NUL-terminated, never past out_size.
    char small[4] = {'x', 'x', 'x', 'x'};
    CHECK_EQ(display::transliterate("ABCDEF", small, sizeof(small)), 3);
    CHECK(std::strcmp(small, "ABC") == 0);
    char one[1] = {'x'};
    CHECK_EQ(display::transliterate("ABC", one, sizeof(one)), 0);
    CHECK_EQ(one[0], '\0');
    CHECK_EQ(display::transliterate("ABC", nullptr, 8U), 0);
    CHECK_EQ(display::transliterate(nullptr, out, sizeof(out)), 0);
    CHECK_EQ(out[0], '\0');
    // A truncated multi-byte sequence at the end does not read past the NUL.
    CHECK_EQ(display::transliterate("A\xC3", out, sizeof(out)), 2);
    CHECK(std::strcmp(out, "A?") == 0);
}

// ---------------------------------------------------------------- AFF-10 / 11

void test_clock_scene() {
    display::TimeInfo t{true, 12U, 34U, 30U, 5U, 11U, 5U};
    FrameBuffer fb;
    display::draw_clock(fb, t, display::kClockCentredX, true, false, kWhite);
    CHECK_EQ(display::kClockWidth, 25);
    // Nothing left of x = 3 nor right of x = 27: 25 columns, centred.
    bool inside = true;
    for (uint8_t y = 0U; y < display::kHeight; ++y) {
        for (uint8_t x = 0U; x < display::kWidth; ++x) {
            const bool lit = fb.get(x, y) != kBlack;
            if (lit && ((x < 3U) || (x > 27U))) {
                inside = false;
            }
        }
    }
    CHECK(inside);
    // The narrow colon: column 15, rows 2 and 4, nothing else in that column.
    CHECK(fb.get(15U, 2U) == kWhite);
    CHECK(fb.get(15U, 4U) == kWhite);
    CHECK(fb.get(15U, 3U) == kBlack);
    CHECK(fb.get(15U, 0U) == kBlack);
    const uint16_t with_colon = lit_count(fb);
    fb.clear();
    display::draw_clock(fb, t, display::kClockCentredX, false, false, kWhite);
    CHECK_EQ(lit_count(fb), with_colon - 2U);

    // Seconds bar: 30 s = 16 pixels on the bottom row.
    fb.clear();
    display::draw_clock(fb, t, display::kClockCentredX, false, true, kWhite);
    uint8_t bar = 0U;
    for (uint8_t x = 0U; x < display::kWidth; ++x) {
        if (fb.get(x, 7U) != kBlack) {
            ++bar;
        }
    }
    CHECK_EQ(bar, 16);

    // AFF-11: time not sure -> "--:--": four dashes on row 3 plus the colon, and
    // nothing resembling a digit (no pixel on row 0 or row 6).
    display::TimeInfo unsure{false, 12U, 34U, 30U, 5U, 11U, 5U};
    fb.clear();
    display::draw_clock(fb, unsure, display::kClockCentredX, true, true, kWhite);
    CHECK_EQ(lit_count(fb), 4U * 5U + 2U);
    bool only_row3 = true;
    for (uint8_t x = 0U; x < display::kWidth; ++x) {
        for (uint8_t y = 0U; y < display::kHeight; ++y) {
            if ((fb.get(x, y) != kBlack) && (y != 3U) && (x != 15U)) {
                only_row3 = false;
            }
        }
    }
    CHECK(only_row3);
    CHECK(fb.get(7U, 7U) == kBlack);  // no seconds bar either: the seconds are not sure
    // And it is not what 00:00 would look like.
    FrameBuffer zero;
    display::TimeInfo midnight{true, 0U, 0U, 0U, 1U, 1U, 0U};
    display::draw_clock(zero, midnight, display::kClockCentredX, true, false, kWhite);
    CHECK(lit_count(zero) != 22U);

    // Date: "SAM 05.11" fits in kDateWidth columns; nothing when not sure.
    fb.clear();
    display::draw_date(fb, t, 1U, 1U, kWhite);
    CHECK(lit_count(fb) > 0U);
    bool date_inside = true;
    for (uint8_t y = 0U; y < display::kHeight; ++y) {
        for (uint8_t x = 0U; x < display::kWidth; ++x) {
            const bool lit = fb.get(x, y) != kBlack;
            if (lit && ((x < 1U) || (x > 30U) || (y < 1U) || (y > 5U))) {
                date_inside = false;
            }
        }
    }
    CHECK(date_inside);
    CHECK(fb.get(22U, 5U) == kWhite);  // the dot between day and month
    fb.clear();
    display::draw_date(fb, unsure, 1U, 1U, kWhite);
    CHECK_EQ(lit_count(fb), 0);
    CHECK(std::strcmp(display::weekday_abbrev(0U), "LUN") == 0);
    CHECK(std::strcmp(display::weekday_abbrev(1U), "MAR") == 0);
    CHECK(std::strcmp(display::weekday_abbrev(2U), "MER") == 0);
    CHECK(std::strcmp(display::weekday_abbrev(3U), "JEU") == 0);
    CHECK(std::strcmp(display::weekday_abbrev(4U), "VEN") == 0);
    CHECK(std::strcmp(display::weekday_abbrev(6U), "DIM") == 0);
    CHECK(std::strcmp(display::weekday_abbrev(7U), "???") == 0);

    // GEN-04: none of this allocates.
    {
        AllocGuard guard;
        fb.clear();
        display::draw_clock(fb, t, display::kClockCentredX, true, true, kWhite);
        display::draw_date(fb, t, 1U, 1U, kWhite);
        char out[16];
        (void)display::transliterate("\xC3\x89t\xC3\xA9", out, sizeof(out));
        display::draw_text(fb, display::kFont3x5, -3, 2, out, kWhite);
    }
    CHECK_EQ(g_alloc_count, 0);
}

}  // namespace

void run_text_tests() {
    test_fonts_and_widths();
    test_draw_text();
    test_transliterate();
    test_clock_scene();
}
