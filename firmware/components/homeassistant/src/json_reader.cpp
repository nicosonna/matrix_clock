/**
 * @file json_reader.cpp
 * @brief Minimal JSON reader (see json_reader.hpp).
 */
#include "homeassistant/json_reader.hpp"

namespace homeassistant {

namespace {

constexpr std::size_t kNpos = static_cast<std::size_t>(-1);
constexpr uint32_t kMaxSignificant = 18U;
constexpr int32_t kMaxExponent = 40;

bool is_ws(char c) noexcept {
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r');
}

std::size_t skip_ws(const char* s, std::size_t i, std::size_t len) noexcept {
    while ((i < len) && is_ws(s[i])) {
        ++i;
    }
    return i;
}

/// End of a string starting with '"' at `i`: index after the closing quote, or kNpos.
std::size_t string_end(const char* s, std::size_t i, std::size_t len) noexcept {
    if ((i >= len) || (s[i] != '"')) {
        return kNpos;
    }
    ++i;
    while (i < len) {
        if (s[i] == '\\') {
            i += 2U;
            continue;
        }
        if (s[i] == '"') {
            return i + 1U;
        }
        ++i;
    }
    return kNpos;
}

/// End of the value starting at `i` (after blanks): index just after, or kNpos.
std::size_t value_end(const char* s, std::size_t i, std::size_t len) noexcept {
    if (i >= len) {
        return kNpos;
    }
    if (s[i] == '"') {
        return string_end(s, i, len);
    }
    if ((s[i] == '{') || (s[i] == '[')) {
        uint32_t depth = 0U;
        while (i < len) {
            const char c = s[i];
            if (c == '"') {
                const std::size_t e = string_end(s, i, len);
                if (e == kNpos) {
                    return kNpos;
                }
                i = e;
                continue;
            }
            if ((c == '{') || (c == '[')) {
                ++depth;
                if (depth > kJsonDepthMax) {
                    return kNpos;  // too deep: rejected, not explored
                }
            } else if ((c == '}') || (c == ']')) {
                --depth;
                if (depth == 0U) {
                    return i + 1U;
                }
            } else {
                // other character
            }
            ++i;
        }
        return kNpos;  // unclosed object or array
    }
    // Literal: up to a delimiter or a blank.
    const std::size_t start = i;
    while ((i < len) && (s[i] != ',') && (s[i] != '}') && (s[i] != ']') && !is_ws(s[i])) {
        ++i;
    }
    return (i > start) ? i : kNpos;
}

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

bool json_find(const char* json, std::size_t len, const char* const* path, std::size_t depth,
               JsonSpan& out) noexcept {
    if ((json == nullptr) || ((path == nullptr) && (depth > 0U))) {
        return false;
    }
    std::size_t start = skip_ws(json, 0U, len);
    std::size_t end = value_end(json, start, len);
    if (end == kNpos) {
        return false;
    }
    for (std::size_t level = 0U; level < depth; ++level) {
        if (json[start] != '{') {
            return false;
        }
        std::size_t i = start + 1U;
        bool found = false;
        while (!found) {
            i = skip_ws(json, i, end);
            if ((i >= end) || (json[i] == '}')) {
                return false;  // key missing at this level
            }
            const std::size_t key_end = string_end(json, i, end);
            if (key_end == kNpos) {
                return false;
            }
            const char* key = json + i + 1U;
            const std::size_t key_len = key_end - i - 2U;
            i = skip_ws(json, key_end, end);
            if ((i >= end) || (json[i] != ':')) {
                return false;
            }
            const std::size_t vstart = skip_ws(json, i + 1U, end);
            const std::size_t vend = value_end(json, vstart, end);
            if (vend == kNpos) {
                return false;
            }
            if (key_equals(key, key_len, path[level])) {
                start = vstart;
                end = vend;
                found = true;
            } else {
                i = skip_ws(json, vend, end);
                if ((i < end) && (json[i] == ',')) {
                    ++i;
                } else {
                    return false;  // end of object without the key, or unexpected separator
                }
            }
        }
    }
    out = JsonSpan{json + start, end - start};
    return true;
}

bool json_is_null(const JsonSpan& v) noexcept {
    return (v.data != nullptr) && key_equals(v.data, v.len, "null");
}

bool json_is_true(const JsonSpan& v) noexcept {
    return (v.data != nullptr) && key_equals(v.data, v.len, "true");
}

bool json_is_string(const JsonSpan& v) noexcept {
    return (v.data != nullptr) && (v.len >= 2U) && (v.data[0] == '"');
}

bool json_string(const JsonSpan& v, char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (size == 0U)) {
        return false;
    }
    out[0] = '\0';
    if (!json_is_string(v) || (v.data[v.len - 1U] != '"')) {
        return false;
    }
    std::size_t w = 0U;
    for (std::size_t i = 1U; (i + 1U) < v.len; ++i) {
        char c = v.data[i];
        if (c == '\\') {
            ++i;
            if ((i + 1U) >= v.len) {
                break;
            }
            const char e = v.data[i];
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'u':
                    i += 4U;  // \uXXXX: not rendered (no font for it anyway)
                    c = '?';
                    break;
                default: c = '?'; break;
            }
        }
        if ((w + 1U) >= size) {
            break;  // truncated
        }
        out[w++] = c;
    }
    out[w] = '\0';
    return true;
}

