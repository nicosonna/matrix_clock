/**
 * @file sensor.cpp
 * @brief ENS160 + AHT21 measurement state machine.
 */
#include "air_quality/sensor.hpp"

namespace air_quality {

namespace {
constexpr uint32_t kAhtBusyRetryMs = 10U;
constexpr uint8_t kAhtBusyRetriesMax = 3U;

bool elapsed(uint32_t now, uint32_t until) noexcept {
    return static_cast<int32_t>(now - until) >= 0;
}
}  // namespace

SensorConfig default_sensor_config() noexcept {
    SensorConfig c{};
    c.ens160_address = kEns160AddressHigh;
    c.temperature_offset_centi = 0;
    c.period_ms = kPeriodMinMs;
    c.heater_cycle = false;
    c.cycle_period_ms = kCyclePeriodDefaultMs;
    return c;
}

bool is_valid(const SensorConfig& c) noexcept {
    const bool addr = (c.ens160_address == kEns160AddressHigh) || (c.ens160_address == kEns160AddressLow);
    const bool off = (c.temperature_offset_centi >= -kOffsetMaxCenti) && (c.temperature_offset_centi <= kOffsetMaxCenti);
    const bool per = (c.period_ms >= kPeriodMinMs) && (c.period_ms <= kPeriodMaxMs);
    const bool cyc = (c.cycle_period_ms >= kCyclePeriodMinMs) && (c.cycle_period_ms <= kCyclePeriodMaxMs);
    return addr && off && per && cyc;
}

AirSensor::AirSensor(I2cBus& bus, const SensorConfig& config) noexcept
    : bus_(bus), config_(is_valid(config) ? config : default_sensor_config()), reading_{}, state_(State::Probe),
      wait_until_ms_(0U), last_cycle_ms_(0U), failures_(0U), aht_crc_failures_(0U), aht_busy_retries_(0U),
      aht_ok_(false), seen_(false), awake_(false), captured_(false), have_cold_(false), wake_step_(0U), wake_ms_(0U), aht_{}, status_{} {}

void AirSensor::configure(const SensorConfig& config) noexcept {
    if (is_valid(config)) {
        const bool address_changed = config.ens160_address != config_.ens160_address;
        const bool cycle_off = config_.heater_cycle && !config.heater_cycle;
        const bool cycle_on = !config_.heater_cycle && config.heater_cycle;
        config_ = config;
        if (address_changed) {
            state_ = State::Probe;
            wait_until_ms_ = 0U;
        } else if (cycle_off && !awake_ && reading_.present && (state_ == State::Idle)) {
            state_ = State::SetMode;  // continuous mode again: heater back on now
        }
        if (cycle_off) {
            have_cold_ = false;
            reading_.climate_cold = false;
        } else if (cycle_on && awake_) {
            captured_ = false;        // the running wake ends at the next valid reading (or the cap)
        }
    }
}

const char* AirSensor::state_name() const noexcept {
    switch (state_) {
        case State::Probe: return "probe";
        case State::Absent: return "absent";
        case State::SetMode: return "set_mode";
        case State::ReadMode: return "read_mode";
        case State::AhtCheck: return "aht_check";
        case State::AhtInit: return "aht_init";
        case State::Idle: return "idle";
        case State::AhtTrigger: return "aht_trigger";
        case State::AhtRead: return "aht_read";
        case State::EnsCompensate: return "ens_compensate";
        case State::EnsStatus: return "ens_status";
        case State::EnsData: return "ens_data";
        case State::SetIdle: return "set_idle";
        case State::Wake: return "wake";
        case State::WakeWait: return "wake_wait";
        case State::Recover: return "recover";
        default: return "?";
    }
}

void AirSensor::fail(uint32_t now_ms) noexcept {
    if (failures_ < 255U) {
        ++failures_;
    }
    if (failures_ >= kConsecutiveFailuresMax) {
        reading_.fault = true;
        reading_.air_valid = false;
        reading_.climate_valid = false;
        reading_.warming_up = false;
        reading_.initial_start = false;
        state_ = State::Recover;
    } else {
        state_ = State::Idle;  // retry next period
    }
    wait_until_ms_ = now_ms + config_.period_ms;
}

void AirSensor::publish_cycle(uint32_t now_ms) noexcept {
    failures_ = 0U;
    reading_.present = true;
    reading_.fault = status_.error;
    reading_.warming_up = status_.validity == Ens160Validity::WarmUp;
    reading_.initial_start = status_.validity == Ens160Validity::InitialStart;
    if (status_.error || (status_.validity == Ens160Validity::Invalid)) {
        reading_.fault = true;
        reading_.air_valid = false;
    }
    reading_.updated_ms = now_ms;
}

void AirSensor::step(uint32_t now_ms) noexcept {
    switch (state_) {
        case State::Probe: {
            if (!elapsed(now_ms, wait_until_ms_)) {
                return;
            }
            const uint8_t reg = kEns160RegPartId;
            uint8_t id[2] = {};
            const bool answered = bus_.write_read(config_.ens160_address, &reg, 1U, id, 2U);
            if (!answered && seen_) {
                // A sensor that used to answer and now NACKs is a bus problem, not an absence.
                fail(now_ms);
                return;
            }
            if (!answered || !ens160_part_id_ok(id)) {
                const uint8_t resets = reading_.bus_resets;
                reading_ = AirReading{};
                reading_.bus_resets = resets;
                have_cold_ = false;
                state_ = State::Absent;
                wait_until_ms_ = now_ms + kProbeRetryMs;
                return;
            }
            seen_ = true;
            reading_.present = true;
            wake_step_ = 0U;
            state_ = config_.heater_cycle ? State::Wake : State::SetMode;
            return;
        }
        case State::Absent:
            if (elapsed(now_ms, wait_until_ms_)) {
                state_ = State::Probe;
            }
            return;
        case State::SetMode: {
            const uint8_t cmd[2] = {kEns160RegOpMode, kEns160OpModeStandard};
            if (!bus_.write(config_.ens160_address, cmd, 2U)) {
                fail(now_ms);
                return;
            }
            awake_ = true;
            captured_ = false;
            wake_ms_ = now_ms;
            reading_.heater_on = true;
            state_ = State::ReadMode;
            return;
        }
        case State::ReadMode: {
            const uint8_t reg = kEns160RegOpMode;
            uint8_t mode = 0xFFU;
            if (!bus_.write_read(config_.ens160_address, &reg, 1U, &mode, 1U)) {
                fail(now_ms);
                return;
            }
            reading_.opmode_raw = mode;
            state_ = State::AhtCheck;
            return;
        }
        case State::AhtCheck: {
            const uint8_t cmd = kAht21StatusCommand;
            uint8_t status = 0U;
            if (!bus_.write_read(kAht21Address, &cmd, 1U, &status, 1U)) {
                // The AHT21 may be absent while the ENS160 works: fallback compensation.
                aht_ok_ = false;
                reading_.compensation_default = true;
                reading_.climate_valid = false;
                state_ = State::Idle;
                wait_until_ms_ = now_ms;
                return;
            }
            if ((status & kAht21StatusCalibrated) == 0U) {
                if (!bus_.write(kAht21Address, kAht21InitCommand, 3U)) {
                    fail(now_ms);
                    return;
                }
                state_ = State::AhtInit;
                wait_until_ms_ = now_ms + kAht21InitDelayMs;
                return;
            }
            state_ = State::Idle;
            wait_until_ms_ = now_ms;
            return;
        }
        case State::AhtInit:
            if (elapsed(now_ms, wait_until_ms_)) {
                state_ = State::Idle;
            }
            return;
        case State::Idle:
            if (!elapsed(now_ms, wait_until_ms_)) {
                return;
            }
            last_cycle_ms_ = now_ms;
            if (!bus_.write(kAht21Address, kAht21MeasureCommand, 3U)) {
                // AHT21 unreachable: skip it this cycle, keep the ENS160 going.
                aht_ok_ = false;
                state_ = State::EnsCompensate;
                return;
            }
            aht_busy_retries_ = 0U;
            state_ = State::AhtTrigger;
            wait_until_ms_ = now_ms + kAht21MeasureDelayMs;
            return;
        case State::AhtTrigger:
            if (elapsed(now_ms, wait_until_ms_)) {
                state_ = State::AhtRead;
            }
            return;
        case State::AhtRead: {
            uint8_t frame[kAht21FrameSize] = {};
            if (!bus_.read(kAht21Address, frame, kAht21FrameSize)) {
                aht_ok_ = false;
                state_ = State::EnsCompensate;
                return;
            }
            Aht21Reading r{};
            switch (aht21_decode(frame, r)) {
                case Aht21Status::Ok:
                    aht_ok_ = true;
                    aht_crc_failures_ = 0U;
                    aht_ = r;
                    break;
                case Aht21Status::Busy:
                    if (aht_busy_retries_ < kAhtBusyRetriesMax) {
                        ++aht_busy_retries_;
                        state_ = State::AhtTrigger;
                        wait_until_ms_ = now_ms + kAhtBusyRetryMs;
                        return;
                    }
                    aht_ok_ = false;
                    break;
                case Aht21Status::NotCalibrated:
                    aht_ok_ = false;
                    if (bus_.write(kAht21Address, kAht21InitCommand, 3U)) {
                        // Re-initialised; this cycle goes on without a climate reading.
                    }
                    break;
                case Aht21Status::CrcError:
                default:
                    aht_ok_ = false;
                    if (aht_crc_failures_ < 255U) {
                        ++aht_crc_failures_;
                    }
                    break;
            }
            state_ = State::EnsCompensate;
            return;
        }
        case State::EnsCompensate: {
            if (config_.heater_cycle && !awake_) {
                // Heater off: publish the climate only, wake the ENS160 when its period is due.
                if (aht_ok_ && (aht_crc_failures_ < kConsecutiveFailuresMax)) {
                    reading_.climate_valid = true;
                    reading_.temperature_centi =
                        static_cast<int16_t>(static_cast<int32_t>(aht_.temperature_centi) - config_.temperature_offset_centi);
                    reading_.humidity_pct10 = aht_.humidity_pct10;
                    reading_.compensation_default = false;
                    reading_.climate_cold = true;
                    reading_.climate_ms = now_ms;
                    have_cold_ = true;
                } else {
                    reading_.climate_valid = false;
                    reading_.climate_cold = false;
                    have_cold_ = false;
                }
                reading_.updated_ms = now_ms;
                failures_ = 0U;
                if (elapsed(now_ms, wake_ms_ + config_.cycle_period_ms)) {
                    wake_step_ = 0U;
                    state_ = State::Wake;  // full start sequence: STANDARD alone leaves the sensor silent
                    return;
                }
                state_ = State::Idle;
                wait_until_ms_ = last_cycle_ms_ + config_.period_ms;
                return;
            }
            int16_t t = kFallbackTemperatureCenti;
            uint16_t h = kFallbackHumidityPct10;
            const bool aht_fault = aht_crc_failures_ >= kConsecutiveFailuresMax;
            // Duty cycle: while the plate heats, the clock keeps showing the last
            // plate-off reading; the ENS160 compensation still gets the live one.
            const bool freeze = config_.heater_cycle && have_cold_ && reading_.climate_valid;
            if (aht_ok_ && !aht_fault) {
                const int32_t corrected = static_cast<int32_t>(aht_.temperature_centi) - config_.temperature_offset_centi;
                t = static_cast<int16_t>(corrected);
                h = aht_.humidity_pct10;
                if (!freeze) {
                    reading_.climate_valid = true;
                    reading_.temperature_centi = t;
                    reading_.humidity_pct10 = h;
                    reading_.climate_cold = false;
                    reading_.climate_ms = now_ms;
                }
                reading_.compensation_default = false;
            } else if (!freeze) {
                reading_.climate_valid = false;
                reading_.climate_cold = false;
                reading_.compensation_default = true;
            } else {
                reading_.compensation_default = true;  // the ENS160 gets the fallback, the frozen value stays
            }
            uint8_t cmd[kEns160CompensationSize];
            ens160_encode_compensation(t, h, cmd);
            if (!bus_.write(config_.ens160_address, cmd, kEns160CompensationSize)) {
                fail(now_ms);
                return;
            }
            state_ = State::EnsStatus;
            return;
        }
        case State::EnsStatus: {
            const uint8_t reg = kEns160RegDeviceStatus;
            uint8_t status = 0U;
            if (!bus_.write_read(config_.ens160_address, &reg, 1U, &status, 1U)) {
                fail(now_ms);
                return;
            }
            status_ = ens160_decode_status(status);
            reading_.status_raw = status;
            publish_cycle(now_ms);
            if ((status_.validity == Ens160Validity::Normal) && !status_.error) {
                if (status_.new_data) {
                    state_ = State::EnsData;
                    return;
                }
                // No new sample: the previous values stay valid only if they existed (AIR-03).
            } else if (!(config_.heater_cycle && reading_.air_valid && !status_.error)) {
                // Duty cycle: a warm-up after a wake keeps the previous (last measured) values.
                reading_.air_valid = false;
            }
            if (config_.heater_cycle && awake_ && !captured_ && elapsed(now_ms, wake_ms_ + kCycleWarmupMaxMs)) {
                state_ = State::SetIdle;  // never became valid: stop heating anyway
                return;
            }
            state_ = State::Idle;
            wait_until_ms_ = last_cycle_ms_ + config_.period_ms;
            return;
        }
        case State::EnsData: {
            const uint8_t reg = kEns160RegDataAqi;
            uint8_t bytes[kEns160DataSize] = {};
            if (!bus_.write_read(config_.ens160_address, &reg, 1U, bytes, kEns160DataSize)) {
                fail(now_ms);
                return;
            }
            const Ens160Data d = ens160_decode_data(bytes);
            const bool good = (d.aqi >= 1U) && (d.aqi <= 5U);
            // Duty cycle: right after the wake sequence the sensor can flag stale "new data"
            // over registers the reset has cleared (AQI 0): keep the previous reading then.
            if (good || !config_.heater_cycle) {
                reading_.aqi = d.aqi;
                reading_.tvoc_ppb = d.tvoc_ppb;
                reading_.eco2_ppm = d.eco2_ppm;
                reading_.air_valid = good;
            }
            if (config_.heater_cycle && good) {
                captured_ = true;
                state_ = State::SetIdle;
                return;
            }
            state_ = State::Idle;
            wait_until_ms_ = last_cycle_ms_ + config_.period_ms;
            return;
        }
        case State::Wake: {
            // Reference start sequence, one write per step: reset, idle, clear GPR; then SetMode (standard).
            uint8_t cmd[2] = {kEns160RegOpMode, kEns160OpModeReset};
            if (wake_step_ == 1U) {
                cmd[1] = kEns160OpModeIdle;
            } else if (wake_step_ >= 2U) {
                cmd[0] = kEns160RegCommand;
                cmd[1] = kEns160CommandClearGpr;
            }
            if (!bus_.write(config_.ens160_address, cmd, 2U)) {
                fail(now_ms);
                return;
            }
            state_ = State::WakeWait;
            wait_until_ms_ = now_ms + kEns160WakeStepDelayMs;
            return;
        }
        case State::WakeWait:
            if (elapsed(now_ms, wait_until_ms_)) {
                if (wake_step_ >= 2U) {
                    state_ = State::SetMode;
                } else {
                    ++wake_step_;
                    state_ = State::Wake;
                }
            }
            return;
        case State::SetIdle: {
            const uint8_t cmd[2] = {kEns160RegOpMode, kEns160OpModeIdle};
            if (!bus_.write(config_.ens160_address, cmd, 2U)) {
                fail(now_ms);
                return;
            }
            awake_ = false;
            reading_.heater_on = false;
            reading_.opmode_raw = kEns160OpModeIdle;
            state_ = State::Idle;
            wait_until_ms_ = last_cycle_ms_ + config_.period_ms;
            return;
        }
        case State::Recover:
        default:
            if (!elapsed(now_ms, wait_until_ms_)) {
                return;
            }
            if (reading_.bus_resets < 255U) {
                ++reading_.bus_resets;
            }
            (void)bus_.recover();
            failures_ = 0U;
            state_ = State::Probe;
            return;
    }
}

}  // namespace air_quality
