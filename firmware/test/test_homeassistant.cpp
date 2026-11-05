/**
 * @file test_homeassistant.cpp
 * @brief HA-01 .. HA-14: JSON reader, tracked values, WebSocket client, reassembly, weather.
 */
#include "test_support.hpp"

#include <cstdio>
#include <cstring>

#include "homeassistant/json_reader.hpp"
#include "homeassistant/reassembler.hpp"
#include "homeassistant/values.hpp"
#include "homeassistant/weather.hpp"
#include "homeassistant/ws_client.hpp"

using namespace homeassistant;

namespace {

std::size_t slen(const char* s) {
    return std::strlen(s);
}

void feed(WsClient& c, const char* json, uint32_t now) {
    c.on_message(json, slen(json), now);
}

/// Pulls every outgoing message into @p out (concatenated with '\n'), returns the count.
uint32_t drain(WsClient& c, char* out, std::size_t size) {
    uint32_t n = 0U;
    std::size_t w = 0U;
    char msg[kOutgoingMax];
    while (true) {
        const std::size_t len = c.next_outgoing(msg, sizeof(msg));
        if (len == 0U) {
            break;
        }
        ++n;
        if ((w + len + 2U) < size) {
            std::memcpy(out + w, msg, len);
            w += len;
            out[w++] = '\n';
        }
    }
    out[w] = '\0';
    return n;
}

ValueList two_values() {
    ValueList l = default_value_list();
    l[0].enabled = true;
    std::strcpy(l[0].name, "Exterieur");
    std::strcpy(l[0].entity, "sensor.temperature_exterieure");
    l[0].type = ValueType::Number;
    l[0].decimals = 1U;
    std::strcpy(l[0].unit, "`C");
    l[0].icon = ValueIcon::Thermometer;
    l[2].enabled = true;
    std::strcpy(l[2].name, "Garage");
    std::strcpy(l[2].entity, "binary_sensor.porte_garage");
    l[2].type = ValueType::Boolean;
    return l;
}

WsConfig enabled_config() {
    WsConfig c = default_ws_config();
    c.enabled = true;
    c.token = "eyJ.TOKEN.abc";
    return c;
}

/// Brings a client to Running: connect, auth, subscriptions, results.
void bring_up(WsClient& c, uint32_t& now, char* out, std::size_t size) {
    c.step(now);
    CHECK(c.take_connect_request());
    c.on_connected(now);
    feed(c, "{\"type\":\"auth_required\",\"ha_version\":\"2026.11.0\"}", now);
    CHECK_EQ(drain(c, out, size), 1U);
    CHECK(std::strstr(out, "\"type\":\"auth\",\"access_token\":\"eyJ.TOKEN.abc\"") != nullptr);
    feed(c, "{\"type\":\"auth_ok\",\"ha_version\":\"2026.11.0\"}", now);
    CHECK_ENUM(c.state(), LinkState::Subscribing);
    CHECK_EQ(drain(c, out, size), 3U);
    feed(c, "{\"id\":1,\"type\":\"result\",\"success\":true,\"result\":null}", now);
    feed(c, "{\"id\":2,\"type\":\"result\",\"success\":true,\"result\":null}", now);
    feed(c, "{\"id\":3,\"type\":\"result\",\"success\":true,\"result\":null}", now);
    CHECK_ENUM(c.state(), LinkState::Running);
}

// ------------------------------------------------------------------ HA-10 (reader)

void test_json_reader() {
    const char* doc = "{\"id\":12,\"type\":\"event\",\"event\":{\"result\":\"3.4\",\"n\":-1.25e1,\"ok\":true,\"z\":null,"
                      "\"s\":\"a\\\"b\\\\c\\n\",\"arr\":[1,{\"k\":2}],\"obj\":{\"deep\":{\"x\":1}}}}";
    const std::size_t len = slen(doc);
    JsonSpan v{};
    const char* p1[] = {"id"};
    CHECK(json_find(doc, len, p1, 1U, v));
    int32_t n = 0;
    CHECK(json_to_fixed(v, 0U, n));
    CHECK_EQ(n, 12);
    const char* p2[] = {"event", "result"};
    CHECK(json_find(doc, len, p2, 2U, v));
    CHECK(json_is_string(v));
    char s[16];
    CHECK(json_string(v, s, sizeof(s)));
    CHECK(std::strcmp(s, "3.4") == 0);
    const char* p3[] = {"event", "n"};
    CHECK(json_find(doc, len, p3, 2U, v));
    CHECK(json_to_fixed(v, 1U, n));
    CHECK_EQ(n, -125);
    CHECK(json_to_fixed(v, 0U, n));
    CHECK_EQ(n, -13);  // rounded away from .5? -12.5 -> 13 (nearest, half up on magnitude)
    const char* p4[] = {"event", "ok"};
    CHECK(json_find(doc, len, p4, 2U, v));
    CHECK(json_is_true(v));
    const char* p5[] = {"event", "z"};
    CHECK(json_find(doc, len, p5, 2U, v));
    CHECK(json_is_null(v));
    CHECK(!json_is_true(v));
    const char* p6[] = {"event", "s"};
    CHECK(json_find(doc, len, p6, 2U, v));
    CHECK(json_string(v, s, sizeof(s)));
    CHECK(std::strcmp(s, "a\"b\\c\n") == 0);
    CHECK(json_string(v, s, 3U));  // truncated to 2 chars
    CHECK(std::strcmp(s, "a\"") == 0);
    const char* p7[] = {"event", "arr"};
    CHECK(json_find(doc, len, p7, 2U, v));
    CHECK_EQ(v.len, 11U);
    CHECK(!json_is_string(v));
    CHECK(!json_string(v, s, sizeof(s)));
    const char* p8[] = {"event", "obj", "deep", "x"};
    CHECK(json_find(doc, len, p8, 4U, v));
    CHECK(json_to_fixed(v, 0U, n) && (n == 1));
    // Missing key, wrong level type, malformed, truncated, too deep.
    const char* p9[] = {"event", "missing"};
    CHECK(!json_find(doc, len, p9, 2U, v));
    const char* p10[] = {"id", "x"};
    CHECK(!json_find(doc, len, p10, 2U, v));
    CHECK(!json_find(doc, len - 5U, p2, 2U, v));  // truncated document
    CHECK(!json_find("{\"a\":}", 6U, p1, 1U, v));
    CHECK(!json_find("{\"a\" 1}", 7U, p1, 1U, v));
    CHECK(!json_find("{\"a\":1 \"b\":2}", 13U, p1 + 0, 0U, v) == false);  // depth 0: whole value
    CHECK(!json_find(nullptr, 0U, p1, 1U, v));
    CHECK(!json_find("{\"a\":1}", 7U, nullptr, 1U, v));
    char deep[80];
    std::size_t w = 0U;
    for (uint32_t i = 0U; i < 20U; ++i) {
        deep[w++] = '[';
    }
    for (uint32_t i = 0U; i < 20U; ++i) {
        deep[w++] = ']';
    }
    deep[w] = '\0';
    CHECK(!json_find(deep, w, p1, 0U, v));  // deeper than kJsonDepthMax
    // Numbers.
    const JsonSpan a{"366.10000000000002", 18U};
    CHECK(json_to_fixed(a, 1U, n) && (n == 3661));
    const JsonSpan b{"-1e-05", 6U};
    CHECK(json_to_fixed(b, 2U, n) && (n == 0));
    const JsonSpan c{"1.", 2U};
    CHECK(!json_to_fixed(c, 0U, n));
    const JsonSpan d{".5", 2U};
    CHECK(!json_to_fixed(d, 0U, n));
    const JsonSpan e{"+1", 2U};
    CHECK(!json_to_fixed(e, 0U, n));
    const JsonSpan f{"01", 2U};
    CHECK(!json_to_fixed(f, 0U, n));
    const JsonSpan g{"3000000000", 10U};
    CHECK(!json_to_fixed(g, 0U, n));
    const JsonSpan h{"1e50", 4U};
    CHECK(!json_to_fixed(h, 0U, n));
    const JsonSpan i2{"12345678901234567890123", 23U};
    CHECK(!json_to_fixed(i2, 0U, n));
    const JsonSpan j{"\"12\"", 4U};
    CHECK(!json_to_fixed(j, 0U, n));
    const JsonSpan k{"2.5", 3U};
    CHECK(json_to_fixed(k, 0U, n) && (n == 3));
    const JsonSpan l{"0.0", 3U};
    CHECK(json_to_fixed(l, 2U, n) && (n == 0));
    const JsonSpan m{"1E2", 3U};
    CHECK(json_to_fixed(m, 0U, n) && (n == 100));
    const JsonSpan o{"1e", 2U};
    CHECK(!json_to_fixed(o, 0U, n));
    const JsonSpan q{"12x", 3U};
    CHECK(!json_to_fixed(q, 0U, n));
    const JsonSpan r{nullptr, 0U};
    CHECK(!json_to_fixed(r, 0U, n));
    CHECK(!json_is_null(r));
    // \u escapes become '?', unknown escapes too, unterminated escape stops.
    const JsonSpan u{"\"a\\u00e9b\\qc\\\"", 14U};
    CHECK(json_string(u, s, sizeof(s)));
    CHECK(std::strcmp(s, "a?b?c") == 0);
    CHECK(!json_string(u, nullptr, 0U));
}

// ------------------------------------------------------------------ HA-03/06/07 (values)

void test_values() {
    ValueConfig v = default_value_config();
    CHECK(is_valid(v));  // disabled, empty: fine
    v.enabled = true;
    CHECK(!is_valid(v));  // enabled without entity
    std::strcpy(v.entity, "sensor.temperature_exterieure");
    CHECK(is_valid(v));
    char t[kTemplateOutMax];
    CHECK(build_template(v, t, sizeof(t)) > 0U);
    CHECK(std::strcmp(t, "{{ states('sensor.temperature_exterieure') }}") == 0);
    std::strcpy(v.attribute, "unit_of_measurement");
    CHECK(build_template(v, t, sizeof(t)) > 0U);
    CHECK(std::strcmp(t, "{{ state_attr('sensor.temperature_exterieure','unit_of_measurement') }}") == 0);
    v.mode = ValueMode::Template;
    std::strcpy(v.tmpl, "{{ states('a.b') | round(1) }} \"x\" \\");
    CHECK(is_valid(v));
    CHECK(build_template(v, t, sizeof(t)) > 0U);
    CHECK(std::strcmp(t, "{{ states('a.b') | round(1) }} \\\"x\\\" \\\\") == 0);
    CHECK_EQ(build_template(v, t, 10U), 0U);  // does not fit
    CHECK_EQ(build_template(v, nullptr, 0U), 0U);
    // Entity charset and shape.
    CHECK(is_valid_entity("sensor.x_1"));
    CHECK(!is_valid_entity("sensor"));
    CHECK(!is_valid_entity("Sensor.x"));
    CHECK(!is_valid_entity(".x"));
    CHECK(!is_valid_entity("x."));
    CHECK(!is_valid_entity(""));
    CHECK(!is_valid_entity(nullptr));
    char longe[80];
    std::memset(longe, 'a', 70U);
    longe[70] = '.';
    longe[71] = 'b';
    longe[72] = '\0';
    CHECK(!is_valid_entity(longe));
    CHECK(!is_valid_template(""));
    CHECK(!is_valid_template("a\tb"));
    CHECK(!is_valid_template(nullptr));
    v = default_value_config();
    v.enabled = true;
    v.mode = ValueMode::Entity;
    std::strcpy(v.entity, "sensor.a");
    std::strcpy(v.attribute, "Bad-Attr");
    CHECK(!is_valid(v));
    v.attribute[0] = '\0';
    v.decimals = 3U;
    CHECK(!is_valid(v));
    v.decimals = 0U;
    v.expiry_s = 10U;
    CHECK(!is_valid(v));
    v.expiry_s = 300U;
    std::strcpy(v.unit, "a\"b");
    CHECK(!is_valid(v));
    std::strcpy(v.unit, "W");
    v.icon = static_cast<ValueIcon>(99U);
    CHECK(!is_valid(v));
    v.icon = ValueIcon::Bolt;
    v.mode = static_cast<ValueMode>(5U);
    CHECK(!is_valid(v));
    ValueList l = two_values();
    CHECK(is_valid(l));
    l[2].entity[0] = '\0';
    CHECK(!is_valid(l));

    // HA-06: numbers.
    int32_t n = 0;
    CHECK(decode_number("3.4", 1U, n) && (n == 34));
    CHECK(decode_number("3.4", 0U, n) && (n == 3));
    CHECK(decode_number("3.456", 2U, n) && (n == 346));
    CHECK(decode_number("-12.5", 1U, n) && (n == -125));
    CHECK(decode_number("21", 1U, n) && (n == 210));
    CHECK(!decode_number("on", 1U, n));
    CHECK(!decode_number("unavailable", 1U, n));
    CHECK(!decode_number("unknown", 1U, n));
    CHECK(!decode_number("null", 1U, n));
    CHECK(!decode_number("", 1U, n));
    CHECK(!decode_number("3000000000", 0U, n));
    CHECK(!decode_number("250000000", 1U, n));  // 2.5e9 after scaling
    CHECK(!decode_number("1.5", 3U, n));
    CHECK(!decode_number(nullptr, 1U, n));
    // HA-07: booleans and text.
    bool b = false;
    CHECK(decode_boolean("on", b) && b);
    CHECK(decode_boolean("home", b) && b);
    CHECK(decode_boolean("open", b) && b);
    CHECK(decode_boolean("true", b) && b);
    CHECK(decode_boolean("off", b) && !b);
    CHECK(decode_boolean("not_home", b) && !b);
    CHECK(decode_boolean("closed", b) && !b);
    CHECK(decode_boolean("false", b) && !b);
    CHECK(!decode_boolean("maybe", b));
    CHECK(!decode_boolean("unknown", b));
    CHECK(!decode_boolean("", b));
    CHECK(decode_text("3.4"));
    CHECK(decode_text("on"));
    CHECK(!decode_text("unknown"));
    CHECK(!decode_text("unavailable"));
    CHECK(!decode_text("none"));
    CHECK(!decode_text(""));
    CHECK(!decode_text(nullptr));
    // decode_result: string, number literal, null, long string truncated.
    ValueState st{};
    const JsonSpan s1{"\"3.4\"", 5U};
    CHECK(decode_result(ValueType::Number, 1U, s1, st) && (st.number == 34));
    CHECK(std::strcmp(st.text, "3.4") == 0);
    const JsonSpan s2{"21.5", 4U};
    CHECK(decode_result(ValueType::Number, 1U, s2, st) && (st.number == 215));
    const JsonSpan s3{"null", 4U};
    CHECK(!decode_result(ValueType::Number, 1U, s3, st));
    CHECK(!st.valid);
    const JsonSpan s4{"\"on\"", 4U};
    CHECK(decode_result(ValueType::Boolean, 0U, s4, st) && st.boolean);
    CHECK(decode_result(ValueType::Text, 0U, s4, st));
    const char* longtext = "\"0123456789012345678901234567890123456789\"";
    const JsonSpan s5{longtext, slen(longtext)};
    CHECK(decode_result(ValueType::Text, 0U, s5, st));
    CHECK_EQ(slen(st.text), kTextMax);
    const JsonSpan s6{"12345678901234567890123456789012345678901234567890", 50U};
    CHECK(!decode_result(ValueType::Number, 0U, s6, st));  // truncated literal -> out of range anyway
    CHECK_EQ(slen(st.text), kTextMax);
    const JsonSpan s7{nullptr, 0U};
    CHECK(!decode_result(ValueType::Text, 0U, s7, st));
    // Names.
    ValueType ty{};
    CHECK(value_type_from_name("Texte", ty) && (ty == ValueType::Text));
    CHECK(value_type_from_name("Booleen", ty) && (ty == ValueType::Boolean));
    CHECK(value_type_from_name("Nombre", ty) && (ty == ValueType::Number));
    CHECK(!value_type_from_name("Float", ty));
    CHECK(!value_type_from_name(nullptr, ty));
    ValueMode mo{};
    CHECK(value_mode_from_name("Gabarit", mo) && (mo == ValueMode::Template));
    CHECK(value_mode_from_name("Entite", mo) && (mo == ValueMode::Entity));
    CHECK(!value_mode_from_name("x", mo));
    CHECK(!value_mode_from_name(nullptr, mo));
    CHECK(std::strcmp(value_mode_name(ValueMode::Template), "Gabarit") == 0);
    ValueIcon ic{};
    for (uint8_t i = 0U; i < static_cast<uint8_t>(ValueIcon::Count); ++i) {
        CHECK(value_icon_from_name(value_icon_name(static_cast<ValueIcon>(i)), ic) && (ic == static_cast<ValueIcon>(i)));
    }
    CHECK(!value_icon_from_name("Licorne", ic));
    CHECK(!value_icon_from_name(nullptr, ic));
    CHECK(std::strcmp(value_icon_name(ValueIcon::Count), "Aucune") == 0);
    CHECK(std::strcmp(value_type_name(static_cast<ValueType>(9U)), "Nombre") == 0);
    // Formatting for the panel.
    char f[24];
    CHECK(format_number(-125, 1U, "`C", f, sizeof(f)) > 0U);
    CHECK(std::strcmp(f, "-12.5`C") == 0);
    CHECK(format_number(346, 2U, "", f, sizeof(f)) > 0U);
    CHECK(std::strcmp(f, "3.46") == 0);
    CHECK(format_number(21, 0U, "W", f, sizeof(f)) > 0U);
    CHECK(std::strcmp(f, "21W") == 0);
    CHECK(format_number(5, 1U, nullptr, f, sizeof(f)) > 0U);
    CHECK(std::strcmp(f, "0.5") == 0);
    CHECK_EQ(format_number(5, 3U, "", f, sizeof(f)), 0U);
    CHECK_EQ(format_number(12345, 0U, "", f, 3U), 0U);
    CHECK_EQ(format_number(1, 0U, "", nullptr, 0U), 0U);
}

// ------------------------------------------------------------------ HA-09 (reassembly)

void test_reassembler() {
    Reassembler r;
    CHECK(!r.feed("{\"a\":", 5U, 0U, 9U));
    CHECK(r.feed("1234}", 5U, 5U, 10U) == false);  // total mismatch: dropped
    CHECK_EQ(r.dropped(), 1U);
    CHECK(!r.feed("{\"a\":", 5U, 0U, 10U));
    CHECK(r.feed("1234}", 5U, 5U, 10U));
    CHECK(std::strcmp(r.message(), "{\"a\":1234}") == 0);
    CHECK_EQ(r.length(), 10U);
    // Single frame.
    CHECK(r.feed("{}", 2U, 0U, 2U));
    // Too long: dropped, continuation ignored, counted once.
    static char big[3000];
    std::memset(big, 'x', sizeof(big));
    CHECK(!r.feed(big, 1000U, 0U, 3000U));
    CHECK(!r.feed(big, 1000U, 1000U, 3000U));
    CHECK(!r.feed(big, 1000U, 2000U, 3000U));
    CHECK_EQ(r.dropped(), 2U);
    // Out of order.
    CHECK(!r.feed("ab", 2U, 0U, 6U));
    CHECK(!r.feed("ef", 2U, 4U, 6U));
    CHECK_EQ(r.dropped(), 3U);
    CHECK(!r.feed("cd", 2U, 2U, 6U));  // still dropping that message
    CHECK_EQ(r.dropped(), 3U);
    // Overflow of the announced total.
    CHECK(!r.feed("ab", 2U, 0U, 3U));
    CHECK(!r.feed("cd", 2U, 2U, 3U));
    CHECK_EQ(r.dropped(), 4U);
    // Null data, zero total.
    CHECK(!r.feed(nullptr, 2U, 0U, 2U));
    CHECK(!r.feed("x", 1U, 0U, 0U));
    r.reset();
    CHECK_EQ(r.length(), 0U);
    // Exactly the maximum fits.
    CHECK(r.feed(big, kMessageMax, 0U, kMessageMax));
    CHECK_EQ(r.length(), kMessageMax);
}

// ------------------------------------------------------------------ HA-01/02/04/05/08/11/12/13

void test_client() {
    WsConfig cfg = enabled_config();
    CHECK(is_valid(cfg));
    WsConfig bad = cfg;
    bad.token = "";
    CHECK(!is_valid(bad));
    bad.enabled = false;
    CHECK(is_valid(bad));
    bad = cfg;
    bad.ping_s = 5U;
    CHECK(!is_valid(bad));
    bad = cfg;
    bad.pong_ms = 1000U;
    CHECK(!is_valid(bad));
    bad = cfg;
    bad.token = "with space";
    CHECK(!is_valid(bad));
    bad = cfg;
    bad.token = nullptr;
    CHECK(!is_valid(bad));
    static char longtoken[kTokenMax + 2U];
    std::memset(longtoken, 'a', sizeof(longtoken) - 1U);
    longtoken[sizeof(longtoken) - 1U] = '\0';
    bad = cfg;
    bad.token = longtoken;
    CHECK(!is_valid(bad));

    static char out[4096];
    // HA-01: full dialogue.
    {
        WsClient c(cfg, two_values());
        uint32_t now = 1000U;
        CHECK_ENUM(c.state(), LinkState::Waiting);
        CHECK(std::strcmp(c.state_name(), "waiting") == 0);
        bring_up(c, now, out, sizeof(out));
        CHECK(std::strstr(out, "{\"id\":1,\"type\":\"render_template\",\"template\":\"{{ states('sensor.temperature_exterieure') }}\",\"report_errors\":true}") != nullptr);
        CHECK(std::strstr(out, "{\"id\":2,\"type\":\"render_template\",\"template\":\"{{ states('binary_sensor.porte_garage') }}\"") != nullptr);
        CHECK(std::strstr(out, "{\"id\":3,\"type\":\"subscribe_events\",\"event_type\":\"horloge_notification\"}") != nullptr);
        CHECK_EQ(c.stats().connections, 1U);
        CHECK(c.link_up());
        CHECK(std::strcmp(c.state_name(), "running") == 0);
        // HA-05: initial value then changes, right id -> right value.
        feed(c, "{\"id\":1,\"type\":\"event\",\"event\":{\"result\":\"3.4\",\"listeners\":{\"all\":false}}}", now);
        CHECK(c.value(0).valid);
        CHECK_EQ(c.value(0).number, 34);
        CHECK(c.value_valid(0U, now));
        CHECK(!c.value(2).valid);
        feed(c, "{\"id\":1,\"type\":\"event\",\"event\":{\"result\":3.2}}", now);  // native number
        CHECK_EQ(c.value(0).number, 32);
        feed(c, "{\"id\":2,\"type\":\"event\",\"event\":{\"result\":\"on\"}}", now);
        CHECK(c.value(2).valid && c.value(2).boolean);
        feed(c, "{\"id\":2,\"type\":\"event\",\"event\":{\"result\":\"unavailable\"}}", now);
        CHECK(!c.value(2).valid);
        CHECK(!c.value_valid(2U, now));
        feed(c, "{\"id\":7,\"type\":\"event\",\"event\":{\"result\":\"1\"}}", now);  // unknown id
        CHECK_EQ(c.stats().events, 4U);
        CHECK_EQ(c.stats().messages_ignored, 1U);
        // HA-08: template error bounded to 96 characters, value invalid.
        char err[400];
        (void)std::snprintf(err, sizeof(err), "{\"id\":1,\"type\":\"event\",\"event\":{\"error\":\"%s\",\"level\":\"ERROR\"}}",
                            "TemplateError: UndefinedError: 'None' has no attribute 'state' and this message goes on and on and on and on and on and on");
        feed(c, err, now);
        CHECK(!c.value(0).valid);
        CHECK_EQ(slen(c.value(0).error), kErrorMax);
        feed(c, "{\"id\":1,\"type\":\"event\",\"event\":{\"result\":\"5\"}}", now);
        CHECK(c.value(0).valid);
        CHECK_EQ(c.value(0).error[0], '\0');
        // HA-13: notification event.
        feed(c, "{\"id\":3,\"type\":\"event\",\"event\":{\"event_type\":\"horloge_notification\",\"data\":{\"cle\":\"garage_ouvert\","
                "\"titre\":\"GARAGE\",\"texte\":\"Porte ouverte depuis 10 min\",\"priorite\":2,\"duree_s\":60,\"persistante\":true,"
                "\"rappel_s\":120},\"origin\":\"LOCAL\"}}", now);
        alerts::Notification n{};
        CHECK(c.take_notification(n));
        CHECK(std::strcmp(n.key, "garage_ouvert") == 0);
        CHECK(std::strcmp(n.title, "GARAGE") == 0);
        CHECK(std::strcmp(n.text, "Porte ouverte depuis 10 min") == 0);
        CHECK_EQ(n.priority, 2U);
        CHECK_EQ(n.duration_s, 60U);
        CHECK(n.persistent);
        CHECK_EQ(n.repeat_s, 120U);
        CHECK(!c.take_notification(n));
        feed(c, "{\"id\":3,\"type\":\"event\",\"event\":{\"data\":{\"texte\":\"sans cle\"}}}", now);
        CHECK(!c.take_notification(n));
        CHECK_EQ(c.stats().notifications, 1U);
        CHECK_EQ(c.stats().notifications_rejected, 1U);
        feed(c, "{\"id\":3,\"type\":\"event\",\"event\":{\"data\":{\"cle\":\"garage_ouvert\",\"effacer\":true}}}", now);
        CHECK(c.take_notification(n));
        CHECK(n.clear);
        // Out-of-bound field replaced by its default and counted (ALR-15 rule).
        feed(c, "{\"id\":3,\"type\":\"event\",\"event\":{\"data\":{\"cle\":\"x\",\"priorite\":9}}}", now);
        CHECK(c.take_notification(n));
        CHECK_EQ(n.priority, 1U);
        CHECK(n.defaults_used >= 1U);
        // HA-10: garbage and unexpected messages leave the state unchanged.
        feed(c, "not json", now);
        feed(c, "{\"type\":12}", now);
        feed(c, "{\"id\":\"x\",\"type\":\"event\"}", now);
        feed(c, "{\"id\":1,\"type\":\"weird\"}", now);
        feed(c, "{\"type\":\"auth_required\"}", now);
        feed(c, "{\"type\":\"auth_ok\"}", now);
        c.on_message(nullptr, 0U, now);
        CHECK_ENUM(c.state(), LinkState::Running);
        CHECK(c.value(0).valid);
        // HA-11: ping after ping_s, pong clears; no pong -> reconnection.
        now += 30000U;
        c.step(now);
        CHECK_EQ(drain(c, out, sizeof(out)), 1U);
        CHECK(std::strstr(out, "\"type\":\"ping\"") != nullptr);
        const char* idp = std::strstr(out, "\"id\":");
        CHECK(idp != nullptr);
        feed(c, "{\"id\":4,\"type\":\"pong\"}", now + 100U);
        c.step(now + 20000U);
        CHECK_ENUM(c.state(), LinkState::Running);
        CHECK(!c.take_close_request());
        now += 30000U;
        c.step(now);
        CHECK_EQ(drain(c, out, sizeof(out)), 1U);
        feed(c, "{\"id\":99,\"type\":\"pong\"}", now);  // wrong id: ignored
        c.step(now + 9999U);
        CHECK_ENUM(c.state(), LinkState::Running);
        c.step(now + 10000U);
        CHECK_ENUM(c.state(), LinkState::Waiting);
        CHECK(c.take_close_request());
        CHECK_EQ(c.stats().pong_misses, 1U);
        CHECK_EQ(c.stats().disconnections, 1U);
        // HA-12: expiry only while the link is down.
        CHECK(c.value_valid(0U, now + 10000U));
        CHECK(c.value_valid(0U, now + 10000U + 299000U));
        CHECK(!c.value_valid(0U, now + 10000U + 300000U));
        CHECK(!c.value_valid(9U, now));
        // HA-04: backoff 5 s then reconnection with ids from 1 and every subscription replayed.
        now += 10000U;
        c.step(now + 4999U);
        CHECK(!c.take_connect_request());
        c.step(now + 5000U);
        CHECK(c.take_connect_request());
        now += 5000U;
        c.on_connected(now);
        feed(c, "{\"type\":\"auth_required\"}", now);
        CHECK_EQ(drain(c, out, sizeof(out)), 1U);
        feed(c, "{\"type\":\"auth_ok\"}", now);
        CHECK_EQ(drain(c, out, sizeof(out)), 3U);
        CHECK(std::strstr(out, "{\"id\":1,\"type\":\"render_template\"") != nullptr);
        CHECK(std::strstr(out, "{\"id\":2,\"type\":\"render_template\"") != nullptr);
        CHECK(std::strstr(out, "{\"id\":3,\"type\":\"subscribe_events\"") != nullptr);
        // A refused subscription keeps its error, the link still comes up.
        feed(c, "{\"id\":1,\"type\":\"result\",\"success\":false,\"error\":{\"code\":\"template_error\",\"message\":\"bad template\"}}", now);
        feed(c, "{\"id\":2,\"type\":\"result\",\"success\":true}", now);
        feed(c, "{\"id\":3,\"type\":\"result\",\"success\":true}", now);
        CHECK_ENUM(c.state(), LinkState::Running);
        CHECK(std::strcmp(c.value(0).error, "bad template") == 0);
        CHECK_EQ(c.stats().connections, 2U);
        // The transport drops: counted, backoff reset after the successful session.
        c.on_disconnected(now);
        CHECK_ENUM(c.state(), LinkState::Waiting);
        CHECK_EQ(c.backoff_ms(), 10000U);
        CHECK_EQ(c.stats().disconnections, 2U);
        // Stale value after the link dropped, then its expiry.
        CHECK(c.value_valid(2U, now) == c.value(2).valid);
        // Disabling closes nothing (already down) and ignores further messages.
        WsConfig off = cfg;
        off.enabled = false;
        c.configure(off, two_values(), now);
        CHECK_ENUM(c.state(), LinkState::Disabled);
        c.step(now + 100000U);
        CHECK(!c.take_connect_request());
        feed(c, "{\"type\":\"auth_required\"}", now);
        CHECK_EQ(drain(c, out, sizeof(out)), 0U);
        c.on_connected(now);
        CHECK(c.take_close_request());
    }
    // HA-02: token refused -> 5 min, no hammering.
    {
        WsClient c(cfg, two_values());
        uint32_t now = 0U;
        c.step(now);
        CHECK(c.take_connect_request());
        c.on_connected(now);
        feed(c, "{\"type\":\"auth_required\"}", now);
        (void)drain(c, out, sizeof(out));
        feed(c, "{\"type\":\"auth_invalid\",\"message\":\"Invalid access token or password\"}", now);
        CHECK_ENUM(c.state(), LinkState::TokenRejected);
        CHECK(std::strcmp(c.state_name(), "token_rejected") == 0);
        CHECK(std::strcmp(c.last_error(), "Invalid access token or password") == 0);
        CHECK(c.take_close_request());
        c.on_disconnected(now);  // transport confirms: no state change
        CHECK_ENUM(c.state(), LinkState::TokenRejected);
        for (uint32_t t = 1000U; t < 300000U; t += 10000U) {
            c.step(t);
            CHECK(!c.take_connect_request());
        }
        c.step(300000U);
        CHECK(c.take_connect_request());
        // auth_invalid without a message.
        c.on_connected(300000U);
        feed(c, "{\"type\":\"auth_invalid\"}", 300000U);
        CHECK(std::strcmp(c.last_error(), "auth_invalid") == 0);
    }
    // HA-11 (backoff ladder): 5, 10, 20, 40, 60, 60 s between failed attempts.
    {
        WsClient c(cfg, two_values());
        uint32_t now = 0U;
        const uint32_t expected[] = {5000U, 10000U, 20000U, 40000U, 60000U, 60000U};
        c.step(now);
        CHECK(c.take_connect_request());
        for (uint32_t e : expected) {
            c.on_connected(now);
            c.on_disconnected(now);  // socket dropped before auth
            c.step(now + e - 1U);
            CHECK(!c.take_connect_request());
            c.step(now + e);
            CHECK(c.take_connect_request());
            now += e;
        }
        // A connection that never answers is also retried (stuck in Connecting).
        c.on_connected(now);
        c.step(now + 60000U);
        CHECK(c.take_close_request());
        CHECK_ENUM(c.state(), LinkState::Waiting);
    }
    // No values, no notifications: auth_ok is enough to be Running; reconfiguration restarts the link.
    {
        WsConfig bare = cfg;
        bare.notifications = false;
        WsClient c(bare, default_value_list());
        uint32_t now = 0U;
        c.step(now);
        (void)c.take_connect_request();
        c.on_connected(now);
        feed(c, "{\"type\":\"auth_required\"}", now);
        (void)drain(c, out, sizeof(out));
        feed(c, "{\"type\":\"auth_ok\"}", now);
        CHECK_ENUM(c.state(), LinkState::Running);
        CHECK_EQ(drain(c, out, sizeof(out)), 0U);
        c.configure(cfg, two_values(), now);
        CHECK(c.take_close_request());
        CHECK_ENUM(c.state(), LinkState::Waiting);
        c.step(now);
        CHECK(c.take_connect_request());
        // Invalid config / values fall back to defaults (disabled).
        WsClient d(bad, two_values());
        CHECK_ENUM(d.state(), LinkState::Disabled);
        ValueList broken = two_values();
        broken[0].decimals = 7U;
        WsClient e(cfg, broken);
        CHECK(!e.value_config(0).enabled);
    }
    // Allocation-free on the hot path.
    {
        WsClient c(cfg, two_values());
        uint32_t now = 0U;
        bring_up(c, now, out, sizeof(out));
        AllocGuard guard;
        feed(c, "{\"id\":1,\"type\":\"event\",\"event\":{\"result\":\"3.4\"}}", now);
        c.step(now + 30000U);
        (void)drain(c, out, sizeof(out));
        CHECK_EQ(g_alloc_count, 0U);
    }
}

// ------------------------------------------------------------------ HA-14 (weather)

void test_weather() {
    WeatherConfig w = default_weather_config();
    CHECK(!w.enabled);
    CHECK(is_valid(w));
    w.latitude_e4 = 462000;
    w.longitude_e4 = 61400;
    char url[160];
    CHECK(weather_url(w, url, sizeof(url)) > 0U);
    CHECK(std::strcmp(url, "https://api.open-meteo.com/v1/forecast?latitude=46.2000&longitude=6.1400&current=temperature_2m") == 0);
    w.latitude_e4 = -338688;
    w.longitude_e4 = 1512093;
    CHECK(weather_url(w, url, sizeof(url)) > 0U);
    CHECK(std::strstr(url, "latitude=-33.8688&longitude=151.2093") != nullptr);
    CHECK_EQ(weather_url(w, url, 20U), 0U);
    CHECK_EQ(weather_url(w, nullptr, 0U), 0U);
    w.latitude_e4 = 900001;
    CHECK(!is_valid(w));
    CHECK_EQ(weather_url(w, url, sizeof(url)), 0U);
    w = default_weather_config();
    w.period_s = 100U;
    CHECK(!is_valid(w));
    // Real response from the fixture.
    static char json[1024];
    std::FILE* f = std::fopen(CLOCK_FIXTURES "/open_meteo.json", "rb");
    CHECK(f != nullptr);
    std::size_t n = 0U;
    if (f != nullptr) {
        n = std::fread(json, 1U, sizeof(json) - 1U, f);
        (void)std::fclose(f);
    }
    json[n] = '\0';
    int16_t t = 0;
    CHECK(decode_weather(json, n, t));
    CHECK_EQ(t, 73);
    CHECK(!decode_weather(json, n / 2U, t));  // truncated
    const char* nul = "{\"current\":{\"temperature_2m\":null}}";
    CHECK(!decode_weather(nul, slen(nul), t));
    const char* absent = "{\"current\":{\"time\":\"x\"}}";
    CHECK(!decode_weather(absent, slen(absent), t));
    const char* hot = "{\"current\":{\"temperature_2m\":99.5}}";
    CHECK(!decode_weather(hot, slen(hot), t));
    const char* neg = "{\"current\":{\"temperature_2m\":-12.34}}";
    CHECK(decode_weather(neg, slen(neg), t) && (t == -123));
}

}  // namespace

void run_homeassistant_tests() {
    test_json_reader();
    test_values();
    test_reassembler();
    test_client();
    test_weather();
}
