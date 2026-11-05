/**
 * @file aht21.cpp
 * @brief AHT21 decoding.
 */
#include "air_quality/aht21.hpp"

namespace air_quality {

uint8_t aht21_crc8(const uint8_t* data, std::size_t len) noexcept {
    uint8_t crc = 0xFFU;
    for (std::size_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 0x80U) != 0U) ? static_cast<uint8_t>((crc << 1U) ^ 0x31U) : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

Aht21Status aht21_decode(const uint8_t frame[kAht21FrameSize], Aht21Reading& out) noexcept {
    if ((frame[0] & kAht21StatusBusy) != 0U) {
        return Aht21Status::Busy;
    }
    if (aht21_crc8(frame, kAht21FrameSize - 1U) != frame[kAht21FrameSize - 1U]) {
        return Aht21Status::CrcError;
    }
    if ((frame[0] & kAht21StatusCalibrated) == 0U) {
        return Aht21Status::NotCalibrated;
    }
    const uint32_t raw_h = (static_cast<uint32_t>(frame[1]) << 12U) | (static_cast<uint32_t>(frame[2]) << 4U) |
                           (static_cast<uint32_t>(frame[3]) >> 4U);
    const uint32_t raw_t = ((static_cast<uint32_t>(frame[3]) & 0x0FU) << 16U) | (static_cast<uint32_t>(frame[4]) << 8U) |
                           static_cast<uint32_t>(frame[5]);
    // RH% = raw / 2^20 * 100 ; T = raw / 2^20 * 200 - 50. Rounded, in tenths and hundredths.
    const uint32_t h10 = ((raw_h * 1000U) + (1U << 19U)) >> 20U;
    const int32_t t100 = static_cast<int32_t>((static_cast<uint64_t>(raw_t) * 20000U + (1U << 19U)) >> 20U) - 5000;
    out.humidity_pct10 = static_cast<uint16_t>((h10 > 1000U) ? 1000U : h10);
    out.temperature_centi = static_cast<int16_t>(t100);
    return Aht21Status::Ok;
}

}  // namespace air_quality
