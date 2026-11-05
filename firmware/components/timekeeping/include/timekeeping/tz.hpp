#pragma once
/**
 * @file tz.hpp
 * @brief Time-zone rule: a POSIX TZ string parsed into offsets and DST transitions.
 *
 * Supported form: `STD offset [DST [offset] ,Mm.w.d[/time],Mm.w.d[/time]]`
 * — e.g. `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Zurich), `UTC0`,
 * `EST5EDT,M3.2.0,M11.1.0`. Offsets are stored in minutes **east** of UTC
 * (POSIX writes them inverted). The `Jn`/`n` day forms and quoted names are
 * not supported: the named zones of this project all use M-rules.
 */

#include <cstddef>
#include <cstdint>

namespace timekeeping {

/// One DST transition, POSIX `Mm.w.d/time`.
struct Transition {
    uint8_t month;          ///< 1..12
    uint8_t week;           ///< 1..5, 5 = last such weekday of the month
    uint8_t weekday;        ///< 0 = Sunday .. 6 = Saturday (POSIX convention)
    int16_t minute_of_day;  ///< Local time of the switch, default 120 (02:00); may exceed a day
};

/// A parsed rule.
struct TzRule {
    int16_t std_offset_min;  ///< Minutes east of UTC in standard time.
    int16_t dst_offset_min;  ///< Minutes east of UTC in daylight time.
    bool has_dst;
    Transition start;        ///< Standard -> daylight (at standard local time).
    Transition end;          ///< Daylight -> standard (at daylight local time).
};

/// Longest POSIX string accepted.
inline constexpr std::size_t kPosixMax = 64U;

/**
 * @brief Parses a POSIX TZ string.
 * @return false on anything unsupported or out of range (@p out untouched).
 */
bool parse_posix_tz(const char* posix, TzRule& out) noexcept;

/// A zone the web page can offer by its IANA name.
struct NamedZone {
    const char* name;   ///< IANA name, e.g. "Europe/Zurich".
    const char* posix;  ///< Its POSIX rule.
};

/// Default zone of the project (Europe/Zurich, EX-03).
inline constexpr char kDefaultZoneName[] = "Europe/Zurich";
inline constexpr char kEuropeZurich[] = "CET-1CEST,M3.5.0,M10.5.0/3";

/// @return The zones offered, in display order.
const NamedZone* named_zones(std::size_t& count) noexcept;

/// @return The POSIX rule of an IANA name, or nullptr if unknown.
const char* posix_for_zone(const char* name) noexcept;

}  // namespace timekeeping
