/**
 * @file test_scenes.cpp
 * @brief Host tests: scrolling (AFF-14), scheduler (AFF-12, AFF-13, AFF-15),
 *        commissioning patterns and chaser (AFF-18).
 */
#include "test_support.hpp"

#include "display/frame_buffer.hpp"
#include "display/icon_zone.hpp"
#include "display/scheduler.hpp"
#include "display/scroll.hpp"
#include "display/test_patterns.hpp"

namespace {

using display::kBlack;
using display::Rgb;
using display::SceneAvailability;
using display::SceneEntry;
using display::SceneKind;
using display::SceneList;
using display::Scheduler;
using display::Shown;

uint16_t lit_count(const display::FrameBuffer& fb) {
    uint16_t n = 0U;
    for (const Rgb& p : fb.pixels()) {
        if (p != kBlack) {
            ++n;
        }
    }
    return n;
}

/// A scratch frame buffer for calls whose output is not inspected.
display::FrameBuffer& fb_unused_guard() {
    static display::FrameBuffer fb;
    return fb;
}

SceneAvailability all_available() {
    SceneAvailability a{};
    a.date = true;
    a.scroll = true;
    for (bool& v : a.values) {
        v = true;
    }
    return a;
}

// --------------------------------------------------------------------- AFF-14

void test_scroller() {
    display::Scroller s;
    CHECK(s.finished());  // nothing to scroll yet

    s.start("HELLO", display::kFont5x7, 20U, 1U);
    CHECK_EQ(s.width(), 29);
    CHECK_EQ(s.x(), 32);
    CHECK(!s.finished());

    // 20 px/s over exactly one second = 20 px, whatever the frame cadence.
    for (uint8_t i = 0U; i < 25U; ++i) {
        s.step(40U);
    }
    CHECK_EQ(s.x(), 12);
    display::Scroller s33;
    s33.start("HELLO", display::kFont5x7, 20U, 1U);
    for (uint8_t i = 0U; i < 30U; ++i) {
        s33.step(33U);
    }
    s33.step(10U);
    CHECK_EQ(s33.x(), 12);  // 990 + 10 ms: identical position, no drift

    // Leaves by the left edge after (32 + 29) px / 20 px/s = 3.05 s.
    uint32_t elapsed = 1000U;
    while (!s.finished() && (elapsed < 10000U)) {
        s.step(40U);
        elapsed += 40U;
    }
    CHECK(s.finished());
    CHECK(elapsed >= 3040U);
    CHECK(elapsed <= 3120U);

    // Two passes: back to the right edge after the first.
    s.start("HI", display::kFont5x7, 60U, 2U);
    uint16_t resets = 0U;
    int16_t last_x = s.x();
    for (uint16_t i = 0U; i < 200U; ++i) {
        s.step(40U);
        if (s.x() > last_x) {
            ++resets;
        }
        last_x = s.x();
    }
    CHECK_EQ(resets, 1);
    CHECK(s.finished());

    // Forever: never finished.
    s.start("LOOP", display::kFont5x7, 60U, 0U);
    for (uint16_t i = 0U; i < 1000U; ++i) {
        s.step(40U);
    }
    CHECK(!s.finished());

    // Speed clamped; text truncated to kTextMax; empty text finishes at once.
    s.start("X", display::kFont5x7, 500U, 1U);
    s.step(1000U);
    CHECK_EQ(s.x(), 32 - 60);
    s.start("X", display::kFont5x7, 0U, 1U);
    s.step(1000U);
    CHECK_EQ(s.x(), 32 - 5);
    char longtext[300];
    for (uint16_t i = 0U; i < 299U; ++i) {
        longtext[i] = 'A';
    }
    longtext[299] = '\0';
    s.start(longtext, display::kFont5x7, 20U, 1U);
    CHECK_EQ(s.width(), 128 * 6 - 1);
    s.start("", display::kFont5x7, 20U, 1U);
    CHECK(s.finished());
    s.step(1000U);  // stepping a finished scroller is a no-op
    CHECK_EQ(s.x(), 32);
    s.draw(fb_unused_guard(), 0, display::kWhite);

    // Drawing follows x(): at x = -2 only part of the first glyph shows.
    display::FrameBuffer fb;
    s.start("A", display::kFont5x7, 20U, 1U);
    while (s.x() > -2) {
        s.step(40U);
    }
    s.draw(fb, 0, display::kWhite);
    CHECK(lit_count(fb) > 0U);
    CHECK(fb.get(5U, 1U) == kBlack);  // the glyph ends before x = 5 when it starts at -2
    CHECK_EQ(fb.out_of_bounds_count(), 0);

    {
        AllocGuard guard;
        s.start("NO ALLOC", display::kFont5x7, 20U, 0U);
        s.step(40U);
        s.draw(fb, 0, display::kWhite);
    }
    CHECK_EQ(g_alloc_count, 0);
}

// ---------------------------------------------------------------- AFF-12 / 13

void test_scheduler_rotation() {
    const SceneList list = display::default_scene_list();
    CHECK(display::is_valid(list));
    CHECK_EQ(list.count, 4);

    SceneAvailability avail = all_available();
    avail.values[1] = false;  // value 1 never arrives

    Scheduler sched(list, 0U);
    Shown s = sched.step(0U, avail, false);
    CHECK(!s.notification);
    CHECK_EQ(s.scene, 0);                 // Clock, 20 s
    s = sched.step(19999U, avail, false);
    CHECK_EQ(s.scene, 0);
    s = sched.step(20000U, avail, false);
    CHECK_EQ(s.scene, 1);                 // Date, 5 s
    s = sched.step(25000U, avail, false);
    CHECK_EQ(s.scene, 2);                 // Value 0, 8 s
    s = sched.step(33000U, avail, false);
    CHECK_EQ(s.scene, 0);                 // Value 1 skipped (no data): back to Clock

    // A scene whose data vanishes mid-way is left at once.
    s = sched.step(53000U, avail, false);
    CHECK_EQ(s.scene, 1);
    avail.date = false;
    s = sched.step(54000U, avail, false);
    CHECK_EQ(s.scene, 2);

    // Everything unavailable: the clock anyway, and recovery within a second.
    SceneList only_date{};
    only_date.entries[0] = SceneEntry{SceneKind::Date, 0U, 5U, true};
    only_date.count = 1U;
    Scheduler sd(only_date, 0U);
    SceneAvailability none{};
    s = sd.step(0U, none, false);
    CHECK_EQ(s.scene, display::kSyntheticClock);
    s = sd.step(500U, none, false);
    CHECK_EQ(s.scene, display::kSyntheticClock);
    none.date = true;
    s = sd.step(1000U, none, false);
    CHECK_EQ(s.scene, 0);

    // Duration 0 = until the scene says it is finished (scrolling text).
    SceneList with_scroll{};
    with_scroll.entries[0] = SceneEntry{SceneKind::Clock, 0U, 2U, true};
    with_scroll.entries[1] = SceneEntry{SceneKind::Scroll, 0U, 0U, true};
    with_scroll.count = 2U;
    Scheduler ss(with_scroll, 0U);
    (void)ss.step(0U, avail, false);
    s = ss.step(2000U, avail, false);
    CHECK_EQ(s.scene, 1);
    s = ss.step(60000U, avail, false);
    CHECK_EQ(s.scene, 1);                 // still scrolling
    s = ss.step(60040U, avail, true);
    CHECK_EQ(s.scene, 0);                 // finished: next

    // Disabled entries are skipped; an invalid list falls back to the default.
    SceneList bad{};
    bad.count = 9U;
    Scheduler sb(bad, 0U);
    CHECK_EQ(sb.list().count, 4);
    SceneList bad_index{};
    bad_index.entries[0] = SceneEntry{SceneKind::Value, 8U, 5U, true};
    bad_index.count = 1U;
    CHECK(!display::is_valid(bad_index));
    SceneList empty{};
    Scheduler se(empty, 0U);
    s = se.step(0U, avail, false);
    CHECK_EQ(s.scene, display::kSyntheticClock);
}

void test_scheduler_notifications() {
    const SceneList list = display::default_scene_list();
    const SceneAvailability avail = all_available();

    // Priority >= 1 interrupts at once and hands back where it was.
    Scheduler sched(list, 0U);
    (void)sched.step(0U, avail, false);                 // Clock from t = 0
    CHECK(sched.notify(2U, 5000U, 5000U));              // 5 s into the clock
    Shown s = sched.step(6000U, avail, false);
    CHECK(s.notification);
    s = sched.step(9999U, avail, false);
    CHECK(s.notification);
    s = sched.step(10000U, avail, false);
    CHECK(!s.notification);
    CHECK_EQ(s.scene, 0);                               // same scene, 15 s left
    s = sched.step(24999U, avail, false);
    CHECK_EQ(s.scene, 0);
    s = sched.step(25000U, avail, false);
    CHECK_EQ(s.scene, 1);

    // Priority 0 waits for the end of the scene.
    Scheduler s0(list, 0U);
    (void)s0.step(0U, avail, false);
    CHECK(!s0.notify(0U, 2000U, 100U));
    s = s0.step(10000U, avail, false);
    CHECK(!s.notification);
    CHECK_EQ(s.scene, 0);
    s = s0.step(20000U, avail, false);
    CHECK(s.notification);                              // shown at the transition
    s = s0.step(22000U, avail, false);
    CHECK(!s.notification);
    CHECK_EQ(s.scene, 1);                               // then the next scene

    // On screen: replaced only by an equal or higher priority.
    Scheduler sr(list, 0U);
    (void)sr.step(0U, avail, false);
    CHECK(sr.notify(2U, 5000U, 1000U));
    CHECK(!sr.notify(1U, 5000U, 2000U));
    CHECK(sr.notify(2U, 1000U, 2000U));                 // replaced, ends at 3000
    s = sr.step(2999U, avail, false);
    CHECK(s.notification);
    s = sr.step(3000U, avail, false);
    CHECK(!s.notification);
    CHECK_EQ(s.scene, 0);

    {
        AllocGuard guard;
        Scheduler sg(list, 0U);
        (void)sg.step(0U, avail, false);
        (void)sg.notify(1U, 10U, 5U);
        (void)sg.step(100U, avail, false);
    }
    CHECK_EQ(g_alloc_count, 0);
}

// --------------------------------------------------------------------- AFF-15

void test_scheduler_wrap() {
    const SceneList list = display::default_scene_list();
    const SceneAvailability avail = all_available();
    const uint32_t start = 0xFFFFF000U;  // wraps 4096 ms later
    Scheduler sched(list, start);
    Shown s = sched.step(start, avail, false);
    CHECK_EQ(s.scene, 0);
    s = sched.step(start + 19999U, avail, false);       // past the wrap
    CHECK_EQ(s.scene, 0);
    s = sched.step(start + 20000U, avail, false);
    CHECK_EQ(s.scene, 1);
    CHECK(sched.notify(3U, 3000U, start + 21000U));
    s = sched.step(start + 23999U, avail, false);
    CHECK(s.notification);
    s = sched.step(start + 24000U, avail, false);
    CHECK(!s.notification);
    CHECK_EQ(s.scene, 1);
}

// --------------------------------------------------------------------- AFF-18

void test_patterns_and_chaser() {
    display::FrameBuffer fb;
    display::draw_test_pattern(fb, display::TestPattern::Pixel);
    CHECK_EQ(lit_count(fb), 1);
    CHECK(fb.get(0U, 0U) == display::kWhite);
    display::draw_test_pattern(fb, display::TestPattern::Column);
    CHECK_EQ(lit_count(fb), 8);
    CHECK(fb.get(0U, 7U) != kBlack);
    CHECK(fb.get(1U, 0U) == kBlack);
    display::draw_test_pattern(fb, display::TestPattern::Diagonal);
    CHECK_EQ(lit_count(fb), 32);
    CHECK(fb.get(4U, 1U) != kBlack);
    CHECK(fb.get(0U, 1U) == kBlack);
    display::draw_test_pattern(fb, display::TestPattern::Full);
    CHECK_EQ(lit_count(fb), 256);
    display::draw_test_pattern(fb, display::TestPattern::None);
    CHECK_EQ(lit_count(fb), 0);

    display::PhysicalFrame frame{};
    display::chaser_frame(0U, frame);
    CHECK(frame[0] == display::kChaserFirstMarker);   // the marker wins over the runner
    CHECK(frame[255] == display::kChaserLastMarker);
    CHECK(frame[1] == kBlack);
    display::chaser_frame(10U, frame);
    CHECK((frame[10] == Rgb{48U, 48U, 48U}));
    CHECK((frame[9] == Rgb{24U, 24U, 24U}));
    CHECK((frame[7] == Rgb{6U, 6U, 6U}));
    CHECK(frame[6] == kBlack);
    CHECK(frame[0] == display::kChaserFirstMarker);
    display::chaser_frame(300U, frame);                // wraps to 44
    CHECK((frame[44] == Rgb{48U, 48U, 48U}));
}

// AFF-16: the icon zone shows one thing, the most important: a held alert, else the air logo, else nothing.
void test_icon_zone() {
    CHECK_ENUM(display::choose_icon_zone(false, false), display::IconZone::None);
    CHECK_ENUM(display::choose_icon_zone(false, true), display::IconZone::Air);
    CHECK_ENUM(display::choose_icon_zone(true, false), display::IconZone::Alert);
    CHECK_ENUM(display::choose_icon_zone(true, true), display::IconZone::Alert);  // alert > air logo
    display::FrameBuffer fb;
    const Rgb c{255U, 30U, 30U};
    display::draw_alert_mark(fb, 26U, 1U, c);
    uint32_t lit = 0U;
    for (uint8_t y = 0U; y < display::kHeight; ++y) {
        for (uint8_t x = 0U; x < display::kWidth; ++x) {
            const Rgb p = fb.get(x, y);
            if ((p.r | p.g | p.b) != 0U) {
                ++lit;
                CHECK((p.r == c.r) && (p.g == c.g) && (p.b == c.b));
                CHECK((x >= 28U) && (x < 30U) && (y >= 1U) && (y < 7U));
                CHECK(y != 5U);  // the gap between the bar and the dot
            }
        }
    }
    CHECK_EQ(lit, display::alert_mark_pixels());
    CHECK_EQ(lit, 10U);
}

}  // namespace

void run_scenes_tests() {
    test_icon_zone();
    test_scroller();
    test_scheduler_rotation();
    test_scheduler_notifications();
    test_scheduler_wrap();
    test_patterns_and_chaser();
}
