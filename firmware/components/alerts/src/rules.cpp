/**
 * @file rules.cpp
 * @brief Rule engine (see rules.hpp).
 */
#include "alerts/rules.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace alerts {

namespace {

constexpr uint32_t kMsPerS = 1000U;
constexpr uint16_t kDurationDefaultRule = 30U;
constexpr uint16_t kRepeatDefaultRule = 300U;
constexpr int32_t kAirAqiVentilate = 4;
constexpr int32_t kAirAqiBad = 5;
constexpr int32_t kFrostDeci = 0;
constexpr int32_t kFrostHysteresisDeci = 5;
constexpr int32_t kHeatDeci = 300;
constexpr int32_t kHeatHysteresisDeci = 10;
constexpr int32_t kEco2HighPpm = 1500;
constexpr int32_t kHumidityHighPct10 = 700;
constexpr int32_t kHumidityHysteresisPct10 = 50;

bool printable(const char* s, std::size_t max) noexcept {
    std::size_t n = 0U;
    for (; s[n] != '\0'; ++n) {
        const unsigned char c = static_cast<unsigned char>(s[n]);
        if ((c < 0x20U) || (c == 0x7FU) || (n >= max)) {
            return false;
        }
    }
    return n <= max;
}

/// Writes @p v in fixed point (@p decimals digits after the point); 0 if it does not fit.
std::size_t write_fixed(int32_t v, uint8_t decimals, char* out, std::size_t size) noexcept {
    int32_t div = 1;
    for (uint8_t i = 0U; i < decimals; ++i) {
        div *= 10;
    }
    const bool negative = v < 0;
    const uint32_t a = negative ? static_cast<uint32_t>(-static_cast<int64_t>(v)) : static_cast<uint32_t>(v);
    const uint32_t d = static_cast<uint32_t>(div);
    int n;
    if (decimals == 0U) {
        n = std::snprintf(out, size, "%s%" PRIu32, negative ? "-" : "", a);
    } else {
        n = std::snprintf(out, size, "%s%" PRIu32 ".%0*" PRIu32, negative ? "-" : "", a / d, static_cast<int>(decimals), a % d);
    }
    if ((n <= 0) || (static_cast<std::size_t>(n) >= size)) {
        if (size > 0U) {
            out[0] = '\0';
        }
        return 0U;
    }
    return static_cast<std::size_t>(n);
}

Rule make(const char* name, Source source, Comparison cmp, int32_t threshold, int32_t hysteresis, uint8_t decimals,
          uint16_t confirmation_s, uint8_t priority, const char* text, bool enabled) noexcept {
    Rule r = default_rule();
    r.enabled = enabled;
    std::strncpy(r.name, name, kRuleNameMax);
    r.source = source;
    r.comparison = cmp;
    r.threshold = threshold;
    r.hysteresis = hysteresis;
    r.decimals = decimals;
    r.confirmation_s = confirmation_s;
    r.priority = priority;
    std::strncpy(r.text, text, kTemplateMax);
    return r;
}

}  // namespace

// ------------------------------------------------------------------ sources and names

bool source_is_boolean(Source s) noexcept {
    switch (s) {
        case Source::Value:
        case Source::AirAqi:
        case Source::AirEco2:
        case Source::AirTvoc:
        case Source::AirTemperature:
        case Source::AirHumidity:
            return false;
        default:
            return true;
    }
}

bool source_has_index(Source s) noexcept {
    return (s == Source::Value) || (s == Source::StaleValue);
}

uint8_t source_decimals(Source s, uint8_t value_decimals) noexcept {
    switch (s) {
        case Source::Value: return value_decimals;
        case Source::AirTemperature: return 2U;
        case Source::AirHumidity: return 1U;
        default: return 0U;
    }
}

const char* source_unit(Source s) noexcept {
    switch (s) {
        case Source::AirTemperature: return "C";
        case Source::AirHumidity: return "%";
        case Source::AirEco2: return "ppm";
        case Source::AirTvoc: return "ppb";
        default: return "";
    }
}

