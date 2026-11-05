/**
 * @file text.cpp
 * @brief Text measurement, drawing, transliteration.
 */

#include "display/text.hpp"

#include "display/geometry.hpp"

namespace display {

uint16_t text_width(const Font& font, const char* text) noexcept {
    uint16_t glyphs = 0U;
    for (const char* p = text; (*p != '\0') && (glyphs < kTextMax); ++p) {
        ++glyphs;
    }
    if (glyphs == 0U) {
        return 0U;
    }
    return static_cast<uint16_t>(glyphs * font.width + (glyphs - 1U) * font.spacing);
}

void draw_text(FrameBuffer& fb, const Font& font, int16_t x, int16_t y, const char* text,
               Rgb color) noexcept {
    int16_t cursor = x;
    std::size_t count = 0U;
    for (const char* p = text; (*p != '\0') && (count < kTextMax); ++p, ++count) {
        const uint8_t* g = font.glyph(*p);
        if (g != nullptr) {
            for (uint8_t col = 0U; col < font.width; ++col) {
                const int16_t px = static_cast<int16_t>(cursor + col);
                if ((px < 0) || (px >= static_cast<int16_t>(kWidth))) {
                    continue;
                }
                const uint8_t bits = g[col];
                for (uint8_t row = 0U; row < font.height; ++row) {
                    if ((bits & static_cast<uint8_t>(1U << row)) == 0U) {
                        continue;
                    }
                    const int16_t py = static_cast<int16_t>(y + row);
                    if ((py < 0) || (py >= static_cast<int16_t>(kHeight))) {
                        continue;
                    }
                    fb.set(static_cast<uint8_t>(px), static_cast<uint8_t>(py), color);
                }
            }
        }
        cursor = static_cast<int16_t>(cursor + font.width + font.spacing);
        if (cursor >= static_cast<int16_t>(kWidth)) {
            break;  // everything further right is invisible
        }
    }
}

namespace {

/// Maps the second byte of a U+00C0..U+00FF sequence (0xC3 0x80..0xBF) to ASCII.
char latin1_base(uint8_t second) noexcept {
    // Upper and lower halves have the same layout 32 code points apart.
    const uint8_t k = static_cast<uint8_t>(second & 0x9FU);  // fold 0xA0..0xBF onto 0x80..0x9F
    switch (k) {
        case 0x80U: case 0x81U: case 0x82U: case 0x83U: case 0x84U: case 0x85U: case 0x86U:
            return 'A';  // À Á Â Ã Ä Å Æ
        case 0x87U:
            return 'C';  // Ç
        case 0x88U: case 0x89U: case 0x8AU: case 0x8BU:
            return 'E';  // È É Ê Ë
        case 0x8CU: case 0x8DU: case 0x8EU: case 0x8FU:
            return 'I';  // Ì Í Î Ï
        case 0x90U:
            return 'D';  // Ð / ð
        case 0x91U:
            return 'N';  // Ñ
        case 0x92U: case 0x93U: case 0x94U: case 0x95U: case 0x96U: case 0x98U:
            return 'O';  // Ò Ó Ô Õ Ö Ø
        case 0x99U: case 0x9AU: case 0x9BU: case 0x9CU:
            return 'U';  // Ù Ú Û Ü
        case 0x9DU:
            return 'Y';  // Ý
        case 0x9FU:
            return (second == 0x9FU) ? 'S' : 'Y';  // ß (upper half) / ÿ (lower half)
        default:
            return '?';  // × ÷ Þ þ
    }
}

char ascii_glyph(uint8_t c) noexcept {
    if ((c >= 'a') && (c <= 'z')) {
        return static_cast<char>(c - ('a' - 'A'));
    }
    if ((c >= ' ') && (c <= '_')) {
        return static_cast<char>(c);
    }
    return '?';  // control characters, '{' .. '~', and the backtick itself
}

}  // namespace

std::size_t transliterate(const char* utf8, char* out, std::size_t out_size) noexcept {
    if ((out == nullptr) || (out_size == 0U)) {
        return 0U;
    }
    std::size_t n = 0U;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8);
    while ((p != nullptr) && (*p != 0U) && (n + 1U < out_size)) {
        const uint8_t c = *p;
        char g = '?';
        if (c < 0x80U) {
            g = ascii_glyph(c);
            ++p;
        } else if ((c == 0xC3U) && ((p[1] & 0xC0U) == 0x80U)) {
            g = latin1_base(p[1]);
            p += 2;
        } else if ((c == 0xC2U) && ((p[1] & 0xC0U) == 0x80U)) {
            g = (p[1] == 0xB0U) ? kDegreeChar : '?';  // ° or another C1/Latin-1 sign
            p += 2;
        } else if ((c == 0xC5U) && ((p[1] == 0x92U) || (p[1] == 0x93U))) {
            g = 'O';  // Œ œ
            p += 2;
        } else {
            // Any other multi-byte sequence: one '?', skip its continuation bytes.
            ++p;
            while ((*p & 0xC0U) == 0x80U) {
                ++p;
            }
        }
        out[n++] = g;
    }
    out[n] = '\0';
    return n;
}

}  // namespace display
