#pragma once
/**
 * @file validity.hpp
 * @brief Is the time trustworthy? (EX-02, spec-affichage.md section 5)
 *
 * `sure` is false until the first successful synchronisation, and becomes
 * false again when the last one is older than `max_age_ms` (48 h by default:
 * beyond that the crystal's drift becomes visible). A clock that shows `--:--`
 * is better than one that shows a time nobody should trust.
 */

#include <cstdint>

namespace timekeeping {

/// Default staleness: 48 hours.
inline constexpr uint32_t kDefaultMaxAgeMs = 48U * 60U * 60U * 1000U;

class SyncValidity {
public:
    explicit SyncValidity(uint32_t max_age_ms = kDefaultMaxAgeMs) noexcept;

    /// Records a successful synchronisation at @p now_ms.
    void mark_synced(uint32_t now_ms) noexcept;

    /// @return true if synchronised at least once and not stale (32-bit wrap tolerated).
    bool sure(uint32_t now_ms) const noexcept;

    /// @return Milliseconds since the last synchronisation, or UINT32_MAX if never.
    uint32_t age_ms(uint32_t now_ms) const noexcept;

    bool ever_synced() const noexcept { return ever_; }

private:
    bool ever_;
    uint32_t last_ms_;
    uint32_t max_age_ms_;
};

}  // namespace timekeeping
