#pragma once
/**
 * @file current_limit.hpp
 * @brief Current estimate and proportional limiter for the whole panel.
 *
 * 256 WS2812B at full white draw about 15.6 A. The limiter is not a comfort
 * feature: it is what protects the power supply and the wiring. It works on
 * the *linear* (post-gamma) channel values, because the current is proportional
 * to the actual PWM duty, not to the logical colour.
 *
 * Integer arithmetic only. Units: microamperes (ua) internally, milliamperes
 * (ma) for the user-facing budget.
 */

#include <array>
#include <cstdint>

#include "display/color.hpp"
#include "display/geometry.hpp"

namespace display {

/// Current of one colour channel at full duty (WS2812B datasheet: ~20 mA per die).
inline constexpr uint32_t kChannelFullScaleUa = 20000U;
/// Quiescent current of one LED's controller, lit or not.
inline constexpr uint32_t kLedQuiescentUa = 1000U;
/// Quiescent current of the whole panel: cannot be limited away.
inline constexpr uint32_t kPanelQuiescentUa = kLedQuiescentUa * kLedCount;

/// Default budget, in milliamperes.
inline constexpr uint16_t kDefaultBudgetMa = 2000U;
/// Smallest accepted budget: must leave room above the quiescent current.
inline constexpr uint16_t kMinBudgetMa = 400U;
/// Largest accepted budget (a 15 A supply, full white allowed).
inline constexpr uint16_t kMaxBudgetMa = 15000U;

/// Fixed-point unit of the applied scale (256 = 1.0).
inline constexpr uint16_t kScaleOne = 256U;

/// @return true if @p budget_ma is within [kMinBudgetMa, kMaxBudgetMa].
constexpr bool is_valid_budget_ma(uint16_t budget_ma) noexcept {
    return (budget_ma >= kMinBudgetMa) && (budget_ma <= kMaxBudgetMa);
}

/// Linear (post-gamma) frame in physical or logical order — the limiter does not care.
using LinearFrame = std::array<Rgb, kLedCount>;

/**
 * @brief Estimates the panel current for linear channel values.
 * @return Microamperes: quiescent current plus the sum of all channels.
 */
uint32_t estimate_current_ua(const LinearFrame& frame) noexcept;

/// Outcome of apply_current_limit(), published in the diagnostics.
struct CurrentLimitResult {
    uint32_t estimated_ua;  ///< Before limiting.
    uint32_t applied_ua;    ///< After limiting (== estimated_ua when not limited).
    uint16_t scale_q8;      ///< Scale applied to every channel, kScaleOne = unchanged.
    bool limited;           ///< true if the frame was scaled down.
};

/**
 * @brief Scales every channel of every LED so the estimate fits the budget.
 * @param frame     Linear frame, modified in place.
 * @param budget_ma Budget; an invalid value falls back to kDefaultBudgetMa.
 * @return What happened.
 * @note Proportional: hues are preserved, only the global brightness drops.
 *       The quiescent current is never scaled (it does not depend on the image).
 */
CurrentLimitResult apply_current_limit(LinearFrame& frame, uint16_t budget_ma) noexcept;

}  // namespace display
