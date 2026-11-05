#pragma once
/**
 * @file json_reader.hpp
 * @brief Minimal JSON reader: value by key path, fixed-point numbers, strings.
 *
 * Taken from ThermoSync (`energie/json_path`) and translated. Meant for
 * **untrusted input** from the network: one pass per level, **no allocation,
 * no recursion** (depth tracked by a counter), everything bounded by the given
 * length. No tree is built: the wanted value is delimited in place.
 *
 * @note Never name a project component `json`: ESP-IDF has one (cJSON) that a
 *       homonym would silently replace. The reader lives inside `homeassistant`.
 */

#include <cstddef>
#include <cstdint>

namespace homeassistant {

/// Non-owning view on a JSON fragment (no terminating zero guaranteed).
struct JsonSpan {
    const char* data;
    std::size_t len;
};

/// Maximum nesting followed while delimiting a value (deeper documents are rejected).
inline constexpr uint32_t kJsonDepthMax = 16U;

/**
 * @brief Delimits the value reached by a chain of object keys.
 * @param json  JSON buffer.
 * @param len   Useful length.
 * @param path  Successive keys, zero-terminated (e.g. {"event","result"}).
 * @param depth Number of keys.
 * @param out   Receives the value (object, array, string with its quotes, literal).
 * @return false if a key is missing, a level is not an object, the nesting is
 *         deeper than kJsonDepthMax, or the JSON is malformed along the path.
 * @note Only the walked path is checked; the rest of the document is only
 *       delimited. The first occurrence of a key wins.
 */
bool json_find(const char* json, std::size_t len, const char* const* path, std::size_t depth,
               JsonSpan& out) noexcept;

/// @return true if the value is the literal `null`.
bool json_is_null(const JsonSpan& v) noexcept;

/// @return true if the value is a string (starts with a quote).
bool json_is_string(const JsonSpan& v) noexcept;

/**
 * @brief Converts a JSON number to a fixed-point integer, rounded to nearest.
 * @param v        Literal value (e.g. `366.10000000000002`, `-1e-05`, `0.0`).
 * @param decimals Decimals kept (0 for W, 2 for centi-degrees).
 * @param out      Receives `round(value × 10^decimals)`.
 * @return false if it is not a valid JSON number (`null`, string, `+1`, `.5`,
 *         `1.`) or if the result does not fit an int32_t.
 */
bool json_to_fixed(const JsonSpan& v, uint8_t decimals, int32_t& out) noexcept;

/**
 * @brief Copies a JSON string into @p out, unescaping `\"`, `\\`, `\/`, `\n`, `\t`
 *        (other escapes, including `\uXXXX`, become `?`), truncating to fit.
 * @param v    String value with its quotes.
 * @param out  Destination, always zero-terminated when size > 0.
 * @param size Destination size.
 * @return false if @p v is not a string; the number of characters written is
 *         not returned on purpose: the caller treats the result as text.
 */
bool json_string(const JsonSpan& v, char* out, std::size_t size) noexcept;

/// @return true if the literal is `true` (`false` and anything else give false).
bool json_is_true(const JsonSpan& v) noexcept;

}  // namespace homeassistant
