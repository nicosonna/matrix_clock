/**
 * @file test_alert_engine.cpp
 * @brief ALR-01 .. ALR-14, ALR-16, ALR-17: rule engine, notification queue, quiet hours.
 */
#include "test_support.hpp"

#include <cstdio>
#include <cstring>

#include "alerts/notification.hpp"
#include "alerts/queue.hpp"
#include "alerts/rules.hpp"

using namespace alerts;

namespace {

constexpr uint32_t kS = 1000U;

Inputs good_inputs() {
    Inputs in{};
    for (std::size_t i = 0U; i < kValuesCount; ++i) {
        in.values[i] = Sample{true, 0};
        in.value_enabled[i] = true;
        in.value_decimals[i] = 1U;
        std::strcpy(in.value_units[i], "C");
    }
    in.values[0] = Sample{true, 34};  // 3.4 C
    in.time_sure = true;
    in.ha_enabled = true;
    in.ha_link_up = true;
    in.token_rejected = false;
    in.network_up = true;
    in.lease_missing = false;
    in.ldr_fault = false;
    in.current_limited = false;
    in.air_enabled = true;
    in.air_present = true;
    in.air_fault = false;
    in.air_warming_up = false;
    in.air_compensation_default = false;
    in.air_aqi = Sample{true, 2};
    in.air_eco2 = Sample{true, 650};
    in.air_tvoc = Sample{true, 120};
    in.air_temperature = Sample{true, 2150};
    in.air_humidity = Sample{true, 456};
    return in;
}

Rule frost_rule(Comparison cmp, int32_t threshold, int32_t hysteresis, uint16_t confirmation_s) {
    Rule r = default_rule();
    r.enabled = true;
    std::strcpy(r.name, "GEL");
    r.source = Source::Value;
    r.index = 0U;
    r.comparison = cmp;
    r.threshold = threshold;
    r.hysteresis = hysteresis;
    r.decimals = 1U;
    r.confirmation_s = confirmation_s;
    r.priority = 1U;
    std::strcpy(r.text, "GEL {v}{u}");
    return r;
}

RuleList single(const Rule& r) {
    RuleList l{};
    for (Rule& x : l) {
        x = default_rule();
    }
    l[0] = r;
    return l;
}

Notification notif(const char* key, uint8_t priority, const char* text, bool persistent = false, uint16_t duration_s = 20U,
                   uint16_t repeat_s = 300U) {
    Notification n{};
    std::strcpy(n.key, key);
    std::strcpy(n.title, "T");
    std::strcpy(n.text, text);
    n.priority = priority;
    n.duration_s = duration_s;
    n.repeat_s = repeat_s;
    n.persistent = persistent;
    n.clear = false;
    return n;
}

/// Runs @p seconds steps of one second from @p now; returns the time after.
uint32_t run(RuleEngine& e, NotificationQueue& q, const Inputs& in, uint32_t now, uint32_t seconds) {
    for (uint32_t i = 0U; i < seconds; ++i) {
        now += kS;
        e.step(in, now, q);
        q.step(now);
    }
    return now;
}

// ALR-01: every comparison, numeric and boolean.
void test_comparisons() {
    RuleEngine e;
    NotificationQueue q;
    Inputs in = good_inputs();
    uint32_t now = 0U;
    struct Case {
        Comparison cmp;
        int32_t threshold;
        int32_t value_true;
        int32_t value_false;
    };
    const Case cases[] = {{Comparison::Less, 0, -1, 0},      {Comparison::LessEq, 0, 0, 1},   {Comparison::Greater, 300, 301, 300},
                          {Comparison::GreaterEq, 300, 300, 299}, {Comparison::Equal, 5, 5, 6}, {Comparison::NotEqual, 5, 6, 5}};
    for (const Case& c : cases) {
        e.configure(single(frost_rule(c.cmp, c.threshold, 0, 0U)));
        in.values[0] = Sample{true, c.value_false};
        now = run(e, q, in, now, 1U);
        CHECK_ENUM(e.state(0U), RuleState::Watching);
        in.values[0] = Sample{true, c.value_true};
        now = run(e, q, in, now, 1U);
        CHECK_ENUM(e.state(0U), RuleState::Raised);
        CHECK(q.find("regle_0") != nullptr);
        in.values[0] = Sample{true, c.value_false};
        now = run(e, q, in, now, 1U);
        CHECK_ENUM(e.state(0U), RuleState::Watching);
        CHECK(q.find("regle_0") == nullptr);
    }
    // Change: one step only, one-shot notification that is not withdrawn.
    e.configure(single(frost_rule(Comparison::Change, 0, 0, 0U)));
    in.values[0] = Sample{true, 10};
    now = run(e, q, in, now, 2U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    in.values[0] = Sample{true, 11};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    const Entry* ev = q.find("regle_0");
    CHECK((ev != nullptr) && !ev->held);
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    CHECK(q.find("regle_0") != nullptr);  // one-shot stays until it expires
    now = run(e, q, in, now, 31U);
    CHECK(q.find("regle_0") == nullptr);
    // Boolean: vrai / faux on an internal source.
    Rule b = default_rule();
    b.enabled = true;
    std::strcpy(b.name, "HEURE");
    b.source = Source::TimeUnsure;
    b.comparison = Comparison::True;
    std::strcpy(b.text, "HEURE NON SURE");
    e.configure(single(b));
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    in.time_sure = false;
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    b.comparison = Comparison::False;
    e.configure(single(b));
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    in.time_sure = true;
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    // Disabled rule: Inactive, nothing queued.
    b.enabled = false;
    e.configure(single(b));
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Inactive);
    CHECK_EQ(q.count(), 0U);
}

// ALR-02: hysteresis — raises at < 0, falls only at >= 0.5; oscillation inside the band = one trigger.
void test_hysteresis() {
    RuleEngine e;
    NotificationQueue q;
    Inputs in = good_inputs();
    e.configure(single(frost_rule(Comparison::Less, 0, 5, 0U)));
    uint32_t now = 0U;
    in.values[0] = Sample{true, 3};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    in.values[0] = Sample{true, -1};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    const uint32_t raised_seq = q.find("regle_0")->seq;
    const int32_t band[] = {0, 2, -1, 4, 0, 3, -2};
    for (int32_t v : band) {
        in.values[0] = Sample{true, v};
        now = run(e, q, in, now, 1U);
        CHECK_ENUM(e.state(0U), RuleState::Raised);
    }
    CHECK_EQ(q.find("regle_0")->seq, raised_seq);  // never withdrawn and re-pushed
    in.values[0] = Sample{true, 5};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    CHECK(q.find("regle_0") == nullptr);
    // Greater with hysteresis: raise > 300, fall <= 290.
    e.configure(single(frost_rule(Comparison::Greater, 300, 10, 0U)));
    in.values[0] = Sample{true, 301};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    in.values[0] = Sample{true, 291};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    in.values[0] = Sample{true, 290};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    // Equal with hysteresis: raise == 5, fall when |v - 5| > 2.
    e.configure(single(frost_rule(Comparison::Equal, 5, 2, 0U)));
    in.values[0] = Sample{true, 5};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    in.values[0] = Sample{true, 7};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    in.values[0] = Sample{true, 8};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
}

// ALR-03: confirmation — 299 s nothing, 300 s raised; a one-step gap resets.
void test_confirmation() {
    RuleEngine e;
    NotificationQueue q;
    Inputs in = good_inputs();
    e.configure(single(frost_rule(Comparison::Less, 0, 5, 300U)));
    uint32_t now = 0U;
    in.values[0] = Sample{true, -10};
    now = run(e, q, in, now, 1U);  // condition true at t=1 s: confirmation starts
    CHECK_ENUM(e.state(0U), RuleState::Confirming);
    now = run(e, q, in, now, 298U);  // t = 299 s
    CHECK_ENUM(e.state(0U), RuleState::Confirming);
    CHECK_EQ(q.count(), 0U);
    now = run(e, q, in, now, 1U);  // t = 300 s: 299 s since... keep one more
    now = run(e, q, in, now, 1U);  // t = 301 s: 300 s elapsed
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    CHECK(q.find("regle_0") != nullptr);
    // Gap of one step resets the counter.
    in.values[0] = Sample{true, 10};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Watching);
    CHECK(q.find("regle_0") == nullptr);
    in.values[0] = Sample{true, -10};
    now = run(e, q, in, now, 200U);
    CHECK_ENUM(e.state(0U), RuleState::Confirming);
    in.values[0] = Sample{true, 10};
    now = run(e, q, in, now, 1U);
    in.values[0] = Sample{true, -10};
    now = run(e, q, in, now, 299U);
    CHECK_ENUM(e.state(0U), RuleState::Confirming);  // 299 s since the gap, not 500
    now = run(e, q, in, now, 2U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
}

// ALR-04: invalid source — Unavailable, never raised; back to valid resets the confirmation.
void test_unavailable() {
    RuleEngine e;
    NotificationQueue q;
    Inputs in = good_inputs();
    e.configure(single(frost_rule(Comparison::Less, 0, 0, 10U)));
    uint32_t now = 0U;
    in.values[0] = Sample{false, -50};  // a stale -5.0 must not count
    now = run(e, q, in, now, 60U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
    CHECK_EQ(q.count(), 0U);
    in.values[0] = Sample{true, -50};
    now = run(e, q, in, now, 5U);
    CHECK_ENUM(e.state(0U), RuleState::Confirming);
    in.values[0] = Sample{false, -50};
    now = run(e, q, in, now, 3U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
    in.values[0] = Sample{true, -50};
    now = run(e, q, in, now, 8U);
    CHECK_ENUM(e.state(0U), RuleState::Confirming);  // 8 s since the return, not 16
    now = run(e, q, in, now, 3U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    // A raised rule whose source vanishes falls (the stale value is not kept raised).
    in.values[0] = Sample{false, -50};
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
    CHECK(q.find("regle_0") == nullptr);
    // Value disabled on the page: unavailable too. Air rules during warm-up / fault / absent / disabled.
    in = good_inputs();
    in.value_enabled[0] = false;
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
    Rule air = default_rule();
    air.enabled = true;
    std::strcpy(air.name, "AERER");
    air.source = Source::AirAqi;
    air.comparison = Comparison::GreaterEq;
    air.threshold = 4;
    std::strcpy(air.text, "AERER");
    e.configure(single(air));
    in = good_inputs();
    in.air_aqi = Sample{false, 5};  // ENS160 says invalid (warm-up)
    in.air_warming_up = true;
    now = run(e, q, in, now, 5U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
    in.air_aqi = Sample{true, 5};
    in.air_warming_up = false;
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    in.air_fault = true;
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
    in.air_fault = false;
    in.air_present = false;
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
    in.air_present = true;
    in.air_enabled = false;
    now = run(e, q, in, now, 1U);
    CHECK_ENUM(e.state(0U), RuleState::Unavailable);
}

// ALR-05: the internal sources.
void test_internal_sources() {
    Inputs in = good_inputs();
    // Every boolean source is valid and false on good inputs...
    const Source booleans[] = {Source::TimeUnsure, Source::HaLinkLost, Source::TokenRejected, Source::NetworkLost,
                               Source::LeaseMissing, Source::StaleValue, Source::LightSensorFault, Source::CurrentLimited,
                               Source::AirSensorFault, Source::AirWarmingUp, Source::AirCompensationDefault};
    for (Source s : booleans) {
        const Sample x = sample(s, 0U, in);
        CHECK(x.valid);
        CHECK_EQ(x.value, 0);
        CHECK(source_is_boolean(s));
    }
    // ... and each one flips on its own input.
    Inputs i2 = in;
    i2.time_sure = false;
    CHECK_EQ(sample(Source::TimeUnsure, 0U, i2).value, 1);
    i2 = in;
    i2.ha_link_up = false;
    CHECK_EQ(sample(Source::HaLinkLost, 0U, i2).value, 1);
    i2 = in;
    i2.token_rejected = true;
    CHECK_EQ(sample(Source::TokenRejected, 0U, i2).value, 1);
    i2 = in;
    i2.network_up = false;
    CHECK_EQ(sample(Source::NetworkLost, 0U, i2).value, 1);
    i2 = in;
    i2.lease_missing = true;
    CHECK_EQ(sample(Source::LeaseMissing, 0U, i2).value, 1);
    i2 = in;
    i2.values[3] = Sample{false, 0};
    CHECK_EQ(sample(Source::StaleValue, 3U, i2).value, 1);
    CHECK_EQ(sample(Source::StaleValue, 0U, i2).value, 0);
    i2 = in;
    i2.ldr_fault = true;
    CHECK_EQ(sample(Source::LightSensorFault, 0U, i2).value, 1);
    i2 = in;
    i2.current_limited = true;
    CHECK_EQ(sample(Source::CurrentLimited, 0U, i2).value, 1);
    i2 = in;
    i2.air_fault = true;
    CHECK_EQ(sample(Source::AirSensorFault, 0U, i2).value, 1);
    i2 = in;
    i2.air_present = false;
    CHECK_EQ(sample(Source::AirSensorFault, 0U, i2).value, 1);
    i2 = in;
    i2.air_warming_up = true;
    CHECK_EQ(sample(Source::AirWarmingUp, 0U, i2).value, 1);
    i2 = in;
    i2.air_compensation_default = true;
    CHECK_EQ(sample(Source::AirCompensationDefault, 0U, i2).value, 1);
    // Availability of the internal sources follows the enabled links/sensors (honesty: no opinion when off).
    i2 = in;
    i2.ha_enabled = false;
    i2.ha_link_up = false;
    CHECK(!sample(Source::HaLinkLost, 0U, i2).valid);
    CHECK(!sample(Source::TokenRejected, 0U, i2).valid);
    CHECK(!sample(Source::StaleValue, 0U, i2).valid);
    i2 = in;
    i2.air_enabled = false;
    CHECK(!sample(Source::AirSensorFault, 0U, i2).valid);
    CHECK(!sample(Source::AirWarmingUp, 0U, i2).valid);
    CHECK(!sample(Source::AirAqi, 0U, i2).valid);
    i2 = in;
    i2.air_present = false;
    CHECK(sample(Source::AirSensorFault, 0U, i2).valid);   // "sensor missing" is a valid statement
    CHECK(!sample(Source::AirWarmingUp, 0U, i2).valid);    // but warm-up is unknown
    CHECK(!sample(Source::AirTemperature, 0U, i2).valid);
    // Numeric air sources pass the sample through; index out of range is invalid.
    CHECK_EQ(sample(Source::AirTemperature, 0U, in).value, 2150);
    CHECK_EQ(sample(Source::AirHumidity, 0U, in).value, 456);
    CHECK_EQ(sample(Source::AirEco2, 0U, in).value, 650);
    CHECK_EQ(sample(Source::AirTvoc, 0U, in).value, 120);
    CHECK(!sample(Source::Value, 8U, in).valid);
    CHECK(!sample(Source::StaleValue, 8U, in).valid);
    CHECK(!sample(Source::Count, 0U, in).valid);
    // Decimals and units of the sources.
    CHECK_EQ(source_decimals(Source::Value, 2U), 2U);
    CHECK_EQ(source_decimals(Source::AirTemperature, 2U), 2U);
    CHECK_EQ(source_decimals(Source::AirHumidity, 0U), 1U);
    CHECK_EQ(source_decimals(Source::AirAqi, 1U), 0U);
    CHECK(std::strcmp(source_unit(Source::AirTemperature), "C") == 0);
    CHECK(std::strcmp(source_unit(Source::AirEco2), "ppm") == 0);
    CHECK(std::strcmp(source_unit(Source::TimeUnsure), "") == 0);
    // Names round-trip.
    for (uint8_t i = 0U; i < static_cast<uint8_t>(Source::Count); ++i) {
        Source s{};
        CHECK(source_from_name(source_name(static_cast<Source>(i)), s));
        CHECK_EQ(static_cast<uint8_t>(s), i);
    }
    for (uint8_t i = 0U; i < static_cast<uint8_t>(Comparison::Count); ++i) {
        Comparison c{};
        CHECK(comparison_from_name(comparison_name(static_cast<Comparison>(i)), c));
        CHECK_EQ(static_cast<uint8_t>(c), i);
    }
    Source s{};
    CHECK(!source_from_name("Inconnu", s));
    CHECK(!source_from_name(nullptr, s));
    Comparison c{};
    CHECK(!comparison_from_name("~", c));
    CHECK(!comparison_from_name(nullptr, c));
    CHECK(std::strcmp(source_name(Source::Count), "?") == 0);
    CHECK(std::strcmp(comparison_name(Comparison::Count), "?") == 0);
    // The default rules of the spec: the internal ones active, all valid, and they behave on good inputs.
    RuleEngine e;
    NotificationQueue q;
    const RuleList defaults = default_rules();
    CHECK(is_valid(defaults));
    CHECK(defaults[2].enabled && (defaults[2].source == Source::TimeUnsure));
    CHECK(defaults[7].enabled && (defaults[7].source == Source::AirAqi) && (defaults[7].threshold == 4));
    CHECK(!defaults[0].enabled && (defaults[0].source == Source::Value));
    e.configure(defaults);
    uint32_t now = run(e, q, in, 0U, 2000U);
    CHECK_EQ(q.count(), 0U);
    CHECK_EQ(e.raised_count(), 0U);
    // Bad air for 2 min: AERER (P1) raised, AIR MAUVAIS (needs 5) not.
    in.air_aqi = Sample{true, 4};
    now = run(e, q, in, now, 121U);
    CHECK_ENUM(e.state(7U), RuleState::Raised);
    CHECK_ENUM(e.state(8U), RuleState::Watching);
    const Entry* a = q.find("regle_7");
    CHECK((a != nullptr) && (std::strcmp(a->text, "AERER") == 0) && a->held && (a->priority == 1U));
    // Air back to good: it falls by itself.
    in.air_aqi = Sample{true, 2};
    now = run(e, q, in, now, 1U);
    CHECK(q.find("regle_7") == nullptr);
    // Ha disabled: DOMOTIQUE and JETON unavailable, not raised.
    in.ha_enabled = false;
    in.ha_link_up = false;
    now = run(e, q, in, now, 400U);
    CHECK_ENUM(e.state(3U), RuleState::Unavailable);
    CHECK_ENUM(e.state(4U), RuleState::Unavailable);
    // Views for the page.
    RuleView v{};
    CHECK(e.view(7U, v));
    CHECK(v.sample_valid && (v.sample == 2));
    CHECK(std::strcmp(v.text, "AERER") == 0);
    CHECK(!e.view(16U, v));
    CHECK_ENUM(e.state(16U), RuleState::Inactive);
    (void)now;
}

// ALR-06 / ALR-07: passes replayed every repeat_s while raised; withdrawn by the rule only.
void test_passes_and_withdrawal() {
    RuleEngine e;
    NotificationQueue q;
    Inputs in = good_inputs();
    Rule r = frost_rule(Comparison::Less, 0, 0, 0U);
    r.duration_s = 30U;
    r.repeat_s = 120U;
    e.configure(single(r));
    uint32_t now = 0U;
    in.values[0] = Sample{true, -23};
    now = run(e, q, in, now, 1U);
    Pass p{};
    CHECK(q.next_pass(now, false, true, p));
    CHECK(std::strcmp(p.key, "regle_0") == 0);
    CHECK(std::strcmp(p.text, "GEL -2.3C") == 0);
    CHECK_EQ(p.duration_s, 30U);
    CHECK(p.held);
    CHECK(p.rendering.interrupts);
    CHECK(!p.rendering.blink);
    CHECK(q.has_held());
    CHECK_EQ(q.held_priority(), 1U);
    // Nothing before repeat_s.
    now = run(e, q, in, now, 119U);
    CHECK(!q.next_pass(now, false, true, p));
    CHECK(q.has_held());  // the icon stays between passes
    now = run(e, q, in, now, 1U);
    CHECK(q.next_pass(now, false, true, p));
    // The text follows the value (replaced, same rank).
    const uint32_t seq = q.find("regle_0")->seq;
    in.values[0] = Sample{true, -41};
    now = run(e, q, in, now, 1U);
    CHECK(std::strcmp(q.find("regle_0")->text, "GEL -4.1C") == 0);
    CHECK_EQ(q.find("regle_0")->seq, seq);
    // ALR-07: no API withdraws it: a clear() from outside with its key is the only thing that could, and the
    // next step pushes it back because the rule is still raised.
    CHECK(q.clear("regle_0"));
    now = run(e, q, in, now, 1U);
    CHECK(q.find("regle_0") != nullptr);
    // The condition falls: withdrawn by itself.
    in.values[0] = Sample{true, 12};
    now = run(e, q, in, now, 1U);
    CHECK(q.find("regle_0") == nullptr);
    CHECK(!q.has_held());
    CHECK(!q.next_pass(now, false, true, p));
    // Notification type has no acknowledgement field (the struct is the API).
    Notification n{};
    CHECK_EQ(sizeof(n.clear), 1U);
    (void)now;
}

// ALR-08: validation.
void test_validation() {
    Rule r = frost_rule(Comparison::Less, 0, 5, 300U);
    CHECK_ENUM(validate(r), RuleStatus::Ok);
    CHECK(rule_status_reason(RuleStatus::Ok)[0] == '\0');
    Rule x = r;
    std::memset(x.name, 'a', sizeof(x.name));
    CHECK_ENUM(validate(x), RuleStatus::NameInvalid);
    x = r;
    x.name[2] = '\t';
    CHECK_ENUM(validate(x), RuleStatus::NameInvalid);
    x = r;
    x.source = Source::Count;
    CHECK_ENUM(validate(x), RuleStatus::SourceInvalid);
    x = r;
    x.index = 8U;
    CHECK_ENUM(validate(x), RuleStatus::IndexInvalid);
    x = r;
    x.source = Source::TimeUnsure;
    x.comparison = Comparison::True;
    x.index = 1U;
    CHECK_ENUM(validate(x), RuleStatus::IndexInvalid);
    x = r;
    x.comparison = Comparison::Count;
    CHECK_ENUM(validate(x), RuleStatus::ComparisonInvalid);
    x = r;
    x.comparison = Comparison::True;  // numeric source
    CHECK_ENUM(validate(x), RuleStatus::ComparisonInvalid);
    x = r;
    x.source = Source::TimeUnsure;
    x.comparison = Comparison::Less;  // boolean source
    CHECK_ENUM(validate(x), RuleStatus::ComparisonInvalid);
    x = r;
    x.threshold = 2000000001;
    CHECK_ENUM(validate(x), RuleStatus::ThresholdInvalid);
    x = r;
    x.hysteresis = -1;
    CHECK_ENUM(validate(x), RuleStatus::HysteresisInvalid);
    x = r;
    x.decimals = 7U;
    CHECK_ENUM(validate(x), RuleStatus::DecimalsInvalid);
    x = r;
    x.confirmation_s = 3601U;
    CHECK_ENUM(validate(x), RuleStatus::ConfirmationInvalid);
    x = r;
    x.priority = 4U;
    CHECK_ENUM(validate(x), RuleStatus::PriorityInvalid);
    x = r;
    std::memset(x.text, 'a', sizeof(x.text));
    CHECK_ENUM(validate(x), RuleStatus::TextInvalid);
    x = r;
    x.icon = kIconCount;
    CHECK_ENUM(validate(x), RuleStatus::IconInvalid);
    x = r;
    x.duration_s = 0U;
    CHECK_ENUM(validate(x), RuleStatus::DurationInvalid);
    x = r;
    x.repeat_s = 9U;
    CHECK_ENUM(validate(x), RuleStatus::RepeatInvalid);
    for (uint8_t i = 1U; i <= static_cast<uint8_t>(RuleStatus::RepeatInvalid); ++i) {
        CHECK(rule_status_reason(static_cast<RuleStatus>(i))[0] != '\0');
    }
    // An invalid rule handed to the engine is replaced by the (disabled) default.
    RuleEngine e;
    x = r;
    x.priority = 9U;
    e.configure(single(x));
    CHECK(!e.rules()[0].enabled);
    RuleList l = single(r);
    CHECK(is_valid(l));
    l[5].repeat_s = 0U;
    CHECK(!is_valid(l));
    // Keys.
    char key[kKeyMax + 1U];
    CHECK_EQ(rule_key(15U, key, sizeof(key)), 8U);
    CHECK(std::strcmp(key, "regle_15") == 0);
    CHECK_EQ(rule_key(16U, key, sizeof(key)), 0U);
    CHECK_EQ(rule_key(0U, key, 4U), 0U);
    CHECK_EQ(rule_key(0U, nullptr, 0U), 0U);
    CHECK(is_valid_key("regle_15"));
}

// ALR-09: templates.
void test_template() {
    char out[kTemplateMax + 1U];
    CHECK(render_template("GEL {v}{u}", "GEL", true, -5, 1U, "C", out, sizeof(out)) > 0U);
    CHECK(std::strcmp(out, "GEL -0.5C") == 0);
    CHECK(render_template("{n}: {v} {u}", "CO2", true, 1234, 0U, "ppm", out, sizeof(out)) > 0U);
    CHECK(std::strcmp(out, "CO2: 1234 ppm") == 0);
    CHECK(render_template("{v}", "x", true, 2150, 2U, "", out, sizeof(out)) > 0U);
    CHECK(std::strcmp(out, "21.50") == 0);
    CHECK(render_template("{v}", "x", true, -2005, 2U, "", out, sizeof(out)) > 0U);
    CHECK(std::strcmp(out, "-20.05") == 0);
    CHECK(render_template("T {v}", "x", false, 99, 1U, "C", out, sizeof(out)) > 0U);
    CHECK(std::strcmp(out, "T --") == 0);
    // Orphan brace, unknown placeholder, braces at the end.
    CHECK(render_template("a{b}c{", "n", true, 1, 0U, "", out, sizeof(out)) > 0U);
    CHECK(std::strcmp(out, "a{b}c{") == 0);
    CHECK(render_template("{x}{}", "n", true, 1, 0U, "", out, sizeof(out)) > 0U);
    CHECK(std::strcmp(out, "{x}{}") == 0);
    CHECK(render_template("{", "n", true, 1, 0U, "", out, sizeof(out)) > 0U);
    CHECK(render_template("", "n", true, 1, 0U, "", out, sizeof(out)) == 0U);
    CHECK(out[0] == '\0');
    // Overflow: 0 and emptied.
    CHECK_EQ(render_template("0123456789", "n", true, 1, 0U, "", out, 10U), 0U);
    CHECK(out[0] == '\0');
    CHECK_EQ(render_template("{v}", "n", true, 123456, 0U, "", out, 4U), 0U);
    CHECK_EQ(render_template("{n}", "abcdef", true, 0, 0U, "", out, 6U), 0U);
    CHECK_EQ(render_template(nullptr, "n", true, 0, 0U, "", out, sizeof(out)), 0U);
    CHECK_EQ(render_template("x", "n", true, 0, 0U, "", nullptr, 0U), 0U);
    CHECK(render_template("{n}{u}", nullptr, true, 0, 0U, nullptr, out, sizeof(out)) == 0U);  // null name
    // Through the engine: an empty template shows the name; a boolean source renders {v} as --.
    RuleEngine e;
    NotificationQueue q;
    Inputs in = good_inputs();
    Rule r = frost_rule(Comparison::Less, 100, 0, 0U);
    r.text[0] = '\0';
    e.configure(single(r));
    (void)run(e, q, in, 0U, 1U);
    CHECK(std::strcmp(q.find("regle_0")->text, "GEL") == 0);
    Rule b = default_rule();
    b.enabled = true;
    std::strcpy(b.name, "DHCP");
    b.source = Source::LeaseMissing;
    b.comparison = Comparison::True;
    std::strcpy(b.text, "IP {v}");
    e.configure(single(b));
    in.lease_missing = true;
    (void)run(e, q, in, 0U, 1U);
    CHECK(std::strcmp(q.find("regle_0")->text, "IP --") == 0);
}

// ALR-10 .. ALR-13: queue semantics.
void test_queue() {
    NotificationQueue q;
    uint32_t now = 1000U;
    CHECK_ENUM(q.push(notif("porte", 1U, "ouverte"), now), PushStatus::Added);
    const uint32_t seq = q.find("porte")->seq;
    CHECK_ENUM(q.push(notif("secheuse", 0U, "fini"), now + 10U), PushStatus::Added);
    // ALR-10: same key replaces content, keeps rank, pushes the expiry back.
    now += 15000U;
    CHECK_ENUM(q.push(notif("porte", 2U, "toujours ouverte"), now), PushStatus::Replaced);
    CHECK_EQ(q.count(), 2U);
    CHECK_EQ(q.find("porte")->seq, seq);
    CHECK_EQ(q.find("porte")->priority, 2U);
    CHECK(std::strcmp(q.find("porte")->text, "toujours ouverte") == 0);
    CHECK_EQ(q.find("porte")->expires_ms, now + 20000U);
    // ALR-12: presentation order: priority then age.
    Pass p{};
    CHECK(q.next_pass(now, false, true, p));
    CHECK(std::strcmp(p.key, "porte") == 0);
    CHECK(q.next_pass(now, false, true, p));
    CHECK(std::strcmp(p.key, "secheuse") == 0);
    CHECK(!p.rendering.interrupts);  // priority 0 waits for the scene
    CHECK(!q.next_pass(now, false, true, p));  // one-shots play once
    // ALR-13: one-shot expires at duration_s; held external stays until `effacer`.
    CHECK_ENUM(q.push(notif("garage", 1U, "ouvert", true, 20U, 60U), now), PushStatus::Added);
    q.step(now + 19999U);
    CHECK_EQ(q.count(), 2U);  // secheuse (pushed 15 s earlier) already expired; porte pushed back to now+20 s
    q.step(now + 20000U);
    CHECK_EQ(q.count(), 1U);  // porte gone
    CHECK(q.find("garage") != nullptr);
    q.step(now + 3600000U);
    CHECK(q.find("garage") != nullptr);
    CHECK(q.next_pass(now + 3600000U, false, true, p));
    CHECK(q.next_pass(now + 3660000U, false, true, p));  // replayed after repeat_s
    CHECK(!q.next_pass(now + 3661000U, false, true, p));
    Notification clr = notif("garage", 1U, "");
    clr.clear = true;
    CHECK_ENUM(q.push(clr, now), PushStatus::Replaced);
    CHECK(q.find("garage") == nullptr);
    CHECK(!q.clear("garage"));  // clearing an absent key: no effect, no error
    CHECK(!q.clear(nullptr));
    CHECK_EQ(q.count(), 0U);
    // Invalid key refused and counted.
    Entry bad{};
    std::strcpy(bad.key, "Majuscule");
    CHECK_ENUM(q.push_entry(bad, now), PushStatus::Rejected);
    CHECK_EQ(q.rejected(), 1U);
    // Text empty: the pass shows the title.
    Notification t = notif("titre_seul", 1U, "");
    CHECK_ENUM(q.push(t, now), PushStatus::Added);
    CHECK(q.next_pass(now, false, true, p));
    CHECK(std::strcmp(p.text, "T") == 0);
    CHECK(q.at(0U) != nullptr);
    CHECK(q.at(kQueueMax) == nullptr);
    CHECK(q.find(nullptr) == nullptr);
    // Colour of a pass: the priority's unless set.
    CHECK(!p.color_set);
    CHECK_EQ(p.color[0], 255U);
    CHECK_EQ(p.color[1], 190U);
    Entry coloured{};
    std::strcpy(coloured.key, "rouge");
    std::strcpy(coloured.text, "x");
    coloured.priority = 1U;
    coloured.color_set = true;
    coloured.color[0] = 1U;
    coloured.color[1] = 2U;
    coloured.color[2] = 3U;
    coloured.duration_s = 5U;
    coloured.repeat_s = 60U;
    CHECK_ENUM(q.push_entry(coloured, now), PushStatus::Added);
    CHECK(q.next_pass(now, false, true, p));
    CHECK(p.color_set && (p.color[0] == 1U) && (p.color[2] == 3U));
}

// ALR-11: full queue eviction and rejection.
void test_queue_full() {
    NotificationQueue q;
    uint32_t now = 0U;
    char key[kKeyMax + 1U];
    for (std::size_t i = 0U; i < kQueueMax; ++i) {
        (void)std::snprintf(key, sizeof(key), "n%u", static_cast<unsigned>(i));
        // Priorities 1, except n3 (0, oldest low) and n9 (0).
        const uint8_t prio = ((i == 3U) || (i == 9U)) ? 0U : 1U;
        CHECK_ENUM(q.push(notif(key, prio, "x", true), now + static_cast<uint32_t>(i)), PushStatus::Added);
    }
    CHECK_EQ(q.count(), kQueueMax);
    // A priority-1 newcomer evicts the oldest priority-0 (n3), not n9.
    CHECK_ENUM(q.push(notif("new1", 1U, "x", true), now + 100U), PushStatus::Evicted);
    CHECK(q.find("n3") == nullptr);
    CHECK(q.find("n9") != nullptr);
    CHECK(q.find("new1") != nullptr);
    // Another priority-1: evicts n9. Then a priority-1 with only priority-1 left: rejected.
    CHECK_ENUM(q.push(notif("new2", 1U, "x", true), now + 101U), PushStatus::Evicted);
    CHECK(q.find("n9") == nullptr);
    CHECK_ENUM(q.push(notif("new3", 1U, "x", true), now + 102U), PushStatus::Rejected);
    CHECK_EQ(q.rejected(), 1U);
    CHECK(q.find("new3") == nullptr);
    // A priority-3 newcomer evicts the oldest priority-1 (n0).
    CHECK_ENUM(q.push(notif("crit", 3U, "x", true), now + 103U), PushStatus::Evicted);
    CHECK(q.find("n0") == nullptr);
    // Replacing an existing key on a full queue never evicts anyone.
    CHECK_ENUM(q.push(notif("n1", 0U, "y", true), now + 104U), PushStatus::Replaced);
    CHECK_EQ(q.count(), kQueueMax);
    CHECK_EQ(q.held_priority(), 3U);
}

// ALR-14: quiet hours.
void test_quiet_hours() {
    QuietConfig c = default_quiet_config();
    CHECK(c.enabled && (c.start_min == 1350U) && (c.end_min == 420U) && c.p3_overrides);
    CHECK(is_valid(c));
    // Crosses midnight.
    CHECK(quiet_now(c, true, 1350U));
    CHECK(quiet_now(c, true, 1439U));
    CHECK(quiet_now(c, true, 0U));
    CHECK(quiet_now(c, true, 419U));
    CHECK(!quiet_now(c, true, 420U));
    CHECK(!quiet_now(c, true, 1349U));
    CHECK(!quiet_now(c, true, 720U));
    // Same-day window, disabled, no sure time, empty window, invalid.
    QuietConfig d = c;
    d.start_min = 60U;
    d.end_min = 120U;
    CHECK(quiet_now(d, true, 60U));
    CHECK(!quiet_now(d, true, 120U));
    CHECK(!quiet_now(d, true, 59U));
    d.enabled = false;
    CHECK(!quiet_now(d, true, 90U));
    CHECK(!quiet_now(c, false, 0U));
    d = c;
    d.start_min = 100U;
    d.end_min = 100U;
    CHECK(!quiet_now(d, true, 100U));
    d.end_min = 1440U;
    CHECK(!is_valid(d));
    CHECK(!quiet_now(d, true, 100U));
    CHECK(!quiet_now(c, true, 1440U));
    // Rendering by priority, muted during quiet hours; P3 with override is exempt.
    Rendering r = rendering_for(0U, false, false);
    CHECK(!r.interrupts && !r.blink && r.scroll && !r.raise_floor);
    r = rendering_for(1U, false, false);
    CHECK(r.interrupts && !r.blink && r.scroll);
    r = rendering_for(2U, false, false);
    CHECK(r.blink && (r.blink_hz == 1U) && !r.raise_floor);
    r = rendering_for(3U, false, false);
    CHECK(r.blink && (r.blink_hz == 2U) && r.raise_floor && (r.color[0] == 255U) && (r.color[1] == 30U));
    r = rendering_for(3U, true, false);
    CHECK(!r.blink && !r.scroll && !r.raise_floor);
    r = rendering_for(3U, true, true);
    CHECK(r.blink && r.scroll && r.raise_floor);
    r = rendering_for(9U, false, false);  // clamped
    CHECK_EQ(r.blink_hz, 2U);
    // Queue: passes suspended during quiet hours, held entries kept and resumed; P3 override passes.
    NotificationQueue q;
    uint32_t now = 0U;
    CHECK_ENUM(q.push(notif("tenue", 1U, "x", true, 20U, 60U), now), PushStatus::Added);
    Entry crit{};
    std::strcpy(crit.key, "feu");
    std::strcpy(crit.text, "FEU");
    crit.priority = 3U;
    crit.held = true;
    crit.override_quiet = true;
    crit.duration_s = 10U;
    crit.repeat_s = 30U;
    CHECK_ENUM(q.push_entry(crit, now), PushStatus::Added);
    Pass p{};
    CHECK(q.next_pass(now, true, true, p));
    CHECK(std::strcmp(p.key, "feu") == 0);
    CHECK(p.rendering.blink && p.rendering.raise_floor);
    CHECK(!q.next_pass(now, true, true, p));  // "tenue" suspended
    CHECK(q.has_held());
    // P3 override not allowed by the settings: suspended too.
    CHECK(!q.next_pass(now + 31000U, true, false, p));
    // End of quiet hours: the held entry plays, then every repeat_s.
    now += 3600000U;
    q.step(now);
    CHECK(q.find("tenue") != nullptr);
    CHECK(q.next_pass(now, false, true, p));
    CHECK(std::strcmp(p.key, "feu") == 0);  // highest priority first
    CHECK(q.next_pass(now, false, true, p));
    CHECK(std::strcmp(p.key, "tenue") == 0);
    CHECK(!p.rendering.blink);
}

// ALR-16: 16 rules active, no allocation anywhere.
void test_no_allocation() {
    RuleList l = default_rules();
    for (std::size_t i = 0U; i < kRulesMax; ++i) {
        l[i].enabled = true;
        if (l[i].name[0] == '\0') {
            l[i] = frost_rule(Comparison::Less, 0, 0, 0U);
        }
    }
    CHECK(is_valid(l));
    Inputs in = good_inputs();
    in.values[0] = Sample{true, -10};
    in.time_sure = false;
    in.air_aqi = Sample{true, 5};
    {
        AllocGuard guard;
        RuleEngine e;
        NotificationQueue q;
        e.configure(l);
        uint32_t now = 0U;
        for (uint32_t s = 0U; s < 2000U; ++s) {
            now += kS;
            e.step(in, now, q);
            q.step(now);
            Pass p{};
            (void)q.next_pass(now, (s % 7U) == 0U, true, p);
        }
        CHECK(e.raised_count() >= 3U);
        CHECK_EQ(g_alloc_count, 0U);
    }
}

// ALR-17: millisecond counters wrap.
void test_wrap() {
    RuleEngine e;
    NotificationQueue q;
    Inputs in = good_inputs();
    e.configure(single(frost_rule(Comparison::Less, 0, 0, 10U)));
    uint32_t now = 0xFFFFFFFFU - 5000U;
    in.values[0] = Sample{true, -10};
    now = run(e, q, in, now, 5U);
    CHECK_ENUM(e.state(0U), RuleState::Confirming);
    now = run(e, q, in, now, 6U);  // crosses zero
    CHECK_ENUM(e.state(0U), RuleState::Raised);
    CHECK(now < 10000U);
    Pass p{};
    CHECK(q.next_pass(now, false, true, p));
    CHECK(!q.next_pass(now + 299000U, false, true, p));
    CHECK(q.next_pass(now + 300000U, false, true, p));
    // One-shot expiry across the wrap.
    NotificationQueue q2;
    const uint32_t late = 0xFFFFFFFFU - 1000U;
    CHECK_ENUM(q2.push(notif("x", 1U, "x", false, 5U), late), PushStatus::Added);
    q2.step(late + 4000U);  // wrapped
    CHECK_EQ(q2.count(), 1U);
    q2.step(late + 5000U);
    CHECK_EQ(q2.count(), 0U);
}

}  // namespace

void run_alert_engine_tests() {
    std::printf("alerts: rules, queue, quiet hours (ALR-01..14, 16, 17)\n");
    test_comparisons();
    test_hysteresis();
    test_confirmation();
    test_unavailable();
    test_internal_sources();
    test_passes_and_withdrawal();
    test_validation();
    test_template();
    test_queue();
    test_queue_full();
    test_quiet_hours();
    test_no_allocation();
    test_wrap();
}
