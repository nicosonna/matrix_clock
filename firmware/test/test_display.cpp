/**
 * @file test_display.cpp
 * @brief Host tests of the pure `display` component (AFF-01 .. AFF-07, render, GEN-04).
 */
#include "test_support.hpp"

#include <array>

#include "display/color.hpp"
#include "display/current_limit.hpp"
#include "display/frame_buffer.hpp"
#include "display/gamma.hpp"
#include "display/panel_map.hpp"
#include "display/render.hpp"

namespace {

using display::FirstAxis;
using display::FrameBuffer;
using display::kBlack;
using display::kHeight;
using display::kLedCount;
using display::kWhite;
using display::kWidth;
using display::LinearFrame;
using display::Origin;
using display::PanelMapConfig;
using display::Rgb;

// ---------------------------------------------------------------- AFF-01 / 02

/// Every combination of origin x axis x serpentine x module width is a bijection.
void test_mapping_bijection() {
    const std::array<Origin, 4> origins{Origin::TopLeft, Origin::TopRight, Origin::BottomLeft,
                                        Origin::BottomRight};
    const std::array<FirstAxis, 2> axes{FirstAxis::Column, FirstAxis::Row};
    const std::array<bool, 2> serpentines{false, true};
    const std::array<uint8_t, 2> widths{0U, display::kChainedModuleWidth};

    for (Origin o : origins) {
        for (FirstAxis a : axes) {
            for (bool s : serpentines) {
                for (uint8_t w : widths) {
                    const PanelMapConfig cfg{a, s, o, w};
                    CHECK(display::is_valid(cfg));
                    std::array<bool, kLedCount> seen{};
                    uint16_t distinct = 0U;
                    for (uint8_t y = 0U; y < kHeight; ++y) {
                        for (uint8_t x = 0U; x < kWidth; ++x) {
                            uint16_t idx = 0xFFFFU;
                            CHECK(display::led_index(cfg, x, y, idx));
                            if ((idx < kLedCount) && !seen[idx]) {
                                seen[idx] = true;
                                ++distinct;
                            }
                        }
                    }
                    CHECK_EQ(distinct, kLedCount);
                }
            }
        }
    }
}

uint16_t index_of(const PanelMapConfig& cfg, uint8_t x, uint8_t y) {
    uint16_t idx = 0xFFFFU;
    (void)display::led_index(cfg, x, y, idx);
    return idx;
}

/// Known positions for the usual wirings, including four chained 8x8 modules.
void test_mapping_known_positions() {
    // The horloge-v1 panel, measured with the physical chaser (2026-11-05):
    // LED 0 top-left, columns of 8 run down then up (serpentine), LED 255
    // top-right. This is the default.
    const PanelMapConfig panel = display::default_panel_map();
    CHECK_ENUM(panel.first_axis, FirstAxis::Column);
    CHECK(panel.serpentine);
    CHECK_ENUM(panel.origin, Origin::TopLeft);
    CHECK_EQ(index_of(panel, 0U, 0U), 0);     // LED 0 (red marker): top-left
    CHECK_EQ(index_of(panel, 31U, 0U), 255);  // LED 255 (blue marker): top-right
    CHECK_EQ(index_of(panel, 0U, 7U), 7);     // first column runs downwards
    CHECK_EQ(index_of(panel, 1U, 7U), 8);     // second column starts at the bottom
    CHECK_EQ(index_of(panel, 1U, 0U), 15);    // and runs upwards

    const PanelMapConfig col_serp{FirstAxis::Column, true, Origin::TopLeft, 0U};
    CHECK_EQ(index_of(col_serp, 0U, 0U), 0);
    CHECK_EQ(index_of(col_serp, 0U, 7U), 7);
    CHECK_EQ(index_of(col_serp, 1U, 0U), 15);  // second column runs upwards
    CHECK_EQ(index_of(col_serp, 1U, 7U), 8);
    CHECK_EQ(index_of(col_serp, 31U, 0U), 255);
    CHECK_EQ(index_of(col_serp, 31U, 7U), 248);

    const PanelMapConfig col_prog{FirstAxis::Column, false, Origin::TopLeft, 0U};
    CHECK_EQ(index_of(col_prog, 1U, 0U), 8);
    CHECK_EQ(index_of(col_prog, 31U, 7U), 255);

    const PanelMapConfig row_serp{FirstAxis::Row, true, Origin::TopLeft, 0U};
    CHECK_EQ(index_of(row_serp, 0U, 0U), 0);
    CHECK_EQ(index_of(row_serp, 31U, 0U), 31);
    CHECK_EQ(index_of(row_serp, 0U, 1U), 63);  // second row runs backwards
    CHECK_EQ(index_of(row_serp, 31U, 1U), 32);
    CHECK_EQ(index_of(row_serp, 0U, 7U), 255);

    const PanelMapConfig top_right{FirstAxis::Column, true, Origin::TopRight, 0U};
    CHECK_EQ(index_of(top_right, 31U, 0U), 0);
    CHECK_EQ(index_of(top_right, 0U, 0U), 255);

    const PanelMapConfig bottom_left{FirstAxis::Column, true, Origin::BottomLeft, 0U};
    CHECK_EQ(index_of(bottom_left, 0U, 7U), 0);
    CHECK_EQ(index_of(bottom_left, 0U, 0U), 7);

    const PanelMapConfig bottom_right{FirstAxis::Row, false, Origin::BottomRight, 0U};
    CHECK_EQ(index_of(bottom_right, 31U, 7U), 0);
    CHECK_EQ(index_of(bottom_right, 0U, 0U), 255);

    // AFF-02: four 8x8 modules chained, each column-serpentine.
    const PanelMapConfig modules{FirstAxis::Column, true, Origin::TopLeft, display::kChainedModuleWidth};
    CHECK_EQ(index_of(modules, 7U, 7U), 56);    // last column of module 0, runs upwards
    CHECK_EQ(index_of(modules, 8U, 0U), 64);    // first LED of module 1
    CHECK_EQ(index_of(modules, 9U, 0U), 79);
    CHECK_EQ(index_of(modules, 31U, 7U), 248);  // module 3, column 7 (odd): row 7 -> 0

    // Off-panel and invalid configurations are refused, output untouched.
    uint16_t idx = 1234U;
    CHECK(!display::led_index(col_serp, 32U, 0U, idx));
    CHECK(!display::led_index(col_serp, 0U, 8U, idx));
    const PanelMapConfig bad_width{FirstAxis::Column, true, Origin::TopLeft, 5U};
    CHECK(!display::is_valid(bad_width));
    CHECK(!display::led_index(bad_width, 0U, 0U, idx));
    CHECK_EQ(idx, 1234);
}

// --------------------------------------------------------------------- AFF-03

/// Out-of-bounds accesses are ignored and counted; clear() keeps the count.
void test_frame_buffer_bounds() {
    FrameBuffer fb;
    CHECK_EQ(fb.out_of_bounds_count(), 0);
    CHECK(fb.get(0U, 0U) == kBlack);

    fb.set(32U, 0U, kWhite);
    CHECK_EQ(fb.out_of_bounds_count(), 1);
    CHECK(fb.get(32U, 0U) == kBlack);
    CHECK_EQ(fb.out_of_bounds_count(), 2);
    fb.set(0U, 8U, kWhite);
    CHECK_EQ(fb.out_of_bounds_count(), 3);

    // 4x4 rectangle at (30, 6): 4 pixels inside, 12 clipped.
    fb.fill_rect(30U, 6U, 4U, 4U, kWhite);
    CHECK_EQ(fb.out_of_bounds_count(), 15);
    CHECK(fb.get(30U, 6U) == kWhite);
    CHECK(fb.get(31U, 7U) == kWhite);
    CHECK(fb.get(29U, 6U) == kBlack);
    CHECK(fb.get(30U, 5U) == kBlack);

    // A rectangle whose end would wrap in uint8_t is entirely clipped, not wrapped.
    fb.fill_rect(250U, 0U, 10U, 1U, kWhite);
    CHECK_EQ(fb.out_of_bounds_count(), 25);
    CHECK(fb.get(0U, 0U) == kBlack);
    CHECK(fb.get(4U, 0U) == kBlack);

    fb.set(5U, 3U, Rgb{1U, 2U, 3U});
    CHECK((fb.pixels()[FrameBuffer::logical_index(5U, 3U)] == Rgb{1U, 2U, 3U}));
    CHECK_EQ(FrameBuffer::logical_index(5U, 3U), 3 * 32 + 5);

    fb.clear();
    CHECK(fb.get(5U, 3U) == kBlack);
    CHECK(fb.get(31U, 7U) == kBlack);
    CHECK_EQ(fb.out_of_bounds_count(), 25);
}

// --------------------------------------------------------------------- AFF-04

void test_gamma_table() {
    CHECK_EQ(display::kGammaTable[0], 0);
    CHECK_EQ(display::kGammaTable[255], 255);
    CHECK_EQ(display::kGammaTable[64], 12);
    CHECK_EQ(display::kGammaTable[128], 56);
    CHECK_EQ(display::kGammaTable[192], 137);
    CHECK_EQ(display::gamma_correct(128U), 56);
    bool monotone = true;
    for (uint16_t i = 1U; i < 256U; ++i) {
        if (display::kGammaTable[i] < display::kGammaTable[i - 1U]) {
            monotone = false;
        }
    }
    CHECK(monotone);
}

// ---------------------------------------------------------------- AFF-05 / 06

void fill(LinearFrame& f, Rgb c) {
    for (Rgb& p : f) {
        p = c;
    }
}

void test_current_estimate() {
    LinearFrame f{};
    fill(f, kWhite);
    CHECK_EQ(display::estimate_current_ua(f), 15616000);  // 256 mA + 15.36 A
    fill(f, kBlack);
    CHECK_EQ(display::estimate_current_ua(f), 256000);
    CHECK_EQ(display::kPanelQuiescentUa, 256000);
}

void test_current_limit() {
    // Full white against the default budget: scaled to fit, channels stay equal.
    LinearFrame f{};
    fill(f, kWhite);
    display::CurrentLimitResult r = display::apply_current_limit(f, display::kDefaultBudgetMa);
    CHECK(r.limited);
    CHECK_EQ(r.estimated_ua, 15616000);
    CHECK(r.applied_ua <= 2000000U);
    // Floor on the scale (29/256 here) then on each channel (255 -> 28) loses
    // about 3 %: the budget is never exceeded, and never undershot by more than 5 %.
    CHECK(r.applied_ua >= 1900000U);
    CHECK(r.scale_q8 < display::kScaleOne);
    bool uniform = true;
    for (const Rgb& p : f) {
        if ((p.r != f[0].r) || (p.g != f[0].r) || (p.b != f[0].r)) {
            uniform = false;
        }
    }
    CHECK(uniform);
    CHECK_EQ(display::estimate_current_ua(f), r.applied_ua);

    // Hue preserved: (255, 128, 64) keeps its 4:2:1 ratios after scaling.
    fill(f, Rgb{255U, 128U, 64U});
    r = display::apply_current_limit(f, 1000U);
    CHECK(r.limited);
    CHECK(r.applied_ua <= 1000000U);
    const int32_t rr = f[0].r;
    const int32_t gg = f[0].g;
    const int32_t bb = f[0].b;
    CHECK((rr - 2 * gg) <= 2 && (rr - 2 * gg) >= -2);
    CHECK((gg - 2 * bb) <= 2 && (gg - 2 * bb) >= -2);

    // Dim frame: under budget, untouched.
    fill(f, Rgb{10U, 10U, 10U});
    r = display::apply_current_limit(f, display::kDefaultBudgetMa);
    CHECK(!r.limited);
    CHECK_EQ(r.scale_q8, display::kScaleOne);
    CHECK_EQ(r.applied_ua, r.estimated_ua);
    CHECK_EQ(r.estimated_ua, 256000 + 602352);
    CHECK((f[100] == Rgb{10U, 10U, 10U}));

    // Black frame: nothing to scale even with the smallest budget.
    fill(f, kBlack);
    r = display::apply_current_limit(f, display::kMinBudgetMa);
    CHECK(!r.limited);
    CHECK_EQ(r.applied_ua, 256000);

    // Invalid budget falls back to the default (AFF-07).
    fill(f, kWhite);
    r = display::apply_current_limit(f, 100U);
    CHECK(r.limited);
    CHECK(r.applied_ua <= 2000000U);
    CHECK(r.applied_ua >= 1900000U);
}

// --------------------------------------------------------------------- AFF-07

void test_budget_validation() {
    CHECK(!display::is_valid_budget_ma(0U));
    CHECK(!display::is_valid_budget_ma(399U));
    CHECK(display::is_valid_budget_ma(400U));
    CHECK(display::is_valid_budget_ma(display::kDefaultBudgetMa));
    CHECK(display::is_valid_budget_ma(15000U));
    CHECK(!display::is_valid_budget_ma(15001U));
    // The minimum budget must leave room above the quiescent current, or the
    // limiter's subtraction would wrap.
    CHECK(static_cast<uint32_t>(display::kMinBudgetMa) * 1000U > display::kPanelQuiescentUa);
}

// ------------------------------------------------------------- render + GEN-04

uint16_t lit_count(const display::PhysicalFrame& f) {
    uint16_t n = 0U;
    for (const Rgb& p : f) {
        if (p != kBlack) {
            ++n;
        }
    }
    return n;
}

void test_render_pipeline() {
    FrameBuffer fb;
    display::PhysicalFrame out{};
    display::RenderStats stats{};
    // Column-serpentine here so the expected LED index is easy to read (15);
    // the default (row-serpentine, bottom-right) is covered by the mapping tests.
    const PanelMapConfig map{FirstAxis::Column, true, Origin::TopLeft, 0U};

    // One white pixel at (1, 0) lands on LED 15 with this mapping.
    fb.set(1U, 0U, kWhite);
    CHECK(display::render(fb, 255U, 15000U, map, out, stats));
    CHECK(out[15] == kWhite);
    CHECK_EQ(lit_count(out), 1);
    CHECK(!stats.limited);

    // Brightness is applied before gamma: 128 -> 56.
    CHECK(display::render(fb, 128U, 15000U, map, out, stats));
    CHECK((out[15] == Rgb{56U, 56U, 56U}));

    // Brightness 0: everything off, only the quiescent current remains.
    CHECK(display::render(fb, 0U, 15000U, map, out, stats));
    CHECK_EQ(lit_count(out), 0);
    CHECK_EQ(stats.estimated_ua, 256000);

    // Full white at full brightness exceeds even a 15 A budget: limited.
    fb.fill_rect(0U, 0U, kWidth, kHeight, kWhite);
    CHECK(display::render(fb, 255U, 15000U, map, out, stats));
    CHECK(stats.limited);
    CHECK(stats.applied_ua <= 15000000U);
    CHECK(out[0].r < 255U);

    // Invalid mapping: refused, frame cleared.
    const PanelMapConfig bad{FirstAxis::Column, true, Origin::TopLeft, 3U};
    CHECK(!display::render(fb, 255U, 15000U, bad, out, stats));
    CHECK_EQ(lit_count(out), 0);

    // GEN-04: the whole pipeline allocates nothing.
    {
        AllocGuard guard;
        (void)display::render(fb, 200U, display::kDefaultBudgetMa, map, out, stats);
        fb.set(3U, 3U, Rgb{9U, 9U, 9U});
        fb.fill_rect(0U, 0U, 4U, 4U, kBlack);
    }
    CHECK_EQ(g_alloc_count, 0);
}

}  // namespace

void run_display_tests() {
    test_mapping_bijection();
    test_mapping_known_positions();
    test_frame_buffer_bounds();
    test_gamma_table();
    test_current_estimate();
    test_current_limit();
    test_budget_validation();
    test_render_pipeline();
}