bool json_to_fixed(const JsonSpan& v, uint8_t decimals, int32_t& out) noexcept {
    if ((v.data == nullptr) || (v.len == 0U)) {
        return false;
    }
    const char* s = v.data;
    const std::size_t n = v.len;
    std::size_t i = 0U;
    const bool negative = (s[0] == '-');
    if (negative) {
        ++i;
    }
    // Integer part: at least one digit; no leading zero followed by a digit.
    if ((i >= n) || (s[i] < '0') || (s[i] > '9')) {
        return false;
    }
    if ((s[i] == '0') && ((i + 1U) < n) && (s[i + 1U] >= '0') && (s[i + 1U] <= '9')) {
        return false;
    }
    uint64_t mant = 0U;
    uint32_t sig = 0U;
    int32_t exp10 = 0;
    while ((i < n) && (s[i] >= '0') && (s[i] <= '9')) {
        if (sig < kMaxSignificant) {
            mant = (mant * 10U) + static_cast<uint64_t>(s[i] - '0');
            if (mant != 0U) {
                ++sig;
            }
        } else {
            ++exp10;  // integer digit beyond the precision: shift
        }
        ++i;
    }
    if ((i < n) && (s[i] == '.')) {
        ++i;
        if ((i >= n) || (s[i] < '0') || (s[i] > '9')) {
            return false;  // "1."
        }
        while ((i < n) && (s[i] >= '0') && (s[i] <= '9')) {
            if (sig < kMaxSignificant) {
                mant = (mant * 10U) + static_cast<uint64_t>(s[i] - '0');
                if (mant != 0U) {
                    ++sig;
                }
                --exp10;
            }
            ++i;
        }
    }
    if ((i < n) && ((s[i] == 'e') || (s[i] == 'E'))) {
        ++i;
        bool exp_neg = false;
        if ((i < n) && ((s[i] == '+') || (s[i] == '-'))) {
            exp_neg = (s[i] == '-');
            ++i;
        }
        if ((i >= n) || (s[i] < '0') || (s[i] > '9')) {
            return false;
        }
        int32_t e = 0;
        while ((i < n) && (s[i] >= '0') && (s[i] <= '9')) {
            if (e < 1000) {
                e = (e * 10) + (s[i] - '0');
            }
            ++i;
        }
        exp10 += exp_neg ? -e : e;
    }
    if (i != n) {
        return false;  // non-numeric remainder
    }

    int32_t shift = exp10 + static_cast<int32_t>(decimals);
    if (mant == 0U) {
        out = 0;
        return true;
    }
    if (shift > kMaxExponent) {
        return false;
    }
    constexpr uint64_t kLimit = 0x7FFFFFFFULL;
    if (shift >= 0) {
        while (shift > 0) {
            if (mant > (kLimit / 10U)) {
                return false;
            }
            mant *= 10U;
            --shift;
        }
    } else {
        if (shift < -(static_cast<int32_t>(kMaxSignificant) + 1)) {
            out = 0;  // smaller than half of the target unit
            return true;
        }
        uint64_t div = 1U;
        for (int32_t k = shift; k < 0; ++k) {
            div *= 10U;
        }
        mant = (mant + (div / 2U)) / div;  // round to nearest
    }
    if (mant > kLimit) {
        return false;
    }
    const int32_t value = static_cast<int32_t>(mant);
    out = negative ? -value : value;
    return true;
}

}  // namespace homeassistant
