/**
 * @file reassembler.cpp
 * @brief WebSocket frame reassembly.
 */
#include "homeassistant/reassembler.hpp"

#include <cstring>

namespace homeassistant {

Reassembler::Reassembler() noexcept : buffer_{}, length_(0U), expected_(0U), dropping_(false), dropped_(0U) {}

void Reassembler::reset() noexcept {
    length_ = 0U;
    expected_ = 0U;
    dropping_ = false;
    buffer_[0] = '\0';
}

bool Reassembler::feed(const char* data, std::size_t len, std::size_t offset, std::size_t total) noexcept {
    if (offset == 0U) {
        // New message.
        length_ = 0U;
        expected_ = total;
        dropping_ = (total > kMessageMax) || (total == 0U);
        if (dropping_) {
            ++dropped_;
        }
    } else if (dropping_ || (offset != length_) || (total != expected_)) {
        // Out of order, or continuation of a dropped message: ignore it all.
        if (!dropping_) {
            ++dropped_;
            dropping_ = true;
        }
        return false;
    } else {
        // in sequence
    }
    if (dropping_) {
        return false;
    }
    if ((data == nullptr) || ((length_ + len) > expected_)) {
        dropping_ = true;
        ++dropped_;
        return false;
    }
    std::memcpy(buffer_ + length_, data, len);
    length_ += len;
    buffer_[length_] = '\0';
    if (length_ == expected_) {
        expected_ = 0U;
        return true;
    }
    return false;
}

}  // namespace homeassistant
