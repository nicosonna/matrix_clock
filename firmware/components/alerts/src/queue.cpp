/**
 * @file queue.cpp
 * @brief Notification queue, quiet hours, rendering (see queue.hpp).
 */
#include "alerts/queue.hpp"

#include <cstring>

namespace alerts {

namespace {

constexpr uint32_t kMsPerS = 1000U;
constexpr uint16_t kQuietStartDefault = 22U * 60U + 30U;
constexpr uint16_t kQuietEndDefault = 7U * 60U;

/// Default colours by priority: pale blue, amber, orange, red.
constexpr uint8_t kPriorityColor[4][3] = {{90U, 160U, 255U}, {255U, 190U, 40U}, {255U, 110U, 0U}, {255U, 30U, 30U}};

uint8_t clamp_priority(uint8_t p) noexcept {
    return (p > kPriorityMax) ? kPriorityMax : p;
}

void copy_bounded(char* dst, std::size_t size, const char* src) noexcept {
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, size - 1U);
    dst[size - 1U] = '\0';
}

bool exempt_from_quiet(const Entry& e, bool p3_overrides) noexcept {
    return (e.priority == kPriorityMax) && e.override_quiet && p3_overrides;
}

}  // namespace

QuietConfig default_quiet_config() noexcept {
    QuietConfig q{};
    q.enabled = true;
    q.start_min = kQuietStartDefault;
    q.end_min = kQuietEndDefault;
    q.p3_overrides = true;
    return q;
}

bool is_valid(const QuietConfig& q) noexcept {
    return (q.start_min < kMinutesPerDay) && (q.end_min < kMinutesPerDay);
}

bool quiet_now(const QuietConfig& q, bool time_sure, uint16_t minute_of_day) noexcept {
    if (!q.enabled || !time_sure || !is_valid(q) || (minute_of_day >= kMinutesPerDay)) {
        return false;
    }
    if (q.start_min == q.end_min) {
        return false;  // an empty window, not a whole day
    }
    if (q.start_min < q.end_min) {
        return (minute_of_day >= q.start_min) && (minute_of_day < q.end_min);
    }
    // Crosses midnight.
    return (minute_of_day >= q.start_min) || (minute_of_day < q.end_min);
}

Rendering rendering_for(uint8_t priority, bool quiet, bool exempt) noexcept {
    const uint8_t p = clamp_priority(priority);
    Rendering r{};
    r.interrupts = p >= 1U;
    const bool muted = quiet && !exempt;
    r.blink = (p >= 2U) && !muted;
    r.blink_hz = r.blink ? ((p == kPriorityMax) ? 2U : 1U) : 0U;
    r.scroll = !muted;
    r.raise_floor = (p == kPriorityMax) && !muted;
    r.color[0] = kPriorityColor[p][0];
    r.color[1] = kPriorityColor[p][1];
    r.color[2] = kPriorityColor[p][2];
    return r;
}

NotificationQueue::NotificationQueue() noexcept : entries_{}, next_seq_(1U), rejected_(0U) {}

std::size_t NotificationQueue::index_of(const char* key) const noexcept {
    for (std::size_t i = 0U; i < kQueueMax; ++i) {
        if (entries_[i].used && (std::strcmp(entries_[i].key, key) == 0)) {
            return i;
        }
    }
    return kQueueMax;
}

PushStatus NotificationQueue::push(const Notification& n, uint32_t now_ms) noexcept {
    if (n.clear) {
        (void)clear(n.key);
        return PushStatus::Replaced;
    }
    Entry e{};
    copy_bounded(e.key, sizeof(e.key), n.key);
    copy_bounded(e.title, sizeof(e.title), n.title);
    copy_bounded(e.text, sizeof(e.text), n.text);
    e.priority = clamp_priority(n.priority);
    e.color_set = false;
    e.icon = 0U;
    e.held = n.persistent;
    e.override_quiet = false;
    e.duration_s = n.duration_s;
    e.repeat_s = n.repeat_s;
    return push_entry(e, now_ms);
}

