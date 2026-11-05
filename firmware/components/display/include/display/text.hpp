#pragma once
/**
 * @file text.hpp
 * @brief Text measurement, drawing with clipping, and transliteration.
 *
 * Drawing clips itself to the panel instead of relying on FrameBuffer's
 * out-of-bounds counter: a scrolling text spends most of its life partly off
 * the panel, and that is not a scene bug.
 */

#include <cstddef>
#include <cstdint>

#include "display/color.hpp"
#include "display/font.hpp"
#include "display/frame_buffer.hpp"

namespace display {

/// Longest text accepted anywhere (scrolling notifications), without the NUL.
inline constexpr std::size_t kTextMax = 128U;

/**
 * @brief Width in pixels of @p text in @p font, spacing included between glyphs.
 * @return 0 for an empty text. Unknown characters count as a blank glyph.
 */
uint16_t text_width(const Font& font, const char* text) noexcept;

/**
 * @brief Draws @p text with its top-left corner at (x, y), clipped to the panel.
 * @param x,y Can be negative or beyond the panel: only visible pixels are set.
 * @note Unknown characters leave their box empty and advance the cursor.
 */
void draw_text(FrameBuffer& fb, const Font& font, int16_t x, int16_t y, const char* text,
               Rgb color) noexcept;

/**
 * @brief Converts UTF-8 text to the panel's ASCII set: capitals, accents stripped.
 *
 * `é` → `E`, `ç` → `C`, `°` → kDegreeChar, lower case → upper case; anything
 * else that has no glyph becomes `?`. Bounded: at most @p out_size - 1
 * characters are written and @p out is always NUL-terminated.
 * @return Number of characters written (NUL excluded).
 */
std::size_t transliterate(const char* utf8, char* out, std::size_t out_size) noexcept;

}  // namespace display
