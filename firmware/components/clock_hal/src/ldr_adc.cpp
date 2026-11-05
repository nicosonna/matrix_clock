/**
 * @file ldr_adc.cpp
 * @brief Photoresistor reading on ADC1.
 */

#include "clock_hal/ldr_adc.hpp"

#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include "clock_board/board.hpp"

namespace clock_hal {

namespace {
constexpr char kTag[] = "ldr_adc";
}

esp_err_t LdrAdc::init() noexcept {
    if (unit_ != nullptr) {
        return ESP_OK;
    }
    adc_unit_t unit_id = ADC_UNIT_1;
    esp_err_t err = adc_oneshot_io_to_channel(static_cast<int>(clock_board::kLdrAdcGpio), &unit_id, &channel_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "GPIO%u is not an ADC pin: %s", static_cast<unsigned>(clock_board::kLdrAdcGpio),
                 esp_err_to_name(err));
        return err;
    }
    if (unit_id != ADC_UNIT_1) {
        ESP_LOGE(kTag, "GPIO%u is on ADC2, unusable with Wi-Fi: move the LDR to an ADC1 pin",
                 static_cast<unsigned>(clock_board::kLdrAdcGpio));
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    err = adc_oneshot_new_unit(&unit_cfg, &unit_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        unit_ = nullptr;
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    err = adc_oneshot_config_channel(unit_, channel_, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
        return err;
    }

    // Calibration: the classic ESP32 uses the line-fitting scheme with the eFuse Vref.
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;
    const esp_err_t cali_err = adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_);
    if (cali_err != ESP_OK) {
        cali_ = nullptr;
        ESP_LOGW(kTag, "no ADC calibration (%s): raw counts only, which the curve handles anyway",
                 esp_err_to_name(cali_err));
    }
    ESP_LOGI(kTag, "ADC1 channel %d on GPIO%u, 12 dB, calibrated=%d", static_cast<int>(channel_),
             static_cast<unsigned>(clock_board::kLdrAdcGpio), calibrated() ? 1 : 0);
    return ESP_OK;
}

brightness::Sample LdrAdc::read() noexcept {
    if (unit_ == nullptr) {
        return brightness::Sample{0U, false};
    }
    uint32_t sum = 0U;
    for (uint8_t i = 0U; i < kOversample; ++i) {
        int raw = 0;
        if (adc_oneshot_read(unit_, channel_, &raw) != ESP_OK) {
            return brightness::Sample{0U, false};
        }
        sum += static_cast<uint32_t>(raw);
    }
    const uint16_t avg = static_cast<uint16_t>(sum / kOversample);
    if (cali_ != nullptr) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(cali_, static_cast<int>(avg), &mv) == ESP_OK) {
            last_mv_ = mv;
        }
    }
    return brightness::Sample{avg, true};
}

}  // namespace clock_hal
