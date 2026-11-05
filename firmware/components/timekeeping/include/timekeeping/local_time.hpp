#pragma once
/**
 * @file local_time.hpp
 * @brief UTC epoch -> local civil time, with the DST rule applied.
 *
 * Proleptic Gregorian calendar (Howard Hinnant's days_from_civil /
 * civil_from_days), valid far beyond 2038 since the epoch is 64-bit.
 */

#include <cstdint>

#include "timekeeping/tz.hpp"

namespace timekeeping {

/// Local civil time.
struct LocalTime {
    int32_t year;
    uint8_t month;     ///< 1..12
    uint8_t day;       ///< 1..31
    uint8_t hour;      ///< 0..23
    uint8_t minute;    ///< 0..59
    uint8_t second;    ///< 0..59
    uint8_t weekday;   ///< 0 = Monday .. 6 = Sunday
    bool is_dst;
};

/// Days since 1970-01-01 of a civil date (negative before).
int64_t days_from_civil(int32_t year, uint8_t month, uint8_t day) noexcept;

/// Civil date of a day count since 1970-01-01.
void civil_from_days(int64_t days, int32_t& year, uint8_t& month, uint8_t& day) noexcept;

/// @return true for a leap year.
bool is_leap(int32_t year) noexcept;

/// @return UTC epoch (seconds) at which @p t occurs in @p year, the local
///         clock being @p offset_min east of UTC at that moment.
int64_t transition_epoch(int32_t year, const Transition& t, int16_t offset_min) noexcept;

/// @return true if @p epoch_utc falls in daylight time under @p rule.
bool is_dst(int64_t epoch_utc, const TzRule& rule) noexcept;

/// Converts a UTC epoch to local civil time.
LocalTime to_local(int64_t epoch_utc, const TzRule& rule) noexcept;

}  // namespace timekeeping
