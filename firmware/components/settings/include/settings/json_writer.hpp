#pragma once
/**
 * @file json_writer.hpp
 * @brief Bounded JSON writer: no allocation, never an invalid document.
 *
 * Writes into a caller-supplied buffer. Once the buffer is too small the
 * writer stops and ok() is false; the caller then knows the document is not
 * usable (spec-serveur-web.md 6: a response that does not fit is reported as
 * incomplete, never truncated into invalid JSON).
 */

#include <cstddef>
#include <cstdint>

namespace settings {

class JsonWriter {
public:
    JsonWriter(char* buffer, std::size_t size) noexcept;

    bool begin_object() noexcept;
    bool end_object() noexcept;
    bool begin_array() noexcept;
    bool end_array() noexcept;
    /// Writes `"key":` (comma handled).
    bool key(const char* name) noexcept;
    /// Writes a string value, escaped.
    bool str(const char* value) noexcept;
    bool integer(int64_t value) noexcept;
    bool boolean(bool value) noexcept;
    /// Writes raw text as a value (already valid JSON).
    bool raw(const char* json) noexcept;

    /// Convenience: key + value.
    bool field(const char* name, const char* value) noexcept { return key(name) && str(value); }
    bool field(const char* name, int64_t value) noexcept { return key(name) && integer(value); }
    bool field(const char* name, bool value) noexcept { return key(name) && boolean(value); }

    /// true while everything fitted.
    bool ok() const noexcept { return ok_; }
    /// Characters written (NUL excluded); 0 when !ok().
    std::size_t length() const noexcept { return ok_ ? len_ : 0U; }

private:
    bool put(char c) noexcept;
    bool puts(const char* s) noexcept;
    bool before_value() noexcept;

    char* buf_;
    std::size_t size_;
    std::size_t len_;
    bool ok_;
    uint8_t depth_;
    bool need_comma_[8];
    bool after_key_;
};

}  // namespace settings