namespace {
constexpr const char* kSourceNames[static_cast<std::size_t>(Source::Count)] = {
    "Valeur", "HeureNonSure", "LiaisonHaPerdue", "JetonRefuse", "ReseauPerdu", "BailAbsent", "DonneePerimee",
    "CapteurLumDefaut", "EcretageCourant", "AirAqi", "AirEco2", "AirTvoc", "AirTemperature", "AirHumidite",
    "AirCapteurDefaut", "AirPrechauffage", "AirCompensationDefaut"};
constexpr const char* kComparisonNames[static_cast<std::size_t>(Comparison::Count)] = {
    "<", "<=", ">", ">=", "==", "!=", "change", "vrai", "faux"};
}  // namespace

const char* source_name(Source s) noexcept {
    const std::size_t i = static_cast<std::size_t>(s);
    return (i < static_cast<std::size_t>(Source::Count)) ? kSourceNames[i] : "?";
}

bool source_from_name(const char* name, Source& out) noexcept {
    if (name == nullptr) {
        return false;
    }
    for (std::size_t i = 0U; i < static_cast<std::size_t>(Source::Count); ++i) {
        if (std::strcmp(kSourceNames[i], name) == 0) {
            out = static_cast<Source>(i);
            return true;
        }
    }
    return false;
}

const char* comparison_name(Comparison c) noexcept {
    const std::size_t i = static_cast<std::size_t>(c);
    return (i < static_cast<std::size_t>(Comparison::Count)) ? kComparisonNames[i] : "?";
}

bool comparison_from_name(const char* name, Comparison& out) noexcept {
    if (name == nullptr) {
        return false;
    }
    for (std::size_t i = 0U; i < static_cast<std::size_t>(Comparison::Count); ++i) {
        if (std::strcmp(kComparisonNames[i], name) == 0) {
            out = static_cast<Comparison>(i);
            return true;
        }
    }
    return false;
}

const char* rule_state_name(RuleState s) noexcept {
    switch (s) {
        case RuleState::Watching: return "surveille";
        case RuleState::Confirming: return "confirmation";
        case RuleState::Raised: return "levee";
        case RuleState::Unavailable: return "indisponible";
        case RuleState::Inactive:
        default: return "inactive";
    }
}

const char* rule_status_reason(RuleStatus s) noexcept {
    switch (s) {
        case RuleStatus::NameInvalid: return "16 caractères imprimables au plus";
        case RuleStatus::SourceInvalid: return "source inconnue";
        case RuleStatus::IndexInvalid: return "valeur 0 à 7 (index 0 pour les autres sources)";
        case RuleStatus::ComparisonInvalid: return "vrai/faux pour une source booléenne, une comparaison numérique sinon";
        case RuleStatus::ThresholdInvalid: return "seuil hors de ±2 000 000 000";
        case RuleStatus::HysteresisInvalid: return "hystérésis positive ou nulle";
        case RuleStatus::DecimalsInvalid: return "décimales 0 à 6, celles de la source";
        case RuleStatus::ConfirmationInvalid: return "de 0 à 3600 s";
        case RuleStatus::PriorityInvalid: return "de 0 à 3";
        case RuleStatus::TextInvalid: return "64 caractères au plus";
        case RuleStatus::IconInvalid: return "icône inconnue";
        case RuleStatus::DurationInvalid: return "de 1 à 3600 s";
        case RuleStatus::RepeatInvalid: return "de 10 à 3600 s";
        case RuleStatus::Ok:
        default: return "";
    }
}

// ------------------------------------------------------------------ rules

Rule default_rule() noexcept {
    Rule r{};
    r.enabled = false;
    r.name[0] = '\0';
    r.source = Source::Value;
    r.index = 0U;
    r.comparison = Comparison::Greater;
    r.threshold = 0;
    r.hysteresis = 0;
    r.decimals = 0U;
    r.confirmation_s = 0U;
    r.priority = kPriorityDefault;
    r.text[0] = '\0';
    r.color_set = false;
    r.color[0] = 0U;
    r.color[1] = 0U;
    r.color[2] = 0U;
    r.icon = 0U;
    r.duration_s = kDurationDefaultRule;
    r.repeat_s = kRepeatDefaultRule;
    r.override_quiet = false;
    return r;
}

