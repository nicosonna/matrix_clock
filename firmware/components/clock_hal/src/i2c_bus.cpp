/**
 * @file i2c_bus.cpp
 * @brief I2C master bus with recovery.
 */
#include "clock_hal/i2c_bus.hpp"

#include "clock_board/board.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace clock_hal {

namespace {
constexpr char kTag[] = "i2c";
constexpr uint32_t kBusHz = 100000U;
constexpr int kTransactionTimeoutMs = 50;  ///< A dead sensor costs at most this per transaction.
constexpr uint8_t kRecoveryPulses = 9U;
constexpr uint32_t kRecoveryHalfPeriodUs = 5U;
}  // namespace

I2cBus::I2cBus() noexcept : bus_(nullptr), slots_{}, ready_(false) {}

esp_err_t I2cBus::init() noexcept {
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = static_cast<gpio_num_t>(clock_board::kI2cSdaGpio);
    cfg.scl_io_num = static_cast<gpio_num_t>(clock_board::kI2cSclGpio);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = 1;
    const esp_err_t err = i2c_new_master_bus(&cfg, &bus_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "bus init failed: %s", esp_err_to_name(err));
        bus_ = nullptr;
        ready_ = false;
        return err;
    }
    ready_ = true;
    ESP_LOGI(kTag, "bus ready: SDA GPIO%u, SCL GPIO%u, %lu Hz", static_cast<unsigned>(clock_board::kI2cSdaGpio),
             static_cast<unsigned>(clock_board::kI2cSclGpio), static_cast<unsigned long>(kBusHz));
    return ESP_OK;
}

i2c_master_dev_handle_t I2cBus::device(uint8_t address) noexcept {
    if (!ready_) {
        return nullptr;
    }
    for (Slot& s : slots_) {
        if ((s.handle != nullptr) && (s.address == address)) {
            return s.handle;
        }
    }
    for (Slot& s : slots_) {
        if (s.handle == nullptr) {
            i2c_device_config_t dcfg = {};
            dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dcfg.device_address = address;
            dcfg.scl_speed_hz = kBusHz;
            if (i2c_master_bus_add_device(bus_, &dcfg, &s.handle) != ESP_OK) {
                s.handle = nullptr;
                return nullptr;
            }
            s.address = address;
            return s.handle;
        }
    }
    return nullptr;
}

void I2cBus::release() noexcept {
    for (Slot& s : slots_) {
        if (s.handle != nullptr) {
            (void)i2c_master_bus_rm_device(s.handle);
            s.handle = nullptr;
        }
    }
    if (bus_ != nullptr) {
        (void)i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
    ready_ = false;
}

bool I2cBus::write(uint8_t address, const uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t d = device(address);
    return (d != nullptr) && (i2c_master_transmit(d, data, len, kTransactionTimeoutMs) == ESP_OK);
}

bool I2cBus::read(uint8_t address, uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t d = device(address);
    return (d != nullptr) && (i2c_master_receive(d, data, len, kTransactionTimeoutMs) == ESP_OK);
}

bool I2cBus::write_read(uint8_t address, const uint8_t* out, std::size_t out_len, uint8_t* in,
                        std::size_t in_len) noexcept {
    i2c_master_dev_handle_t d = device(address);
    return (d != nullptr) && (i2c_master_transmit_receive(d, out, out_len, in, in_len, kTransactionTimeoutMs) == ESP_OK);
}

bool I2cBus::recover() noexcept {
    ESP_LOGW(kTag, "bus recovery: clocking SCL %u times", static_cast<unsigned>(kRecoveryPulses));
    release();
    // Clock SCL by hand until a slave stuck mid-byte releases SDA, then a STOP.
    const gpio_num_t scl = static_cast<gpio_num_t>(clock_board::kI2cSclGpio);
    const gpio_num_t sda = static_cast<gpio_num_t>(clock_board::kI2cSdaGpio);
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << static_cast<unsigned>(scl)) | (1ULL << static_cast<unsigned>(sda));
    io.mode = GPIO_MODE_OUTPUT_OD;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    (void)gpio_config(&io);
    (void)gpio_set_level(sda, 1);
    for (uint8_t i = 0U; i < kRecoveryPulses; ++i) {
        (void)gpio_set_level(scl, 0);
        esp_rom_delay_us(kRecoveryHalfPeriodUs);
        (void)gpio_set_level(scl, 1);
        esp_rom_delay_us(kRecoveryHalfPeriodUs);
    }
    // STOP condition: SDA low -> high while SCL high.
    (void)gpio_set_level(sda, 0);
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    (void)gpio_set_level(sda, 1);
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    (void)gpio_reset_pin(scl);
    (void)gpio_reset_pin(sda);
    return init() == ESP_OK;
}

}  // namespace clock_hal
