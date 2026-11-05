/**
 * @file credentials.cpp
 * @brief Wi-Fi credentials (see credentials.hpp).
 */
#include "network/credentials.hpp"

#include <cstdio>

namespace network {

namespace {

/// Value of a hexadecimal digit, or -1.
int32_t hex_value(char c) noexcept {
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return (c - 'a') + 10;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return (c - 'A') + 10;
    }
    return -1;
}

/// Is the segment [s, s+n) exactly the key?
bool key_equals(const char* s, std::size_t n, const char* key) noexcept {
    std::size_t k = 0U;
    while ((k < n) && (key[k] != '\0')) {
        if (s[k] != key[k]) {
            return false;
        }
        ++k;
    }
    return (k == n) && (key[k] == '\0');
}

}  // namespace

CredStatus validate(const char* ssid, std::size_t ssid_len, const char* pass,
                    std::size_t pass_len) noexcept {
    if ((ssid == nullptr) || (ssid_len == 0U)) {
        return CredStatus::SsidEmpty;
    }
    if (ssid_len > kSsidMax) {
        return CredStatus::SsidTooLong;
    }
    if (pass_len == 0U) {
        return CredStatus::Ok;  // open network
    }
    if (pass == nullptr) {
        return CredStatus::PassTooShort;
    }
    if (pass_len < kPassMin) {
        return CredStatus::PassTooShort;
    }
    if (pass_len > kPassMax) {
        return CredStatus::PassTooLong;
    }
    for (std::size_t i = 0U; i < pass_len; ++i) {
        const unsigned char c = static_cast<unsigned char>(pass[i]);
        if ((c < 0x20U) || (c > 0x7EU)) {
            return CredStatus::PassInvalidChar;
        }
    }
    return CredStatus::Ok;
}

bool form_field(const char* body, std::size_t len, const char* key, char* out,
                std::size_t out_size, std::size_t& out_len) noexcept {
    out_len = 0U;
    if ((body == nullptr) || (key == nullptr) || (out == nullptr) || (out_size == 0U)) {
        return false;
    }
    std::size_t i = 0U;
    while (i < len) {
        // Pair [i, end) delimited by '&'.
        std::size_t end = i;
        while ((end < len) && (body[end] != '&')) {
            ++end;
        }
        std::size_t eq = i;
        while ((eq < end) && (body[eq] != '=')) {
            ++eq;
        }
        if ((eq < end) && key_equals(body + i, eq - i, key)) {
            std::size_t w = 0U;
            for (std::size_t r = eq + 1U; r < end; ++r) {
                char c = body[r];
                if (c == '+') {
                    c = ' ';
                } else if (c == '%') {
                    // Two hex digits INSIDE the field: r+1 and r+2 < end.
                    if ((r + 2U) >= end) {
                        out[0] = '\0';
                        return false;
                    }
                    const int32_t hi = hex_value(body[r + 1U]);
                    const int32_t lo = hex_value(body[r + 2U]);
                    if ((hi < 0) || (lo < 0)) {
                        out[0] = '\0';
                        return false;
                    }
                    c = static_cast<char>((hi << 4) | lo);
                    r += 2U;
                } else {
                    // literal character
                }
                if ((c == '\0') || ((w + 1U) >= out_size)) {
                    out[0] = '\0';
                    return false;  // injected NUL, or does not fit
                }
                out[w] = c;
                ++w;
            }
            out[w] = '\0';
            out_len = w;
            return true;
        }
        i = end + 1U;
    }
    out[0] = '\0';
    return false;
}

std::size_t json_escape(const char* in, std::size_t in_len, char* out,
                        std::size_t out_size) noexcept {
    if ((out == nullptr) || (out_size == 0U)) {
        return 0U;
    }
    std::size_t w = 0U;
    for (std::size_t i = 0U; i < in_len; ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        char seq[7] = {};
        std::size_t n = 0U;
        if ((c == '"') || (c == '\\')) {
            seq[0] = '\\';
            seq[1] = static_cast<char>(c);
            n = 2U;
        } else if (c < 0x20U) {
            (void)std::snprintf(seq, sizeof(seq), "\\u%04x", static_cast<unsigned>(c));
            n = 6U;
        } else {
            seq[0] = static_cast<char>(c);
            n = 1U;
        }
        if ((w + n) >= out_size) {
            out[0] = '\0';
            return 0U;
        }
        for (std::size_t k = 0U; k < n; ++k) {
            out[w + k] = seq[k];
        }
        w += n;
    }
    out[w] = '\0';
    return w;
}

std::size_t model_json(char* out, std::size_t out_size) noexcept {
    if ((out == nullptr) || (out_size == 0U)) {
        return 0U;
    }
    const int n = std::snprintf(out, out_size, "{\"ssid_max\":%u,\"pass_min\":%u,\"pass_max\":%u}",
                                static_cast<unsigned>(kSsidMax), static_cast<unsigned>(kPassMin),
                                static_cast<unsigned>(kPassMax));
    if ((n < 0) || (static_cast<std::size_t>(n) >= out_size)) {
        out[0] = '\0';
        return 0U;
    }
    return static_cast<std::size_t>(n);
}

void derive_ap_password(const std::array<uint8_t, 6>& mac,
                        std::array<char, kApPassLen + 1U>& out) noexcept {
    // FNV-1a 64 over a fixed prefix then the MAC: fast, deterministic.
    constexpr uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr uint64_t kPrime = 0x100000001b3ULL;
    constexpr char kPrefix[] = "matrix_clock-AP";
    uint64_t h = kOffset;
    for (const char c : kPrefix) {
        h = (h ^ static_cast<uint8_t>(c)) * kPrime;
    }
    for (const uint8_t b : mac) {
        h = (h ^ b) * kPrime;
    }
    // Digits and capitals only: the panel's 5x7 font has no lower case, and a
    // password read off the panel must be typed exactly. No O/I: ambiguous
    // with 0/1 at that size.
    constexpr char kAlphabet[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    constexpr uint64_t kBase = 34U;
    for (std::size_t i = 0U; i < kApPassLen; ++i) {
        out[i] = kAlphabet[h % kBase];
        h /= kBase;
        if (h == 0U) {
            h = (kOffset ^ static_cast<uint64_t>(i)) * kPrime;  // never out of digits
        }
    }
    out[kApPassLen] = '\0';
}

}  // namespace network
