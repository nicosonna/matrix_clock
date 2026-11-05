#pragma once
/**
 * @file values.hpp
 * @brief Tracked Home Assistant values: configuration, template building, decoding.
 *
 * A value is a `render_template` subscription. Its result arrives as text or a
 * JSON number and is decoded by type into an integer fixed-point number, a
 * boolean or a short text. Anything the sensor cannot vouch for is **invalid**,
 * never 0 (EX-32, EX-44).
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include "homeassistant/json_reader.hpp"

namespace homeassistant {

inline constexpr std::size_t kValuesMax = 8U;
inline constexpr std::size_t kNameMax = 16U;
inline constexpr std::size_t kEntityMax = 64U;
inline constexpr std::size_t kAttributeMax = 32U;
inline constexpr std::size_t kTemplateMax = 128U;
inline constexpr std::size_t kUnitMax = 8U;
inline constexpr std::size_t kTextMax = 32U;        ///< Decoded text result.
inline constexpr std::size_t kErrorMax = 96U;       ///< Template error kept for the page.
inline constexpr uint16_t kExpiryMinS = 30U;
inline constexpr uint16_t kExpiryMaxS = 3600U;
inline constexpr uint16_t kExpiryDefaultS = 300U;
inline constexpr uint8_t kDecimalsMax = 2U;
inline constexpr int32_t kNumberLimit = 2000000000;
/// Longest template the firmware emits (entity + attribute wrapped in `state_attr`), with escaping.
inline constexpr std::size_t kTemplateOutMax = 2U * kTemplateMax + 8U;

enum class ValueMode : uint8_t {
    Entity = 0U,   ///< `{{ states('<entity>') }}` or `{{ state_attr('<entity>','<attribute>') }}`.
    Template,      ///< Expert: the template as typed.
};

enum class ValueType : uint8_t {
    Number = 0U,
    Text,
    Boolean,
};

/// Icon of the Value scene (8x8 glyphs in `display`).
enum class ValueIcon : uint8_t {
    None = 0U,
    Thermometer,
    Drop,
    Bolt,
    Door,
    Wind,
    Sun,
    Home,
    Count,  ///< Not an icon.
};

/// One tracked value as configured on the page.
struct ValueConfig {
    bool enabled;
    char name[kNameMax + 1U];
    ValueMode mode;
    char entity[kEntityMax + 1U];
    char attribute[kAttributeMax + 1U];
    char tmpl[kTemplateMax + 1U];
    ValueType type;
    uint8_t decimals;        ///< Number: stored × 10^decimals.
    char unit[kUnitMax + 1U];
    ValueIcon icon;
    uint16_t expiry_s;       ///< Invalid after this long without link (see WsClient).
};

using ValueList = std::array<ValueConfig, kValuesMax>;

/// Live state of a tracked value.
struct ValueState {
    bool valid;
    int32_t number;          ///< When type == Number and valid.
    bool boolean;            ///< When type == Boolean and valid.
    char text[kTextMax + 1U];  ///< When type == Text and valid (raw result otherwise, for the page).
    uint32_t received_ms;    ///< Time of the last event.
    bool ever_received;
    char error[kErrorMax + 1U];  ///< Template error reported by Home Assistant ("" if none).
};

ValueConfig default_value_config() noexcept;
ValueList default_value_list() noexcept;

/// @return true if every enabled entry is well formed (entity charset, lengths, decimals, expiry).
bool is_valid(const ValueConfig& v) noexcept;
bool is_valid(const ValueList& list) noexcept;
/// `[a-z0-9_.]`, one dot at least, 1..kEntityMax.
bool is_valid_entity(const char* entity) noexcept;
/// ASCII printable, 1..kTemplateMax.
bool is_valid_template(const char* tmpl) noexcept;

/**
 * @brief Builds the template sent to Home Assistant, JSON-escaped (quotes, backslashes).
 * @return Length written, 0 if it does not fit.
 */
std::size_t build_template(const ValueConfig& v, char* out, std::size_t size) noexcept;

/**
 * @brief Decodes a render_template result into the value state.
 * @param type     Expected type.
 * @param decimals Number decimals.
 * @param result   The `event.result` JSON value (string with quotes, number, literal).
 * @param out      Updated: valid flag and the decoded field; `text` always holds the raw text (bounded).
 * @return The new validity.
 */
bool decode_result(ValueType type, uint8_t decimals, const JsonSpan& result, ValueState& out) noexcept;

/// Number from text: "3.4" with 1 decimal -> 34; "on"/"unknown"/"" -> false.
bool decode_number(const char* text, uint8_t decimals, int32_t& out) noexcept;
/// Boolean from text: on/true/home/open/yes -> true; off/false/not_home/closed/no -> false; else invalid.
bool decode_boolean(const char* text, bool& out) noexcept;
/// Text: invalid when empty, "unknown", "unavailable", "none".
bool decode_text(const char* text) noexcept;

const char* value_type_name(ValueType t) noexcept;
bool value_type_from_name(const char* s, ValueType& t) noexcept;
const char* value_mode_name(ValueMode m) noexcept;
bool value_mode_from_name(const char* s, ValueMode& m) noexcept;
const char* value_icon_name(ValueIcon i) noexcept;
bool value_icon_from_name(const char* s, ValueIcon& i) noexcept;

/**
 * @brief Formats a number for the panel: `-12.3` with the given decimals, then the unit.
 * @return Length written, 0 if it does not fit.
 */
std::size_t format_number(int32_t number, uint8_t decimals, const char* unit, char* out, std::size_t size) noexcept;

}  // namespace homeassistant
