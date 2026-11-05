/**
 * @file ens160.cpp
 * @brief ENS160 encoding and decoding.
 */
#include "air_quality/ens160.hpp"

namespace air_quality {

namespace {
constexpr int32_t kTempMinCenti = -4000;
constexpr int32_t kTempMaxCenti = 8500;
constexpr int32_t kKelvinOffsetCenti = 27315;
}  // namespace

bool ens160_part_id_ok(const uint8_t bytes[2]) noexcept {
    const uint16_t id = static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8U));
    return id == kEns160PartId;
}

Ens160Status ens160_decode_status(uint8_t status) noexcept {
    Ens160Status s{};
    s.validity = static_cast<Ens160Validity>((status & kEns160StatusValidityMask) >> kEns160StatusValidityShift);
    s.new_data = (status & kEns160StatusNewData) != 0U;
    s.error = (status & kEns160StatusError) != 0U;
    return s;
}

Ens160Data ens160_decode_data(const uint8_t bytes[kEns160DataSize]) noexcept {
    Ens160Data d{};
    d.aqi = static_cast<uint8_t>(bytes[0] & 0x07U);
    d.tvoc_ppb = static_cast<uint16_t>(bytes[1] | (static_cast<uint16_t>(bytes[2]) << 8U));
    d.eco2_ppm = static_cast<uint16_t>(bytes[3] | (static_cast<uint16_t>(bytes[4]) << 8U));
    return d;
}

void ens160_encode_compensation(int16_t temperature_centi, uint16_t humidity_pct10,
                                uint8_t out[kEns160CompensationSize]) noexcept {
    int32_t t = temperature_centi;
    if (t < kTempMinCenti) {
        t = kTempMinCenti;
    }
    if (t > kTempMaxCenti) {
        t = kTempMaxCenti;
    }
    // Kelvin x 64 = (centi + 27315) * 64 / 100, rounded.
    const int32_t kelvin64 = (((t + kKelvinOffsetCenti) * 64) + 50) / 100;
    const uint32_t h = (humidity_pct10 > 1000U) ? 1000U : humidity_pct10;
    // % x 512 = pct10 * 512 / 10, rounded.
    const uint32_t rh512 = ((h * 512U) + 5U) / 10U;
    out[0] = kEns160RegTempIn;
    out[1] = static_cast<uint8_t>(kelvin64 & 0xFF);
    out[2] = static_cast<uint8_t>((kelvin64 >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(rh512 & 0xFFU);
    out[4] = static_cast<uint8_t>((rh512 >> 8U) & 0xFFU);
}

}  // namespace air_quality
