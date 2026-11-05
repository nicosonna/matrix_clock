/**
 * @file test_brightness.cpp
 * @brief Host tests of the pure `brightness` component (LUM-01 .. LUM-14).
 */
#include "test_support.hpp"

#include "brightness/brightness.hpp"
#include "brightness/filters.hpp"

namespace {

using brightness::Config;
using brightness::Controller;
using brightness::Curve;
using brightness::CurvePoint;
using brightness::Mode;
using brightness::Output;
using brightness::Sample;

Sample ok(uint16_t raw) {
    return Sample{raw, true};
}

// --------------------------------------------------------------------- LUM-01

void test_median() {
    brightness::Median5 m;
    CHECK_EQ(m.push(1000U), 1000);
    CHECK_EQ(m.push(1000U), 1000);
    CHECK_EQ(m.push(4000U), 1000);  // one spike: rejected
    CHECK_EQ(m.push(1000U), 1000);
    CHECK_EQ(m.push(1000U), 1000);
    CHECK_EQ(m.push(4000U), 1000);
    CHECK_EQ(m.push(4000U), 4000);  // two consecutive spikes pass (documented limit D7): 1000,4000,1000,4000,4000... median 4000? window = [1000,4000,1000,4000,4000] -> 4000
    m.reset();
    CHECK_EQ(m.push(7U), 7);
    CHECK_EQ(m.push(3U), 7);  // padded with the first sample
    CHECK_EQ(m.push(9U), 7);
    CHECK_EQ(m.push(1U), 7);
    CHECK_EQ(m.push(5U), 5);  // full window 7,3,9,1,5 -> 5
}

// --------------------------------------------------------------------- LUM-02

void test_average_time_constants() {
    brightness::AsymmetricAverage a;
    a.configure(10U, 60U);
    a.reset(0U);
    // Rise: 63 % of a 1000 step after 10 samples, +-10 %.
    uint16_t v = 0U;
    for (uint8_t i = 0U; i < 10U; ++i) {
        v = a.push(1000U);
    }
    CHECK(v >= 570U);
    CHECK(v <= 700U);
    // Fall from 1000 to 0: after 10 samples still far above 37 % -> slower.
    a.reset(1000U);
    for (uint8_t i = 0U; i < 10U; ++i) {
        v = a.push(0U);
    }
    CHECK(v >= 800U);   // 60 s constant: ~85 % left after 10 s
    for (uint8_t i = 0U; i < 50U; ++i) {
        v = a.push(0U);
    }
    CHECK(v >= 330U);   // after 60 samples: ~37 %, +-10 %
    CHECK(v <= 410U);
    // First push seeds without lag.
    brightness::AsymmetricAverage b;
    b.configure(10U, 60U);
    CHECK_EQ(b.push(2222U), 2222);
    // tau 1 s = no filtering; huge tau = alpha floor of 1/256.
    brightness::AsymmetricAverage c;
    c.configure(1U, 1U);
    c.reset(0U);
    CHECK_EQ(c.push(500U), 500);
    brightness::AsymmetricAverage d;
    d.configure(600U, 600U);
    d.reset(0U);
    CHECK(d.push(2560U) <= 11U);
}

// --------------------------------------------------------------------- LUM-03

void test_no_pumping() {
    // The panel adds 400 counts at level 255 and the sensor sees it.
    Config cfg = brightness::default_config();
    cfg.compensation_counts = 400;
    cfg.rise_s = 2U;
    cfg.fall_s = 2U;
    Controller ctl(cfg);
    const uint16_t ambient = 1500U;
    uint8_t applied = 0U;
    uint8_t lo = 255U;
    uint8_t hi = 0U;
    for (uint16_t i = 0U; i < 100U; ++i) {
        const uint16_t seen = static_cast<uint16_t>(ambient + (400U * applied) / 255U);
        const Output o = ctl.step(ok(seen), applied, false, 0U);
        applied = o.level;
        if (i >= 50U) {
            if (applied < lo) {
                lo = applied;
            }
            if (applied > hi) {
                hi = applied;
            }
        }
    }
    CHECK(static_cast<uint8_t>(hi - lo) <= cfg.step);
    // And the converged level is the one for the true ambient light.
    CHECK_EQ(applied, brightness::interpolate(cfg.curve, ambient) / 4U * 4U);
}

// ---------------------------------------------------------------- LUM-04 / 05

void test_interpolation() {
    Curve c{};
    c.points[0] = CurvePoint{100U, 10U};
    c.points[1] = CurvePoint{200U, 30U};
    c.points[2] = CurvePoint{400U, 230U};
    c.count = 3U;
    CHECK(brightness::is_valid(c));
    CHECK_EQ(brightness::interpolate(c, 100U), 10);
    CHECK_EQ(brightness::interpolate(c, 200U), 30);
    CHECK_EQ(brightness::interpolate(c, 400U), 230);
    CHECK_EQ(brightness::interpolate(c, 150U), 20);
    CHECK_EQ(brightness::interpolate(c, 300U), 130);
    CHECK_EQ(brightness::interpolate(c, 0U), 10);      // flat below
    CHECK_EQ(brightness::interpolate(c, 4095U), 230);  // flat above

    // Wiring (b): decreasing levels are fine, raw must still increase.
    Curve inv{};
    inv.points[0] = CurvePoint{500U, 255U};
    inv.points[1] = CurvePoint{3500U, 8U};
    inv.count = 2U;
    CHECK(brightness::is_valid(inv));
    CHECK_EQ(brightness::interpolate(inv, 500U), 255);
    CHECK_EQ(brightness::interpolate(inv, 2000U), 131);  // 131.5: ties round away from zero on a falling segment
    CHECK_EQ(brightness::interpolate(inv, 3500U), 8);
}

// --------------------------------------------------------------------- LUM-06

void test_validation() {
    Config cfg = brightness::default_config();
    CHECK(brightness::is_valid(cfg));

    Curve one{};
    one.points[0] = CurvePoint{1U, 1U};
    one.count = 1U;
    CHECK(!brightness::is_valid(one));
    Curve dup{};
    dup.points[0] = CurvePoint{100U, 1U};
    dup.points[1] = CurvePoint{100U, 2U};
    dup.count = 2U;
    CHECK(!brightness::is_valid(dup));
    Curve down{};
    down.points[0] = CurvePoint{200U, 1U};
    down.points[1] = CurvePoint{100U, 2U};
    down.count = 2U;
    CHECK(!brightness::is_valid(down));
    Curve big{};
    big.points[0] = CurvePoint{1U, 1U};
    big.points[1] = CurvePoint{5000U, 2U};
    big.count = 2U;
    CHECK(!brightness::is_valid(big));
    Curve many{};
    many.count = 7U;
    CHECK(!brightness::is_valid(many));

    Config bad = cfg;
    bad.min_level = 200U;
    bad.max_level = 100U;
    CHECK(!brightness::is_valid(bad));
    bad = cfg;
    bad.step = 0U;
    CHECK(!brightness::is_valid(bad));
    bad = cfg;
    bad.step = 33U;
    CHECK(!brightness::is_valid(bad));
    bad = cfg;
    bad.rise_s = 0U;
    CHECK(!brightness::is_valid(bad));
    bad = cfg;
    bad.fall_s = 601U;
    CHECK(!brightness::is_valid(bad));
    bad = cfg;
    bad.compensation_counts = 4096;
    CHECK(!brightness::is_valid(bad));
    bad = cfg;
    bad.off_start_min = 1440U;
    CHECK(!brightness::is_valid(bad));
    bad = cfg;
    bad.curve = down;
    CHECK(!brightness::is_valid(bad));

    // An invalid configuration is replaced by the defaults, and the output says so.
    Controller ctl(bad);
    CHECK(brightness::is_valid(ctl.config()));
    const Output o = ctl.step(ok(1000U), 0U, false, 0U);
    CHECK(o.config_replaced);
    Controller good(cfg);
    CHECK(!good.step(ok(1000U), 0U, false, 0U).config_replaced);
}

// ---------------------------------------------------------------- LUM-07 / 08 / 09

void test_dead_band_and_bounds() {
    Config cfg = brightness::default_config();
    cfg.rise_s = 1U;
    cfg.fall_s = 1U;  // no averaging: the dead band alone must hold
    Controller ctl(cfg);
    // Mid-curve: 1450 -> between (900,40) and (2000,160): 40 + 550*120/1100 = 100.
    Output o = ctl.step(ok(1450U), 0U, false, 0U);
    CHECK_EQ(o.level, 100);
    // +-1 LSB (about +-0.1 level) and even +-20 counts (+-2 levels) do not move it.
    for (uint16_t i = 0U; i < 20U; ++i) {
        o = ctl.step(ok(static_cast<uint16_t>(1450U + ((i % 2U) ? 20U : 0U))), o.level, false, 0U);
        CHECK_EQ(o.level, 100);
    }
    // A real change (+100 counts = +11 levels) moves it, to a multiple of the step.
    for (uint8_t i = 0U; i < 5U; ++i) {
        o = ctl.step(ok(1550U), o.level, false, 0U);
    }
    CHECK_EQ(o.level, 112);  // 110.9 -> rounded to 4s: 112
    CHECK_EQ(o.level % 4U, 0);

    // Floor and ceiling.
    cfg.min_level = 20U;
    cfg.max_level = 200U;
    Controller bounded(cfg);
    CHECK_EQ(bounded.step(ok(1U), 0U, false, 0U).level, 20);
    for (uint8_t i = 0U; i < 5U; ++i) {
        o = bounded.step(ok(4000U), 0U, false, 0U);
    }
    CHECK_EQ(o.level, 200);
    cfg.min_level = 0U;
    cfg.curve.points[0].level = 0U;
    Controller zero(cfg);
    CHECK_EQ(zero.step(ok(1U), 0U, false, 0U).level, 0);  // min 0 = full off allowed
}

// ---------------------------------------------------------------- LUM-10 / 11

void test_sensor_fault() {
    Config cfg = brightness::default_config();
    cfg.rise_s = 1U;
    cfg.fall_s = 1U;
    Controller ctl(cfg);
    Output o = ctl.step(ok(1450U), 0U, false, 0U);
    CHECK_EQ(o.level, 100);
    // Four misses: hold the last level, no fault yet.
    for (uint8_t i = 0U; i < 4U; ++i) {
        o = ctl.step(Sample{0U, false}, o.level, false, 0U);
        CHECK(!o.sensor_fault);
        CHECK_EQ(o.level, 100);
    }
    // Fifth: fault, fallback level.
    o = ctl.step(Sample{0U, false}, o.level, false, 0U);
    CHECK(o.sensor_fault);
    CHECK_EQ(o.level, brightness::kFallbackLevel);
    // One good reading restores.
    o = ctl.step(ok(1450U), o.level, false, 0U);
    CHECK(!o.sensor_fault);
    CHECK_EQ(o.level, 100);

    // LUM-11: stuck at a rail is a fault too, not "night" or "daylight".
    Controller rail(cfg);
    for (uint8_t i = 0U; i < 5U; ++i) {
        o = rail.step(ok(0U), 0U, false, 0U);
    }
    CHECK(o.sensor_fault);
    CHECK_EQ(o.level, brightness::kFallbackLevel);
    Controller rail_hi(cfg);
    for (uint8_t i = 0U; i < 5U; ++i) {
        o = rail_hi.step(ok(4095U), 0U, false, 0U);
    }
    CHECK(o.sensor_fault);
    // Fault from the very first samples: fallback, never 0 nor 255.
    Controller cold(cfg);
    for (uint8_t i = 0U; i < 5U; ++i) {
        o = cold.step(Sample{0U, false}, 0U, false, 0U);
    }
    CHECK_EQ(o.level, 64);
}

// --------------------------------------------------------------------- LUM-12

void test_off_window() {
    CHECK(brightness::in_window(23U * 60U + 45U, 23U * 60U + 30U, 6U * 60U + 30U));
    CHECK(brightness::in_window(3U * 60U, 23U * 60U + 30U, 6U * 60U + 30U));
    CHECK(brightness::in_window(6U * 60U + 29U, 23U * 60U + 30U, 6U * 60U + 30U));
    CHECK(!brightness::in_window(6U * 60U + 30U, 23U * 60U + 30U, 6U * 60U + 30U));
    CHECK(!brightness::in_window(12U * 60U, 23U * 60U + 30U, 6U * 60U + 30U));
    CHECK(brightness::in_window(10U * 60U, 9U * 60U, 11U * 60U));   // same day
    CHECK(!brightness::in_window(8U * 60U, 9U * 60U, 11U * 60U));
    CHECK(!brightness::in_window(100U, 100U, 100U));               // empty window

    Config cfg = brightness::default_config();
    cfg.off_schedule = true;
    Controller ctl(cfg);
    Output o = ctl.step(ok(1450U), 0U, true, 2U * 60U);  // 02:00, time sure
    CHECK(o.off_window);
    CHECK_EQ(o.level, 0);
    o = ctl.step(ok(1450U), 0U, false, 2U * 60U);        // time not sure: no window applied
    CHECK(!o.off_window);
    CHECK(o.level > 0U);
    o = ctl.step(ok(1450U), 0U, true, 12U * 60U);        // noon
    CHECK(!o.off_window);
}

// --------------------------------------------------------------------- LUM-13

void test_modes() {
    Config cfg = brightness::default_config();
    cfg.mode = Mode::Manual;
    cfg.manual_level = 123U;
    Controller manual(cfg);
    CHECK_EQ(manual.step(ok(10U), 0U, false, 0U).level, 123);
    CHECK_EQ(manual.step(Sample{0U, false}, 0U, false, 0U).level, 123);
    cfg.mode = Mode::Off;
    Controller off(cfg);
    CHECK_EQ(off.step(ok(4000U), 0U, false, 0U).level, 0);
}

// --------------------------------------------------------------------- LUM-14

void test_wizard_curve() {
    Config cfg = brightness::default_config();
    // Wiring (a): dark 200, dark+panel 500, evening 1200, day 3800.
    CHECK(brightness::build_from_wizard(200U, 500U, 1200U, 3800U, cfg));
    CHECK_EQ(cfg.compensation_counts, 300);
    CHECK_EQ(cfg.curve.count, 3);
    CHECK_EQ(cfg.curve.points[0].raw, 200);
    CHECK_EQ(cfg.curve.points[0].level, cfg.min_level);
    CHECK_EQ(cfg.curve.points[1].raw, 1200);
    CHECK_EQ(cfg.curve.points[1].level, 40);
    CHECK_EQ(cfg.curve.points[2].raw, 3800);
    CHECK_EQ(cfg.curve.points[2].level, cfg.max_level);
    CHECK(brightness::is_valid(cfg));

    // Wiring (b): the readings come in reverse; the panel *lowers* the count.
    Config inv = brightness::default_config();
    CHECK(brightness::build_from_wizard(3800U, 3500U, 2600U, 300U, inv));
    CHECK_EQ(inv.compensation_counts, -300);
    CHECK_EQ(inv.curve.points[0].raw, 300);
    CHECK_EQ(inv.curve.points[0].level, inv.max_level);  // day = brightest, lowest count
    CHECK_EQ(inv.curve.points[2].raw, 3800);
    CHECK_EQ(inv.curve.points[2].level, inv.min_level);
    CHECK(brightness::is_valid(inv));
    // The controller then drives the panel the right way round.
    inv.rise_s = 1U;
    inv.fall_s = 1U;
    Controller ctl(inv);
    CHECK_EQ(ctl.step(ok(3800U), 0U, false, 0U).level, inv.min_level);

    // Two identical readings: refused, config untouched.
    Config keep = brightness::default_config();
    CHECK(!brightness::build_from_wizard(1000U, 1200U, 1000U, 3000U, keep));
    CHECK_EQ(keep.curve.count, 4);
    CHECK(!brightness::build_from_wizard(1000U, 1200U, 2000U, 5000U, keep));

    {
        AllocGuard guard;
        Controller g(cfg);
        (void)g.step(ok(1000U), 10U, true, 100U);
        (void)brightness::build_from_wizard(200U, 500U, 1200U, 3800U, cfg);
    }
    CHECK_EQ(g_alloc_count, 0);
}

}  // namespace

void run_brightness_tests() {
    test_median();
    test_average_time_constants();
    test_no_pumping();
    test_interpolation();
    test_validation();
    test_dead_band_and_bounds();
    test_sensor_fault();
    test_off_window();
    test_modes();
    test_wizard_curve();
}
