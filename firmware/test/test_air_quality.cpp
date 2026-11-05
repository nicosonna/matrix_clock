/**
 * @file test_air_quality.cpp
 * @brief AIR-01 .. AIR-21: ENS160 + AHT21 decoding, measurement loop on a fake bus, levels, logo.
 */
#include "test_support.hpp"

#include <cstring>

#include "air_quality/aht21.hpp"
#include "air_quality/ens160.hpp"
#include "air_quality/i2c_bus.hpp"
#include "air_quality/levels.hpp"
#include "air_quality/sensor.hpp"
#include "display/air_logo.hpp"
#include "display/frame_buffer.hpp"
#include "display/value_icons.hpp"

using namespace air_quality;

namespace {

// ------------------------------------------------------------------ fake bus

/// Replays a scripted ENS160 + AHT21 module. Every transaction is logged.
class FakeBus final : public I2cBus {
public:
    // Scripted behaviour
    bool ens_present = true;
    uint16_t part_id = kEns160PartId;
    uint8_t ens_status = 0x02U;         ///< NEWDAT, validity 0
    uint8_t ens_data[5] = {3U, 0x2CU, 0x01U, 0x34U, 0x03U};  ///< AQI 3, TVOC 300, eCO2 820
    bool aht_present = true;
    uint8_t aht_status = 0x18U;         ///< calibrated, not busy
    uint8_t aht_frame[7] = {};
    bool aht_frame_busy = false;
    bool aht_frame_bad_crc = false;
    bool fail_all = false;              ///< Stuck bus: every transaction fails.
    // Log
    uint32_t ens_writes = 0U;
    uint32_t comp_writes = 0U;
    uint32_t data_reads = 0U;
    uint32_t status_reads = 0U;
    uint32_t aht_inits = 0U;
    uint32_t aht_triggers = 0U;
    uint32_t recoveries = 0U;
    uint8_t last_comp[5] = {};
    uint32_t order_counter = 0U;
    uint32_t last_comp_order = 0U;
    uint32_t last_data_order = 0U;
    uint8_t opmode = 0U;

    FakeBus() { set_aht(2345, 456U); }

    /// Builds a valid AHT21 frame for a temperature (centi) and humidity (tenths).
    void set_aht(int32_t temp_centi, uint32_t hum_pct10) {
        uint64_t rh = (static_cast<uint64_t>(hum_pct10) << 20U) / 1000U;
        uint64_t rt = (static_cast<uint64_t>(temp_centi + 5000) << 20U) / 20000U;
        const uint32_t raw_h = static_cast<uint32_t>((rh > 0xFFFFFU) ? 0xFFFFFU : rh);
        const uint32_t raw_t = static_cast<uint32_t>((rt > 0xFFFFFU) ? 0xFFFFFU : rt);
        aht_frame[0] = 0x18U;
        aht_frame[1] = static_cast<uint8_t>(raw_h >> 12U);
        aht_frame[2] = static_cast<uint8_t>(raw_h >> 4U);
        aht_frame[3] = static_cast<uint8_t>(((raw_h & 0x0FU) << 4U) | ((raw_t >> 16U) & 0x0FU));
        aht_frame[4] = static_cast<uint8_t>(raw_t >> 8U);
        aht_frame[5] = static_cast<uint8_t>(raw_t);
        aht_frame[6] = aht21_crc8(aht_frame, 6U);
    }

    bool write(uint8_t address, const uint8_t* data, std::size_t len) noexcept override {
        ++order_counter;
        if (fail_all) {
            return false;
        }
        if (address == kAht21Address) {
            if (!aht_present) {
                return false;
            }
            if ((len == 3U) && (data[0] == 0xBEU)) {
                ++aht_inits;
                aht_status |= kAht21StatusCalibrated;
            } else if ((len == 3U) && (data[0] == 0xACU)) {
                ++aht_triggers;
            }
            return true;
        }
        if (!ens_present) {
            return false;
        }
        ++ens_writes;
        if ((len == 2U) && (data[0] == kEns160RegOpMode)) {
            opmode = data[1];
        } else if ((len == 5U) && (data[0] == kEns160RegTempIn)) {
            ++comp_writes;
            last_comp_order = order_counter;
            std::memcpy(last_comp, data, 5U);
        }
        return true;
    }

