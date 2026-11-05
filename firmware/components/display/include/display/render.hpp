#pragma once
/**
 * @file render.hpp
 * @brief The rendering pipeline, in the order imposed by spec-affichage.md 2:
 *
 *   logical colour -> global brightness -> gamma -> current limit -> panel mapping
 *
 * Gamma comes after brightness so dimming stays perceptually smooth; the
 * limiter comes after gamma because current follows the real duty cycle.
 */

#include <array>
#include <cstdint>

#include "display/color.hpp"
#include "display/current_limit.hpp"
#include "display/frame_buffer.hpp"
#include "display/panel_map.hpp"

namespace display {

/// Frame in *physical* LED order, linear values, ready for the driver.
using PhysicalFrame = std::array<Rgb, kLedCount>;

/// Diagnostics of one rendered frame.
struct RenderStats {
    uint32_t estimated_ua;  ///< Current estimate before limiting.
    uint32_t applied_ua;    ///< Current estimate after limiting.
    uint16_t scale_q8;      ///< Limiter scale (256 = none).
    bool limited;           ///< true if the limiter acted.
};

/**
 * @brief Renders a logical frame into physical LED order.
 * @param fb         Logical frame.
 * @param brightness Global brightness 0..255.
 * @param budget_ma  Current budget (invalid -> default).
 * @param map        Panel mapping (invalid -> @p out cleared, returns false).
 * @param out        Physical frame.
 * @param stats      Diagnostics.
 * @return true on success; false if the mapping is invalid (out is all black).
 * @note No allocation, no floating point.
 */
bool render(const FrameBuffer& fb, uint8_t brightness, uint16_t budget_ma,
            const PanelMapConfig& map, PhysicalFrame& out, RenderStats& stats) noexcept;

}  // namespace display
