/**
 * @file validity.cpp
 * @brief Time validity.
 */
#include "timekeeping/validity.hpp"

namespace timekeeping {

SyncValidity::SyncValidity(uint32_t max_age_ms) noexcept
    : ever_(false), last_ms_(0U), max_age_ms_((max_age_ms == 0U) ? kDefaultMaxAgeMs : max_age_ms) {}

void SyncValidity::mark_synced(uint32_t now_ms) noexcept {
    ever_ = true;
    last_ms_ = now_ms;
}

uint32_t SyncValidity::age_ms(uint32_t now_ms) const noexcept {
    return ever_ ? (now_ms - last_ms_) : UINT32_MAX;
}

bool SyncValidity::sure(uint32_t now_ms) const noexcept {
    return ever_ && ((now_ms - last_ms_) < max_age_ms_);
}

}  // namespace timekeeping