RuleList default_rules() noexcept {
    RuleList l{};
    for (Rule& r : l) {
        r = default_rule();
    }
    l[0] = make("GEL", Source::Value, Comparison::Less, kFrostDeci, kFrostHysteresisDeci, 1U, 300U, 1U, "GEL {v}{u}", false);
    l[1] = make("CANICULE", Source::Value, Comparison::Greater, kHeatDeci, kHeatHysteresisDeci, 1U, 600U, 1U, "{v}{u}", false);
    l[2] = make("HEURE", Source::TimeUnsure, Comparison::True, 0, 0, 0U, 120U, 2U, "HEURE NON SURE", true);
    l[3] = make("DOMOTIQUE", Source::HaLinkLost, Comparison::True, 0, 0, 0U, 300U, 1U, "HOME ASSISTANT HS", true);
    l[4] = make("JETON", Source::TokenRejected, Comparison::True, 0, 0, 0U, 0U, 2U, "JETON A REFAIRE", true);
    l[5] = make("DHCP", Source::LeaseMissing, Comparison::True, 0, 0, 0U, 1800U, 2U, "PAS D ADRESSE IP", true);
    l[6] = make("COURANT", Source::CurrentLimited, Comparison::True, 0, 0, 0U, 60U, 0U, "BUDGET COURANT SERRE", true);
    l[7] = make("AERER", Source::AirAqi, Comparison::GreaterEq, kAirAqiVentilate, 0, 0U, 120U, 1U, "AERER", true);
    l[8] = make("AIR MAUVAIS", Source::AirAqi, Comparison::GreaterEq, kAirAqiBad, 0, 0U, 60U, 2U, "AIR MAUVAIS", true);
    l[9] = make("AIR CAPTEUR", Source::AirSensorFault, Comparison::True, 0, 0, 0U, 300U, 1U, "CAPTEUR AIR HS", true);
    l[10] = make("CO2", Source::AirEco2, Comparison::GreaterEq, kEco2HighPpm, 0, 0U, 120U, 1U, "CO2 {v} PPM", false);
    l[11] = make("HUMIDITE", Source::AirHumidity, Comparison::Greater, kHumidityHighPct10, kHumidityHysteresisPct10, 1U, 600U, 0U,
                 "HUMIDITE {v}{u}", false);
    return l;
}

RuleStatus validate(const Rule& r) noexcept {
    if ((r.name[kRuleNameMax] != '\0') || !printable(r.name, kRuleNameMax)) {
        return RuleStatus::NameInvalid;
    }
    if (static_cast<uint8_t>(r.source) >= static_cast<uint8_t>(Source::Count)) {
        return RuleStatus::SourceInvalid;
    }
    if (source_has_index(r.source) ? (r.index >= kValuesCount) : (r.index != 0U)) {
        return RuleStatus::IndexInvalid;
    }
    if (static_cast<uint8_t>(r.comparison) >= static_cast<uint8_t>(Comparison::Count)) {
        return RuleStatus::ComparisonInvalid;
    }
    const bool boolean_cmp = (r.comparison == Comparison::True) || (r.comparison == Comparison::False);
    if (source_is_boolean(r.source) ? !boolean_cmp : boolean_cmp) {
        return RuleStatus::ComparisonInvalid;
    }
    if ((r.threshold > kThresholdMax) || (r.threshold < -kThresholdMax)) {
        return RuleStatus::ThresholdInvalid;
    }
    if ((r.hysteresis < 0) || (r.hysteresis > kThresholdMax)) {
        return RuleStatus::HysteresisInvalid;
    }
    if (r.decimals > kDecimalsMax) {
        return RuleStatus::DecimalsInvalid;
    }
    if (r.confirmation_s > kConfirmationMaxS) {
        return RuleStatus::ConfirmationInvalid;
    }
    if (r.priority > kPriorityMax) {
        return RuleStatus::PriorityInvalid;
    }
    if ((r.text[kTemplateMax] != '\0') || !printable(r.text, kTemplateMax)) {
        return RuleStatus::TextInvalid;
    }
    if (r.icon >= kIconCount) {
        return RuleStatus::IconInvalid;
    }
    if ((r.duration_s < kDurationMin) || (r.duration_s > kDurationMax)) {
        return RuleStatus::DurationInvalid;
    }
    if ((r.repeat_s < kRepeatMin) || (r.repeat_s > kRepeatMax)) {
        return RuleStatus::RepeatInvalid;
    }
    return RuleStatus::Ok;
}

