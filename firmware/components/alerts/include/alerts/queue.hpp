#pragma once
/**
 * @file queue.hpp
 * @brief Bounded notification queue, quiet hours and rendering by priority.
 *
 * Sixteen slots, deduplicated by key, ordered by priority then arrival. Two
 * kinds, **neither can be acknowledged** (EX-46):
 * - **held**: by a raised rule, or by an external source with `persistante`;
 *   replayed every `repeat_s`, removed only by the rule falling or by `effacer`
 *   from the same source;
 * - **one-shot**: played once, expires after `duration_s`.
 * A full queue evicts the lowest-priority, oldest entry only if its priority
 * is strictly below the newcomer's; otherwise the newcomer is rejected and
 * counted (spec-alertes.md section 3).
 */

#include <cstddef>
#include <cstdint>

#include "alerts/notification.hpp"

namespace alerts {

inline constexpr std::size_t kQueueMax = 16U;
inline constexpr uint8_t kCriticalFloor = 64U;     ///< Brightness floor raised by a priority-3 pass.
inline constexpr uint16_t kMinutesPerDay = 1440U;

/// Quiet hours (EX-45).
struct QuietConfig {
    bool enabled;
    uint16_t start_min;     ///< Minutes since midnight; may cross midnight.
    uint16_t end_min;
    bool p3_overrides;      ///< A priority-3 alert flagged `override_quiet` is shown normally.
};

QuietConfig default_quiet_config() noexcept;   ///< On, 22:30 -> 07:00, P3 overrides.
bool is_valid(const QuietConfig& q) noexcept;
/// @return true inside the window. Without a sure time, never quiet (we do not know).
bool quiet_now(const QuietConfig& q, bool time_sure, uint16_t minute_of_day) noexcept;

/// How a pass is drawn.
struct Rendering {
    bool interrupts;      ///< false: waits for the end of the scene (priority 0).
    bool blink;
    uint8_t blink_hz;     ///< 1 or 2 when blinking.
    bool scroll;          ///< false: static band, text truncated.
    bool raise_floor;     ///< Brightness floor to kCriticalFloor.
    uint8_t color[3];     ///< Default colour of the priority.
};

/// @param quiet  Quiet hours active now.
/// @param exempt The entry may ignore quiet hours (priority 3 with override_quiet, and p3_overrides).
Rendering rendering_for(uint8_t priority, bool quiet, bool exempt) noexcept;

/// One queued notification.
struct Entry {
    bool used;
    char key[kKeyMax + 1U];
    char title[kTitleMax + 1U];
    char text[kTextMax + 1U];
    uint8_t priority;
    bool color_set;
    uint8_t color[3];
    uint8_t icon;
    bool held;
    bool override_quiet;
    uint16_t duration_s;
    uint16_t repeat_s;
    uint32_t seq;          ///< Arrival rank (never changed by a replacement).
    uint32_t arrival_ms;
    uint32_t expires_ms;   ///< One-shot only.
    bool passed;           ///< Played at least once.
    uint32_t pass_ms;      ///< Start of the last pass.
};

enum class PushStatus : uint8_t {
    Added = 0U,
    Replaced,   ///< Same key: content replaced, rank kept.
    Evicted,    ///< Added after evicting a lower-priority entry.
    Rejected,   ///< Full of entries of priority >= the newcomer's.
};

/// What the display plays.
struct Pass {
    char key[kKeyMax + 1U];
    char text[kTextMax + 1U];   ///< Text, or the title if the text is empty.
    uint8_t priority;
    bool color_set;
    uint8_t color[3];
    uint8_t icon;
    uint16_t duration_s;
    bool held;
    Rendering rendering;
};

class NotificationQueue {
public:
    NotificationQueue() noexcept;

    /// External notification (held when `persistent`); `clear` removes its key instead.
    PushStatus push(const Notification& n, uint32_t now_ms) noexcept;
    /// Any entry (rules use it); `e.used`, `seq`, `arrival`, `passed` are managed here.
    PushStatus push_entry(const Entry& e, uint32_t now_ms) noexcept;
    /// Removes the entry of @p key; false if absent (no error: clearing twice is fine).
    bool clear(const char* key) noexcept;
    /// Expires one-shot entries.
    void step(uint32_t now_ms) noexcept;

    /**
     * @brief Picks the next pass: an entry never played, or a held one whose `repeat_s` elapsed;
     *        highest priority first, then oldest. During quiet hours only exempt entries play.
     * @return false if nothing is due. The chosen entry is marked played now.
     */
    bool next_pass(uint32_t now_ms, bool quiet, bool p3_overrides, Pass& out) noexcept;

    bool has_held() const noexcept;
    /// Highest priority among held entries (0 if none).
    uint8_t held_priority() const noexcept;
    std::size_t count() const noexcept;
    uint32_t rejected() const noexcept { return rejected_; }
    /// Slot @p i (used or not), nullptr if out of range — for the page.
    const Entry* at(std::size_t i) const noexcept;
    /// Entry of @p key, nullptr if absent.
    const Entry* find(const char* key) const noexcept;

private:
    std::size_t index_of(const char* key) const noexcept;

    Entry entries_[kQueueMax];
    uint32_t next_seq_;
    uint32_t rejected_;
};

}  // namespace alerts