    bool read(uint8_t address, uint8_t* data, std::size_t len) noexcept override {
        ++order_counter;
        if (fail_all || (address != kAht21Address) || !aht_present || (len != 7U)) {
            return false;
        }
        std::memcpy(data, aht_frame, 7U);
        if (aht_frame_busy) {
            data[0] |= kAht21StatusBusy;
        }
        if (aht_frame_bad_crc) {
            data[6] ^= 0xFFU;
        }
        return true;
    }

    bool write_read(uint8_t address, const uint8_t* out, std::size_t out_len, uint8_t* in,
                    std::size_t in_len) noexcept override {
        ++order_counter;
        if (fail_all || (out_len != 1U)) {
            return false;
        }
        if (address == kAht21Address) {
            if (!aht_present || (out[0] != kAht21StatusCommand) || (in_len != 1U)) {
                return false;
            }
            in[0] = aht_status;
            return true;
        }
        if (!ens_present) {
            return false;
        }
        switch (out[0]) {
            case kEns160RegPartId:
                if (in_len != 2U) {
                    return false;
                }
                in[0] = static_cast<uint8_t>(part_id & 0xFFU);
                in[1] = static_cast<uint8_t>(part_id >> 8U);
                return true;
            case kEns160RegOpMode:
                in[0] = opmode;
                return true;
            case kEns160RegDeviceStatus:
                ++status_reads;
                in[0] = ens_status;
                return true;
            case kEns160RegDataAqi:
                if (in_len != 5U) {
                    return false;
                }
                ++data_reads;
                last_data_order = order_counter;
                std::memcpy(in, ens_data, 5U);
                return true;
            default:
                return false;
        }
    }

