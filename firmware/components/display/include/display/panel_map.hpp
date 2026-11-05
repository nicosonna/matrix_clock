#pragma once
/**
 * @file panel_map.hpp
 * @brief Logical (x, y) to physical LED index: a *setting*, never an assumption.
 *
 * Off-the-shelf 8x32 panels are wired differently (one serpentine strip, or
 * four 8x8 modules chained). The four parameters below cover the usual cases
 * and are determined by the visual mapping wizard (spec-affichage.md, 2.4).
 * The mapping is a bijection {0..31}x{0..7} -> {0..255}; the host tests prove
 * it for every combination.
 */

#include <cstdint>

#include "display/geometry.hpp"

namespace display {

/// Which corner of the viewer's panel holds LED 0 of the strip.
enum class Origin : uint8_t {
    TopLeft = 0U,
    TopRight,
    BottomLeft,
    BottomRight,
};

/// Along which axis the strip advances first from the origin.
enum class FirstAxis : uint8_t {
    Column = 0U,  ///< The strip runs down (or up) a column of 8, then moves to the next column.
    Row,          ///< The strip runs along a row of 32, then moves to the next row.
};

/// Mapping parameters.
struct PanelMapConfig {
    FirstAxis first_axis;   ///< See FirstAxis.
    bool serpentine;        ///< Every other line runs backwards.
    Origin origin;          ///< Corner of LED 0.
    uint8_t module_width;   ///< 0 = whole panel, 8 = four 8x8 modules chained left to right.
};

/// Module width accepted besides 0.
inline constexpr uint8_t kChainedModuleWidth = 8U;

/// @return The wiring of the horloge-v1 panel, **measured with the physical
/// chaser** (2026-11-05): LED 0 at the top-left corner, the strip runs down a
/// column of 8, up the next one (serpentine), LED 255 at the top-right corner.
/// The vendor drawing (rows, input bottom-right) did not describe this panel.
/// Mounting it another way round only changes `origin`; the wizard sorts it out.
constexpr PanelMapConfig default_panel_map() noexcept {
    return PanelMapConfig{FirstAxis::Column, true, Origin::TopLeft, 0U};
}

/// @return true if the configuration is one the mapping can honour.
constexpr bool is_valid(const PanelMapConfig& cfg) noexcept {
    return ((cfg.module_width == 0U) || (cfg.module_width == kChainedModuleWidth)) &&
           (static_cast<uint8_t>(cfg.origin) <= static_cast<uint8_t>(Origin::BottomRight)) &&
           (static_cast<uint8_t>(cfg.first_axis) <= static_cast<uint8_t>(FirstAxis::Row));
}

/**
 * @brief Physical LED index of a logical pixel.
 * @param cfg Mapping (must satisfy is_valid()).
 * @param x   Column 0..31.
 * @param y   Row 0..7.
 * @param out Receives the LED index 0..255.
 * @return false if (x, y) is off the panel or @p cfg is invalid; @p out untouched.
 */
bool led_index(const PanelMapConfig& cfg, uint8_t x, uint8_t y, uint16_t& out) noexcept;

}  // namespace display
