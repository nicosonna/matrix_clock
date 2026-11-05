/**
 * @file local_time.cpp
 * @brief Calendar arithmetic and DST.
 */
#include "timekeeping/local_time.hpp"

namespace timekeeping {

namespace {
constexpr int64_t kSecondsPerDay = 86400;
constexpr int64_t kSecondsPerMinute = 60;

/// Floor division for possibly negative numerators.
int64_t floor_div(int64_t a, int64_t b) noexcept {
    const int64_t q = a / b;
    return ((a % b != 0) && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/// Weekday of a day count, 0 = Sunday (POSIX convention), 1970-01-01 was a Thursday.
uint8_t posix_weekday(int64_t days) noexcept {
    const int64_t w = (days + 4) % 7;
    return static_cast<uint8_t>((w < 0) ? w + 7 : w);
}
}  // namespace

bool is_leap(int32_t year) noexcept {
    return ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
}

int64_t days_from_civil(int32_t year, uint8_t month, uint8_t day) noexcept {
    const int32_t y = year - ((month <= 2U) ? 1 : 0);
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    const uint32_t m = month;
    const uint32_t doy = (153U * (m + ((m > 2U) ? static_cast<uint32_t>(-3) : 9U)) + 2U) / 5U + day - 1U;
    const uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t days, int32_t& year, uint8_t& month, uint8_t& day) noexcept {
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const uint32_t mp = (5U * doy + 2U) / 153U;
    const uint32_t d = doy - (153U * mp + 2U) / 5U + 1U;
    const uint32_t m = (mp < 10U) ? mp + 3U : mp - 9U;
    year = static_cast<int32_t>(y + ((m <= 2U) ? 1 : 0));
    month = static_cast<uint8_t>(m);
    day = static_cast<uint8_t>(d);
}

int64_t transition_epoch(int32_t year, const Transition& t, int16_t offset_min) noexcept {
    // Day of the month: the `week`-th `weekday` (week 5 = last).
    const int64_t first = days_from_civil(year, t.month, 1U);
    const uint8_t first_wd = posix_weekday(first);
    int32_t day = 1 + ((static_cast<int32_t>(t.weekday) - first_wd + 7) % 7) + (static_cast<int32_t>(t.week) - 1) * 7;
    const uint8_t next_month = static_cast<uint8_t>((t.month == 12U) ? 1U : t.month + 1U);
    const int32_t next_year = (t.month == 12U) ? year + 1 : year;
    const int32_t days_in_month = static_cast<int32_t>(days_from_civil(next_year, next_month, 1U) - first);
    while (day > days_in_month) {
        day -= 7;  // week 5 asked but the month has only 4 such weekdays
    }
    const int64_t local = (first + day - 1) * kSecondsPerDay + static_cast<int64_t>(t.minute_of_day) * kSecondsPerMinute;
    return local - static_cast<int64_t>(offset_min) * kSecondsPerMinute;
}

bool is_dst(int64_t epoch_utc, const TzRule& rule) noexcept {
    if (!rule.has_dst) {
        return false;
    }
    // Year of the local standard time: the transitions of that year decide.
    int32_t year = 0;
    uint8_t month = 0U;
    uint8_t day = 0U;
    civil_from_days(floor_div(epoch_utc + static_cast<int64_t>(rule.std_offset_min) * kSecondsPerMinute, kSecondsPerDay),
                    year, month, day);
    const int64_t start = transition_epoch(year, rule.start, rule.std_offset_min);
    const int64_t end = transition_epoch(year, rule.end, rule.dst_offset_min);
    if (start < end) {
        return (epoch_utc >= start) && (epoch_utc < end);   // northern hemisphere
    }
    return (epoch_utc >= start) || (epoch_utc < end);       // southern: DST spans the new year
}

LocalTime to_local(int64_t epoch_utc, const TzRule& rule) noexcept {
    LocalTime lt{};
    lt.is_dst = is_dst(epoch_utc, rule);
    const int16_t offset = lt.is_dst ? rule.dst_offset_min : rule.std_offset_min;
    const int64_t local = epoch_utc + static_cast<int64_t>(offset) * kSecondsPerMinute;
    const int64_t days = floor_div(local, kSecondsPerDay);
    const int64_t sod = local - days * kSecondsPerDay;
    civil_from_days(days, lt.year, lt.month, lt.day);
    lt.hour = static_cast<uint8_t>(sod / 3600);
    lt.minute = static_cast<uint8_t>((sod % 3600) / 60);
    lt.second = static_cast<uint8_t>(sod % 60);
    // 0 = Monday: POSIX Sunday-based weekday shifted by one.
    lt.weekday = static_cast<uint8_t>((posix_weekday(days) + 6U) % 7U);
    return lt;
}

}  // namespace timekeeping
