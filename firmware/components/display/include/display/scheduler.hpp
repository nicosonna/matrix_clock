#pragma once
/**
 * @file scheduler.hpp
 * @brief Scene rotation (spec-affichage.md, 4).
 *
 * Rules:
 *  - a scene whose data is invalid is skipped; if everything is skipped, the
 *    clock is shown anyway;
 *  - a notification of priority >= 1 interrupts the current scene and hands it
 *    back where it was; priority 0 waits for the end of the scene;
 *  - durations are in seconds, 0 = until the scene reports it is finished
 *    (scrolling text).
 * Pure, no allocation, millisecond counters tolerate the 32-bit wrap.
 */

#include <array>
#include <cstdint>

namespace display {

/// Scene kinds.
enum class SceneKind : uint8_t {
    Clock = 0U,  ///< HH:MM (or --:--).
    Date,        ///< JJJ DD.MM.
    Value,       ///< Icon + one tracked value.
    TwoLines,    ///< Two short values in 3x4.
    Scroll,      ///< Scrolling text.
    Night,       ///< Dim clock, no blink.
    Test,        ///< Commissioning pattern.
    Air,         ///< Air-quality logo 8x8 + pilot value.
};

/// One entry of the rotation.
struct SceneEntry {
    SceneKind kind;
    uint8_t value_index;  ///< For Value: which tracked value (0..7).
    uint16_t duration_s;  ///< 0 = until finished.
    bool enabled;
};

/// Entries at most.
inline constexpr uint8_t kSceneMax = 8U;
/// Tracked values at most (mirrors the Home Assistant spec).
inline constexpr uint8_t kValueMax = 8U;
/// Returned by step() when no entry can be shown: the caller draws the clock.
inline constexpr uint8_t kSyntheticClock = 0xFFU;

struct SceneList {
    std::array<SceneEntry, kSceneMax> entries;
    uint8_t count;
};

/// Which scenes have valid data right now. The clock is always available.
struct SceneAvailability {
    bool date;
    bool scroll;
    bool air;   ///< Air sensor present (even warming up: the grey logo is honest).
    std::array<bool, kValueMax> values;
};

/// What to show.
struct Shown {
    bool notification;  ///< true: the notification is on screen, `scene` is meaningless.
    uint8_t scene;      ///< Index into the list, or kSyntheticClock.
};

/// @return Clock 20 s, Date 5 s, Value 0 and 1 for 8 s each (spec defaults).
SceneList default_scene_list() noexcept;

/// @return true if the list is well formed (count, indices, kinds).
bool is_valid(const SceneList& list) noexcept;

class Scheduler {
public:
    /// @param list An invalid list is replaced by default_scene_list().
    Scheduler(const SceneList& list, uint32_t now_ms) noexcept;

    /**
     * @brief Advances the rotation.
     * @param now_ms           Current time (wrap tolerated).
     * @param avail            Which scenes have data.
     * @param current_finished The current scene says it is done (scroll ended).
     */
    Shown step(uint32_t now_ms, const SceneAvailability& avail, bool current_finished) noexcept;

    /**
     * @brief Requests a notification display.
     * @param priority    0 waits for the end of the scene; >= 1 interrupts now.
     * @param duration_ms How long it stays.
     * @return true if it is on screen right away.
     * @note A notification already on screen is replaced only by a priority
     *       greater than or equal to its own.
     */
    bool notify(uint8_t priority, uint32_t duration_ms, uint32_t now_ms) noexcept;

    /// Entry currently shown (valid when the last step() did not return a notification).
    uint8_t current() const noexcept { return current_; }

    const SceneList& list() const noexcept { return list_; }

private:
    bool available(uint8_t index, const SceneAvailability& avail) const noexcept;
    void advance(uint32_t now_ms, const SceneAvailability& avail) noexcept;
    void start_notification(uint32_t now_ms) noexcept;

    SceneList list_;
    uint8_t current_;
    bool started_;  ///< First step() picks the first scene without waiting.
    uint32_t scene_start_ms_;
    // Notification state.
    bool notif_on_;
    uint8_t notif_priority_;
    uint32_t notif_end_ms_;
    uint32_t elapsed_before_ms_;  ///< Scene time already spent when interrupted.
    bool pending_;
    uint8_t pending_priority_;
    uint32_t pending_duration_ms_;
};

}  // namespace display
