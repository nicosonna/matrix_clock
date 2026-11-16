#pragma once
/**
 * @file sensor.hpp
 * @brief Non-blocking measurement loop for the ENS160 + AHT21 module.
 *
 * `step(now_ms)` performs **at most one** bus transaction per call and never
 * waits: the delays of the sensors (80 ms measurement, 10 ms init) are
 * timestamps. Call it every 10..100 ms from a low-priority task.
 *
 * Order of one measurement cycle (AIR-10): AHT21 trigger → read → ENS160
 * compensation written **before** the data is read → status → data if valid
 * and new. Everything the sensor declares invalid stays invalid in the
 * published reading (warm-up, initial start, fault): nothing is invented.
 *
 * Heater duty cycle (SensorConfig::heater_cycle, AIR-24): the ENS160 hot
 * plate is the main source of the module's self-heating. In this mode the
 * ENS160 is put in IDLE (heater off) as soon as one valid reading has been
 * captured, and woken up again every cycle_period_ms through the reference
 * start sequence (RESET, IDLE, clear GPR, STANDARD, 10 ms apart: on the
 * bench, IDLE -> STANDARD alone, and a bare RESET, left the sensor "normal"
 * without ever raising NEWDAT); while it warms up the
 * previous reading stays published (it is the last measurement, its age is
 * known), and a wake that never becomes valid within kCycleWarmupMaxMs goes
 * back to sleep anyway. The AHT21 keeps being read every period_ms, but
 * while the plate heats the published temperature / humidity stay frozen at
 * the last reading taken with the plate off (climate_cold, climate_ms): the
 * ENS160 compensation still gets the live reading, the clock does not show
 * the climb (AIR-25).
 */

#include <cstdint>

#include "air_quality/aht21.hpp"
#include "air_quality/ens160.hpp"
#include "air_quality/i2c_bus.hpp"

namespace air_quality {

/// Published measurement (every quantity carries its own validity).
struct AirReading {
    bool present;              ///< ENS160 answered with the right PART_ID.
    bool fault;                ///< Bus or sensor fault (3 consecutive failures or STATER).
    bool warming_up;           ///< VALIDITY_FLAG 1 (3 min after power-up).
    bool initial_start;        ///< VALIDITY_FLAG 2 (first hour of use).
    bool air_valid;            ///< AQI / TVOC / eCO2 usable.
    uint8_t aqi;               ///< 1..5.
    uint16_t tvoc_ppb;
    uint16_t eco2_ppm;
    bool climate_valid;        ///< Temperature / humidity usable.
    int16_t temperature_centi; ///< After the configured offset.
    uint16_t humidity_pct10;
    bool compensation_default; ///< AHT21 in fault: 25 °C / 50 % written instead.
    uint32_t updated_ms;       ///< Time of the last successful cycle.
    uint8_t bus_resets;        ///< Recoveries performed so far (diagnostics).
    uint8_t status_raw;        ///< Last DEVICE_STATUS byte (diagnostics).
    uint8_t opmode_raw;        ///< OPMODE read back after it was set (diagnostics).
    bool heater_on;            ///< ENS160 in STANDARD mode (hot plate on); false while idle in the duty cycle.
    bool climate_cold;         ///< Published climate was measured with the plate off (duty cycle only).
    uint32_t climate_ms;       ///< When the published climate was measured.
};

/// Loop configuration.
struct SensorConfig {
    uint8_t ens160_address;    ///< 0x52 or 0x53.
    int16_t temperature_offset_centi;  ///< Subtracted from the AHT21 reading (self-heating), -1000..1000.
    uint32_t period_ms;        ///< Cycle period, >= 1000.
    bool heater_cycle;         ///< true: ENS160 idle between captures (less self-heating, slower air data).
    uint32_t cycle_period_ms;  ///< Wake-to-wake period of the heater cycle, kCyclePeriodMinMs..kCyclePeriodMaxMs.
};

inline constexpr int16_t kOffsetMaxCenti = 1000;
inline constexpr uint32_t kPeriodMinMs = 1000U;
inline constexpr uint32_t kPeriodMaxMs = 60000U;
inline constexpr uint32_t kCyclePeriodMinMs = 5U * 60U * 1000U;
inline constexpr uint32_t kCyclePeriodMaxMs = 60U * 60U * 1000U;
inline constexpr uint32_t kCyclePeriodDefaultMs = 15U * 60U * 1000U;
/// Longest the heater stays on per wake when the sensor never declares its data valid
/// (the 3 min warm-up after the reset, plus margin).
inline constexpr uint32_t kCycleWarmupMaxMs = 4U * 60U * 1000U;
/// Settling time between the steps of the wake sequence (reset, idle, clear GPR, standard).
inline constexpr uint32_t kEns160WakeStepDelayMs = 20U;
inline constexpr uint8_t kConsecutiveFailuresMax = 3U;
/// A missing sensor is probed again this often.
inline constexpr uint32_t kProbeRetryMs = 60000U;

SensorConfig default_sensor_config() noexcept;
bool is_valid(const SensorConfig& c) noexcept;

class AirSensor {
public:
    explicit AirSensor(I2cBus& bus, const SensorConfig& config) noexcept;

