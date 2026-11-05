#pragma once
/**
 * @file i2c_bus.hpp
 * @brief I2C master bus (ESP-IDF `i2c_master`) behind the pure `air_quality::I2cBus` interface.
 *
 * Short transaction timeout (no task ever blocks on a dead sensor), bus
 * recovery by clocking SCL as a GPIO when a slave holds SDA low, pins from
 * `clock_board`.
 */

#include <cstddef>
#include <cstdint>

#include "air_quality/i2c_bus.hpp"
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace clock_hal {

class I2cBus final : public air_quality::I2cBus {
public:
    I2cBus() noexcept;

    /// Creates the bus on the board's pins (100 kHz, internal pull-ups on).
    esp_err_t init() noexcept;

    bool write(uint8_t address, const uint8_t* data, std::size_t len) noexcept override;
    bool read(uint8_t address, uint8_t* data, std::size_t len) noexcept override;
    bool write_read(uint8_t address, const uint8_t* out, std::size_t out_len, uint8_t* in,
                    std::size_t in_len) noexcept override;
    bool recover() noexcept override;

private:
    i2c_master_dev_handle_t device(uint8_t address) noexcept;
    void release() noexcept;

    i2c_master_bus_handle_t bus_;
    /// One device handle per known address (ENS160 at 0x52 or 0x53, AHT21 at 0x38).
    struct Slot {
        uint8_t address;
        i2c_master_dev_handle_t handle;
    };
    Slot slots_[3];
    bool ready_;
};

}  // namespace clock_hal
