/**
 * @file json_writer.cpp
 * @brief Bounded JSON writer.
 */
#include "settings/json_writer.hpp"

#include <cstdio>

#include "network/credentials.hpp"

namespace settings {

JsonWriter::JsonWriter(char* buffer, std::size_t size) noexcept
    : buf_(buffer), size_(size), len_(0U), ok_((buffer != nullptr) && (size > 0U)), depth_(0U), need_comma_{},
      after_key_(false) {
    if (ok_) {
        buf_[0] = '\0';
    }
}

bool JsonWriter::put(char c) noexcept {
    if (!ok_ || ((len_ + 1U) >= size_)) {
        ok_ = false;
        return false;
    }
    buf_[len_++] = c;
    buf_[len_] = '\0';
    return true;
}

bool JsonWriter::puts(const char* s) noexcept {
    if (s == nullptr) {
        ok_ = false;
        return false;
    }
    for (; *s != '\0'; ++s) {
        if (!put(*s)) {
            return false;
        }
    }
    return true;
}

bool JsonWriter::before_value() noexcept {
    if (after_key_) {
        after_key_ = false;
        return true;
    }
    if ((depth_ > 0U) && need_comma_[depth_ - 1U]) {
        if (!put(',')) {
            return false;
        }
    }
    if (depth_ > 0U) {
        need_comma_[depth_ - 1U] = true;
    }
    return true;
}

bool JsonWriter::begin_object() noexcept {
    if (!before_value() || (depth_ >= 8U)) {
        ok_ = false;
        return false;
    }
    need_comma_[depth_++] = false;
    return put('{');
}

bool JsonWriter::end_object() noexcept {
    if (depth_ == 0U) {
        ok_ = false;
        return false;
    }
    --depth_;
    return put('}');
}

bool JsonWriter::begin_array() noexcept {
    if (!before_value() || (depth_ >= 8U)) {
        ok_ = false;
        return false;
    }
    need_comma_[depth_++] = false;
    return put('[');
}

bool JsonWriter::end_array() noexcept {
    if (depth_ == 0U) {
        ok_ = false;
        return false;
    }
    --depth_;
    return put(']');
}

bool JsonWriter::key(const char* name) noexcept {
    if (!before_value()) {
        return false;
    }
    if (!put('"') || !puts(name) || !put('"') || !put(':')) {
        return false;
    }
    after_key_ = true;
    return true;
}

bool JsonWriter::str(const char* value) noexcept {
    if (value == nullptr) {
        ok_ = false;
        return false;
    }
    if (!before_value() || !put('"')) {
        return false;
    }
    std::size_t n = 0U;
    while (value[n] != '\0') {
        ++n;
    }
    // Escape into the remaining room; json_escape fails if it does not fit.
    const std::size_t room = (size_ > len_) ? (size_ - len_) : 0U;
    const std::size_t w = network::json_escape(value, n, buf_ + len_, room);
    if ((w == 0U) && (n != 0U)) {
        ok_ = false;
        return false;
    }
    len_ += w;
    return put('"');
}

bool JsonWriter::integer(int64_t value) noexcept {
    if (!before_value()) {
        return false;
    }
    char tmp[24];
    const int n = std::snprintf(tmp, sizeof(tmp), "%lld", static_cast<long long>(value));
    if (n <= 0) {
        ok_ = false;
        return false;
    }
    return puts(tmp);
}

bool JsonWriter::boolean(bool value) noexcept {
    return before_value() && puts(value ? "true" : "false");
}

bool JsonWriter::raw(const char* json) noexcept {
    return before_value() && puts(json);
}

}  // namespace settings