bool is_valid(const Rule& r) noexcept {
    return validate(r) == RuleStatus::Ok;
}

bool is_valid(const RuleList& l) noexcept {
    for (const Rule& r : l) {
        if (!is_valid(r)) {
            return false;
        }
    }
    return true;
}

std::size_t rule_key(std::size_t index, char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (index >= kRulesMax)) {
        return 0U;
    }
    const int n = std::snprintf(out, size, "regle_%u", static_cast<unsigned>(index));
    if ((n <= 0) || (static_cast<std::size_t>(n) >= size)) {
        if (size > 0U) {
            out[0] = '\0';
        }
        return 0U;
    }
    return static_cast<std::size_t>(n);
}

// ------------------------------------------------------------------ sampling

namespace {
Sample boolean(bool valid, bool value) noexcept {
    return Sample{valid, value ? 1 : 0};
}
}  // namespace

Sample sample(Source s, uint8_t index, const Inputs& in) noexcept {
    const bool idx_ok = index < kValuesCount;
    const bool air_sensor = in.air_enabled && in.air_present && !in.air_fault;
    switch (s) {
        case Source::Value:
            return (idx_ok && in.value_enabled[index]) ? in.values[index] : Sample{false, 0};
        case Source::TimeUnsure:
            return boolean(true, !in.time_sure);
        case Source::HaLinkLost:
            return boolean(in.ha_enabled, !in.ha_link_up);
        case Source::TokenRejected:
            return boolean(in.ha_enabled, in.token_rejected);
        case Source::NetworkLost:
            return boolean(true, !in.network_up);
        case Source::LeaseMissing:
            return boolean(true, in.lease_missing);
        case Source::StaleValue:
            return boolean(idx_ok && in.ha_enabled && in.value_enabled[index], idx_ok && !in.values[index].valid);
        case Source::LightSensorFault:
            return boolean(true, in.ldr_fault);
        case Source::CurrentLimited:
            return boolean(true, in.current_limited);
        case Source::AirAqi:
            return air_sensor ? in.air_aqi : Sample{false, 0};
        case Source::AirEco2:
            return air_sensor ? in.air_eco2 : Sample{false, 0};
        case Source::AirTvoc:
            return air_sensor ? in.air_tvoc : Sample{false, 0};
        case Source::AirTemperature:
            return air_sensor ? in.air_temperature : Sample{false, 0};
        case Source::AirHumidity:
            return air_sensor ? in.air_humidity : Sample{false, 0};
        case Source::AirSensorFault:
            return boolean(in.air_enabled, !in.air_present || in.air_fault);
        case Source::AirWarmingUp:
            return boolean(air_sensor, in.air_warming_up);
        case Source::AirCompensationDefault:
            return boolean(air_sensor, in.air_compensation_default);
        case Source::Count:
        default:
            return Sample{false, 0};
    }
}

// ------------------------------------------------------------------ template

