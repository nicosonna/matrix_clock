#pragma once
/**
 * @file reassembler.hpp
 * @brief Reassembles fragmented WebSocket text frames into a static buffer.
 *
 * `esp_websocket_client` delivers `payload_offset`, `payload_len`, `data_len`.
 * A message longer than the buffer is **dropped and counted**, never truncated
 * and interpreted (a truncated JSON can decode into something else).
 */

#include <cstddef>
#include <cstdint>

namespace homeassistant {

inline constexpr std::size_t kMessageMax = 2048U;

class Reassembler {
public:
    Reassembler() noexcept;

    /**
     * @brief Feeds one fragment.
     * @param data   Fragment bytes.
     * @param len    Fragment length.
     * @param offset Offset of the fragment in the message.
     * @param total  Total message length announced by the transport.
     * @return true when the message is complete and available through message().
     */
    bool feed(const char* data, std::size_t len, std::size_t offset, std::size_t total) noexcept;

    /// Complete message (zero-terminated), valid after feed() returned true until the next feed().
    const char* message() const noexcept { return buffer_; }
    std::size_t length() const noexcept { return length_; }

    /// Messages dropped because they exceeded kMessageMax or arrived out of order.
    uint32_t dropped() const noexcept { return dropped_; }

    /// Forgets a partial message (on disconnection).
    void reset() noexcept;

private:
    char buffer_[kMessageMax + 1U];
    std::size_t length_;
    std::size_t expected_;
    bool dropping_;
    uint32_t dropped_;
};

}  // namespace homeassistant
