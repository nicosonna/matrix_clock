#pragma once
/**
 * @file brightness.hpp
 * @brief Automatic brightness: settings model, curve, controller, wizard helper.
 *
 * Inputs: a raw ADC sample with a validity flag, the brightness currently
 * applied to the panel (for the self-illumination compensation), and the local
 * time when it is sure (for the off window). Output: the brightness to apply,
 * plus honest flags (sensor fault, configuration replaced).
 *
 * The controller runs at 1 Hz (brightness::kSampleRateHz). Integer only.
 */

#include <array>
#include <cstdint>

#include "brightness/filters.hpp"

namespace brightness {

/// ADC resolution the raw samples are expressed in.
inline constexpr uint16_t kRawMax = 4095U;

/// Brightness applied when the sensor is in fault: visible, not blinding (EX-13).
inline constexpr uint8_t kFallbackLevel = 64U;
/// Invalid samples in a row before the sensor is declared in fault.
inline constexpr uint8_t kFaultAfterInvalid = 5U;

/// Curve points at most.
inline constexpr uint8_t kCurvePointsMax = 6U;
inline constexpr uint8_t kCurvePointsMin = 2U;

/// Operating mode.
enum class Mode : uint8_t {
    Auto = 0U,  ///< Follows the sensor.
    Manual,     ///< Fixed level.
    Off,        ///< Panel off.
};

/// One point of the raw -> level curve.
struct CurvePoint {
    uint16_t raw;   ///< Corrected raw count, 0..kRawMax.
    uint8_t level;  ///< Brightness 0..255.
};

/// Piecewise-linear curve; raw strictly increasing, levels free (either wiring).
struct Curve {
    std::array<CurvePoint, kCurvePointsMax> points;
    uint8_t count;
};

/// Settings (bounds in spec-luminosite.md, 3).
struct Config {
    Mode mode;
    uint8_t manual_level;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t step;                      ///< 1..32: output moves by whole steps (dead band).
    uint16_t rise_s;                   ///< 1..300
    uint16_t fall_s;                   ///< 1..600
    int16_t compensation_counts;       ///< Raw counts the panel adds (or removes) at level 255.
    bool off_schedule;
    uint16_t off_start_min;            ///< Minutes since midnight, 0..1439.
    uint16_t off_end_min;              ///< May be below off_start_min (crosses midnight).
    Curve curve;
};

inline constexpr uint8_t kStepMin = 1U;
inline constexpr uint8_t kStepMax = 32U;
inline constexpr uint16_t kRiseMax = 300U;
inline constexpr uint16_t kFallMax = 600U;
inline constexpr int16_t kCompensationMax = 4095;
inline constexpr uint16_t kMinutesPerDay = 1440U;

/// @return The spec defaults; the default curve assumes wiring (a): more light = higher raw.
Config default_config() noexcept;

/// @return true if the curve has 2..6 points with strictly increasing raw.
bool is_valid(const Curve& curve) noexcept;
/// @return true if every field is within bounds and the curve is valid.
bool is_valid(const Config& cfg) noexcept;

/// Linear interpolation on the curve, flat beyond the ends.
uint8_t interpolate(const Curve& curve, uint16_t raw) noexcept;

/// @return true if @p minute_of_day is inside [start, end), crossing midnight when end < start.
bool in_window(uint16_t minute_of_day, uint16_t start, uint16_t end) noexcept;

/// A raw ADC reading.
struct Sample {
    uint16_t raw;  ///< 0..kRawMax
    bool valid;    ///< false: the read failed.
};

/// What the controller decided.
struct Output {
    uint8_t level;          ///< Brightness to apply.
    bool sensor_fault;      ///< Fallback level in use.
    bool config_replaced;   ///< The configuration was invalid; defaults in use.
    bool off_window;        ///< Off schedule active right now.
    uint16_t filtered_raw;  ///< After median, average and compensation.
};

class Controller {
public:
    /// @param cfg Invalid settings are replaced by default_config() (flagged in Output).
    explicit Controller(const Config& cfg) noexcept;

    /**
     * @brief One 1 Hz step.
     * @param sample        Raw reading.
     * @param applied_level Brightness currently on the panel (for compensation).
     * @param time_sure     Local time is trustworthy (off schedule only applies then).
     * @param minute_of_day Local minutes since midnight when time_sure.
     */
    Output step(Sample sample, uint8_t applied_level, bool time_sure, uint16_t minute_of_day) noexcept;

    const Config& config() const noexcept { return cfg_; }

private:
    uint8_t auto_level(uint16_t corrected) noexcept;

    Config cfg_;
    bool replaced_;
    Median5 median_;
    AsymmetricAverage average_;
    uint8_t invalid_run_;
    uint8_t current_;
    bool have_level_;
};

/**
 * @brief Builds the compensation and the curve from the wizard's four readings.
 * @param raw_dark_off   Dark room, panel off.
 * @param raw_dark_lit   Dark room, panel at 255: the self-illumination.
 * @param raw_evening    Evening lighting, panel off.
 * @param raw_day        Daylight, panel off.
 * @param cfg            compensation_counts and curve are written (min/max levels read).
 * @return false if two readings coincide (the curve would not be monotone); cfg untouched.
 * @note Either wiring: points are sorted by raw, levels follow.
 */
bool build_from_wizard(uint16_t raw_dark_off, uint16_t raw_dark_lit, uint16_t raw_evening,
                       uint16_t raw_day, Config& cfg) noexcept;

/// Self-illumination above which the wizard tells the user to move the sensor.
inline constexpr int16_t kCompensationWarnCounts = 600;

}  // namespace brightness
