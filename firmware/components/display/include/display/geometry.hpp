#pragma once
/**
 * @file geometry.hpp
 * @brief Panel geometry: one WS2812B matrix of 8 rows by 32 columns, 256 LEDs.
 *
 * Logical coordinates: x grows to the right (0..31), y grows downwards (0..7),
 * origin at the top-left corner *as seen by the viewer*. The mapping to the
 * physical LED order is a setting (panel_map.hpp), never assumed.
 */

#include <cstdint>

namespace display {

/// Columns.
inline constexpr uint8_t kWidth = 32U;
/// Rows.
inline constexpr uint8_t kHeight = 8U;
/// LEDs on the panel.
inline constexpr uint16_t kLedCount = static_cast<uint16_t>(kWidth) * kHeight;

/// @return true if (x, y) is on the panel.
constexpr bool in_bounds(uint8_t x, uint8_t y) noexcept {
    return (x < kWidth) && (y < kHeight);
}

}  // namespace display