    bool recover() noexcept override {
        ++recoveries;
        return true;
    }
};

/// Runs the state machine for @p ms milliseconds in 10 ms steps.
void run(AirSensor& s, uint32_t& now, uint32_t ms) {
    const uint32_t end = now + ms;
    while (now < end) {
        s.step(now);
        now += 10U;
    }
}

// ------------------------------------------------------------- AIR-06/07/08

void test_aht21() {
    // Known vector of the AHT CRC-8 (poly 0x31, init 0xFF): the empty message gives 0xFF.
    CHECK_EQ(aht21_crc8(nullptr, 0U), 0xFFU);
    const uint8_t one[1] = {0x00U};
    CHECK_EQ(aht21_crc8(one, 1U), 0xACU);
    FakeBus bus;
    Aht21Reading r{};
    // Typical room: 23.45 degC / 45.6 % -> rounding within 1 centi / 1 tenth.
    CHECK_ENUM(aht21_decode(bus.aht_frame, r), Aht21Status::Ok);
    CHECK((r.temperature_centi >= 2344) && (r.temperature_centi <= 2346));
    CHECK((r.humidity_pct10 >= 455U) && (r.humidity_pct10 <= 457U));
    // Extremes.
    bus.set_aht(-4000, 0U);
    CHECK_ENUM(aht21_decode(bus.aht_frame, r), Aht21Status::Ok);
    CHECK((r.temperature_centi >= -4001) && (r.temperature_centi <= -3999));
    CHECK_EQ(r.humidity_pct10, 0U);
    bus.set_aht(8500, 1000U);
    CHECK_ENUM(aht21_decode(bus.aht_frame, r), Aht21Status::Ok);
    CHECK((r.temperature_centi >= 8499) && (r.temperature_centi <= 8501));
    CHECK_EQ(r.humidity_pct10, 1000U);
    // Wrong CRC: rejected, output untouched.
    bus.set_aht(2000, 500U);
    uint8_t bad[7];
    std::memcpy(bad, bus.aht_frame, 7U);
    bad[6] ^= 0x01U;
    r.temperature_centi = 1234;
    CHECK_ENUM(aht21_decode(bad, r), Aht21Status::CrcError);
    CHECK_EQ(r.temperature_centi, 1234);
    // Busy and not calibrated.
    std::memcpy(bad, bus.aht_frame, 7U);
    bad[0] |= kAht21StatusBusy;
    CHECK_ENUM(aht21_decode(bad, r), Aht21Status::Busy);
    std::memcpy(bad, bus.aht_frame, 7U);
    bad[0] = static_cast<uint8_t>(bad[0] & ~kAht21StatusCalibrated);
    bad[6] = aht21_crc8(bad, 6U);
    CHECK_ENUM(aht21_decode(bad, r), Aht21Status::NotCalibrated);
}

// ------------------------------------------------------------- AIR-04/05

void test_ens160_codec() {
    const uint8_t id_ok[2] = {0x60U, 0x01U};
    const uint8_t id_bad[2] = {0x01U, 0x60U};
    CHECK(ens160_part_id_ok(id_ok));
    CHECK(!ens160_part_id_ok(id_bad));
    Ens160Status s = ens160_decode_status(0x02U);
    CHECK_ENUM(s.validity, Ens160Validity::Normal);
    CHECK(s.new_data);
    CHECK(!s.error);
    s = ens160_decode_status(0x04U);
    CHECK_ENUM(s.validity, Ens160Validity::WarmUp);
    s = ens160_decode_status(0x08U);
    CHECK_ENUM(s.validity, Ens160Validity::InitialStart);
    s = ens160_decode_status(0x4CU);
    CHECK_ENUM(s.validity, Ens160Validity::Invalid);
    CHECK(s.error);
    // Data: LE words, AQI masked to 3 bits, extremes.
    const uint8_t d[5] = {0xFDU, 0xFFU, 0xFFU, 0x00U, 0x00U};
    const Ens160Data x = ens160_decode_data(d);
    CHECK_EQ(x.aqi, 5U);
    CHECK_EQ(x.tvoc_ppb, 65535U);
    CHECK_EQ(x.eco2_ppm, 0U);
    // Compensation: 25.00 degC -> 298.15 K * 64 = 19081.6 -> 19082 ; 50.0 % * 512 = 25600.
    uint8_t c[5];
    ens160_encode_compensation(2500, 500U, c);
    CHECK_EQ(c[0], kEns160RegTempIn);
    CHECK_EQ(static_cast<uint32_t>(c[1] | (c[2] << 8)), 19082U);
    CHECK_EQ(static_cast<uint32_t>(c[3] | (c[4] << 8)), 25600U);
    // Clamping.
    ens160_encode_compensation(-9000, 2000U, c);
    CHECK_EQ(static_cast<uint32_t>(c[1] | (c[2] << 8)), ((23315 * 64) + 50) / 100);
    CHECK_EQ(static_cast<uint32_t>(c[3] | (c[4] << 8)), 51200U);
    ens160_encode_compensation(12000, 0U, c);
    CHECK_EQ(static_cast<uint32_t>(c[1] | (c[2] << 8)), ((35815 * 64) + 50) / 100);
}

// ------------------------------------------------------------- AIR-01/02/03/09/10/21

void test_sensor_loop() {
    SensorConfig cfg = default_sensor_config();
    CHECK(is_valid(cfg));
    SensorConfig bad = cfg;
    bad.ens160_address = 0x50U;
    CHECK(!is_valid(bad));
    bad = cfg;
    bad.temperature_offset_centi = 1001;
    CHECK(!is_valid(bad));
    bad = cfg;
    bad.period_ms = 500U;
    CHECK(!is_valid(bad));

    // Nominal cycle.
    {
        FakeBus bus;
        AirSensor s(bus, cfg);
        uint32_t now = 1000U;
        AllocGuard guard;
        run(s, now, 2000U);
        const AirReading& r = s.reading();
        CHECK(r.present);
        CHECK(!r.fault);
        CHECK(r.air_valid);
        CHECK_EQ(r.aqi, 3U);
        CHECK_EQ(r.tvoc_ppb, 300U);
        CHECK_EQ(r.eco2_ppm, 820U);
        CHECK(r.climate_valid);
        CHECK((r.temperature_centi >= 2344) && (r.temperature_centi <= 2346));
        CHECK(!r.compensation_default);
        CHECK_EQ(bus.opmode, kEns160OpModeStandard);
        CHECK_EQ(r.opmode_raw, kEns160OpModeStandard);
        CHECK_EQ(r.status_raw, 0x02U);
        CHECK(bus.comp_writes >= 1U);
        CHECK(bus.data_reads >= 1U);
        // AIR-10: the compensation is written before the data is read.
        CHECK(bus.last_comp_order < bus.last_data_order);
        // The written compensation is the AHT21 reading (23.45 degC -> 296.60 K * 64 = 18982).
        const uint32_t k64 = static_cast<uint32_t>(bus.last_comp[1] | (bus.last_comp[2] << 8));
        CHECK((k64 >= 18981U) && (k64 <= 18983U));
        // One cycle per second, not more.
        const uint32_t cycles_before = bus.aht_triggers;
        run(s, now, 5000U);
        CHECK((bus.aht_triggers - cycles_before) >= 4U);
        CHECK((bus.aht_triggers - cycles_before) <= 6U);
        CHECK_EQ(g_alloc_count, 0U);
    }
    // AIR-01: wrong PART_ID -> absent, no transaction storm, retried after a minute.
    {
        FakeBus bus;
        bus.part_id = 0x0161U;
        AirSensor s(bus, cfg);
        uint32_t now = 0U;
        run(s, now, 3000U);
        CHECK(!s.reading().present);
        CHECK(!s.reading().air_valid);
        CHECK_EQ(bus.comp_writes, 0U);
        CHECK(std::strcmp(s.state_name(), "absent") == 0);
        bus.part_id = kEns160PartId;
        run(s, now, 30000U);
        CHECK(!s.reading().present);  // not before the retry delay
        run(s, now, 35000U);
        CHECK(s.reading().present);
    }
    // AIR-02: warm-up and initial start -> invalid, flagged; fault -> fault.
    {
        FakeBus bus;
        bus.ens_status = 0x06U;  // validity 1, NEWDAT
        AirSensor s(bus, cfg);
        uint32_t now = 0U;
        run(s, now, 2500U);
        CHECK(s.reading().present);
        CHECK(s.reading().warming_up);
        CHECK(!s.reading().air_valid);
        CHECK(!s.reading().fault);
        CHECK_EQ(bus.data_reads, 0U);
        CHECK(s.reading().climate_valid);  // the AHT21 is fine meanwhile
        bus.ens_status = 0x0AU;  // validity 2
        run(s, now, 2000U);
        CHECK(s.reading().initial_start);
        CHECK(!s.reading().warming_up);
        CHECK(!s.reading().air_valid);
        bus.ens_status = 0x02U;  // normal, new data
        run(s, now, 2000U);
        CHECK(s.reading().air_valid);
        CHECK(!s.reading().initial_start);
        bus.ens_status = 0x0EU;  // validity 3
        run(s, now, 2000U);
        CHECK(s.reading().fault);
        CHECK(!s.reading().air_valid);
    }
    // AIR-03: no NEWDAT -> the data is not re-read, the previous value stays (valid, not refreshed).
    {
        FakeBus bus;
        AirSensor s(bus, cfg);
        uint32_t now = 0U;
        run(s, now, 2500U);
        CHECK(s.reading().air_valid);
        const uint32_t reads = bus.data_reads;
        bus.ens_status = 0x00U;  // validity 0, no new data
        bus.ens_data[0] = 5U;
        run(s, now, 3000U);
        CHECK_EQ(bus.data_reads, reads);
        CHECK_EQ(s.reading().aqi, 3U);
    }
    // AIR-07/09: 3 CRC failures -> AHT21 in fault, fallback compensation, ENS160 still read; recovery on a good frame.
    {
        FakeBus bus;
        AirSensor s(bus, cfg);
        uint32_t now = 0U;
        run(s, now, 2500U);
        CHECK(s.reading().climate_valid);
        bus.aht_frame_bad_crc = true;
        run(s, now, 1000U);  // 1 rejection: previous climate no longer claimed valid
        CHECK(!s.reading().climate_valid);
        CHECK(s.reading().compensation_default);
        run(s, now, 3000U);
        CHECK(s.reading().compensation_default);
        CHECK(s.reading().air_valid);  // ENS160 kept going
        const uint32_t k64 = static_cast<uint32_t>(bus.last_comp[1] | (bus.last_comp[2] << 8));
        CHECK_EQ(k64, 19082U);  // 25.00 degC fallback
        bus.aht_frame_bad_crc = false;
        run(s, now, 2000U);
        CHECK(s.reading().climate_valid);
        CHECK(!s.reading().compensation_default);
    }
    // AIR-08: not calibrated at check -> init command sent; busy frames retried.
    {
        FakeBus bus;
        bus.aht_status = 0x10U;  // not calibrated
        AirSensor s(bus, cfg);
        uint32_t now = 0U;
        run(s, now, 2500U);
        CHECK(bus.aht_inits >= 1U);
        CHECK(s.reading().climate_valid);
        bus.aht_frame_busy = true;
        run(s, now, 2000U);
        CHECK(!s.reading().climate_valid);
        CHECK(s.reading().air_valid);
    }
    // AHT21 absent entirely: ENS160 still works with the fallback compensation.
    {
        FakeBus bus;
        bus.aht_present = false;
        AirSensor s(bus, cfg);
        uint32_t now = 0U;
        run(s, now, 3000U);
        CHECK(s.reading().present);
        CHECK(s.reading().air_valid);
        CHECK(!s.reading().climate_valid);
        CHECK(s.reading().compensation_default);
    }
    // AIR-21: stuck bus -> fault after 3 consecutive failures, recovery, then back to normal; nothing blocks.
    {
        FakeBus bus;
        AirSensor s(bus, cfg);
        uint32_t now = 0U;
        run(s, now, 2500U);
        CHECK(s.reading().air_valid);
        bus.fail_all = true;
        run(s, now, 1500U);
        CHECK(!s.reading().fault);  // not yet: 3 failures needed
        run(s, now, 4000U);
        CHECK(s.reading().fault);
        CHECK(!s.reading().air_valid);
        CHECK(!s.reading().climate_valid);
        run(s, now, 2000U);
        CHECK(bus.recoveries >= 1U);
        CHECK_EQ(s.reading().bus_resets, bus.recoveries);
        bus.fail_all = false;
        run(s, now, 4000U);
        CHECK(!s.reading().fault);
        CHECK(s.reading().air_valid);
    }
    // Offset applied to the published temperature and to the compensation (AIR-16).
    {
        FakeBus bus;
        bus.set_aht(2600, 400U);
        SensorConfig off = cfg;
        off.temperature_offset_centi = 350;
        AirSensor s(bus, off);
        uint32_t now = 0U;
        run(s, now, 2500U);
        CHECK((s.reading().temperature_centi >= 2249) && (s.reading().temperature_centi <= 2251));
        const uint32_t k64 = static_cast<uint32_t>(bus.last_comp[1] | (bus.last_comp[2] << 8));
        // 22.50 degC -> 295.65 K * 64 = 18921.6 -> 18922 (+-1 for the AHT rounding)
        CHECK((k64 >= 18921U) && (k64 <= 18923U));
        // Hot reconfiguration with a new address re-probes.
        SensorConfig other = off;
        other.ens160_address = kEns160AddressLow;
        s.configure(other);
        CHECK(std::strcmp(s.state_name(), "probe") == 0);
        s.configure(bad);  // invalid: ignored
    }
}

// ------------------------------------------------------------- AIR-11..15, 17, 18

AirReading valid_reading(uint8_t aqi, uint16_t eco2, uint16_t tvoc) {
    AirReading r{};
    r.present = true;
    r.air_valid = true;
    r.aqi = aqi;
    r.eco2_ppm = eco2;
    r.tvoc_ppb = tvoc;
    return r;
}

void test_levels() {
    LevelConfig c = default_level_config();
    CHECK(is_valid(c));
    LevelConfig bad = c;
    bad.eco2_thresholds[2] = 800U;  // not increasing
    CHECK(!is_valid(bad));
    bad = c;
    bad.confirmation_s = 3601U;
    CHECK(!is_valid(bad));
    bad = c;
    bad.hysteresis_eco2 = 200U;  // swallows the 600..800 level
    CHECK(!is_valid(bad));
    bad = c;
    bad.tvoc_thresholds[0] = 0U;
    CHECK(!is_valid(bad));
    bad = c;
    bad.pilot = static_cast<PilotQuantity>(7U);
    CHECK(!is_valid(bad));

    // AIR-11: the five levels for each pilot, at the exact thresholds.
    CHECK_ENUM(raw_level(c, valid_reading(1U, 0U, 0U)), AirLevel::Excellent);
    CHECK_ENUM(raw_level(c, valid_reading(5U, 0U, 0U)), AirLevel::VeryPoor);
    CHECK_ENUM(raw_level(c, valid_reading(0U, 0U, 0U)), AirLevel::Excellent);  // clamped
    CHECK_ENUM(raw_level(c, valid_reading(7U, 0U, 0U)), AirLevel::VeryPoor);
    c.pilot = PilotQuantity::Eco2;
    CHECK_ENUM(raw_level(c, valid_reading(1U, 599U, 0U)), AirLevel::Excellent);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 600U, 0U)), AirLevel::Good);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 800U, 0U)), AirLevel::Moderate);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 1000U, 0U)), AirLevel::Poor);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 1499U, 0U)), AirLevel::Poor);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 1500U, 0U)), AirLevel::VeryPoor);
    c.pilot = PilotQuantity::Tvoc;
    CHECK_ENUM(raw_level(c, valid_reading(1U, 0U, 219U)), AirLevel::Excellent);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 0U, 220U)), AirLevel::Good);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 0U, 660U)), AirLevel::Moderate);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 0U, 1430U)), AirLevel::Poor);
    CHECK_ENUM(raw_level(c, valid_reading(1U, 0U, 2200U)), AirLevel::VeryPoor);
    // Invalid readings -> Unknown whatever the numbers.
    AirReading inv = valid_reading(5U, 5000U, 5000U);
    inv.air_valid = false;
    CHECK_ENUM(raw_level(c, inv), AirLevel::Unknown);
    inv = valid_reading(5U, 5000U, 5000U);
    inv.fault = true;
    CHECK_ENUM(raw_level(c, inv), AirLevel::Unknown);

    // Pilot value and names.
    c.pilot = PilotQuantity::Eco2;
    PilotValue pv = pilot_value(c, valid_reading(2U, 820U, 300U));
    CHECK(pv.valid);
    CHECK_EQ(pv.value, 820U);
    CHECK(std::strcmp(pv.unit, "ppm") == 0);
    c.pilot = PilotQuantity::Tvoc;
    pv = pilot_value(c, valid_reading(2U, 820U, 300U));
    CHECK_EQ(pv.value, 300U);
    CHECK(std::strcmp(pv.unit, "ppb") == 0);
    c.pilot = PilotQuantity::Aqi;
    pv = pilot_value(c, inv);
    CHECK(!pv.valid);
    CHECK_EQ(pv.value, 5U);
    CHECK(std::strcmp(pv.unit, "") == 0);
    PilotQuantity p{};
    CHECK(pilot_from_name("Eco2", p) && (p == PilotQuantity::Eco2));
    CHECK(pilot_from_name("Tvoc", p) && (p == PilotQuantity::Tvoc));
    CHECK(pilot_from_name("Aqi", p) && (p == PilotQuantity::Aqi));
    CHECK(!pilot_from_name("Co2", p));
    CHECK(!pilot_from_name(nullptr, p));
    CHECK(std::strcmp(pilot_name(PilotQuantity::Aqi), "Aqi") == 0);
    CHECK(std::strcmp(level_name(AirLevel::VeryPoor), "Tres mauvais") == 0);
    CHECK(std::strcmp(level_name(AirLevel::Unknown), "Inconnu") == 0);
    CHECK(std::strcmp(level_name(AirLevel::Good), "Bon") == 0);
    CHECK(std::strcmp(level_name(AirLevel::Moderate), "Moyen") == 0);
    CHECK(std::strcmp(level_name(AirLevel::Poor), "Mauvais") == 0);
    CHECK(std::strcmp(level_name(AirLevel::Excellent), "Excellent") == 0);

    // AIR-13: confirmation 59 s -> no change, 60 s -> change, interruption resets.
    {
        LevelTracker t;
        c = default_level_config();
        uint32_t now = 0U;
        CHECK_ENUM(t.step(c, valid_reading(2U, 0U, 0U), now), AirLevel::Good);  // first valid: immediate
        for (uint32_t i = 0U; i < 60U; ++i) {  // held 0..59 s: not yet
            now += 1000U;
            CHECK_ENUM(t.step(c, valid_reading(4U, 0U, 0U), now), AirLevel::Good);
        }
        CHECK_ENUM(t.candidate(), AirLevel::Poor);
        now += 1000U;
        CHECK_ENUM(t.step(c, valid_reading(4U, 0U, 0U), now), AirLevel::Poor);
        // Interruption: 30 s at 2, back to 4 -> the counter restarts.
        for (uint32_t i = 0U; i < 30U; ++i) {
            now += 1000U;
            t.step(c, valid_reading(2U, 0U, 0U), now);
        }
        for (uint32_t i = 0U; i < 10U; ++i) {
            now += 1000U;
            CHECK_ENUM(t.step(c, valid_reading(4U, 0U, 0U), now), AirLevel::Poor);
        }
        for (uint32_t i = 0U; i < 60U; ++i) {
            now += 1000U;
            CHECK_ENUM(t.step(c, valid_reading(2U, 0U, 0U), now), AirLevel::Poor);
        }
        now += 1000U;
        CHECK_ENUM(t.step(c, valid_reading(2U, 0U, 0U), now), AirLevel::Good);
        // Invalid -> Unknown at once (honesty), then the next valid is immediate.
        now += 1000U;
        CHECK_ENUM(t.step(c, inv, now), AirLevel::Unknown);
        now += 1000U;
        CHECK_ENUM(t.step(c, valid_reading(3U, 0U, 0U), now), AirLevel::Moderate);
    }
    // AIR-12/14: hysteresis on eCO2 — oscillation around 1000 ppm gives one change; rise without margin.
    {
        LevelTracker t;
        c = default_level_config();
        c.pilot = PilotQuantity::Eco2;
        c.confirmation_s = 0U;
        uint32_t now = 0U;
        CHECK_ENUM(t.step(c, valid_reading(1U, 990U, 0U), now), AirLevel::Moderate);
        now += 1000U;
        CHECK_ENUM(t.step(c, valid_reading(1U, 1000U, 0U), now), AirLevel::Poor);  // up at the threshold
        uint32_t changes = 0U;
        AirLevel last = AirLevel::Poor;
        for (uint32_t i = 0U; i < 20U; ++i) {
            now += 1000U;
            const AirLevel l = t.step(c, valid_reading(1U, ((i % 2U) == 0U) ? 990U : 1010U, 0U), now);
            if (l != last) {
                ++changes;
                last = l;
            }
        }
        CHECK_EQ(changes, 0U);  // 990 is above 1000 - 50: still Poor
        now += 1000U;
        CHECK_ENUM(t.step(c, valid_reading(1U, 951U, 0U), now), AirLevel::Poor);
        now += 1000U;
        CHECK_ENUM(t.step(c, valid_reading(1U, 949U, 0U), now), AirLevel::Moderate);  // below threshold - hysteresis
        // Two levels down at once: 500 -> Excellent directly.
        now += 1000U;
        CHECK_ENUM(t.step(c, valid_reading(1U, 500U, 0U), now), AirLevel::Excellent);
        // TVOC pilot uses its own hysteresis.
        c.pilot = PilotQuantity::Tvoc;
        LevelTracker t2;
        CHECK_ENUM(t2.step(c, valid_reading(1U, 0U, 700U), now), AirLevel::Moderate);
        now += 1000U;
        CHECK_ENUM(t2.step(c, valid_reading(1U, 0U, 600U), now), AirLevel::Moderate);  // 660 - 100 = 560
        now += 1000U;
        CHECK_ENUM(t2.step(c, valid_reading(1U, 0U, 559U), now), AirLevel::Good);
    }
    // AIR-18: colours — grey for unknown, never green.
    const LevelColor grey = level_color(AirLevel::Unknown);
    CHECK((grey.r == grey.g) && (grey.g == grey.b));
    CHECK(level_color(AirLevel::Excellent).g > level_color(AirLevel::Excellent).r);
    CHECK(level_color(AirLevel::VeryPoor).r > level_color(AirLevel::VeryPoor).g);
    CHECK(level_color(AirLevel::Poor).r == 255U);
    CHECK(level_color(AirLevel::Moderate).g == 200U);
    CHECK(level_color(AirLevel::Good).g == 220U);
    // AIR-17: offset wizard.
    int16_t off = 0;
    CHECK(build_offset(2100, 2450, off));
    CHECK_EQ(off, 350);
    CHECK(off > kOffsetWarnCenti);
    CHECK(build_offset(2100, 2000, off));
    CHECK_EQ(off, -100);
    CHECK(!build_offset(2100, 3500, off));
    CHECK(!build_offset(2100, 900, off));
}

