#pragma once
/**
 * @file rules.hpp
 * @brief Local alert rules: sources, comparisons, hysteresis, confirmation, engine.
 *
 * Up to 16 rules evaluated at 1 Hz on what the clock knows (the 8 Home
 * Assistant values, the air sensor, the time, the links, the light sensor,
 * the current limiter). A rule whose source is **invalid** never raises
 * (EX-44): it is `Unavailable`, and an internal rule (`StaleValue`,
 * `HaLinkLost`, `TimeUnsure`...) says why. A raised rule holds a notification
 * in the queue (`regle_<i>`), replayed every `repeat_s`, and withdraws it
 * **by itself** when the condition falls: there is no acknowledgement
 * anywhere (EX-46, spec-alertes.md section 2).
 *
 * Pure logic: no ESP-IDF, no allocation, no float (fixed point in the
 * source's own decimals).
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include "alerts/queue.hpp"

namespace alerts {

inline constexpr std::size_t kRulesMax = 16U;
inline constexpr std::size_t kRuleNameMax = 16U;
inline constexpr std::size_t kTemplateMax = 64U;
inline constexpr std::size_t kValuesCount = 8U;      ///< Home Assistant values (mirrors homeassistant::kValuesMax).
inline constexpr std::size_t kUnitMax = 8U;
inline constexpr std::size_t kValueNameMax = 16U;
inline constexpr int32_t kThresholdMax = 2000000000;
inline constexpr uint8_t kDecimalsMax = 6U;
inline constexpr uint16_t kConfirmationMaxS = 3600U;
inline constexpr uint8_t kIconCount = 8U;            ///< Mirrors display::Icon::Count.

/// What a rule watches.
enum class Source : uint8_t {
    Value = 0U,             ///< One of the 8 Home Assistant values (index).
    TimeUnsure,             ///< SNTP never synchronised, or stale.
    HaLinkLost,             ///< WebSocket link down (only meaningful when the link is enabled).
    TokenRejected,          ///< auth_invalid: the token must be redone.
    NetworkLost,            ///< No IP.
    LeaseMissing,           ///< Associated without a DHCP lease.
    StaleValue,             ///< A tracked value is invalid or expired (index).
    LightSensorFault,       ///< Photoresistor fault (fallback brightness).
    CurrentLimited,         ///< The current ceiling is clipping.
    AirAqi,                 ///< UBA index 1..5.
    AirEco2,                ///< ppm.
    AirTvoc,                ///< ppb.
    AirTemperature,         ///< centi-degC, offset applied.
    AirHumidity,            ///< tenths of a percent.
    AirSensorFault,         ///< ENS160 absent or in fault.
    AirWarmingUp,           ///< ENS160 warming up or in its first hour.
    AirCompensationDefault, ///< AHT21 in fault: fallback compensation in use.
    Count,
};

enum class Comparison : uint8_t {
    Less = 0U,
    LessEq,
    Greater,
    GreaterEq,
    Equal,
    NotEqual,
    Change,   ///< The value differs from the previous valid one (one-shot notification).
    True,     ///< Boolean / internal sources.
    False,
    Count,
};

enum class RuleState : uint8_t {
    Inactive = 0U,   ///< Rule disabled.
    Watching,        ///< Source valid, condition false.
    Confirming,      ///< Condition true, waiting `confirmation_s`.
    Raised,          ///< Alert held in the queue.
    Unavailable,     ///< Source invalid: never raises.
};

struct Rule {
    bool enabled;
    char name[kRuleNameMax + 1U];
    Source source;
    uint8_t index;              ///< For Value and StaleValue.
    Comparison comparison;
    int32_t threshold;          ///< In the source's fixed point (`decimals`).
    int32_t hysteresis;         ///< >= 0, same fixed point; the gap to cross back before falling.
    uint8_t decimals;           ///< Fixed point of threshold / hysteresis / `{v}` (checked against the source).
    uint16_t confirmation_s;    ///< The condition must hold this long without a gap.
    uint8_t priority;           ///< 0..3
    char text[kTemplateMax + 1U];  ///< Template: `{n}` name, `{v}` value, `{u}` unit.
    bool color_set;             ///< false: the priority's default colour.
    uint8_t color[3];
    uint8_t icon;               ///< display::Icon index, 0 = none.
    uint16_t duration_s;        ///< One pass.
    uint16_t repeat_s;          ///< Between passes while raised.
    bool override_quiet;        ///< With priority 3: shown normally during quiet hours (if allowed).
};

using RuleList = std::array<Rule, kRulesMax>;

enum class RuleStatus : uint8_t {
    Ok = 0U,
    NameInvalid,
    SourceInvalid,
    IndexInvalid,
    ComparisonInvalid,     ///< Numeric comparison on a boolean source, or `vrai`/`faux` on a numeric one.
    ThresholdInvalid,
    HysteresisInvalid,
    DecimalsInvalid,
    ConfirmationInvalid,
    PriorityInvalid,
    TextInvalid,
    IconInvalid,
    DurationInvalid,
    RepeatInvalid,
};

/// @return true for sources evaluated as booleans (`vrai` / `faux` only).
bool source_is_boolean(Source s) noexcept;
/// @return true for sources that take a value index (0..7).
bool source_has_index(Source s) noexcept;
/// Fixed point of a source: the value's own decimals for Value, 2 for AirTemperature, 1 for AirHumidity, else 0.
uint8_t source_decimals(Source s, uint8_t value_decimals) noexcept;
/// Unit of the air sources ("C", "%", "ppm", "ppb"), "" otherwise (values carry their own).
const char* source_unit(Source s) noexcept;

const char* source_name(Source s) noexcept;
bool source_from_name(const char* name, Source& out) noexcept;
const char* comparison_name(Comparison c) noexcept;
bool comparison_from_name(const char* name, Comparison& out) noexcept;
const char* rule_state_name(RuleState s) noexcept;
/// French reason for the page, "" for Ok.
const char* rule_status_reason(RuleStatus s) noexcept;

/// A disabled rule with safe values (priority 1, 30 s passes every 300 s).
Rule default_rule() noexcept;
/// The rules of spec-alertes.md section 2 (internal ones active, value ones disabled).
RuleList default_rules() noexcept;
RuleStatus validate(const Rule& r) noexcept;
bool is_valid(const Rule& r) noexcept;
bool is_valid(const RuleList& l) noexcept;

/// Writes the queue key of rule @p index (`regle_<index>`); 0 if it does not fit.
std::size_t rule_key(std::size_t index, char* out, std::size_t size) noexcept;

/// One numeric sample: `valid` false means the source has nothing trustworthy to say.
struct Sample {
    bool valid;
    int32_t value;
};

/// Everything the engine looks at, filled by the application once per second.
struct Inputs {
    std::array<Sample, kValuesCount> values;
    std::array<bool, kValuesCount> value_enabled;
    std::array<uint8_t, kValuesCount> value_decimals;
    char value_units[kValuesCount][kUnitMax + 1U];
    bool time_sure;
    bool ha_enabled;
    bool ha_link_up;
    bool token_rejected;
    bool network_up;
    bool lease_missing;
    bool ldr_fault;
    bool current_limited;
    bool air_enabled;
    bool air_present;
    bool air_fault;
    bool air_warming_up;       ///< Warm-up or first hour.
    bool air_compensation_default;
    Sample air_aqi;            ///< valid only when the ENS160 says its data is usable.
    Sample air_eco2;
    Sample air_tvoc;
    Sample air_temperature;    ///< centi-degC.
    Sample air_humidity;       ///< tenths of a percent.
};

/// @return The sample of a source; booleans as 0/1, `valid` false when unavailable.
Sample sample(Source s, uint8_t index, const Inputs& in) noexcept;

/**
 * @brief Renders a template: `{n}` @p name, `{v}` the value in fixed point (`--` if invalid),
 *        `{u}` @p unit; an orphan `{` or an unknown placeholder is copied as is.
 * @return Length written, 0 if @p out is too small (emptied).
 */
