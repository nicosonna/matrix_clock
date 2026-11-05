#pragma once
/**
 * @file captive_dns.hpp
 * @brief Captive portal DNS answer: every name resolves to the board.
 *
 * Like the charger (`DNSServer` with `NoError`): every A query gets the access
 * point's address; other types get an empty but **error-free** answer, which
 * is what makes phones and computers (iOS, Android, Windows) open the portal
 * window.
 *
 * The query comes from any device within radio range: **untrusted input**.
 * Decoding is bounded, allocation-free, non-recursive, and rejects malformed
 * packets (compression in the question, inconsistent lengths, more than one
 * question).
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace network {

/// Largest DNS message over UDP without EDNS (RFC 1035).
inline constexpr std::size_t kDnsMaxMessage = 512U;

/**
 * @brief Builds the captive answer to a DNS query.
 * @param query    Received query.
 * @param len      Query length.
 * @param ip       IPv4 to return (e.g. 192.168.4.1), network byte order.
 * @param out      Answer buffer.
 * @param out_size Buffer size (at least the query + 16 bytes).
 * @return Answer length, or 0 when nothing should be sent (malformed packet,
 *         a response rather than a query, non-standard opcode).
 */
std::size_t build_captive_response(const uint8_t* query, std::size_t len,
                                   const std::array<uint8_t, 4>& ip, uint8_t* out,
                                   std::size_t out_size) noexcept;

}  // namespace network
