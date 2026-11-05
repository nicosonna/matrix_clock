/**
 * @file led_panel.cpp
 * @brief WS2812B panel driver (led_strip, SPI or RMT backend per Kconfig).
 */

#include "clock_display/led_panel.hpp"

#include "esp_log.h"
#include "sdkconfig.h"

#include "clock_board/board.hpp"
#include "display/geometry.hpp"

namespace clock_display {

namespace {

constexpr char kTag[] = "led_panel";

/// RMT resolution: 10 MHz gives 0.1 us steps for the WS2812B timings.
constexpr uint32_t kRmtResolutionHz = 10U * 1000U * 1000U;
/// RMT symbol memory per channel on the ESP32 (one block).
constexpr size_t kRmtMemBlockSymbols = 64U;

}  // namespace

esp_err_t LedPanel::init() noexcept {
    if (strip_ != nullptr) {
        return ESP_OK;
    }
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = static_cast<int>(clock_board::kLedDataGpio);
    strip_config.max_leds = display::kLedCount;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.flags.invert_out = false;

#if defined(CONFIG_CLOCK_LED_BACKEND_RMT)
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = kRmtResolutionHz;
    rmt_config.mem_block_symbols = kRmtMemBlockSymbols;
    rmt_config.flags.with_dma = false;  // no RMT DMA on the classic ESP32
    const esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip_);
    ESP_LOGI(kTag, "RMT backend on GPIO%d: %s", strip_config.strip_gpio_num, esp_err_to_name(err));
#else
    led_strip_spi_config_t spi_config = {};
    spi_config.clk_src = SPI_CLK_SRC_DEFAULT;
    spi_config.spi_bus = SPI2_HOST;
    spi_config.flags.with_dma = true;
    const esp_err_t err = led_strip_new_spi_device(&strip_config, &spi_config, &strip_);
    ESP_LOGI(kTag, "SPI backend (DMA) on GPIO%d: %s", strip_config.strip_gpio_num, esp_err_to_name(err));
#endif
    if (err != ESP_OK) {
        strip_ = nullptr;
    }
    return err;
}

esp_err_t LedPanel::show(const display::PhysicalFrame& frame) noexcept {
    if (strip_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint16_t i = 0U; i < display::kLedCount; ++i) {
        const display::Rgb& p = frame[i];
        const esp_err_t err = led_strip_set_pixel(strip_, i, p.r, p.g, p.b);
        if (err != ESP_OK) {
            return err;
        }
    }
    return led_strip_refresh(strip_);
}

esp_err_t LedPanel::clear() noexcept {
    if (strip_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return led_strip_clear(strip_);
}

}  // namespace clock_display
