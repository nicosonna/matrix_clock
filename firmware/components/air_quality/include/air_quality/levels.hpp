#pragma once
/**
 * @file levels.hpp
 * @brief Air-quality levels: thresholds, confirmation, asymmetric hysteresis, colours.
 *
 * A level goes **up** as soon as a threshold is crossed (the air degrades, say
 * it quickly) but only after the confirmation time; it goes **down** only once
 * the value is back below the threshold minus the hysteresis, again held for
 * the confirmation time. An invalid reading gives `Unknown` at once (honesty).
 */

#include <cstdint>

#include "air_quality/sensor.hpp"

namespace air_quality {

enum class AirLevel : uint8_t {
    Unknown = 0U,  ///< Warm-up, fault, absent.
    Excellent,
    Good,
    Moderate,
    Poor,
    VeryPoor,
};

/// Which quantity decides the level, the logo and the message.
enum class PilotQuantity : uint8_t {
    Aqi = 0U,
    Eco2,
    Tvoc,
};

inline constexpr uint8_t kThresholdCount = 4U;   ///< 5 levels = 4 boundaries.
inline constexpr uint16_t kConfirmationMaxS = 3600U;

struct LevelConfig {
    PilotQuantity pilot;
    uint16_t eco2_thresholds[kThresholdCount];  ///< ppm, strictly increasing: Good, Moderate, Poor, VeryPoor.
    uint16_t tvoc_thresholds[kThresholdCount];  ///< ppb, strictly increasing.
    uint16_t hysteresis_eco2;                   ///< ppm to go back down.
    uint16_t hysteresis_tvoc;                   ///< ppb to go back down.
    uint16_t confirmation_s;                    ///< 0..3600.
};

LevelConfig default_level_config() noexcept;
bool is_valid(const LevelConfig& c) noexcept;

/// The level the reading maps to without any smoothing (Unknown if invalid).
AirLevel raw_level(const LevelConfig& c, const AirReading& r) noexcept;

/// Smoothing: confirmation and asymmetric hysteresis.
class LevelTracker {
public:
    LevelTracker() noexcept;

    /// @param now_ms Monotonic time; call once per reading (about 1 Hz).
    AirLevel step(const LevelConfig& c, const AirReading& r, uint32_t now_ms) noexcept;

    AirLevel current() const noexcept { return current_; }
    /// Level waiting for confirmation (equals current() when nothing is pending).
    AirLevel candidate() const noexcept { return candidate_; }

private:
    AirLevel current_;
    AirLevel candidate_;
    uint32_t candidate_since_ms_;
    bool pending_;
};

/// Value of the pilot quantity, and its unit, for the page and the Air scene.
struct PilotValue {
    bool valid;
    uint16_t value;
    const char* unit;  ///< "", "ppm" or "ppb".
};
PilotValue pilot_value(const LevelConfig& c, const AirReading& r) noexcept;

/// Colour of the level: grey for Unknown, never green when unsure (AIR-18).
struct LevelColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
LevelColor level_color(AirLevel level) noexcept;

/// French name, as the page shows it.
const char* level_name(AirLevel level) noexcept;
const char* pilot_name(PilotQuantity p) noexcept;
bool pilot_from_name(const char* s, PilotQuantity& out) noexcept;

/**
 * @brief Temperature offset of the self-heating wizard.
 * @param reference_centi Temperature of a trusted thermometer next to the module.
 * @param measured_centi What the AHT21 reads at the same time.
 * @param out The offset to subtract from the sensor (measured - reference).
 * @return false if the offset exceeds kOffsetMaxCenti (sensor or reference wrong).
 */
bool build_offset(int16_t reference_centi, int16_t measured_centi, int16_t& out) noexcept;
/// Beyond this offset the page says "move the sensor" (2.0 °C).
inline constexpr int16_t kOffsetWarnCenti = 200;

}  // namespace air_quality
