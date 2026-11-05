#pragma once
/**
 * @file clock_scene.hpp
 * @brief The `Heure` and `Date` scenes, and the honest time model.
 *
 * EX-02: as long as the time has never been synchronised, the clock shows
 * `--:--`, never a wrong time. `TimeInfo::sure` is the only thing the scenes
 * look at to decide; whoever fills it (SNTP, later an RTC) owns that decision.
 */

#include <cstdint>

#include "display/color.hpp"
#include "display/frame_buffer.hpp"

namespace display {

/// Local time as published by the time component.
struct TimeInfo {
    bool sure;        ///< false: never synchronised, or synchronisation too old.
    uint8_t hour;     ///< 0..23
    uint8_t minute;   ///< 0..59
    uint8_t second;   ///< 0..59
    uint8_t day;      ///< 1..31
    uint8_t month;    ///< 1..12
    uint8_t weekday;  ///< 0 = Monday .. 6 = Sunday
};

/// Width of `HH:MM` in the 5x7 font with a 1-pixel colon: 5+1+5+1+1+1+5+1+5.
inline constexpr uint8_t kClockWidth = 25U;
/// Left offset that centres the clock on the panel.
inline constexpr uint8_t kClockCentredX = 3U;
/// Width of `SAM 05.11` in the 3x5 font with 1-pixel dots: 11 + 2 + 17.
inline constexpr uint8_t kDateWidth = 30U;

/// Colours of the clock scene: digits, colon and seconds bar can differ to
/// make the display easier to read at a glance.
struct ClockColors {
    Rgb digits;
    Rgb colon;
    Rgb seconds_bar;
};

/// Colours of the date scene: weekday and numeric date.
struct DateColors {
    Rgb weekday;
    Rgb date;
};

/**
 * @brief Draws `HH:MM` (or `--:--`) with its left edge at @p x, top row 0.
 * @param colon_on   false hides the two colon dots (blink).
 * @param seconds_bar true draws a progress bar of the seconds on the last row.
 */
void draw_clock(FrameBuffer& fb, const TimeInfo& t, uint8_t x, bool colon_on, bool seconds_bar,
                const ClockColors& colors) noexcept;

/**
 * @brief Draws `JJJ DD.MM` (French weekday) in 3x5, left edge at @p x, top row @p y.
 * @note Does nothing when @p t is not sure: a date is time too.
 */
void draw_date(FrameBuffer& fb, const TimeInfo& t, uint8_t x, uint8_t y, const DateColors& colors) noexcept;

/// @return "LUN".."DIM" for weekday 0..6, "???" otherwise.
const char* weekday_abbrev(uint8_t weekday) noexcept;

}  // namespace display