// ------------------------------------------------------------- AIR-19 (glyphs)

void test_logo() {
    display::FrameBuffer fb;
    const display::Rgb c{10U, 20U, 30U};
    display::draw_air_logo_6(fb, 0, 1, c);
    uint32_t lit = 0U;
    for (uint8_t y = 0U; y < display::kHeight; ++y) {
        for (uint8_t x = 0U; x < display::kWidth; ++x) {
            const display::Rgb p = fb.get(x, y);
            if ((p.r | p.g | p.b) != 0U) {
                ++lit;
                CHECK((p.r == c.r) && (p.g == c.g) && (p.b == c.b));
                CHECK((x < 6U) && (y >= 1U) && (y < 7U));
            }
        }
    }
    CHECK_EQ(lit, display::kAirLogo6Pixels);
    fb.clear();
    display::draw_air_logo_8(fb, 24, 0, c);
    lit = 0U;
    for (uint8_t y = 0U; y < display::kHeight; ++y) {
        for (uint8_t x = 0U; x < display::kWidth; ++x) {
            const display::Rgb p = fb.get(x, y);
            if ((p.r | p.g | p.b) != 0U) {
                ++lit;
                CHECK(x >= 24U);
            }
        }
    }
    CHECK_EQ(lit, display::kAirLogo8Pixels);
    // Clipping: partly off-panel glyph draws only the visible part and never corrupts the buffer.
    fb.clear();
    display::draw_air_logo_8(fb, 28, -2, c);
    CHECK_EQ(fb.out_of_bounds_count(), 0U);
    // Value icons: every icon lights the announced number of pixels inside its 8x8 box; None lights nothing.
    for (uint8_t i = 0U; i < static_cast<uint8_t>(display::Icon::Count); ++i) {
        fb.clear();
        const display::Icon icon = static_cast<display::Icon>(i);
        display::draw_icon(fb, icon, 2, 0, c);
        uint32_t count = 0U;
        for (uint8_t y = 0U; y < display::kHeight; ++y) {
            for (uint8_t x = 0U; x < display::kWidth; ++x) {
                const display::Rgb p = fb.get(x, y);
                if ((p.r | p.g | p.b) != 0U) {
                    ++count;
                    CHECK((x >= 2U) && (x < 10U));
                }
            }
        }
        CHECK_EQ(count, display::icon_pixels(icon));
        if (i > 0U) {
            CHECK(count >= 8U);  // a recognisable glyph, not a dot
        }
    }
    CHECK_EQ(display::icon_pixels(display::Icon::Count), 0U);
    fb.clear();
    display::draw_icon(fb, display::Icon::Count, 0, 0, c);
    display::draw_icon(fb, display::Icon::Sun, 30, 6, c);  // clipped
    CHECK_EQ(fb.out_of_bounds_count(), 0U);
}

}  // namespace

void run_air_quality_tests() {
    test_aht21();
    test_ens160_codec();
    test_sensor_loop();
    test_levels();
    test_logo();
}
