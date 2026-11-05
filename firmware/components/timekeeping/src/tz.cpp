/**
 * @file tz.cpp
 * @brief POSIX TZ parsing and the named zones.
 */
#include "timekeeping/tz.hpp"

namespace timekeeping {

namespace {

constexpr int16_t kDefaultSwitchMinute = 120;  // 02:00
constexpr int32_t kMaxOffsetMin = 24 * 60;

bool is_alpha(char c) noexcept {
    return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
}

bool is_digit(char c) noexcept {
    return (c >= '0') && (c <= '9');
}

/// Reads an unsigned decimal of at most `max_digits`; false if none.
bool read_number(const char*& p, uint8_t max_digits, int32_t& out) noexcept {
    int32_t v = 0;
    uint8_t n = 0U;
    while (is_digit(*p) && (n < max_digits)) {
        v = v * 10 + (*p - '0');
        ++p;
        ++n;
    }
    if (n == 0U) {
        return false;
    }
    out = v;
    return true;
}

/// Reads `[+-]hh[:mm[:ss]]` as minutes; false if malformed.
bool read_time(const char*& p, int32_t& minutes) noexcept {
    bool negative = false;
    if ((*p == '+') || (*p == '-')) {
        negative = (*p == '-');
        ++p;
    }
    int32_t hours = 0;
    if (!read_number(p, 3U, hours)) {
        return false;
    }
    int32_t mins = 0;
    if (*p == ':') {
        ++p;
        if (!read_number(p, 2U, mins) || (mins > 59)) {
            return false;
        }
        if (*p == ':') {  // seconds: accepted, ignored
            ++p;
            int32_t secs = 0;
            if (!read_number(p, 2U, secs) || (secs > 59)) {
                return false;
            }
        }
    }
    minutes = hours * 60 + mins;
    if (negative) {
        minutes = -minutes;
    }
    return true;
}

/// Skips a zone name (3+ letters); false if too short.
bool skip_name(const char*& p) noexcept {
    uint8_t n = 0U;
    while (is_alpha(*p)) {
        ++p;
        ++n;
    }
    return n >= 3U;
}

/// Reads `Mm.w.d[/time]`.
bool read_transition(const char*& p, Transition& t) noexcept {
    if (*p != 'M') {
        return false;
    }
    ++p;
    int32_t m = 0;
    int32_t w = 0;
    int32_t d = 0;
    if (!read_number(p, 2U, m) || (*p != '.')) {
        return false;
    }
    ++p;
    if (!read_number(p, 1U, w) || (*p != '.')) {
        return false;
    }
    ++p;
    if (!read_number(p, 1U, d)) {
        return false;
    }
    if ((m < 1) || (m > 12) || (w < 1) || (w > 5) || (d > 6)) {
        return false;
    }
    int32_t minute = kDefaultSwitchMinute;
    if (*p == '/') {
        ++p;
        if (!read_time(p, minute)) {
            return false;
        }
        if ((minute < -(7 * 24 * 60)) || (minute > (7 * 24 * 60))) {
            return false;
        }
    }
    t.month = static_cast<uint8_t>(m);
    t.week = static_cast<uint8_t>(w);
    t.weekday = static_cast<uint8_t>(d);
    t.minute_of_day = static_cast<int16_t>(minute);
    return true;
}

constexpr NamedZone kZones[] = {
    {"Europe/Zurich", kEuropeZurich},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Moscow", "MSK-3"},
    {"UTC", "UTC0"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Montreal", "EST5EDT,M3.2.0,M11.1.0"},
    {"Asia/Tokyo", "JST-9"},
    {"Asia/Shanghai", "CST-8"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};

bool same(const char* a, const char* b) noexcept {
    std::size_t i = 0U;
    while ((a[i] != '\0') && (a[i] == b[i])) {
        ++i;
    }
    return a[i] == b[i];
}

}  // namespace

bool parse_posix_tz(const char* posix, TzRule& out) noexcept {
    if (posix == nullptr) {
        return false;
    }
    std::size_t len = 0U;
    while ((posix[len] != '\0') && (len <= kPosixMax)) {
        ++len;
    }
    if ((len == 0U) || (len > kPosixMax)) {
        return false;
    }
    const char* p = posix;
    TzRule r{};

    if (!skip_name(p)) {
        return false;
    }
    int32_t std_posix = 0;
    if (!read_time(p, std_posix) || (std_posix < -kMaxOffsetMin) || (std_posix > kMaxOffsetMin)) {
        return false;
    }
    r.std_offset_min = static_cast<int16_t>(-std_posix);  // POSIX offsets are west-positive
    r.has_dst = false;
    r.dst_offset_min = r.std_offset_min;

    if (*p == '\0') {
        out = r;
        return true;
    }
    // Daylight name, optional offset, then the mandatory rule.
    if (!skip_name(p)) {
        return false;
    }
    if ((*p != ',') && (*p != '\0')) {
        int32_t dst_posix = 0;
        if (!read_time(p, dst_posix) || (dst_posix < -kMaxOffsetMin) || (dst_posix > kMaxOffsetMin)) {
            return false;
        }
        r.dst_offset_min = static_cast<int16_t>(-dst_posix);
    } else {
        r.dst_offset_min = static_cast<int16_t>(r.std_offset_min + 60);
    }
    if (*p != ',') {
        return false;  // a daylight name without transition rules: not supported
    }
    ++p;
    if (!read_transition(p, r.start) || (*p != ',')) {
        return false;
    }
    ++p;
    if (!read_transition(p, r.end) || (*p != '\0')) {
        return false;
    }
    r.has_dst = true;
    out = r;
    return true;
}

const NamedZone* named_zones(std::size_t& count) noexcept {
    count = sizeof(kZones) / sizeof(kZones[0]);
    return kZones;
}

const char* posix_for_zone(const char* name) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    for (const NamedZone& z : kZones) {
        if (same(z.name, name)) {
            return z.posix;
        }
    }
    return nullptr;
}

}  // namespace timekeeping
