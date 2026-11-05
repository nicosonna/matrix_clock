/**
 * @file scheduler.cpp
 * @brief Scene rotation.
 */

#include "display/scheduler.hpp"

namespace display {

SceneList default_scene_list() noexcept {
    SceneList l{};
    l.entries[0] = SceneEntry{SceneKind::Clock, 0U, 20U, true};
    l.entries[1] = SceneEntry{SceneKind::Date, 0U, 5U, true};
    l.entries[2] = SceneEntry{SceneKind::Value, 0U, 8U, true};
    l.entries[3] = SceneEntry{SceneKind::Value, 1U, 8U, true};
    l.count = 4U;
    return l;
}

bool is_valid(const SceneList& list) noexcept {
    if (list.count > kSceneMax) {
        return false;
    }
    for (uint8_t i = 0U; i < list.count; ++i) {
        const SceneEntry& e = list.entries[i];
        if (static_cast<uint8_t>(e.kind) > static_cast<uint8_t>(SceneKind::Air)) {
            return false;
        }
        if ((e.kind == SceneKind::Value) && (e.value_index >= kValueMax)) {
            return false;
        }
    }
    return true;
}

Scheduler::Scheduler(const SceneList& list, uint32_t now_ms) noexcept
    : list_(is_valid(list) ? list : default_scene_list()), current_(kSyntheticClock), started_(false),
      scene_start_ms_(now_ms), notif_on_(false), notif_priority_(0U), notif_end_ms_(now_ms),
      elapsed_before_ms_(0U), pending_(false), pending_priority_(0U), pending_duration_ms_(0U) {}

bool Scheduler::available(uint8_t index, const SceneAvailability& avail) const noexcept {
    const SceneEntry& e = list_.entries[index];
    if (!e.enabled) {
        return false;
    }
    switch (e.kind) {
        case SceneKind::Date:
            return avail.date;
        case SceneKind::Scroll:
            return avail.scroll;
        case SceneKind::Value:
            return avail.values[e.value_index];
        case SceneKind::Air:
            return avail.air;
        case SceneKind::Clock:
        case SceneKind::TwoLines:
        case SceneKind::Night:
        case SceneKind::Test:
        default:
            return true;
    }
}

void Scheduler::advance(uint32_t now_ms, const SceneAvailability& avail) noexcept {
    scene_start_ms_ = now_ms;
    if (list_.count == 0U) {
        current_ = kSyntheticClock;
        return;
    }
    // Next available entry after the current one, wrapping, at most count tries.
    uint8_t idx = (current_ == kSyntheticClock) ? 0U : static_cast<uint8_t>((current_ + 1U) % list_.count);
    for (uint8_t tries = 0U; tries < list_.count; ++tries) {
        if (available(idx, avail)) {
            current_ = idx;
            return;
        }
        idx = static_cast<uint8_t>((idx + 1U) % list_.count);
    }
    // Nothing can be shown: the clock, always (EX: never a dark panel by neglect).
    current_ = kSyntheticClock;
}

void Scheduler::start_notification(uint32_t now_ms) noexcept {
    if (!notif_on_) {
        elapsed_before_ms_ = now_ms - scene_start_ms_;
    }
    notif_on_ = true;
    notif_priority_ = pending_priority_;
    notif_end_ms_ = now_ms + pending_duration_ms_;
    pending_ = false;
}

bool Scheduler::notify(uint8_t priority, uint32_t duration_ms, uint32_t now_ms) noexcept {
    if (notif_on_ && (priority < notif_priority_)) {
        return false;  // a more important one is on screen
    }
    pending_ = true;
    pending_priority_ = priority;
    pending_duration_ms_ = duration_ms;
    if (notif_on_ || (priority >= 1U)) {
        start_notification(now_ms);
        return true;
    }
    return false;  // priority 0: waits for the end of the scene
}

Shown Scheduler::step(uint32_t now_ms, const SceneAvailability& avail, bool current_finished) noexcept {
    if (!started_) {
        started_ = true;
        advance(now_ms, avail);
    }
    if (notif_on_) {
        if (static_cast<int32_t>(now_ms - notif_end_ms_) >= 0) {
            // Hand back to the interrupted scene where it was.
            notif_on_ = false;
            scene_start_ms_ = now_ms - elapsed_before_ms_;
        } else {
            return Shown{true, current_};
        }
    }

    bool scene_over = (current_ == kSyntheticClock);
    if (!scene_over) {
        const uint32_t duration_ms = static_cast<uint32_t>(list_.entries[current_].duration_s) * 1000U;
        const uint32_t elapsed = now_ms - scene_start_ms_;
        scene_over = current_finished || ((duration_ms != 0U) && (elapsed >= duration_ms)) ||
                     !available(current_, avail);
    } else {
        // The synthetic clock is re-evaluated every second so a scene that
        // becomes available again is picked up without waiting.
        scene_over = (now_ms - scene_start_ms_) >= 1000U;
    }

    if (scene_over) {
        if (pending_) {  // priority 0 waited for this moment
            start_notification(now_ms);
            elapsed_before_ms_ = 0U;
            advance(now_ms, avail);
            scene_start_ms_ = now_ms;
            return Shown{true, current_};
        }
        advance(now_ms, avail);
    }
    return Shown{false, current_};
}

}  // namespace display
