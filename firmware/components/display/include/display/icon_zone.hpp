#pragma once
/**
 * @file icon_zone.hpp
 * @brief The 6x6 icon zone next to a left-aligned clock: what it shows and the alert mark.
 *
 * Priority (spec-affichage.md section 4, spec-alertes.md section 3): a **held
 * alert** first — its mark stays between two passes, so a problem that is
 * still there is never hidden by a green air logo — then the air-quality
 * logo when the user chose the icon zone for it, else nothing and the clock
 * is centred. Decision of 2026-11-05: no 3 s alternation; one thing at a
 * time, the most important one.
 */

#include <cstdint>

#include "display/color.hpp"
#include "display/frame_buffer.hpp"

namespace display {

enum class IconZone : uint8_t {
    None = 0U,  ///< Nothing: the clock is centred.
    Alert,      ///< A held alert: the "!" mark in the priority's colour.
    Air,        ///< The 6x6 air-quality logo.
};

/// @param alert_held An alert is held in the queue.
/// @param air_logo   The air sensor is shown and the icon-zone logo is selected.
IconZone choose_icon_zone(bool alert_held, bool air_logo) noexcept;

/// Draws the 6x6 "!" mark with its top-left corner at (x, y).
void draw_alert_mark(FrameBuffer& fb, uint8_t x, uint8_t y, Rgb color) noexcept;

/// Lit pixels of the alert mark (for tests).
uint8_t alert_mark_pixels() noexcept;

}  // namespace display
