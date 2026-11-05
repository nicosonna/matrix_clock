/**
 * @file notification.cpp
 * @brief Inbound notification decoder.
 */
#include "alerts/notification.hpp"

#include "network/credentials.hpp"

namespace alerts {

namespace {

/// Parses an unsigned decimal; false if empty, non-digit or > 65535.
bool parse_u16(const char* s, uint16_t& out) noexcept {
    uint32_t v = 0U;
    std::size_t n = 0U;
    for (; s[n] != '\0'; ++n) {
        if ((s[n] < '0') || (s[n] > '9') || (n >= 5U)) {
            return false;
        }
        v = v * 10U + static_cast<uint32_t>(s[n] - '0');
    }
    if ((n == 0U) || (v > 65535U)) {
        return false;
    }
    out = static_cast<uint16_t>(v);
    return true;
}

/// "1", "true", "oui", "on" => true; "0", "false", "non", "off", "" => false; else invalid.
bool parse_bool(const char* s, bool& out) noexcept {
    const char* const yes[] = {"1", "true", "oui", "on"};
    const char* const no[] = {"0", "false", "non", "off", ""};
    for (const char* y : yes) {
        std::size_t i = 0U;
        while ((y[i] != '\0') && (s[i] == y[i])) {
            ++i;
        }
        if ((y[i] == '\0') && (s[i] == '\0')) {
            out = true;
            return true;
        }
    }
    for (const char* n : no) {
        std::size_t i = 0U;
        while ((n[i] != '\0') && (s[i] == n[i])) {
            ++i;
        }
        if ((n[i] == '\0') && (s[i] == '\0')) {
            out = false;
            return true;
        }
    }
    return false;
}

void copy_bounded(const char* src, char* dst, std::size_t dst_size) noexcept {
    std::size_t i = 0U;
    while ((src[i] != '\0') && ((i + 1U) < dst_size)) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

}  // namespace

bool is_valid_key(const char* key) noexcept {
    if (key == nullptr) {
        return false;
    }
    std::size_t n = 0U;
    for (; key[n] != '\0'; ++n) {
        const char c = key[n];
        const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_');
        if (!ok || (n >= kKeyMax)) {
            return false;
        }
    }
    return n > 0U;
}

DecodeStatus decode_form(const char* body, std::size_t len, Notification& out) noexcept {
    out = Notification{};
    out.priority = kPriorityDefault;
    out.duration_s = kDurationDefault;
    out.repeat_s = kRepeatDefault;

    // Scratch large enough for any field of a bounded body: a value longer
    // than the model is truncated at the copy (and counted), never refused.
    char field[kScratchMax + 1U];
    std::size_t n = 0U;

    if (!network::form_field(body, len, "cle", field, sizeof(field), n)) {
        return DecodeStatus::KeyMissing;  // absent, malformed or absurdly long: no key, no message
    }
    if (!is_valid_key(field)) {
        return DecodeStatus::KeyInvalid;
    }
    copy_bounded(field, out.key, sizeof(out.key));

    if (network::form_field(body, len, "titre", field, sizeof(field), n)) {
        if (n > kTitleMax) {
            ++out.defaults_used;  // truncated: counted as a replaced field
        }
        copy_bounded(field, out.title, sizeof(out.title));
    }
    if (network::form_field(body, len, "texte", field, sizeof(field), n)) {
        if (n > kTextMax) {
            ++out.defaults_used;
        }
        copy_bounded(field, out.text, sizeof(out.text));
    }
    if (network::form_field(body, len, "priorite", field, sizeof(field), n)) {
        uint16_t v = 0U;
        if (parse_u16(field, v) && (v <= kPriorityMax)) {
            out.priority = static_cast<uint8_t>(v);
        } else {
            ++out.defaults_used;
        }
    }
    if (network::form_field(body, len, "duree_s", field, sizeof(field), n)) {
        uint16_t v = 0U;
        if (parse_u16(field, v) && (v >= kDurationMin) && (v <= kDurationMax)) {
            out.duration_s = v;
        } else {
            ++out.defaults_used;
        }
    }
    if (network::form_field(body, len, "rappel_s", field, sizeof(field), n)) {
        uint16_t v = 0U;
        if (parse_u16(field, v) && (v >= kRepeatMin) && (v <= kRepeatMax)) {
            out.repeat_s = v;
        } else {
            ++out.defaults_used;
        }
    }
    if (network::form_field(body, len, "persistante", field, sizeof(field), n)) {
        bool b = false;
        if (parse_bool(field, b)) {
            out.persistent = b;
        } else {
            ++out.defaults_used;
        }
    }
    if (network::form_field(body, len, "effacer", field, sizeof(field), n)) {
        bool b = false;
        if (parse_bool(field, b)) {
            out.clear = b;
        } else {
            ++out.defaults_used;
        }
    }
    return DecodeStatus::Ok;
}

}  // namespace alerts
