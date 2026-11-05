/**
 * @file captive_dns.cpp
 * @brief Captive portal DNS answer (see captive_dns.hpp).
 */
#include "network/captive_dns.hpp"

namespace network {

namespace {

constexpr std::size_t kHeader = 12U;
constexpr uint8_t kFlagQr = 0x80U;       ///< Byte 2: "response" bit.
constexpr uint8_t kOpcodeMask = 0x78U;   ///< Byte 2: opcode (0 = standard query).
constexpr uint8_t kFlagRd = 0x01U;       ///< Byte 2: recursion desired.
constexpr uint8_t kFlagAa = 0x04U;       ///< Byte 2: authoritative answer.
constexpr uint8_t kFlagRa = 0x80U;       ///< Byte 3: recursion available.
constexpr uint16_t kTypeA = 1U;
constexpr uint16_t kClassIn = 1U;
constexpr std::size_t kLabelMax = 63U;
constexpr std::size_t kNameMax = 255U;
constexpr std::size_t kAnswerLen = 16U;  ///< Pointer(2)+type(2)+class(2)+TTL(4)+len(2)+IPv4(4).
constexpr uint32_t kTtlS = 60U;

uint16_t read16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

}  // namespace

std::size_t build_captive_response(const uint8_t* query, std::size_t len,
                                   const std::array<uint8_t, 4>& ip, uint8_t* out,
                                   std::size_t out_size) noexcept {
    if ((query == nullptr) || (out == nullptr) || (len < kHeader) || (len > kDnsMaxMessage)) {
        return 0U;
    }
    if (((query[2] & kFlagQr) != 0U) || ((query[2] & kOpcodeMask) != 0U)) {
        return 0U;  // a response, or a non-standard opcode: ignored
    }
    if (read16(query + 4) != 1U) {
        return 0U;  // exactly one question
    }

    // Question name: labels ended by 0, no compression.
    std::size_t i = kHeader;
    std::size_t name_len = 0U;
    while (true) {
        if (i >= len) {
            return 0U;
        }
        const std::size_t label = query[i];
        if (label == 0U) {
            ++i;
            break;
        }
        if (label > kLabelMax) {
            return 0U;  // compression (0xC0) or reserved length: refused in a question
        }
        name_len += label + 1U;
        if (name_len > kNameMax) {
            return 0U;
        }
        i += label + 1U;
    }
    if ((i + 4U) > len) {
        return 0U;  // type and class missing
    }
    const uint16_t qtype = read16(query + i);
    const uint16_t qclass = read16(query + i + 2U);
    const std::size_t question_end = i + 4U;
    const bool answer_a = (qtype == kTypeA) && (qclass == kClassIn);

    const std::size_t total = question_end + (answer_a ? kAnswerLen : 0U);
    if (total > out_size) {
        return 0U;
    }

    // Header + question copied (bytes beyond, e.g. EDNS, are ignored).
    for (std::size_t k = 0U; k < question_end; ++k) {
        out[k] = query[k];
    }
    out[2] = static_cast<uint8_t>(kFlagQr | kFlagAa | (query[2] & kFlagRd));
    out[3] = kFlagRa;  // RCODE 0 = NoError, always
    out[6] = 0U;
    out[7] = answer_a ? 1U : 0U;  // ANCOUNT
    out[8] = 0U;
    out[9] = 0U;                  // NSCOUNT
    out[10] = 0U;
    out[11] = 0U;                 // ARCOUNT

    if (answer_a) {
        std::size_t w = question_end;
        out[w++] = 0xC0U;  // pointer to the question name (offset 12)
        out[w++] = static_cast<uint8_t>(kHeader);
        out[w++] = 0U;
        out[w++] = static_cast<uint8_t>(kTypeA);
        out[w++] = 0U;
        out[w++] = static_cast<uint8_t>(kClassIn);
        out[w++] = static_cast<uint8_t>((kTtlS >> 24) & 0xFFU);
        out[w++] = static_cast<uint8_t>((kTtlS >> 16) & 0xFFU);
        out[w++] = static_cast<uint8_t>((kTtlS >> 8) & 0xFFU);
        out[w++] = static_cast<uint8_t>(kTtlS & 0xFFU);
        out[w++] = 0U;
        out[w++] = 4U;
        for (const uint8_t b : ip) {
            out[w++] = b;
        }
    }
    return total;
}

}  // namespace network
