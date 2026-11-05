#pragma once
/**
 * @file i2c_bus.hpp
 * @brief I2C bus interface injected into the air-quality logic.
 *
 * The real implementation lives in `clock_hal`; the host tests use a fake that
 * replays frames (good ones, wrong CRCs, NACKs, a stuck bus). Every call is
 * **non-blocking from the logic's point of view**: a transaction either
 * completes within the driver's own short timeout or fails.
 */

#include <cstddef>
#include <cstdint>

namespace air_quality {

class I2cBus {
public:
    virtual ~I2cBus() = default;

    /// Writes @p len bytes to @p address. false on NACK or timeout.
    virtual bool write(uint8_t address, const uint8_t* data, std::size_t len) noexcept = 0;

    /// Reads @p len bytes from @p address. false on NACK or timeout.
    virtual bool read(uint8_t address, uint8_t* data, std::size_t len) noexcept = 0;

    /// Writes then reads with a repeated start (register read).
    virtual bool write_read(uint8_t address, const uint8_t* out, std::size_t out_len, uint8_t* in,
                            std::size_t in_len) noexcept = 0;

    /// Recovers a stuck bus (clocks SCL until SDA is released). true when done.
    virtual bool recover() noexcept = 0;
};

}  // namespace air_quality
