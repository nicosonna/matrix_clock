/**
 * @file brightness.cpp
 * @brief Automatic brightness controller.
 */

#include "brightness/brightness.hpp"

namespace brightness {

Config default_config() noexcept {
    Config c{};
    c.mode = Mode::Auto;
    c.manual_level = 96U;
    c.min_level = 8U;
    c.max_level = 255U;
    c.step = 4U;
    c.rise_s = 10U;
    c.fall_s = 60U;
    c.compensation_counts = 0;
    c.off_schedule = false;
    c.off_start_min = 23U * 60U + 30U;
    c.off_end_min = 6U * 60U + 30U;
    // Wiring (a): more light, higher count. The wizard replaces this.
    c.curve.points[0] = CurvePoint{300U, 8U};
    c.curve.points[1] = CurvePoint{900U, 40U};
    c.curve.points[2] = CurvePoint{2000U, 160U};
    c.curve.points[3] = CurvePoint{3600U, 255U};
    c.curve.count = 4U;
    return c;
}

bool is_valid(const Curve& curve) noexcept {
    if ((curve.count < kCurvePointsMin) || (curve.count > kCurvePointsMax)) {
        return false;
    }
    for (uint8_t i = 0U; i < curve.count; ++i) {
        if (curve.points[i].raw > kRawMax) {
            return false;
        }
        if ((i > 0U) && (curve.points[i].raw <= curve.points[i - 1U].raw)) {
            return false;
        }
    }
    return true;
}

bool is_valid(const Config& cfg) noexcept {
    return (static_cast<uint8_t>(cfg.mode) <= static_cast<uint8_t>(Mode::Off)) &&
           (cfg.min_level <= cfg.max_level) && (cfg.step >= kStepMin) && (cfg.step <= kStepMax) &&
           (cfg.rise_s >= 1U) && (cfg.rise_s <= kRiseMax) && (cfg.fall_s >= 1U) &&
           (cfg.fall_s <= kFallMax) && (cfg.compensation_counts >= -kCompensationMax) &&
           (cfg.compensation_counts <= kCompensationMax) && (cfg.off_start_min < kMinutesPerDay) &&
           (cfg.off_end_min < kMinutesPerDay) && is_valid(cfg.curve);
}

uint8_t interpolate(const Curve& curve, uint16_t raw) noexcept {
    const CurvePoint& first = curve.points[0];
    const CurvePoint& last = curve.points[curve.count - 1U];
    if (raw <= first.raw) {
        return first.level;
    }
    if (raw >= last.raw) {
        return last.level;
    }
    for (uint8_t i = 1U; i < curve.count; ++i) {
        const CurvePoint& a = curve.points[i - 1U];
        const CurvePoint& b = curve.points[i];
        if (raw <= b.raw) {
            const int32_t dx = static_cast<int32_t>(b.raw) - a.raw;
            const int32_t dy = static_cast<int32_t>(b.level) - a.level;
            const int32_t x = static_cast<int32_t>(raw) - a.raw;
            // Rounded to nearest; dx > 0 by validation.
            const int32_t v = static_cast<int32_t>(a.level) + (dy * x + ((dy >= 0) ? dx / 2 : -dx / 2)) / dx;
            return static_cast<uint8_t>((v < 0) ? 0 : ((v > 255) ? 255 : v));
        }
    }
    return last.level;  // unreachable with a valid curve
}

bool in_window(uint16_t minute_of_day, uint16_t start, uint16_t end) noexcept {
    if (start == end) {
        return false;
    }
    if (start < end) {
        return (minute_of_day >= start) && (minute_of_day < end);
    }
    return (minute_of_day >= start) || (minute_of_day < end);  // crosses midnight
}

Controller::Controller(const Config& cfg) noexcept
    : cfg_(is_valid(cfg) ? cfg : default_config()), replaced_(!is_valid(cfg)), median_(), average_(),
      invalid_run_(0U), current_(kFallbackLevel), have_level_(false) {
    average_.configure(cfg_.rise_s, cfg_.fall_s);
}

uint8_t Controller::auto_level(uint16_t corrected) noexcept {
    uint8_t candidate = interpolate(cfg_.curve, corrected);
    if (candidate < cfg_.min_level) {
        candidate = cfg_.min_level;
    } else if (candidate > cfg_.max_level) {
        candidate = cfg_.max_level;
    }
    if (!have_level_) {
        have_level_ = true;
        current_ = candidate;
        return current_;
    }
    // Dead band of one step: +-1 LSB of noise never moves the output. Beyond
    // it, the output jumps to the candidate rounded to a multiple of the step,
    // kept inside [min, max].
    const int16_t diff = static_cast<int16_t>(static_cast<int16_t>(candidate) - current_);
    const int16_t mag = (diff < 0) ? static_cast<int16_t>(-diff) : diff;
    if (mag >= cfg_.step) {
        const uint16_t half = static_cast<uint16_t>(cfg_.step / 2U);
        uint16_t q = static_cast<uint16_t>(((candidate + half) / cfg_.step) * cfg_.step);
        if (q < cfg_.min_level) {
            q = cfg_.min_level;
        } else if (q > cfg_.max_level) {
            q = cfg_.max_level;
        }
        current_ = static_cast<uint8_t>(q);
    }
    return current_;
}

Output Controller::step(Sample sample, uint8_t applied_level, bool time_sure, uint16_t minute_of_day) noexcept {
    Output out{};
    out.config_replaced = replaced_;

    // A reading stuck at either rail is a broken sensor, not darkness or daylight.
    const bool valid = sample.valid && (sample.raw != 0U) && (sample.raw < kRawMax);
    if (valid) {
        invalid_run_ = 0U;
        const uint16_t med = median_.push(sample.raw);
        const uint16_t avg = average_.push(med);
        // Self-illumination: what the panel itself adds at the applied level.
        const int32_t added = (static_cast<int32_t>(cfg_.compensation_counts) * applied_level) / 255;
        int32_t corrected = static_cast<int32_t>(avg) - added;
        if (corrected < 0) {
            corrected = 0;
        } else if (corrected > static_cast<int32_t>(kRawMax)) {
            corrected = kRawMax;
        }
        out.filtered_raw = static_cast<uint16_t>(corrected);
    } else if (invalid_run_ < kFaultAfterInvalid) {
        ++invalid_run_;
    }
    out.sensor_fault = (invalid_run_ >= kFaultAfterInvalid);
    if (!valid && !out.sensor_fault) {
        out.filtered_raw = average_.value();  // a single miss: keep the last state
    }

    out.off_window = cfg_.off_schedule && time_sure && in_window(minute_of_day, cfg_.off_start_min, cfg_.off_end_min);

    switch (cfg_.mode) {
        case Mode::Manual:
            out.level = cfg_.manual_level;
            return out;
        case Mode::Off:
            out.level = 0U;
            return out;
        case Mode::Auto:
        default:
            break;
    }
    if (out.off_window) {
        out.level = 0U;
        return out;
    }
    if (out.sensor_fault) {
        out.level = kFallbackLevel;
        have_level_ = false;  // re-seed the dead band when the sensor comes back
        return out;
    }
    if (!valid) {
        out.level = have_level_ ? current_ : kFallbackLevel;  // one miss: hold
        return out;
    }
    out.level = auto_level(out.filtered_raw);
    return out;
}

bool build_from_wizard(uint16_t raw_dark_off, uint16_t raw_dark_lit, uint16_t raw_evening,
                       uint16_t raw_day, Config& cfg) noexcept {
    if ((raw_dark_off == raw_evening) || (raw_evening == raw_day) || (raw_dark_off == raw_day)) {
        return false;
    }
    if ((raw_dark_off > kRawMax) || (raw_dark_lit > kRawMax) || (raw_evening > kRawMax) || (raw_day > kRawMax)) {
        return false;
    }
    // Three ambient levels -> three points, sorted by raw; the level follows its reading.
    std::array<CurvePoint, 3> pts{CurvePoint{raw_dark_off, cfg.min_level},
                                  CurvePoint{raw_evening, 40U},
                                  CurvePoint{raw_day, cfg.max_level}};
    for (uint8_t i = 0U; i < 2U; ++i) {  // bubble, 3 elements
        for (uint8_t j = 0U; j < static_cast<uint8_t>(2U - i); ++j) {
            if (pts[j].raw > pts[j + 1U].raw) {
                const CurvePoint t = pts[j];
                pts[j] = pts[j + 1U];
                pts[j + 1U] = t;
            }
        }
    }
    const int32_t comp = static_cast<int32_t>(raw_dark_lit) - raw_dark_off;
    cfg.compensation_counts = static_cast<int16_t>(comp);
    cfg.curve.points[0] = pts[0];
    cfg.curve.points[1] = pts[1];
    cfg.curve.points[2] = pts[2];
    cfg.curve.count = 3U;
    return true;
}

}  // namespace brightness