    /// Applies a new configuration (hot).
    void configure(const SensorConfig& config) noexcept;

    /// Advances the state machine by at most one transaction.
    void step(uint32_t now_ms) noexcept;

    /// @return The latest published reading.
    const AirReading& reading() const noexcept { return reading_; }

    /// Diagnostics: name of the current state (English, for logs).
    const char* state_name() const noexcept;

private:
    enum class State : uint8_t {
        Probe,          ///< Read PART_ID.
        Absent,         ///< Wrong PART_ID / no answer: retry later.
        SetMode,        ///< Write OPMODE standard.
        ReadMode,       ///< Read OPMODE back (diagnostics).
        AhtCheck,       ///< Read AHT21 status.
        AhtInit,        ///< Init sent, wait 10 ms.
        Idle,           ///< Wait for the next period.
        AhtTrigger,     ///< Measurement triggered, wait 80 ms.
        AhtRead,        ///< Read the 7-byte frame.
        EnsCompensate,  ///< Write TEMP_IN / RH_IN.
        EnsStatus,      ///< Read DEVICE_STATUS.
        EnsData,        ///< Read AQI / TVOC / eCO2.
        SetIdle,        ///< Duty cycle: write OPMODE idle (heater off) until the next wake.
        Wake,           ///< Duty cycle wake: one step of the start sequence (reset, idle, clear GPR).
        WakeWait,       ///< Settling between two steps, then the next one or SetMode.
        Recover,        ///< Bus recovery after repeated failures.
    };

    void fail(uint32_t now_ms) noexcept;
    void publish_cycle(uint32_t now_ms) noexcept;

    I2cBus& bus_;
    SensorConfig config_;
    AirReading reading_;
    State state_;
    uint32_t wait_until_ms_;
    uint32_t last_cycle_ms_;
    uint8_t failures_;         ///< Consecutive bus failures.
    uint8_t aht_crc_failures_; ///< Consecutive CRC / busy rejections.
    uint8_t aht_busy_retries_;
    bool aht_ok_;              ///< This cycle's AHT21 reading is usable.
    bool seen_;                ///< The ENS160 answered at least once (NACK afterwards = fault).
    bool awake_;               ///< ENS160 in STANDARD mode as far as we know.
    bool captured_;            ///< A valid reading was taken since the last wake (duty cycle).
    bool have_cold_;           ///< A plate-off climate reading has been published (duty cycle).
    uint8_t wake_step_;        ///< Next step of the wake sequence (0 reset, 1 idle, 2 clear GPR).
    uint32_t wake_ms_;         ///< When the heater was last switched on (duty cycle).
    Aht21Reading aht_;
    Ens160Status status_;
};

}  // namespace air_quality