std::size_t render_template(const char* tmpl, const char* name, bool valid, int32_t value, uint8_t decimals,
                            const char* unit, char* out, std::size_t size) noexcept;

/// Runtime of one rule, for the page.
struct RuleView {
    RuleState state;
    bool sample_valid;
    int32_t sample;
    uint32_t confirming_s;   ///< Seconds in Confirming so far.
    char text[kTemplateMax + 1U];   ///< Rendered text of the last step.
};

class RuleEngine {
public:
    RuleEngine() noexcept;

    /// New rules: every runtime is reset (nothing stays raised on a stale rule).
    void configure(const RuleList& rules) noexcept;

    /**
     * @brief One evaluation step (call at 1 Hz) and synchronisation with the queue:
     *        a rule that raises pushes its held notification, one that falls withdraws it.
     * @param in     Inputs of this second.
     * @param now_ms Milliseconds (wrap tolerated).
     * @param queue  The notification queue.
     */
    void step(const Inputs& in, uint32_t now_ms, NotificationQueue& queue) noexcept;

    RuleState state(std::size_t index) const noexcept;
    /// Fills @p out for the page; false if @p index is out of range.
    bool view(std::size_t index, RuleView& out) const noexcept;
    const RuleList& rules() const noexcept { return rules_; }
    /// Number of rules currently raised.
    std::size_t raised_count() const noexcept;

private:
    struct Runtime {
        RuleState state;
        uint32_t since_ms;       ///< Start of the confirmation.
        bool has_last;
        int32_t last;
        bool sample_valid;
        int32_t sample;
        bool pushed;             ///< Its notification is in the queue.
        char text[kTemplateMax + 1U];
    };

    bool condition(const Rule& r, const Runtime& rt, int32_t value) const noexcept;
    void render(std::size_t i, const Inputs& in, const Sample& s, char* out, std::size_t size) const noexcept;

    RuleList rules_;
    std::array<Runtime, kRulesMax> rt_;
};

}  // namespace alerts