std::size_t render_template(const char* tmpl, const char* name, bool valid, int32_t value, uint8_t decimals,
                            const char* unit, char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (size == 0U)) {
        return 0U;
    }
    if (tmpl == nullptr) {
        out[0] = '\0';
        return 0U;
    }
    std::size_t w = 0U;
    bool ok = true;
    auto put = [&](const char* s) noexcept {
        if (!ok || (s == nullptr)) {
            return;
        }
        const std::size_t n = std::strlen(s);
        if ((w + n) >= size) {
            ok = false;
            return;
        }
        std::memcpy(out + w, s, n);
        w += n;
    };
    for (std::size_t i = 0U; ok && (tmpl[i] != '\0'); ++i) {
        const char c = tmpl[i];
        if ((c == '{') && (tmpl[i + 1U] != '\0') && (tmpl[i + 2U] == '}')) {
            const char p = tmpl[i + 1U];
            if (p == 'n') {
                put(name);
                i += 2U;
                continue;
            }
            if (p == 'u') {
                put(unit);
                i += 2U;
                continue;
            }
            if (p == 'v') {
                if (valid) {
                    char num[16];
                    if (write_fixed(value, decimals, num, sizeof(num)) == 0U) {
                        ok = false;
                    }
                    put(num);
                } else {
                    put("--");
                }
                i += 2U;
                continue;
            }
            // unknown placeholder: copied as is
        }
        if ((w + 1U) >= size) {
            ok = false;
            break;
        }
        out[w++] = c;
    }
    if (!ok) {
        out[0] = '\0';
        return 0U;
    }
    out[w] = '\0';
    return w;
}

// ------------------------------------------------------------------ engine

RuleEngine::RuleEngine() noexcept : rules_(default_rules()), rt_{} {
    for (Runtime& r : rt_) {
        r.state = RuleState::Inactive;
    }
}

void RuleEngine::configure(const RuleList& rules) noexcept {
    rules_ = rules;
    for (std::size_t i = 0U; i < kRulesMax; ++i) {
        if (!is_valid(rules_[i])) {
            rules_[i] = default_rule();
        }
        rt_[i] = Runtime{};
        rt_[i].state = RuleState::Inactive;
    }
}

bool RuleEngine::condition(const Rule& r, const Runtime& rt, int32_t v) const noexcept {
    const int64_t t = r.threshold;
    const int64_t h = r.hysteresis;
    const bool raised = rt.state == RuleState::Raised;
    switch (r.comparison) {
        case Comparison::Less: return raised ? (v < (t + h)) : (v < t);
        case Comparison::LessEq: return raised ? (v <= (t + h)) : (v <= t);
        case Comparison::Greater: return raised ? (v > (t - h)) : (v > t);
        case Comparison::GreaterEq: return raised ? (v >= (t - h)) : (v >= t);
        case Comparison::Equal: {
            const int64_t gap = (v > t) ? (v - t) : (t - v);
            return raised ? (gap <= h) : (v == t);
        }
        case Comparison::NotEqual: return v != t;
        case Comparison::Change: return rt.has_last && (v != rt.last);
        case Comparison::True: return v != 0;
        case Comparison::False: return v == 0;
        case Comparison::Count:
        default: return false;
    }
}

void RuleEngine::render(std::size_t i, const Inputs& in, const Sample& s, char* out, std::size_t size) const noexcept {
    const Rule& r = rules_[i];
    const char* unit = (r.source == Source::Value) ? in.value_units[r.index] : source_unit(r.source);
    const bool boolean = source_is_boolean(r.source);
    const char* tmpl = (r.text[0] != '\0') ? r.text : r.name;
    if (boolean) {
        (void)render_template(tmpl, r.name, false, 0, 0U, unit, out, size);
        // `{v}` on a boolean source is "--": nothing numeric to say.
        return;
    }
    if (render_template(tmpl, r.name, s.valid, s.value, r.decimals, unit, out, size) == 0U) {
        std::strncpy(out, r.name, size - 1U);
        out[size - 1U] = '\0';
    }
}

