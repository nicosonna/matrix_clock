#pragma once
/**
 * @file font.hpp
 * @brief Fixed-width bitmap fonts, column-major, bit 0 = top row.
 *
 * Three fonts (spec-affichage.md, 3): 5x7 for the big clock and scrolling
 * text, 3x5 for a secondary line, 3x4 for the two-line scene. Every glyph of a
 * font has the same width; the character set is contiguous ASCII from `first`
 * to `last`, unsupported characters inside the range are blank, characters
 * outside the range are drawn as an empty box (nothing lit, width advanced) —
 * never a crash, never a random glyph.
 *
 * Accents do not fit in 7 rows: text goes through transliterate() first
 * (text.hpp). The degree sign is mapped to kDegreeChar, a slot every font fills.
 */

#include <cstdint>

namespace display {

/// ASCII slot used for the degree sign (the backtick is never needed otherwise).
inline constexpr char kDegreeChar = '`';

struct Font {
    uint8_t width;          ///< Columns per glyph.
    uint8_t height;         ///< Rows per glyph (bits used in each column byte).
    uint8_t spacing;        ///< Blank columns between glyphs.
    char first;             ///< First character of the contiguous set.
    char last;              ///< Last character of the contiguous set.
    const uint8_t* columns; ///< (last - first + 1) * width bytes.

    /// @return true if @p c has a slot in this font.
    constexpr bool has(char c) const noexcept { return (c >= first) && (c <= last); }

    /// @return The glyph's columns, or nullptr when @p c has no slot.
    const uint8_t* glyph(char c) const noexcept {
        if (!has(c)) {
            return nullptr;
        }
        return columns + (static_cast<uint16_t>(static_cast<uint8_t>(c) - static_cast<uint8_t>(first)) * width);
    }
};

/// 5 columns x 7 rows, ' '..'`' (digits, punctuation, A-Z, degree). Big clock, scrolling.
extern const Font kFont5x7;
/// 3 columns x 5 rows, ' '..'`' (digits, A-Z, a few signs, degree). Secondary line, date.
extern const Font kFont3x5;
/// 3 columns x 4 rows, ' '..':' (digits, '.', '-', ':'). Two-line scene.
extern const Font kFont3x4;

}  // namespace display
