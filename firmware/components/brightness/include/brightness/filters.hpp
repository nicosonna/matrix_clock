#pragma once
/**
 * @file filters.hpp
 * @brief Median-of-5 and asymmetric exponential average, integer only.
 *
 * The chain (spec-luminosite.md, 2): a 5-sample median rejects an isolated
 * spike (torch, flash, ADC glitch); an exponential average with a *fast rise
 * and slow fall* follows a lamp being switched on in seconds while ignoring a
 * passing shadow — asymmetry is the remedy to the pumping loop where the
 * clock dims, the sensor sees less, and the clock dims again.
 */

#include <array>
#include <cstdint>

namespace brightness {

/// Sliding median over the last 5 samples (network of comparisons, no sort loop).
class Median5 {
public:
    Median5() noexcept;
    /// Pushes a sample and returns the median of the window (padded with the
    /// first sample until 5 have been seen).
    uint16_t push(uint16_t sample) noexcept;
    void reset() noexcept;

private:
    std::array<uint16_t, 5> window_;
    uint8_t next_;
    uint8_t count_;
};

/// Samples per second the filters are tuned for (the controller runs at 1 Hz).
inline constexpr uint16_t kSampleRateHz = 1U;

/**
 * @brief Exponential average with distinct time constants up and down.
 *
 * State in 1/256 of a count. alpha = 256 / tau_samples, at least 1.
 */
class AsymmetricAverage {
public:
    AsymmetricAverage() noexcept;
    /// @param rise_s Time constant when the input is above the state (gets brighter).
    /// @param fall_s Time constant when the input is below the state (gets darker).
    void configure(uint16_t rise_s, uint16_t fall_s) noexcept;
    /// Seeds the state (first sample).
    void reset(uint16_t value) noexcept;
    /// Advances by one sample and returns the state.
    uint16_t push(uint16_t sample) noexcept;
    uint16_t value() const noexcept { return static_cast<uint16_t>(state_q8_ / 256); }

private:
    static uint16_t alpha_q8(uint16_t tau_s) noexcept;
    int32_t state_q8_;
    uint16_t alpha_rise_q8_;
    uint16_t alpha_fall_q8_;
    bool seeded_;
};

}  // namespace brightness