void RuleEngine::step(const Inputs& in, uint32_t now_ms, NotificationQueue& queue) noexcept {
    char key[kKeyMax + 1U];
    for (std::size_t i = 0U; i < kRulesMax; ++i) {
        const Rule& r = rules_[i];
        Runtime& rt = rt_[i];
        if (!r.enabled) {
            rt.state = RuleState::Inactive;
            rt.has_last = false;
            rt.sample_valid = false;
        } else {
            const Sample s = sample(r.source, r.index, in);
            rt.sample_valid = s.valid;
            rt.sample = s.value;
            if (!s.valid) {
                rt.state = RuleState::Unavailable;
                rt.has_last = false;
            } else {
                if ((rt.state == RuleState::Inactive) || (rt.state == RuleState::Unavailable)) {
                    rt.state = RuleState::Watching;  // the confirmation starts from here, never earlier
                }
                const bool cond = condition(r, rt, s.value);
                switch (rt.state) {
                    case RuleState::Watching:
                        if (cond) {
                            rt.since_ms = now_ms;
                            rt.state = (r.confirmation_s == 0U) ? RuleState::Raised : RuleState::Confirming;
                        }
                        break;
                    case RuleState::Confirming:
                        if (!cond) {
                            rt.state = RuleState::Watching;  // one gap: back to zero
                        } else if ((now_ms - rt.since_ms) >= (static_cast<uint32_t>(r.confirmation_s) * kMsPerS)) {
                            rt.state = RuleState::Raised;
                        } else {
                            // still confirming
                        }
                        break;
                    case RuleState::Raised:
                        if (!cond) {
                            rt.state = RuleState::Watching;
                        }
                        break;
                    case RuleState::Inactive:
                    case RuleState::Unavailable:
                    default:
                        break;
                }
                rt.last = s.value;
                rt.has_last = true;
            }
            render(i, in, s, rt.text, sizeof(rt.text));
        }

        // Queue synchronisation: the rule, and only the rule, holds and withdraws its notification.
        if (rule_key(i, key, sizeof(key)) == 0U) {
            continue;
        }
        if (rt.state == RuleState::Raised) {
            const Entry* existing = queue.find(key);
            const bool same = (existing != nullptr) && (std::strcmp(existing->text, rt.text) == 0);
            if (!rt.pushed || (existing == nullptr) || !same) {
                Entry e{};
                std::strncpy(e.key, key, kKeyMax);
                std::strncpy(e.title, r.name, kTitleMax);
                std::strncpy(e.text, rt.text, kTextMax);
                e.priority = r.priority;
                e.color_set = r.color_set;
                e.color[0] = r.color[0];
                e.color[1] = r.color[1];
                e.color[2] = r.color[2];
                e.icon = r.icon;
                e.held = r.comparison != Comparison::Change;  // a change is a one-shot event
                e.override_quiet = r.override_quiet;
                e.duration_s = r.duration_s;
                e.repeat_s = r.repeat_s;
                (void)queue.push_entry(e, now_ms);
                rt.pushed = true;
            }
        } else {
            // Not raised (fallen, unavailable, disabled, or reconfigured): a held notification of this
            // rule falls by itself — nothing else ever clears it. A one-shot (Change) just expires.
            const Entry* existing = queue.find(key);
            if ((existing != nullptr) && existing->held) {
                (void)queue.clear(key);
            }
            rt.pushed = false;
        }
    }
}

RuleState RuleEngine::state(std::size_t index) const noexcept {
    return (index < kRulesMax) ? rt_[index].state : RuleState::Inactive;
}

bool RuleEngine::view(std::size_t index, RuleView& out) const noexcept {
    if (index >= kRulesMax) {
        return false;
    }
    const Runtime& rt = rt_[index];
    out.state = rt.state;
    out.sample_valid = rt.sample_valid;
    out.sample = rt.sample;
    out.confirming_s = 0U;
    std::memcpy(out.text, rt.text, sizeof(out.text));
    return true;
}

std::size_t RuleEngine::raised_count() const noexcept {
    std::size_t n = 0U;
    for (const Runtime& rt : rt_) {
        n += (rt.state == RuleState::Raised) ? 1U : 0U;
    }
    return n;
}

}  // namespace alerts
