#pragma once
/**
 * @file credentials.hpp
 * @brief Wi-Fi credentials: model, validation, portal form decoding.
 *
 * The data comes from an HTTP form sent by any device within radio range:
 * **untrusted input**. Everything is bounded, validated and decoded without
 * allocation.
 *
 * The **model** (length bounds) is the single source of truth: the firmware
 * validates with it and the portal page reads it from `/api/wifi/modele`
 * (`model_json`) — the wizard rule of CLAUDE.md.
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace network {

/// Maximum SSID length (bytes), 802.11.
inline constexpr std::size_t kSsidMax = 32U;
/// Minimum WPA2 passphrase length.
inline constexpr std::size_t kPassMin = 8U;
/// Maximum WPA2 passphrase length (ASCII).
inline constexpr std::size_t kPassMax = 63U;
/// Length of the access point password derived from the MAC.
inline constexpr std::size_t kApPassLen = 10U;

/// Credentials validation result.
enum class CredStatus : uint8_t {
    Ok = 0U,
    SsidEmpty,        ///< Empty SSID.
    SsidTooLong,      ///< SSID > kSsidMax bytes.
    PassTooShort,     ///< Non-empty password < kPassMin.
    PassTooLong,      ///< Password > kPassMax.
    PassInvalidChar,  ///< Character outside printable ASCII (0x20-0x7E).
};

/**
 * @brief Validates an SSID / password pair.
 * @param ssid     SSID (not NUL-terminated).
 * @param ssid_len SSID length.
 * @param pass     Password (not NUL-terminated); length 0 = open network.
 * @param pass_len Password length.
 * @return CredStatus::Ok or the first defect.
 */
CredStatus validate(const char* ssid, std::size_t ssid_len, const char* pass,
                    std::size_t pass_len) noexcept;

/**
 * @brief Extracts and decodes one field of an `application/x-www-form-urlencoded` body.
 * @param body     Received body (not NUL-terminated).
 * @param len      Body length.
 * @param key      Field name, NUL-terminated (e.g. "ssid").
 * @param out      Output buffer; always NUL-terminated on success.
 * @param out_size Buffer size.
 * @param out_len  Receives the decoded length, NUL excluded.
 * @return true if the field is present, well encoded and fits in @p out.
 * @note Decodes `+` as space and `%XX`. A malformed `%` or a decoded NUL byte
 *       fails the field (rejected rather than repaired).
 */
bool form_field(const char* body, std::size_t len, const char* key, char* out,
                std::size_t out_size, std::size_t& out_len) noexcept;

/**
 * @brief Escapes a string for insertion in JSON (`"`, `\`, control characters).
 * @return Length written, or 0 if the buffer is too small (@p out emptied).
 */
std::size_t json_escape(const char* in, std::size_t in_len, char* out,
                        std::size_t out_size) noexcept;

/**
 * @brief Credentials model as JSON, for the portal page.
 * @return Length written, or 0 if the buffer is too small.
 */
std::size_t model_json(char* out, std::size_t out_size) noexcept;

/**
 * @brief Access point password derived from the MAC address, like the charger.
 * @param mac Access point MAC address.
 * @param out Receives kApPassLen characters among digits and capitals without
 *            O and I (readable on the 5x7 font, typed exactly), plus the NUL.
 * @note Deterministic and specific to each board, but **guessable** by whoever
 *       knows the algorithm and reads the BSSID: acceptable for a commissioning
 *       portal, to be replaced by a setting (known debt D1).
 */
void derive_ap_password(const std::array<uint8_t, 6>& mac,
                        std::array<char, kApPassLen + 1U>& out) noexcept;

}  // namespace network
