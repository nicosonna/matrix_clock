/**
 * @file test_timekeeping.cpp
 * @brief Host tests of the pure `timekeeping` component (TIME-01 .. TIME-08).
 *
 * Reference epochs computed with Python's datetime (UTC).
 */
#include "test_support.hpp"

#include <cstring>

#include "timekeeping/local_time.hpp"
#include "timekeeping/tz.hpp"
#include "timekeeping/validity.hpp"

namespace {

using timekeeping::LocalTime;
using timekeeping::TzRule;

TzRule zurich() {
    TzRule r{};
    CHECK(timekeeping::parse_posix_tz(timekeeping::kEuropeZurich, r));
    return r;
}

// --------------------------------------------------------------------- TIME-01

void test_parse() {
    TzRule r{};
    CHECK(timekeeping::parse_posix_tz("CET-1CEST,M3.5.0,M10.5.0/3", r));
    CHECK_EQ(r.std_offset_min, 60);
    CHECK_EQ(r.dst_offset_min, 120);
    CHECK(r.has_dst);
    CHECK_EQ(r.start.month, 3);
    CHECK_EQ(r.start.week, 5);
    CHECK_EQ(r.start.weekday, 0);
    CHECK_EQ(r.start.minute_of_day, 120);
    CHECK_EQ(r.end.month, 10);
    CHECK_EQ(r.end.minute_of_day, 180);

    CHECK(timekeeping::parse_posix_tz("UTC0", r));
    CHECK_EQ(r.std_offset_min, 0);
    CHECK(!r.has_dst);
    CHECK(timekeeping::parse_posix_tz("EST5EDT,M3.2.0,M11.1.0", r));
    CHECK_EQ(r.std_offset_min, -300);
    CHECK_EQ(r.dst_offset_min, -240);
    CHECK_EQ(r.start.week, 2);
    CHECK_EQ(r.end.month, 11);
    CHECK(timekeeping::parse_posix_tz("GMT0BST,M3.5.0/1,M10.5.0", r));
    CHECK_EQ(r.std_offset_min, 0);
    CHECK_EQ(r.dst_offset_min, 60);
    CHECK_EQ(r.start.minute_of_day, 60);
    CHECK(timekeeping::parse_posix_tz("AEST-10AEDT,M10.1.0,M4.1.0/3", r));
    CHECK_EQ(r.std_offset_min, 600);
    CHECK_EQ(r.dst_offset_min, 660);
    CHECK(timekeeping::parse_posix_tz("IST-5:30", r));
    CHECK_EQ(r.std_offset_min, 330);
    CHECK(timekeeping::parse_posix_tz("NST3:30NDT,M3.2.0,M11.1.0", r));
    CHECK_EQ(r.std_offset_min, -210);
    CHECK_EQ(r.dst_offset_min, -150);
    CHECK(timekeeping::parse_posix_tz("XYZ-1ABC-2,M3.5.0/-1,M10.5.0/25", r));  // explicit dst offset, odd hours
    CHECK_EQ(r.dst_offset_min, 120);
    CHECK_EQ(r.start.minute_of_day, -60);
    CHECK_EQ(r.end.minute_of_day, 1500);

    // Seconds in an offset are accepted and ignored; a name may be longer than 3 letters.
    CHECK(timekeeping::parse_posix_tz("CET-1:00:30", r));
    CHECK_EQ(r.std_offset_min, 60);
    CHECK(timekeeping::parse_posix_tz("CENTRAL-1", r));
    CHECK_EQ(r.std_offset_min, 60);

    // Refused.
    const TzRule keep = r;
    const char* bad[] = {"", "CET", "CE-1", "CET-25", "CET-1CEST", "CET-1CEST,M13.5.0,M10.5.0", "CET-1CEST,M3.6.0,M10.5.0",
                         "CET-1CEST,M3.5.7,M10.5.0", "CET-1CEST,M3.5.0", "CET-1CEST,J80,J300", "CET-1CEST,M3.5.0,M10.5.0x",
                         "CET-1CEST,M3.5.0/1:60,M10.5.0", "CET-1:60", "CET-1:00:60", "CET-1CEST-2", "CET-1CEST-25,M3.5.0,M10.5.0",
                         "CET-1CEST,M3.5.0/200,M10.5.0", "CET-1CEST,M3.5.0,M10.5.0,extra", "CET-1CEST,M3.5,M10.5.0",
                         "CET-1CEST,M3,M10.5.0", "CET-1CEST,M3.5.0,X10.5.0"};
    for (const char* b : bad) {
        TzRule t = keep;
        CHECK(!timekeeping::parse_posix_tz(b, t));
        CHECK_EQ(t.std_offset_min, keep.std_offset_min);
    }
    CHECK(!timekeeping::parse_posix_tz(nullptr, r));
    char too_long[80];
    std::memset(too_long, 'A', 79U);
    too_long[79] = '\0';
    CHECK(!timekeeping::parse_posix_tz(too_long, r));
}

// ---------------------------------------------------------------- TIME-02 / 03

void test_zurich_and_dst() {
    const TzRule z = zurich();
    // 2026-11-05 12:00:00 UTC = 13:00 CET, Thursday, no DST.
    LocalTime t = timekeeping::to_local(1793880000, z);
    CHECK_EQ(t.year, 2026);
    CHECK_EQ(t.month, 11);
    CHECK_EQ(t.day, 5);
    CHECK_EQ(t.hour, 13);
    CHECK_EQ(t.minute, 0);
    CHECK_EQ(t.second, 0);
    CHECK_EQ(t.weekday, 3);  // Thursday (0 = Monday)
    CHECK(!t.is_dst);

    // DST starts 2026-03-29 01:00 UTC: 01:59:59 CET becomes 03:00:00 CEST.
    t = timekeeping::to_local(1774745999, z);
    CHECK_EQ(t.hour, 1);
    CHECK_EQ(t.minute, 59);
    CHECK_EQ(t.second, 59);
    CHECK(!t.is_dst);
    t = timekeeping::to_local(1774746000, z);
    CHECK_EQ(t.hour, 3);
    CHECK_EQ(t.minute, 0);
    CHECK(t.is_dst);
    CHECK_EQ(t.weekday, 6);  // Sunday

    // DST ends 2026-10-25 01:00 UTC: 02:59:59 CEST becomes 02:00:00 CET.
    t = timekeeping::to_local(1792889999, z);
    CHECK_EQ(t.hour, 2);
    CHECK_EQ(t.minute, 59);
    CHECK(t.is_dst);
    t = timekeeping::to_local(1792890000, z);
    CHECK_EQ(t.hour, 2);
    CHECK_EQ(t.minute, 0);
    CHECK(!t.is_dst);

    // TIME-03: United States, second Sunday of March at 02:00 local.
    TzRule ny{};
    CHECK(timekeeping::parse_posix_tz("EST5EDT,M3.2.0,M11.1.0", ny));
    t = timekeeping::to_local(1772953199, ny);  // 2026-03-08 06:59:59 UTC
    CHECK_EQ(t.hour, 1);
    CHECK_EQ(t.minute, 59);
    CHECK(!t.is_dst);
    t = timekeeping::to_local(1772953200, ny);
    CHECK_EQ(t.hour, 3);
    CHECK(t.is_dst);
    CHECK_EQ(t.day, 8);
}

// ---------------------------------------------------------------- TIME-04 / 05

void test_calendar() {
    // Leap day crossing midnight in Zurich: 2028-02-29 23:30 UTC = 2028-03-01 00:30 CET, Wednesday.
    const TzRule z = zurich();
    LocalTime t = timekeeping::to_local(1835479800, z);
    CHECK_EQ(t.year, 2028);
    CHECK_EQ(t.month, 3);
    CHECK_EQ(t.day, 1);
    CHECK_EQ(t.hour, 0);
    CHECK_EQ(t.minute, 30);
    CHECK_EQ(t.weekday, 2);
    CHECK(timekeeping::is_leap(2028));
    CHECK(!timekeeping::is_leap(2100));
    CHECK(timekeeping::is_leap(2000));
    CHECK(!timekeeping::is_leap(2026));

    TzRule utc{};
    CHECK(timekeeping::parse_posix_tz("UTC0", utc));
    t = timekeeping::to_local(946684800, utc);  // 2000-01-01 00:00 Saturday
    CHECK_EQ(t.year, 2000);
    CHECK_EQ(t.month, 1);
    CHECK_EQ(t.day, 1);
    CHECK_EQ(t.weekday, 5);
    t = timekeeping::to_local(2147483647, utc);  // 2038-01-19 03:14:07 Tuesday: no 32-bit cliff
    CHECK_EQ(t.year, 2038);
    CHECK_EQ(t.hour, 3);
    CHECK_EQ(t.minute, 14);
    CHECK_EQ(t.second, 7);
    CHECK_EQ(t.weekday, 1);
    t = timekeeping::to_local(4107542400, utc);  // 2100-03-01 00:00 Monday (2100 is not leap)
    CHECK_EQ(t.year, 2100);
    CHECK_EQ(t.month, 3);
    CHECK_EQ(t.day, 1);
    CHECK_EQ(t.weekday, 0);
    t = timekeeping::to_local(0, utc);  // 1970-01-01 Thursday
    CHECK_EQ(t.year, 1970);
    CHECK_EQ(t.weekday, 3);
    t = timekeeping::to_local(-1, utc);  // just before the epoch: 1969-12-31 23:59:59 Wednesday
    CHECK_EQ(t.year, 1969);
    CHECK_EQ(t.month, 12);
    CHECK_EQ(t.day, 31);
    CHECK_EQ(t.hour, 23);
    CHECK_EQ(t.second, 59);
    CHECK_EQ(t.weekday, 2);

    // Round trip of the calendar arithmetic over a wide range.
    bool round_trip = true;
    for (int64_t d = -200000; d <= 200000; d += 7919) {
        int32_t y = 0;
        uint8_t m = 0U;
        uint8_t dd = 0U;
        timekeeping::civil_from_days(d, y, m, dd);
        if (timekeeping::days_from_civil(y, m, dd) != d) {
            round_trip = false;
        }
    }
    CHECK(round_trip);
}

// --------------------------------------------------------------------- TIME-06

void test_southern_hemisphere() {
    TzRule syd{};
    CHECK(timekeeping::parse_posix_tz("AEST-10AEDT,M10.1.0,M4.1.0/3", syd));
    // 2026-01-15 00:00 UTC: January is summer, 11:00 AEDT.
    LocalTime t = timekeeping::to_local(1768435200, syd);
    CHECK_EQ(t.hour, 11);
    CHECK(t.is_dst);
    CHECK_EQ(t.day, 15);
    // 2026-11-05 12:00 UTC: November is summer too, 23:00 AEDT.
    t = timekeeping::to_local(1793880000, syd);
    CHECK_EQ(t.hour, 23);
    CHECK(t.is_dst);
    // Mid-winter: 2026-06-15 12:00 UTC = 22:00 AEST.
    t = timekeeping::to_local(1793880000 - 143 * 86400, syd);
    CHECK_EQ(t.month, 6);
    CHECK_EQ(t.hour, 22);
    CHECK(!t.is_dst);
}

// --------------------------------------------------------------------- TIME-07

void test_validity() {
    timekeeping::SyncValidity v;
    CHECK(!v.sure(0U));
    CHECK(!v.ever_synced());
    CHECK_EQ(v.age_ms(5000U), UINT32_MAX);
    v.mark_synced(1000U);
    CHECK(v.sure(1000U));
    CHECK(v.ever_synced());
    CHECK_EQ(v.age_ms(6000U), 5000);
    CHECK(v.sure(1000U + timekeeping::kDefaultMaxAgeMs - 1U));
    CHECK(!v.sure(1000U + timekeeping::kDefaultMaxAgeMs));  // 48 h: stale
    v.mark_synced(1000U + timekeeping::kDefaultMaxAgeMs);
    CHECK(v.sure(1000U + timekeeping::kDefaultMaxAgeMs + 10U));
    // Wrap of the millisecond counter.
    timekeeping::SyncValidity w(60000U);
    w.mark_synced(0xFFFFF000U);
    CHECK(w.sure(0xFFFFF000U + 59000U));   // across the wrap
    CHECK(!w.sure(0xFFFFF000U + 60000U));
    timekeeping::SyncValidity z(0U);  // 0 => default
    z.mark_synced(0U);
    CHECK(z.sure(timekeeping::kDefaultMaxAgeMs - 1U));
}

// --------------------------------------------------------------------- TIME-08

void test_named_zones() {
    std::size_t n = 0U;
    const timekeeping::NamedZone* zones = timekeeping::named_zones(n);
    CHECK(n >= 10U);
    bool all_parse = true;
    bool has_default = false;
    for (std::size_t i = 0U; i < n; ++i) {
        TzRule r{};
        if (!timekeeping::parse_posix_tz(zones[i].posix, r)) {
            all_parse = false;
        }
        if (std::strcmp(zones[i].name, timekeeping::kDefaultZoneName) == 0) {
            has_default = true;
        }
    }
    CHECK(all_parse);
    CHECK(has_default);
    CHECK(std::strcmp(timekeeping::posix_for_zone("Europe/Zurich"), timekeeping::kEuropeZurich) == 0);
    CHECK(std::strcmp(timekeeping::posix_for_zone("UTC"), "UTC0") == 0);
    CHECK(timekeeping::posix_for_zone("Mars/Olympus") == nullptr);
    CHECK(timekeeping::posix_for_zone("Europe/Zuric") == nullptr);
    CHECK(timekeeping::posix_for_zone(nullptr) == nullptr);

    {
        AllocGuard guard;
        TzRule r{};
        (void)timekeeping::parse_posix_tz(timekeeping::kEuropeZurich, r);
        (void)timekeeping::to_local(1793880000, r);
        (void)timekeeping::posix_for_zone("UTC");
    }
    CHECK_EQ(g_alloc_count, 0);
}

}  // namespace

void run_timekeeping_tests() {
    test_parse();
    test_zurich_and_dst();
    test_calendar();
    test_southern_hemisphere();
    test_validity();
    test_named_zones();
}
