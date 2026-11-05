#pragma once
/**
 * @file notification.hpp
 * @brief Inbound notification: model, bounds, and the form decoder shared by
 *        every entry point (spec-home-assistant.md section 4, spec-alertes.md).
 *
 * A field out of bounds is replaced by its default and counted; a missing or
 * invalid `cle` rejects the whole message (nothing can be deduplicated or
 * cleared without a key). No acknowledgement exists anywhere (EX-46): a held
 * notification only goes away through `effacer` from its source.
 */

#include <cstddef>
#include <cstdint>

namespace alerts {

inline constexpr std::size_t kKeyMax = 24U;
inline constexpr std::size_t kTitleMax = 16U;
inline constexpr std::size_t kTextMax = 128U;
inline constexpr uint8_t kPriorityMax = 3U;
inline constexpr uint8_t kPriorityDefault = 1U;
inline constexpr uint16_t kDurationMin = 1U;
inline constexpr uint16_t kDurationMax = 3600U;
inline constexpr uint16_t kDurationDefault = 20U;
inline constexpr uint16_t kRepeatMin = 10U;
inline constexpr uint16_t kRepeatMax = 3600U;
inline constexpr uint16_t kRepeatDefault = 300U;
/// Longest decoded field the decoder accepts before truncating (bounded body).
inline constexpr std::size_t kScratchMax = 1024U;

/// One inbound notification, as decoded (text still UTF-8, not transliterated).
struct Notification {
    char key[kKeyMax + 1U];
    char title[kTitleMax + 1U];
    char text[kTextMax + 1U];
    uint8_t priority;      ///< 0..3
    uint16_t duration_s;   ///< Duration of one pass.
    uint16_t repeat_s;     ///< Interval between passes while held.
    bool persistent;       ///< Held until `clear` from the source.
    bool clear;            ///< Remove the notification of this key.
    uint8_t defaults_used; ///< Fields that were out of bounds and replaced (diagnostic).
};

/// Outcome of decoding.
enum class DecodeStatus : uint8_t {
    Ok = 0U,
    KeyMissing,   ///< No `cle` field.
    KeyInvalid,   ///< `cle` empty, too long, or outside [a-z0-9_].
    BodyInvalid,  ///< Malformed form (bad percent-encoding in a present field).
};

/// @return true if @p key is 1..kKeyMax characters of [a-z0-9_].
bool is_valid_key(const char* key) noexcept;

/**
 * @brief Decodes an `application/x-www-form-urlencoded` body into a Notification.
 * @param body Body (not NUL-terminated); fields `cle`, `titre`, `texte`,
 *             `priorite`, `duree_s`, `rappel_s`, `persistante`, `effacer`.
 * @param len  Body length.
 * @param out  Filled on Ok (defaults applied; `defaults_used` counts replacements).
 */
DecodeStatus decode_form(const char* body, std::size_t len, Notification& out) noexcept;

}  // namespace alerts
