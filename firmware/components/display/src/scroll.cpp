/**
 * @file scroll.cpp
 * @brief Horizontal text scrolling.
 */

#include "display/scroll.hpp"

#include "display/geometry.hpp"

namespace display {

Scroller::Scroller() noexcept
    : text_{}, font_(&kFont5x7), width_(0U), speed_(kScrollSpeedDefault), loops_(1U), done_(0U),
      pos_q8_(static_cast<int32_t>(kWidth) * 256), remainder_(0U), finished_(true) {}

void Scroller::start(const char* text, const Font& font, uint16_t speed_px_s, uint8_t loops) noexcept {
    std::size_t n = 0U;
    if (text != nullptr) {
        while ((text[n] != '\0') && (n < kTextMax)) {
            text_[n] = text[n];
            ++n;
        }
    }
    text_[n] = '\0';
    font_ = &font;
    width_ = text_width(font, text_);
    if (speed_px_s < kScrollSpeedMin) {
        speed_ = kScrollSpeedMin;
    } else if (speed_px_s > kScrollSpeedMax) {
        speed_ = kScrollSpeedMax;
    } else {
        speed_ = speed_px_s;
    }
    loops_ = loops;
    done_ = 0U;
    pos_q8_ = static_cast<int32_t>(kWidth) * 256;  // enters from the right edge
    remainder_ = 0U;
    finished_ = (n == 0U);  // nothing to scroll
}

void Scroller::step(uint32_t dt_ms) noexcept {
    if (finished_) {
        return;
    }
    // Exact: speed * dt * 256 / 1000, with the remainder carried over.
    const uint32_t num = static_cast<uint32_t>(speed_) * dt_ms * 256U + remainder_;
    pos_q8_ -= static_cast<int32_t>(num / 1000U);
    remainder_ = num % 1000U;

    const int32_t gone_q8 = -static_cast<int32_t>(width_) * 256;
    if (pos_q8_ <= gone_q8) {
        ++done_;
        if ((loops_ != 0U) && (done_ >= loops_)) {
            finished_ = true;
            return;
        }
        pos_q8_ = static_cast<int32_t>(kWidth) * 256;
    }
}

void Scroller::draw(FrameBuffer& fb, int16_t y, Rgb color) const noexcept {
    if (finished_) {
        return;
    }
    draw_text(fb, *font_, x(), y, text_, color);
}

}  // namespace display
