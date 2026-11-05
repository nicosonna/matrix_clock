#pragma once
/**
 * @file ldr_adc.hpp
 * @brief Photoresistor reading on ADC1 (oneshot, 64-sample average, eFuse calibration).
 *
 * ADC1 on purpose: ADC2 is unusable while Wi-Fi is on. A failed read returns an
 * invalid sample, never a stale or invented value (the controller decides what
 * to do with it).
 */

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#include "brightness/brightness.hpp"

namespace clock_hal {

class LdrAdc {
public:
    LdrAdc() noexcept = default;
    LdrAdc(const LdrAdc&) = delete;
    LdrAdc& operator=(const LdrAdc&) = delete;

    /// Oversampling per read: the classic ESP32 ADC is noisy.
    static constexpr uint8_t kOversample = 64U;

    /**
     * @brief Opens ADC1 on the board's LDR pin and tries the eFuse calibration.
     * @return ESP_OK, ESP_ERR_INVALID_ARG if the pin is not on ADC1, or the driver error.
     */
    esp_err_t init() noexcept;

    /// @return The averaged raw reading (0..4095), invalid if any read failed or before init().
    brightness::Sample read() noexcept;

    /// true if the eFuse Vref calibration is available (millivolts are then meaningful).
    bool calibrated() const noexcept { return cali_ != nullptr; }

    /// Last reading in millivolts (0 when not calibrated).
    int last_mv() const noexcept { return last_mv_; }

private:
    adc_oneshot_unit_handle_t unit_{nullptr};
    adc_cali_handle_t cali_{nullptr};
    adc_channel_t channel_{ADC_CHANNEL_0};
    int last_mv_{0};
};

}  // namespace clock_hal
