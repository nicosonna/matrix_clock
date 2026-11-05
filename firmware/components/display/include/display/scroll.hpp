#pragma once
/**
 * @file scroll.hpp
 * @brief Horizontal scrolling of a text, at an integer speed, without jitter.
 *
 * The position is kept in 1/256 pixel with an exact millisecond remainder, so
 * 20 px/s over one second is 20 pixels whatever the frame cadence. The text
 * enters from the right edge and leaves by the left edge; `loops` passes, or
 * forever when 0.
 */

#include <cstdint>

#include "display/text.hpp"

namespace display {

/// Scrolling speed bounds, pixels per second (spec-affichage.md, 4).
inline constexpr uint16_t kScrollSpeedMin = 5U;
inline constexpr uint16_t kScrollSpeedMax = 60U;
inline constexpr uint16_t kScrollSpeedDefault = 20U;

class Scroller {
public:
    Scroller() noexcept;

    /**
     * @brief Starts scrolling @p text (already transliterated), copied and truncated to kTextMax.
     * @param speed_px_s Clamped to [kScrollSpeedMin, kScrollSpeedMax].
     * @param loops      Passes before finished(); 0 = forever.
     */
    void start(const char* text, const Font& font, uint16_t speed_px_s, uint8_t loops) noexcept;

    /// Advances by @p dt_ms.
    void step(uint32_t dt_ms) noexcept;

    /// Current left edge of the text, in pixels (can be negative).
    int16_t x() const noexcept { return static_cast<int16_t>(pos_q8_ / 256); }

    /// true once the requested passes are done (never when loops == 0).
    bool finished() const noexcept { return finished_; }

    /// Draws the text at its current position, top row @p y.
    void draw(FrameBuffer& fb, int16_t y, Rgb color) const noexcept;

    const char* text() const noexcept { return text_; }
    uint16_t width() const noexcept { return width_; }

private:
    char text_[kTextMax + 1U];
    const Font* font_;
    uint16_t width_;
    uint16_t speed_;
    uint8_t loops_;
    uint8_t done_;
    int32_t pos_q8_;
    uint32_t remainder_;
    bool finished_;
};

}  // namespace display
