#pragma once
/**
 * @file color.hpp
 * @brief 8-bit RGB colour, the only colour type of the pure display logic.
 */

#include <cstdint>

namespace display {

/// Logical colour (before gamma). The physical byte order (GRB for WS2812B)
/// is the driver's business, not the logic's.
struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr bool operator==(Rgb a, Rgb b) noexcept {
    return (a.r == b.r) && (a.g == b.g) && (a.b == b.b);
}

constexpr bool operator!=(Rgb a, Rgb b) noexcept {
    return !(a == b);
}

inline constexpr Rgb kBlack{0U, 0U, 0U};
inline constexpr Rgb kWhite{255U, 255U, 255U};

/**
 * @brief Scales a colour by a global brightness (0..255), channel by channel.
 * @param c          Colour.
 * @param brightness 0 = off, 255 = unchanged.
 * @return Scaled colour (floor, so 255 * 254 / 255 = 254).
 */
constexpr Rgb scale(Rgb c, uint8_t brightness) noexcept {
    return Rgb{static_cast<uint8_t>((static_cast<uint16_t>(c.r) * brightness) / 255U),
               static_cast<uint8_t>((static_cast<uint16_t>(c.g) * brightness) / 255U),
               static_cast<uint8_t>((static_cast<uint16_t>(c.b) * brightness) / 255U)};
}

}  // namespace display
