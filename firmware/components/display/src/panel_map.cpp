/**
 * @file panel_map.cpp
 * @brief Logical to physical LED index.
 */

#include "display/panel_map.hpp"

namespace display {

namespace {

/// Index inside one block of `width` columns by kHeight rows, origin already applied.
uint16_t block_index(FirstAxis axis, bool serpentine, uint8_t width, uint8_t col,
                     uint8_t row) noexcept {
    if (axis == FirstAxis::Column) {
        // The strip runs down column 0, then (serpentine) up column 1, and so on.
        const bool reversed = serpentine && ((col % 2U) == 1U);
        const uint8_t r = reversed ? static_cast<uint8_t>(kHeight - 1U - row) : row;
        return static_cast<uint16_t>(static_cast<uint16_t>(col) * kHeight + r);
    }
    // Row-first: the strip runs along row 0, then (serpentine) back along row 1.
    const bool reversed = serpentine && ((row % 2U) == 1U);
    const uint8_t c = reversed ? static_cast<uint8_t>(width - 1U - col) : col;
    return static_cast<uint16_t>(static_cast<uint16_t>(row) * width + c);
}

}  // namespace

bool led_index(const PanelMapConfig& cfg, uint8_t x, uint8_t y, uint16_t& out) noexcept {
    if (!in_bounds(x, y) || !is_valid(cfg)) {
        return false;
    }
    // 1. Bring the viewer's coordinates into the strip's frame (LED 0 at (0, 0)).
    const bool flip_x = (cfg.origin == Origin::TopRight) || (cfg.origin == Origin::BottomRight);
    const bool flip_y = (cfg.origin == Origin::BottomLeft) || (cfg.origin == Origin::BottomRight);
    const uint8_t sx = flip_x ? static_cast<uint8_t>(kWidth - 1U - x) : x;
    const uint8_t sy = flip_y ? static_cast<uint8_t>(kHeight - 1U - y) : y;

    // 2. Whole panel, or chained modules of module_width columns.
    if (cfg.module_width == 0U) {
        out = block_index(cfg.first_axis, cfg.serpentine, kWidth, sx, sy);
        return true;
    }
    const uint8_t module = static_cast<uint8_t>(sx / cfg.module_width);
    const uint8_t local_x = static_cast<uint8_t>(sx % cfg.module_width);
    const uint16_t leds_per_module = static_cast<uint16_t>(static_cast<uint16_t>(cfg.module_width) * kHeight);
    out = static_cast<uint16_t>(static_cast<uint16_t>(module) * leds_per_module +
                                block_index(cfg.first_axis, cfg.serpentine, cfg.module_width, local_x, sy));
    return true;
}

}  // namespace display
