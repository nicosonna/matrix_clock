#pragma once
/**
 * @file led_panel.hpp
 * @brief WS2812B panel driver on top of espressif/led_strip.
 *
 * The driver owns the byte order (GRB) and the transmission; it receives a
 * frame already rendered in physical LED order (display::render). It never
 * blocks on anything but the transfer itself.
 */

#include "esp_err.h"
#include "led_strip.h"

#include "display/render.hpp"

namespace clock_display {

class LedPanel {
public:
    LedPanel() noexcept = default;
    LedPanel(const LedPanel&) = delete;
    LedPanel& operator=(const LedPanel&) = delete;

    /**
     * @brief Creates the led_strip device on the board's data pin.
     * @return ESP_OK, or the driver error (the panel then stays dark).
     */
    esp_err_t init() noexcept;

    /**
     * @brief Pushes a physical frame to the panel.
     * @return ESP_OK, ESP_ERR_INVALID_STATE before init(), or the driver error.
     */
    esp_err_t show(const display::PhysicalFrame& frame) noexcept;

    /// Turns every LED off (explicitly: an uninitialised strip shows noise).
    esp_err_t clear() noexcept;

private:
    led_strip_handle_t strip_{nullptr};
};

}  // namespace clock_display
