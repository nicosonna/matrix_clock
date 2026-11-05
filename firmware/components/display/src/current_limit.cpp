/**
 * @file current_limit.cpp
 * @brief Current estimate and proportional limiter.
 */

#include "display/current_limit.hpp"

namespace display {

namespace {

/// Sum of all channel values of the frame (at most 256 * 765 = 195 840).
uint32_t channel_sum(const LinearFrame& frame) noexcept {
    uint32_t sum = 0U;
    for (const Rgb& p : frame) {
        sum += static_cast<uint32_t>(p.r) + p.g + p.b;
    }
    return sum;
}

/// Microamperes drawn by the channels alone, for a given channel sum.
uint32_t channels_ua(uint32_t sum) noexcept {
    // sum * 20000 is at most 3.9e9: fits uint32_t, but a 64-bit intermediate
    // keeps the headroom obvious should the constants change.
    const uint64_t ua = (static_cast<uint64_t>(sum) * kChannelFullScaleUa) / 255U;
    return static_cast<uint32_t>(ua);
}

uint8_t scale_channel(uint8_t v, uint16_t scale_q8) noexcept {
    return static_cast<uint8_t>((static_cast<uint32_t>(v) * scale_q8) / kScaleOne);
}

}  // namespace

uint32_t estimate_current_ua(const LinearFrame& frame) noexcept {
    return kPanelQuiescentUa + channels_ua(channel_sum(frame));
}

CurrentLimitResult apply_current_limit(LinearFrame& frame, uint16_t budget_ma) noexcept {
    const uint16_t budget = is_valid_budget_ma(budget_ma) ? budget_ma : kDefaultBudgetMa;
    const uint32_t budget_ua = static_cast<uint32_t>(budget) * 1000U;

    CurrentLimitResult result{};
    const uint32_t sum = channel_sum(frame);
    const uint32_t chan_ua = channels_ua(sum);
    result.estimated_ua = kPanelQuiescentUa + chan_ua;
    result.scale_q8 = kScaleOne;
    result.limited = false;
    result.applied_ua = result.estimated_ua;

    if ((result.estimated_ua <= budget_ua) || (chan_ua == 0U)) {
        return result;
    }

    // Budget left for the channels once the quiescent current is paid.
    // kMinBudgetMa guarantees this is positive (400 mA > 256 mA).
    const uint32_t available_ua = budget_ua - kPanelQuiescentUa;
    // Floor keeps the result at or below the budget.
    const uint64_t scale = (static_cast<uint64_t>(available_ua) * kScaleOne) / chan_ua;
    result.scale_q8 = static_cast<uint16_t>(scale);
    result.limited = true;

    for (Rgb& p : frame) {
        p.r = scale_channel(p.r, result.scale_q8);
        p.g = scale_channel(p.g, result.scale_q8);
        p.b = scale_channel(p.b, result.scale_q8);
    }
    result.applied_ua = kPanelQuiescentUa + channels_ua(channel_sum(frame));
    return result;
}

}  // namespace display
