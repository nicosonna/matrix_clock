/**
 * @file filters.cpp
 * @brief Median-of-5 and asymmetric exponential average.
 */

#include "brightness/filters.hpp"

namespace brightness {

namespace {

void order(uint16_t& a, uint16_t& b) noexcept {
    if (a > b) {
        const uint16_t t = a;
        a = b;
        b = t;
    }
}

}  // namespace

Median5::Median5() noexcept : window_{}, next_(0U), count_(0U) {}

void Median5::reset() noexcept {
    next_ = 0U;
    count_ = 0U;
}

uint16_t Median5::push(uint16_t sample) noexcept {
    if (count_ == 0U) {
        for (uint16_t& w : window_) {
            w = sample;  // pad: the first sample is the only truth we have
        }
    }
    window_[next_] = sample;
    next_ = static_cast<uint8_t>((next_ + 1U) % 5U);
    if (count_ < 5U) {
        ++count_;
    }
    // Median of 5 with a fixed comparison network (no data-dependent loop).
    uint16_t a = window_[0];
    uint16_t b = window_[1];
    uint16_t c = window_[2];
    uint16_t d = window_[3];
    uint16_t e = window_[4];
    order(a, b);
    order(d, e);
    order(a, c);
    order(b, c);
    order(a, d);
    order(c, d);
    order(b, e);
    order(b, c);
    order(c, d);
    return c;
}

AsymmetricAverage::AsymmetricAverage() noexcept
    : state_q8_(0), alpha_rise_q8_(256U), alpha_fall_q8_(256U), seeded_(false) {}

uint16_t AsymmetricAverage::alpha_q8(uint16_t tau_s) noexcept {
    const uint32_t tau_samples = static_cast<uint32_t>(tau_s) * kSampleRateHz;
    if (tau_samples <= 1U) {
        return 256U;
    }
    const uint32_t a = 256U / tau_samples;
    return (a == 0U) ? 1U : static_cast<uint16_t>(a);
}

void AsymmetricAverage::configure(uint16_t rise_s, uint16_t fall_s) noexcept {
    alpha_rise_q8_ = alpha_q8(rise_s);
    alpha_fall_q8_ = alpha_q8(fall_s);
}

void AsymmetricAverage::reset(uint16_t value) noexcept {
    state_q8_ = static_cast<int32_t>(value) * 256;
    seeded_ = true;
}

uint16_t AsymmetricAverage::push(uint16_t sample) noexcept {
    if (!seeded_) {
        reset(sample);
        return sample;
    }
    const int32_t x_q8 = static_cast<int32_t>(sample) * 256;
    const int32_t delta = x_q8 - state_q8_;
    const uint16_t alpha = (delta > 0) ? alpha_rise_q8_ : alpha_fall_q8_;
    state_q8_ += (delta * static_cast<int32_t>(alpha)) / 256;
    return value();
}

}  // namespace brightness