PushStatus NotificationQueue::push_entry(const Entry& proto, uint32_t now_ms) noexcept {
    if (!is_valid_key(proto.key)) {
        ++rejected_;
        return PushStatus::Rejected;
    }
    const uint8_t priority = clamp_priority(proto.priority);
    std::size_t slot = index_of(proto.key);
    PushStatus status = PushStatus::Replaced;
    if (slot == kQueueMax) {
        status = PushStatus::Added;
        for (std::size_t i = 0U; i < kQueueMax; ++i) {
            if (!entries_[i].used) {
                slot = i;
                break;
            }
        }
        if (slot == kQueueMax) {
            // Full: evict the lowest priority, oldest, only if strictly below the newcomer.
            std::size_t victim = kQueueMax;
            for (std::size_t i = 0U; i < kQueueMax; ++i) {
                const Entry& e = entries_[i];
                if (e.priority >= priority) {
                    continue;
                }
                if ((victim == kQueueMax) || (e.priority < entries_[victim].priority) ||
                    ((e.priority == entries_[victim].priority) && (e.seq < entries_[victim].seq))) {
                    victim = i;
                }
            }
            if (victim == kQueueMax) {
                ++rejected_;
                return PushStatus::Rejected;
            }
            slot = victim;
            status = PushStatus::Evicted;
        }
    }
    Entry& e = entries_[slot];
    const bool replacing = status == PushStatus::Replaced;
    const uint32_t seq = replacing ? e.seq : next_seq_++;
    const uint32_t arrival = replacing ? e.arrival_ms : now_ms;
    const bool passed = replacing ? e.passed : false;
    const uint32_t pass_ms = replacing ? e.pass_ms : 0U;
    e = proto;
    e.used = true;
    e.priority = priority;
    e.seq = seq;
    e.arrival_ms = arrival;
    e.expires_ms = now_ms + (static_cast<uint32_t>(proto.duration_s) * kMsPerS);  // pushed back on replacement
    e.passed = passed;
    e.pass_ms = pass_ms;
    return status;
}

bool NotificationQueue::clear(const char* key) noexcept {
    if (key == nullptr) {
        return false;
    }
    const std::size_t slot = index_of(key);
    if (slot == kQueueMax) {
        return false;
    }
    entries_[slot] = Entry{};
    return true;
}

void NotificationQueue::step(uint32_t now_ms) noexcept {
    for (Entry& e : entries_) {
        if (e.used && !e.held && (static_cast<int32_t>(now_ms - e.expires_ms) >= 0)) {
            e = Entry{};
        }
    }
}

bool NotificationQueue::next_pass(uint32_t now_ms, bool quiet, bool p3_overrides, Pass& out) noexcept {
    std::size_t best = kQueueMax;
    for (std::size_t i = 0U; i < kQueueMax; ++i) {
        const Entry& e = entries_[i];
        if (!e.used) {
            continue;
        }
        const bool exempt = exempt_from_quiet(e, p3_overrides);
        if (quiet && !exempt) {
            continue;  // suspended, kept
        }
        const bool due = !e.passed || (e.held && ((now_ms - e.pass_ms) >= (static_cast<uint32_t>(e.repeat_s) * kMsPerS)));
        if (!due) {
            continue;
        }
        if ((best == kQueueMax) || (e.priority > entries_[best].priority) ||
            ((e.priority == entries_[best].priority) && (e.seq < entries_[best].seq))) {
            best = i;
        }
    }
    if (best == kQueueMax) {
        return false;
    }
    Entry& e = entries_[best];
    e.passed = true;
    e.pass_ms = now_ms;
    out = Pass{};
    copy_bounded(out.key, sizeof(out.key), e.key);
    copy_bounded(out.text, sizeof(out.text), (e.text[0] != '\0') ? e.text : e.title);
    out.priority = e.priority;
    out.color_set = e.color_set;
    out.color[0] = e.color[0];
    out.color[1] = e.color[1];
    out.color[2] = e.color[2];
    out.icon = e.icon;
    out.duration_s = e.duration_s;
    out.held = e.held;
    out.rendering = rendering_for(e.priority, quiet, exempt_from_quiet(e, p3_overrides));
    if (!out.color_set) {
        out.color[0] = out.rendering.color[0];
        out.color[1] = out.rendering.color[1];
        out.color[2] = out.rendering.color[2];
    }
    return true;
}

bool NotificationQueue::has_held() const noexcept {
    for (const Entry& e : entries_) {
        if (e.used && e.held) {
            return true;
        }
    }
    return false;
}

uint8_t NotificationQueue::held_priority() const noexcept {
    uint8_t p = 0U;
    for (const Entry& e : entries_) {
        if (e.used && e.held && (e.priority > p)) {
            p = e.priority;
        }
    }
    return p;
}

std::size_t NotificationQueue::count() const noexcept {
    std::size_t n = 0U;
    for (const Entry& e : entries_) {
        n += e.used ? 1U : 0U;
    }
    return n;
}

const Entry* NotificationQueue::at(std::size_t i) const noexcept {
    return (i < kQueueMax) ? &entries_[i] : nullptr;
}

const Entry* NotificationQueue::find(const char* key) const noexcept {
    if (key == nullptr) {
        return nullptr;
    }
    const std::size_t slot = index_of(key);
    return (slot == kQueueMax) ? nullptr : &entries_[slot];
}

}  // namespace alerts
