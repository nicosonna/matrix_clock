/**
 * @file levels.cpp
 * @brief Air-quality levels.
 */
#include "air_quality/levels.hpp"

#include <cstring>

namespace air_quality {

namespace {

uint8_t level_index(AirLevel l) noexcept {
    return static_cast<uint8_t>(l);  // Unknown 0, Excellent 1 .. VeryPoor 5
}

/// Level of @p value against increasing thresholds, lowering the boundaries by @p hysteresis
/// for the levels below @p reference (descent must cross threshold - hysteresis).
AirLevel level_of(uint16_t value, const uint16_t* thresholds, uint16_t hysteresis, AirLevel reference) noexcept {
    uint8_t level = 1U;  // Excellent
    for (uint8_t i = 0U; i < kThresholdCount; ++i) {
        // Boundary i separates level i+1 from level i+2.
        const uint8_t upper = static_cast<uint8_t>(i + 2U);
        int32_t boundary = thresholds[i];
        if (upper <= level_index(reference)) {
            boundary -= hysteresis;  // coming down from this level needs the margin
        }
        if (static_cast<int32_t>(value) >= boundary) {
            level = upper;
        }
    }
    return static_cast<AirLevel>(level);
}

}  // namespace

LevelConfig default_level_config() noexcept {
    LevelConfig c{};
    c.pilot = PilotQuantity::Aqi;
    const uint16_t eco2[kThresholdCount] = {600U, 800U, 1000U, 1500U};
    const uint16_t tvoc[kThresholdCount] = {220U, 660U, 1430U, 2200U};
    std::memcpy(c.eco2_thresholds, eco2, sizeof(eco2));
    std::memcpy(c.tvoc_thresholds, tvoc, sizeof(tvoc));
    c.hysteresis_eco2 = 50U;
    c.hysteresis_tvoc = 100U;
    c.confirmation_s = 60U;
    return c;
}

bool is_valid(const LevelConfig& c) noexcept {
    if (c.confirmation_s > kConfirmationMaxS) {
        return false;
    }
    if ((c.pilot != PilotQuantity::Aqi) && (c.pilot != PilotQuantity::Eco2) && (c.pilot != PilotQuantity::Tvoc)) {
        return false;
    }
    for (uint8_t i = 1U; i < kThresholdCount; ++i) {
        if ((c.eco2_thresholds[i] <= c.eco2_thresholds[i - 1U]) || (c.tvoc_thresholds[i] <= c.tvoc_thresholds[i - 1U])) {
            return false;
        }
    }
    if ((c.eco2_thresholds[0] == 0U) || (c.tvoc_thresholds[0] == 0U)) {
        return false;
    }
    // The hysteresis must not swallow a whole level.
    for (uint8_t i = 1U; i < kThresholdCount; ++i) {
        if ((c.hysteresis_eco2 >= (c.eco2_thresholds[i] - c.eco2_thresholds[i - 1U])) ||
            (c.hysteresis_tvoc >= (c.tvoc_thresholds[i] - c.tvoc_thresholds[i - 1U]))) {
            return false;
        }
    }
    return true;
}

AirLevel raw_level(const LevelConfig& c, const AirReading& r) noexcept {
    if (!r.present || r.fault || !r.air_valid) {
        return AirLevel::Unknown;
    }
    switch (c.pilot) {
        case PilotQuantity::Eco2:
            return level_of(r.eco2_ppm, c.eco2_thresholds, 0U, AirLevel::Unknown);
        case PilotQuantity::Tvoc:
            return level_of(r.tvoc_ppb, c.tvoc_thresholds, 0U, AirLevel::Unknown);
        case PilotQuantity::Aqi:
        default: {
            const uint8_t aqi = (r.aqi < 1U) ? 1U : ((r.aqi > 5U) ? 5U : r.aqi);
            return static_cast<AirLevel>(aqi);
        }
    }
}

LevelTracker::LevelTracker() noexcept
    : current_(AirLevel::Unknown), candidate_(AirLevel::Unknown), candidate_since_ms_(0U), pending_(false) {}

AirLevel LevelTracker::step(const LevelConfig& c, const AirReading& r, uint32_t now_ms) noexcept {
    AirLevel target = AirLevel::Unknown;
    if (r.present && !r.fault && r.air_valid) {
        switch (c.pilot) {
            case PilotQuantity::Eco2:
                target = level_of(r.eco2_ppm, c.eco2_thresholds, c.hysteresis_eco2, current_);
                break;
            case PilotQuantity::Tvoc:
                target = level_of(r.tvoc_ppb, c.tvoc_thresholds, c.hysteresis_tvoc, current_);
                break;
            case PilotQuantity::Aqi:
            default:
                target = raw_level(c, r);
                break;
        }
    }
    if (target == AirLevel::Unknown) {
        // Honesty: an invalid reading is shown as unknown at once, no confirmation.
        current_ = AirLevel::Unknown;
        candidate_ = AirLevel::Unknown;
        pending_ = false;
        return current_;
    }
    if (current_ == AirLevel::Unknown) {
        // First valid reading after unknown: show it immediately.
        current_ = target;
        candidate_ = target;
        pending_ = false;
        return current_;
    }
    if (target == current_) {
        candidate_ = current_;
        pending_ = false;
        return current_;
    }
    if (!pending_ || (candidate_ != target)) {
        candidate_ = target;
        candidate_since_ms_ = now_ms;
        pending_ = true;
    }
    const uint32_t held_ms = now_ms - candidate_since_ms_;
    if (held_ms >= (static_cast<uint32_t>(c.confirmation_s) * 1000U)) {
        current_ = candidate_;
        pending_ = false;
    }
    return current_;
}

PilotValue pilot_value(const LevelConfig& c, const AirReading& r) noexcept {
    PilotValue v{};
    v.valid = r.present && !r.fault && r.air_valid;
    switch (c.pilot) {
        case PilotQuantity::Eco2:
            v.value = r.eco2_ppm;
            v.unit = "ppm";
            break;
        case PilotQuantity::Tvoc:
            v.value = r.tvoc_ppb;
            v.unit = "ppb";
            break;
        case PilotQuantity::Aqi:
        default:
            v.value = r.aqi;
            v.unit = "";
            break;
    }
    return v;
}

LevelColor level_color(AirLevel level) noexcept {
    switch (level) {
        case AirLevel::Excellent: return LevelColor{0U, 200U, 60U};
        case AirLevel::Good: return LevelColor{150U, 220U, 0U};
        case AirLevel::Moderate: return LevelColor{255U, 200U, 0U};
        case AirLevel::Poor: return LevelColor{255U, 110U, 0U};
        case AirLevel::VeryPoor: return LevelColor{255U, 30U, 30U};
        case AirLevel::Unknown:
        default: return LevelColor{70U, 70U, 70U};
    }
}

const char* level_name(AirLevel level) noexcept {
    switch (level) {
        case AirLevel::Excellent: return "Excellent";
        case AirLevel::Good: return "Bon";
        case AirLevel::Moderate: return "Moyen";
        case AirLevel::Poor: return "Mauvais";
        case AirLevel::VeryPoor: return "Tres mauvais";
        case AirLevel::Unknown:
        default: return "Inconnu";
    }
}

const char* pilot_name(PilotQuantity p) noexcept {
    switch (p) {
        case PilotQuantity::Eco2: return "Eco2";
        case PilotQuantity::Tvoc: return "Tvoc";
        case PilotQuantity::Aqi:
        default: return "Aqi";
    }
}

bool pilot_from_name(const char* s, PilotQuantity& out) noexcept {
    if (s == nullptr) {
        return false;
    }
    const PilotQuantity all[] = {PilotQuantity::Aqi, PilotQuantity::Eco2, PilotQuantity::Tvoc};
    for (PilotQuantity p : all) {
        if (std::strcmp(s, pilot_name(p)) == 0) {
            out = p;
            return true;
        }
    }
    return false;
}

bool build_offset(int16_t reference_centi, int16_t measured_centi, int16_t& out) noexcept {
    const int32_t diff = static_cast<int32_t>(measured_centi) - reference_centi;
    if ((diff < -kOffsetMaxCenti) || (diff > kOffsetMaxCenti)) {
        return false;
    }
    out = static_cast<int16_t>(diff);
    return true;
}

}  // namespace air_quality
