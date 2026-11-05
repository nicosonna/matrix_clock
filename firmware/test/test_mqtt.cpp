/**
 * @file test_mqtt.cpp
 * @brief MQ-01 .. MQ-10: MQTT settings, topics, discovery, state, publish policy.
 */
#include "test_support.hpp"

#include <cstdio>
#include <cstring>

#include "homeassistant/json_reader.hpp"
#include "homeassistant/mqtt.hpp"
#include "settings/forms.hpp"
#include "settings/json.hpp"
#include "settings/model.hpp"

using namespace homeassistant;

namespace {

DeviceIdentity ident() {
    DeviceIdentity d{};
    const std::array<uint8_t, 6> mac{{0x10U, 0x20U, 0xBAU, 0x5CU, 0x7DU, 0xF4U}};
    CHECK(build_identity(mac, "0.1.0", "horloge-v1", "10.0.0.5", d));
    return d;
}

air_quality::AirReading good_reading() {
    air_quality::AirReading r{};
    r.present = true;
    r.fault = false;
    r.warming_up = false;
    r.initial_start = false;
    r.air_valid = true;
    r.aqi = 2U;
    r.tvoc_ppb = 123U;
    r.eco2_ppm = 654U;
    r.climate_valid = true;
    r.temperature_centi = 2149;  // 21.49 -> 21.5
    r.humidity_pct10 = 456U;
    return r;
}

MqttState good_state() {
    MqttState s{};
    s.air_enabled = true;
    s.air = good_reading();
    s.level = air_quality::AirLevel::Good;
    s.ldr_valid = true;
    s.ldr_raw = 1234U;
    s.time_sure = true;
    return s;
}

/// Finds `"key":` in a JSON object (top level) and returns the raw value start.
const char* value_of(const char* json, const char* key) {
    char needle[48];
    (void)std::snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char* p = std::strstr(json, needle);
    return (p == nullptr) ? nullptr : (p + std::strlen(needle));
}

bool value_is(const char* json, const char* key, const char* expected) {
    const char* v = value_of(json, key);
    return (v != nullptr) && (std::strncmp(v, expected, std::strlen(expected)) == 0) &&
           ((v[std::strlen(expected)] == ',') || (v[std::strlen(expected)] == '}'));
}

/// Pulls actions until None (at most 16), writes them to @p out.
std::size_t drain(MqttPublisher& p, uint32_t now, MqttAction* out, std::size_t max) {
    std::size_t n = 0U;
    while (n < max) {
        const MqttAction a = p.next_action(now);
        if (a == MqttAction::None) {
            break;
        }
        out[n++] = a;
    }
    return n;
}

MqttConfig enabled_config() {
    MqttConfig c = default_mqtt_config();
    c.enabled = true;
    std::strcpy(c.host, "broker.lan");
    std::strcpy(c.user, "horloge");
    std::strcpy(c.password, "secret");
    c.period_s = 30U;
    return c;
}

// MQ-01: topics.
void test_topics() {
    const DeviceIdentity d = ident();
    CHECK(std::strcmp(d.id.data(), "1020ba5c7df4") == 0);
    CHECK(std::strcmp(d.mac.data(), "10:20:ba:5c:7d:f4") == 0);
    CHECK(std::strcmp(d.url.data(), "http://10.0.0.5/") == 0);
    char t[kMqttTopicMax];
    CHECK(mqtt_topic(MqttTopic::Availability, d, t, sizeof(t)) > 0U);
    CHECK(std::strcmp(t, "horloge/1020ba5c7df4/disponible") == 0);
    CHECK(mqtt_topic(MqttTopic::State, d, t, sizeof(t)) > 0U);
    CHECK(std::strcmp(t, "horloge/1020ba5c7df4/etat") == 0);
    CHECK(ha_status_topic("homeassistant", t, sizeof(t)) > 0U);
    CHECK(std::strcmp(t, "homeassistant/status") == 0);
    // Overflow refused, buffer emptied.
    char small[12];
    CHECK_EQ(mqtt_topic(MqttTopic::State, d, small, sizeof(small)), 0U);
    CHECK(small[0] == '\0');
    CHECK_EQ(mqtt_topic(MqttTopic::State, d, nullptr, 0U), 0U);
    // Identity without IP: no configuration_url; too long strings refused.
    DeviceIdentity e{};
    const std::array<uint8_t, 6> mac{{1U, 2U, 3U, 4U, 5U, 6U}};
    CHECK(build_identity(mac, "0.1.0", "horloge-v1", nullptr, e));
    CHECK(e.url[0] == '\0');
    CHECK(build_identity(mac, "0.1.0", "horloge-v1", "", e));
    CHECK(!build_identity(mac, "0.1.0-une-version-beaucoup-trop-longue", "horloge-v1", nullptr, e));
    CHECK(!build_identity(mac, "0.1.0", "un-nom-de-carte-beaucoup-trop-long", nullptr, e));
    CHECK(!build_identity(mac, nullptr, "horloge-v1", nullptr, e));
    CHECK(!build_identity(mac, "0.1.0", "horloge-v1", "une-adresse-beaucoup-trop-longue-pour-le-tampon-prevu", e));
}

// MQ-02: discovery messages, one per entity, JSON valid and complete.
void test_discovery() {
    const DeviceIdentity d = ident();
    static char topic[kMqttTopicMax];
    static char payload[kMqttDiscoveryMax];
    CHECK_EQ(mqtt_entity_count(), 10U);
    for (std::size_t i = 0U; i < mqtt_entity_count(); ++i) {
        const std::size_t len = mqtt_discovery(i, d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload));
        CHECK(len > 0U);
        if (len == 0U) {
            continue;
        }
        CHECK(len < kMqttDiscoveryMax);
        // Topic: <prefix>/<platform>/horloge_<id>/<object>/config
        char expected[kMqttTopicMax];
        const bool binary = std::strstr(payload, "\"payload_on\":\"ON\"") != nullptr;
        (void)std::snprintf(expected, sizeof(expected), "homeassistant/%s/horloge_1020ba5c7df4/%s/config",
                            binary ? "binary_sensor" : "sensor", mqtt_entity_object(i));
        CHECK(std::strcmp(topic, expected) == 0);
        // JSON parses: name, unique_id, state_topic, device.identifiers[0], availability.
        JsonSpan v{};
        const char* p_name[] = {"name"};
        const char* p_uid[] = {"unique_id"};
        const char* p_state[] = {"state_topic"};
        const char* p_avail[] = {"availability_topic"};
        const char* p_tmpl[] = {"value_template"};
        const char* p_origin[] = {"origin", "name"};
        const char* p_url[] = {"device", "configuration_url"};
        char s[96];
        CHECK(json_find(payload, len, p_name, 1U, v) && json_is_string(v));
        CHECK(json_find(payload, len, p_uid, 1U, v) && json_string(v, s, sizeof(s)));
        char uid[64];
        (void)std::snprintf(uid, sizeof(uid), "horloge_1020ba5c7df4_%s", mqtt_entity_object(i));
        CHECK(std::strcmp(s, uid) == 0);
        CHECK(json_find(payload, len, p_state, 1U, v) && json_string(v, s, sizeof(s)));
        CHECK(std::strcmp(s, "horloge/1020ba5c7df4/etat") == 0);
        CHECK(std::strstr(payload, "\"device\":{\"identifiers\":[\"horloge_1020ba5c7df4\"]") != nullptr);
        CHECK(std::strstr(payload, "\"connections\":[[\"mac\",\"10:20:ba:5c:7d:f4\"]]") != nullptr);
        CHECK(json_find(payload, len, p_avail, 1U, v) && json_string(v, s, sizeof(s)));
        CHECK(std::strcmp(s, "horloge/1020ba5c7df4/disponible") == 0);
        CHECK(json_find(payload, len, p_origin, 2U, v) && json_string(v, s, sizeof(s)));
        CHECK(std::strcmp(s, "Horloge") == 0);
        CHECK(json_find(payload, len, p_url, 2U, v) && json_string(v, s, sizeof(s)));
        CHECK(std::strcmp(s, "http://10.0.0.5/") == 0);
        CHECK(json_find(payload, len, p_tmpl, 1U, v) && json_string(v, s, sizeof(s)));
        char tmpl[64];
        (void)std::snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", mqtt_entity_key(i));
        CHECK(std::strcmp(s, tmpl) == 0);
        CHECK(std::strstr(payload, "\"has_entity_name\":true") != nullptr);
        CHECK(std::strstr(payload, "\"payload_available\":\"online\"") != nullptr);
        CHECK(std::strstr(payload, "\"payload_not_available\":\"offline\"") != nullptr);
        CHECK(std::strstr(payload, "\"model\":\"horloge-v1\"") != nullptr);
        CHECK(std::strstr(payload, "\"sw_version\":\"0.1.0\"") != nullptr);
        // Home Assistant rules: a measurement needs a state_class, an enum needs options and no unit.
        const bool enum_sensor = std::strstr(payload, "\"device_class\":\"enum\"") != nullptr;
        if (enum_sensor) {
            CHECK(std::strstr(payload, "\"options\":[\"excellent\",\"bon\",\"moyen\",\"mauvais\",\"tres_mauvais\"]") != nullptr);
            CHECK(std::strstr(payload, "unit_of_measurement") == nullptr);
            CHECK(std::strstr(payload, "state_class") == nullptr);
        }
        if (binary) {
            CHECK(std::strstr(payload, "state_class") == nullptr);
            CHECK(std::strstr(payload, "unit_of_measurement") == nullptr);
        }
    }
    // Specific classes and units (spec table).
    (void)mqtt_discovery(0U, d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload));
    CHECK(std::strstr(payload, "\"device_class\":\"temperature\"") != nullptr);
    CHECK(std::strstr(payload, "\"unit_of_measurement\":\"°C\"") != nullptr);
    CHECK(std::strstr(payload, "\"state_class\":\"measurement\"") != nullptr);
    CHECK(std::strstr(payload, "\"suggested_display_precision\":1") != nullptr);
    (void)mqtt_discovery(2U, d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload));
    CHECK(std::strstr(payload, "\"device_class\":\"carbon_dioxide\"") != nullptr);
    CHECK(std::strstr(payload, "\"unit_of_measurement\":\"ppm\"") != nullptr);
    (void)mqtt_discovery(3U, d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload));
    CHECK(std::strstr(payload, "\"device_class\":\"volatile_organic_compounds_parts\"") != nullptr);
    (void)mqtt_discovery(6U, d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload));
    CHECK(std::strstr(payload, "\"device_class\":\"problem\"") != nullptr);
    CHECK(std::strstr(payload, "\"entity_category\":\"diagnostic\"") != nullptr);
    (void)mqtt_discovery(9U, d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload));
    CHECK(std::strstr(payload, "\"device_class\":\"connectivity\"") != nullptr);
    // Another prefix, out of range, too small buffers, no prefix.
    CHECK(mqtt_discovery(0U, d, "ha/disc", topic, sizeof(topic), payload, sizeof(payload)) > 0U);
    CHECK(std::strncmp(topic, "ha/disc/sensor/", 15U) == 0);
    CHECK_EQ(mqtt_discovery(mqtt_entity_count(), d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload)), 0U);
    CHECK_EQ(mqtt_discovery(0U, d, nullptr, topic, sizeof(topic), payload, sizeof(payload)), 0U);
    CHECK_EQ(mqtt_discovery(0U, d, "homeassistant", topic, 20U, payload, sizeof(payload)), 0U);
    CHECK_EQ(mqtt_discovery(0U, d, "homeassistant", topic, sizeof(topic), payload, 200U), 0U);
    CHECK(payload[0] == '\0');
    CHECK(mqtt_entity_object(99U) == nullptr);
    CHECK(mqtt_entity_key(99U) == nullptr);
}

// MQ-03: every value_template reads a published key, every published key is read.
void test_keys_consistency() {
    const DeviceIdentity d = ident();
    static char topic[kMqttTopicMax];
    static char payload[kMqttDiscoveryMax];
    static char state[kMqttStateMax];
    const std::size_t slen = mqtt_state_json(good_state(), state, sizeof(state));
    CHECK(slen > 0U);
    // Every entity key is in the state.
    for (std::size_t i = 0U; i < mqtt_entity_count(); ++i) {
        (void)mqtt_discovery(i, d, "homeassistant", topic, sizeof(topic), payload, sizeof(payload));
        const char* key = mqtt_entity_key(i);
        CHECK(key != nullptr);
        if (key == nullptr) {
            continue;
        }
        const bool published = value_of(state, key) != nullptr;
        if (!published) {
            std::printf("  cle lue mais non publiee : %s\n", key);
        }
        CHECK(published);
        // Object ids are unique.
        for (std::size_t j = 0U; j < i; ++j) {
            CHECK(std::strcmp(mqtt_entity_object(i), mqtt_entity_object(j)) != 0);
        }
    }
    // Every state key is read by an entity: walk the top-level keys.
    const char* p = state + 1;
    uint32_t keys = 0U;
    while ((p = std::strchr(p, '"')) != nullptr) {
        const char* end = std::strchr(p + 1, '"');
        if ((end == nullptr) || (end[1] != ':')) {
            break;
        }
        char key[32];
        const std::size_t n = static_cast<std::size_t>(end - p - 1);
        if (n >= sizeof(key)) {
            break;
        }
        std::memcpy(key, p + 1, n);
        key[n] = '\0';
        bool read = false;
        for (std::size_t i = 0U; i < mqtt_entity_count(); ++i) {
            read = read || (std::strcmp(mqtt_entity_key(i), key) == 0);
        }
        if (!read) {
            std::printf("  cle publiee mais lue par aucune entite : %s\n", key);
        }
        CHECK(read);
        ++keys;
        // Skip the value (a string may contain no quote in our state document).
        p = end + 2;
        const char* next = std::strpbrk(p, ",}");
        if (next == nullptr) {
            break;
        }
        p = next;
    }
    CHECK_EQ(keys, mqtt_entity_count());
}

// MQ-04 / MQ-05: state document, decimals, null for everything invalid, level enum.
void test_state() {
    static char out[kMqttStateMax];
    MqttState s = good_state();
    std::size_t len = mqtt_state_json(s, out, sizeof(out));
    CHECK(len > 0U);
    CHECK(value_is(out, "temperature", "21.5"));
    CHECK(value_is(out, "humidite", "45.6"));
    CHECK(value_is(out, "eco2", "654"));
    CHECK(value_is(out, "tvoc", "123"));
    CHECK(value_is(out, "aqi", "2"));
    CHECK(value_is(out, "niveau", "\"bon\""));
    CHECK(value_is(out, "capteur_defaut", "\"OFF\""));
    CHECK(value_is(out, "prechauffage", "\"OFF\""));
    CHECK(value_is(out, "luminosite_brute", "1234"));
    CHECK(value_is(out, "heure_sure", "\"ON\""));
    // Negative temperature, rounding half away from zero, zero padding.
    s.air.temperature_centi = -5;   // -0.05 -> -0.1
    s.air.humidity_pct10 = 7U;      // 0.7
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "temperature", "-0.1"));
    CHECK(value_is(out, "humidite", "0.7"));
    s.air.temperature_centi = -1234;  // -12.34 -> -12.3
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "temperature", "-12.3"));
    s.air.temperature_centi = 2005;   // 20.05 -> 20.1
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "temperature", "20.1"));
    // Warm-up: air keys null, climate still there, prechauffage ON, niveau null even if the tracker says otherwise.
    s = good_state();
    s.air.warming_up = true;
    s.air.air_valid = false;
    s.level = air_quality::AirLevel::Unknown;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "eco2", "null"));
    CHECK(value_is(out, "tvoc", "null"));
    CHECK(value_is(out, "aqi", "null"));
    CHECK(value_is(out, "niveau", "null"));
    CHECK(value_is(out, "temperature", "21.5"));
    CHECK(value_is(out, "prechauffage", "\"ON\""));
    CHECK(value_is(out, "capteur_defaut", "\"OFF\""));
    // First hour: prechauffage ON as well.
    s = good_state();
    s.air.initial_start = true;
    s.air.air_valid = false;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "prechauffage", "\"ON\""));
    CHECK(value_is(out, "aqi", "null"));
    // Stale level with invalid air is never published (honesty rule).
    s = good_state();
    s.air.air_valid = false;
    s.level = air_quality::AirLevel::Poor;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "niveau", "null"));
    // Sensor fault: everything null, capteur_defaut ON, prechauffage unknown.
    s = good_state();
    s.air.fault = true;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "temperature", "null"));
    CHECK(value_is(out, "humidite", "null"));
    CHECK(value_is(out, "eco2", "null"));
    CHECK(value_is(out, "niveau", "null"));
    CHECK(value_is(out, "capteur_defaut", "\"ON\""));
    CHECK(value_is(out, "prechauffage", "null"));
    // Absent sensor: capteur_defaut ON, prechauffage null.
    s = good_state();
    s.air.present = false;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "capteur_defaut", "\"ON\""));
    CHECK(value_is(out, "prechauffage", "null"));
    CHECK(value_is(out, "temperature", "null"));
    // Sensor disabled on the page: no opinion at all.
    s = good_state();
    s.air_enabled = false;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "capteur_defaut", "null"));
    CHECK(value_is(out, "prechauffage", "null"));
    CHECK(value_is(out, "aqi", "null"));
    CHECK(value_is(out, "luminosite_brute", "1234"));
    // Climate invalid alone (AHT21 CRC): only temperature/humidity null.
    s = good_state();
    s.air.climate_valid = false;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "temperature", "null"));
    CHECK(value_is(out, "humidite", "null"));
    CHECK(value_is(out, "eco2", "654"));
    // LDR invalid, time unsure.
    s = good_state();
    s.ldr_valid = false;
    s.time_sure = false;
    (void)mqtt_state_json(s, out, sizeof(out));
    CHECK(value_is(out, "luminosite_brute", "null"));
    CHECK(value_is(out, "heure_sure", "\"OFF\""));
    // MQ-05: the five enum values.
    const air_quality::AirLevel levels[] = {air_quality::AirLevel::Excellent, air_quality::AirLevel::Good,
                                            air_quality::AirLevel::Moderate, air_quality::AirLevel::Poor,
                                            air_quality::AirLevel::VeryPoor};
    const char* const names[] = {"\"excellent\"", "\"bon\"", "\"moyen\"", "\"mauvais\"", "\"tres_mauvais\""};
    for (std::size_t i = 0U; i < 5U; ++i) {
        s = good_state();
        s.level = levels[i];
        (void)mqtt_state_json(s, out, sizeof(out));
        CHECK(value_is(out, "niveau", names[i]));
        // Each option is in the discovery list.
        char bare[16];
        std::strncpy(bare, names[i] + 1, sizeof(bare) - 1U);
        bare[sizeof(bare) - 1U] = '\0';
        bare[std::strlen(bare) - 1U] = '\0';
        CHECK(std::strcmp(mqtt_level_option(levels[i]), bare) == 0);
    }
    CHECK(mqtt_level_option(air_quality::AirLevel::Unknown) == nullptr);
    // Too small buffer: 0 and emptied.
    CHECK_EQ(mqtt_state_json(good_state(), out, 40U), 0U);
    CHECK(out[0] == '\0');
    // Fits the declared maximum with margin.
    len = mqtt_state_json(good_state(), out, sizeof(out));
    CHECK(len < (kMqttStateMax / 2U));
}

// MQ-06 / MQ-07 / MQ-10: publish policy.
void test_publisher() {
    MqttAction acts[16];
    MqttPublisher p;
    CHECK_ENUM(p.link_state(), MqttLinkState::Disabled);
    CHECK(std::strcmp(mqtt_link_state_name(p.link_state()), "disabled") == 0);
    // Disabled by default: nothing to do, even after a configure.
    CHECK_EQ(drain(p, 0U, acts, 16U), 0U);
    p.configure(default_mqtt_config());
    CHECK_EQ(drain(p, 0U, acts, 16U), 0U);
    // Enabled: Connect once.
    p.configure(enabled_config());
    CHECK_EQ(drain(p, 1000U, acts, 16U), 1U);
    CHECK_ENUM(acts[0], MqttAction::Connect);
    CHECK_ENUM(p.link_state(), MqttLinkState::Connecting);
    CHECK(std::strcmp(mqtt_link_state_name(p.link_state()), "connecting") == 0);
    // Connected: online, discovery, but no state before the first measurement.
    p.on_connected();
    CHECK_EQ(drain(p, 2000U, acts, 16U), 2U);
    CHECK_ENUM(acts[0], MqttAction::PublishOnline);
    CHECK_ENUM(acts[1], MqttAction::PublishDiscovery);
    CHECK_ENUM(p.link_state(), MqttLinkState::Connected);
    CHECK_EQ(p.stats().connections, 1U);
    CHECK_EQ(p.stats().discoveries, 1U);
    // First measurement: state at once (period elapsed since 0).
    MqttState s = good_state();
    p.update_state(s, 3000U);
    CHECK_EQ(drain(p, 3000U, acts, 16U), 1U);
    CHECK_ENUM(acts[0], MqttAction::PublishState);
    CHECK_EQ(p.stats().states, 1U);
    CHECK(p.has_state());
    CHECK_EQ(p.state().ldr_raw, 1234U);
    // MQ-06: new measurements every second, same level: nothing before 30 s.
    for (uint32_t t = 4000U; t < 33000U; t += 1000U) {
        s.ldr_raw = static_cast<uint16_t>(t / 100U);
        p.update_state(s, t);
        CHECK_EQ(drain(p, t, acts, 16U), 0U);
    }
    p.update_state(s, 33000U);
    CHECK_EQ(drain(p, 33000U, acts, 16U), 1U);
    CHECK_ENUM(acts[0], MqttAction::PublishState);
    // Level change: published without waiting.
    s.level = air_quality::AirLevel::Poor;
    p.update_state(s, 34000U);
    CHECK_EQ(drain(p, 34000U, acts, 16U), 1U);
    CHECK_ENUM(acts[0], MqttAction::PublishState);
    CHECK_EQ(p.stats().states, 3U);
    // Same level again: nothing.
    p.update_state(s, 35000U);
    CHECK_EQ(drain(p, 35000U, acts, 16U), 0U);
    // MQ-07: Home Assistant restarts -> discovery and state again; other messages ignored.
    const char* t_other = "homeassistant/sensor/x/config";
    p.on_message(t_other, std::strlen(t_other), "online", 6U);
    CHECK_EQ(drain(p, 36000U, acts, 16U), 0U);
    p.on_message("homeassistant/status", 20U, "offline", 7U);
    CHECK_EQ(drain(p, 36000U, acts, 16U), 0U);
    p.on_message("homeassistant/status", 20U, "online", 6U);
    CHECK_EQ(drain(p, 36000U, acts, 16U), 2U);
    CHECK_ENUM(acts[0], MqttAction::PublishDiscovery);
    CHECK_ENUM(acts[1], MqttAction::PublishState);
    CHECK_EQ(p.stats().discoveries, 2U);
    // Page button.
    p.request_discovery();
    CHECK_EQ(drain(p, 37000U, acts, 16U), 2U);
    CHECK_ENUM(acts[0], MqttAction::PublishDiscovery);
    // Broker lost: nothing is queued while down; back: online + discovery + state.
    p.on_disconnected();
    CHECK_EQ(p.stats().disconnections, 1U);
    CHECK_ENUM(p.link_state(), MqttLinkState::Connecting);
    p.update_state(s, 80000U);
    CHECK_EQ(drain(p, 80000U, acts, 16U), 0U);
    p.on_connected();
    CHECK_EQ(drain(p, 81000U, acts, 16U), 3U);
    CHECK_ENUM(acts[0], MqttAction::PublishOnline);
    CHECK_ENUM(acts[1], MqttAction::PublishDiscovery);
    CHECK_ENUM(acts[2], MqttAction::PublishState);
    // MQ-10: disabled while connected -> offline published, then disconnect, no connect.
    MqttConfig off = enabled_config();
    off.enabled = false;
    p.configure(off);
    CHECK_EQ(drain(p, 82000U, acts, 16U), 2U);
    CHECK_ENUM(acts[0], MqttAction::PublishOffline);
    CHECK_ENUM(acts[1], MqttAction::Disconnect);
    CHECK_ENUM(p.link_state(), MqttLinkState::Disabled);
    CHECK(!p.connected());
    // The transport's disconnect event does not count twice.
    p.on_disconnected();
    CHECK_EQ(p.stats().disconnections, 2U);
    CHECK_EQ(drain(p, 83000U, acts, 16U), 0U);
    // Re-enabled with another broker: connect again; settings change while connected: offline, disconnect, connect.
    p.configure(enabled_config());
    CHECK_EQ(drain(p, 84000U, acts, 16U), 1U);
    CHECK_ENUM(acts[0], MqttAction::Connect);
    p.on_connected();
    CHECK_EQ(drain(p, 85000U, acts, 16U), 3U);
    MqttConfig other = enabled_config();
    std::strcpy(other.host, "autre.lan");
    p.configure(other);
    CHECK_EQ(drain(p, 86000U, acts, 16U), 3U);
    CHECK_ENUM(acts[0], MqttAction::PublishOffline);
    CHECK_ENUM(acts[1], MqttAction::Disconnect);
    CHECK_ENUM(acts[2], MqttAction::Connect);
    CHECK(std::strcmp(p.config().host, "autre.lan") == 0);
    // Settings change while connecting (not connected): disconnect then connect, no offline.
    p.configure(enabled_config());
    CHECK_EQ(drain(p, 87000U, acts, 16U), 2U);
    CHECK_ENUM(acts[0], MqttAction::Disconnect);
    CHECK_ENUM(acts[1], MqttAction::Connect);
    // An invalid configuration falls back to the defaults (disabled).
    MqttConfig bad = enabled_config();
    bad.host[0] = '\0';
    p.configure(bad);
    CHECK_EQ(drain(p, 88000U, acts, 16U), 1U);
    CHECK_ENUM(acts[0], MqttAction::Disconnect);
    CHECK(!p.config().enabled);
    // No allocation anywhere.
    {
        AllocGuard guard;
        MqttPublisher q;
        q.configure(enabled_config());
        q.on_connected();
        q.update_state(good_state(), 0U);
        (void)drain(q, 0U, acts, 16U);
        static char buf[kMqttDiscoveryMax];
        static char topic[kMqttTopicMax];
        (void)mqtt_discovery(0U, ident(), "homeassistant", topic, sizeof(topic), buf, sizeof(buf));
        (void)mqtt_state_json(good_state(), buf, sizeof(buf));
        CHECK_EQ(g_alloc_count, 0U);
    }
}

// MQ-08: settings validation, every status, fallback on the defaults.
void test_config() {
    MqttConfig c = default_mqtt_config();
    CHECK_ENUM(validate(c), MqttConfigStatus::Ok);
    CHECK(!c.enabled);
    CHECK_EQ(c.port, 1883U);
    CHECK(std::strcmp(c.prefix, "homeassistant") == 0);
    CHECK_EQ(c.period_s, 30U);
    CHECK(mqtt_status_reason(MqttConfigStatus::Ok)[0] == '\0');
    c.enabled = true;
    CHECK_ENUM(validate(c), MqttConfigStatus::HostEmpty);
    CHECK(mqtt_status_reason(MqttConfigStatus::HostEmpty)[0] != '\0');
    std::strcpy(c.host, "-broker");
    CHECK_ENUM(validate(c), MqttConfigStatus::HostInvalid);
    std::strcpy(c.host, "broker.");
    CHECK_ENUM(validate(c), MqttConfigStatus::HostInvalid);
    std::strcpy(c.host, "bro ker");
    CHECK_ENUM(validate(c), MqttConfigStatus::HostInvalid);
    std::strcpy(c.host, "10.0.0.2");
    CHECK_ENUM(validate(c), MqttConfigStatus::Ok);
    c.port = 0U;
    CHECK_ENUM(validate(c), MqttConfigStatus::PortInvalid);
    c.port = 8883U;
    std::strcpy(c.user, "ab\tc");
    CHECK_ENUM(validate(c), MqttConfigStatus::UserInvalid);
    std::strcpy(c.user, "horloge");
    std::strcpy(c.password, "mot de passe\x01");
    CHECK_ENUM(validate(c), MqttConfigStatus::PasswordInvalid);
    std::strcpy(c.password, "ok");
    c.user[0] = '\0';
    CHECK_ENUM(validate(c), MqttConfigStatus::PasswordWithoutUser);
    std::strcpy(c.user, "horloge");
    c.prefix[0] = '\0';
    CHECK_ENUM(validate(c), MqttConfigStatus::PrefixInvalid);
    std::strcpy(c.prefix, "/ha");
    CHECK_ENUM(validate(c), MqttConfigStatus::PrefixInvalid);
    std::strcpy(c.prefix, "ha/");
    CHECK_ENUM(validate(c), MqttConfigStatus::PrefixInvalid);
    std::strcpy(c.prefix, "Home");
    CHECK_ENUM(validate(c), MqttConfigStatus::PrefixInvalid);
    std::strcpy(c.prefix, "ha/+");
    CHECK_ENUM(validate(c), MqttConfigStatus::PrefixInvalid);
    std::strcpy(c.prefix, "maison/ha-2_x");
    CHECK_ENUM(validate(c), MqttConfigStatus::Ok);
    c.period_s = 4U;
    CHECK_ENUM(validate(c), MqttConfigStatus::PeriodInvalid);
    c.period_s = 601U;
    CHECK_ENUM(validate(c), MqttConfigStatus::PeriodInvalid);
    c.period_s = 600U;
    CHECK(is_valid(c));
    // Unterminated array: refused before anything is read.
    MqttConfig u = c;
    std::memset(u.host, 'a', sizeof(u.host));
    CHECK_ENUM(validate(u), MqttConfigStatus::HostInvalid);
    u = c;
    std::memset(u.prefix, 'a', sizeof(u.prefix));
    CHECK_ENUM(validate(u), MqttConfigStatus::HostInvalid);
    // Disabled: an empty host is fine, the rest is still checked.
    c.enabled = false;
    c.host[0] = '\0';
    CHECK(is_valid(c));
    c.port = 0U;
    CHECK(!is_valid(c));
    // Every status has a reason for the page.
    for (uint8_t i = 1U; i <= static_cast<uint8_t>(MqttConfigStatus::PeriodInvalid); ++i) {
        CHECK(mqtt_status_reason(static_cast<MqttConfigStatus>(i))[0] != '\0');
    }
    // Settings domain: defaults, form, JSON views.
    settings::Settings* all = new settings::Settings;
    settings::default_settings_into(*all);
    CHECK(is_valid(all->mqtt));
    CHECK(!all->mqtt.enabled);
    delete all;
}

// MQ-09: form <-> model, the page is checked by WEB-04 (test_settings.cpp).
void test_form() {
    const MqttConfig current = default_mqtt_config();
    MqttConfig out{};
    settings::FormError err{};
    const char* body = "actif=1&hote=broker.lan&port=1884&utilisateur=horloge&mot_de_passe=s3cret&prefixe=ha&periode_s=60";
    CHECK(settings::apply_mqtt_form(body, std::strlen(body), current, out, err));
    CHECK(out.enabled);
    CHECK(std::strcmp(out.host, "broker.lan") == 0);
    CHECK_EQ(out.port, 1884U);
    CHECK(std::strcmp(out.user, "horloge") == 0);
    CHECK(std::strcmp(out.password, "s3cret") == 0);
    CHECK(std::strcmp(out.prefix, "ha") == 0);
    CHECK_EQ(out.period_s, 60U);
    // Password empty = kept; effacer_mdp clears it; absent fields keep their value.
    const MqttConfig saved = out;
    MqttConfig next{};
    body = "mot_de_passe=&port=1885";
    CHECK(settings::apply_mqtt_form(body, std::strlen(body), saved, next, err));
    CHECK(std::strcmp(next.password, "s3cret") == 0);
    CHECK_EQ(next.port, 1885U);
    CHECK(std::strcmp(next.host, "broker.lan") == 0);
    body = "effacer_mdp=1";
    CHECK(settings::apply_mqtt_form(body, std::strlen(body), saved, next, err));
    CHECK(next.password[0] == '\0');
    // Empty prefix = default.
    body = "prefixe=";
    CHECK(settings::apply_mqtt_form(body, std::strlen(body), saved, next, err));
    CHECK(std::strcmp(next.prefix, "homeassistant") == 0);
    // Refusals, all or nothing: out untouched.
    const char* bad[] = {"actif=2", "hote=-x", "port=0", "port=70000", "port=abc", "utilisateur=a%09b", "mot_de_passe=%01",
                         "prefixe=/x", "prefixe=Ha", "periode_s=4", "periode_s=601", "effacer_mdp=oui_non",
                         "hote=%ZZ", "actif=1&hote=", "utilisateur=&mot_de_passe=x",
                         "hote=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (const char* b : bad) {
        MqttConfig untouched = saved;
        err = settings::FormError{};
        const bool ok = settings::apply_mqtt_form(b, std::strlen(b), saved, untouched, err);
        if (ok) {
            std::printf("  corps accepte a tort : %s\n", b);
        }
        CHECK(!ok);
        CHECK(err.reason != nullptr);
        CHECK(std::memcmp(&untouched, &saved, sizeof(saved)) == 0);
    }
    // A badly encoded value in any field is refused by that field's name.
    for (std::size_t i = 0U; i < settings::kMqttFieldCount; ++i) {
        char b[64];
        (void)std::snprintf(b, sizeof(b), "%s=%%ZZ", settings::kMqttFields[i]);
        err = settings::FormError{};
        CHECK(!settings::apply_mqtt_form(b, std::strlen(b), saved, next, err));
        CHECK((err.field != nullptr) && (std::strcmp(err.field, settings::kMqttFields[i]) == 0));
    }
    // The defaults as current: enabling without a host is refused by name.
    body = "actif=1";
    CHECK(!settings::apply_mqtt_form(body, std::strlen(body), current, next, err));
    CHECK(std::strcmp(err.field, "hote") == 0);
    // Model JSON: every field with its label; password is a secret; JSON values never echo the password.
    static char json[settings::kJsonMax];
    const std::size_t len = settings::mqtt_model_json(json, sizeof(json));
    CHECK(len > 0U);
    for (std::size_t i = 0U; i < settings::kMqttFieldCount; ++i) {
        char needle[64];
        (void)std::snprintf(needle, sizeof(needle), "\"%s\":{\"type\":\"", settings::kMqttFields[i]);
        const bool present = std::strstr(json, needle) != nullptr;
        if (!present) {
            std::printf("  champ absent du modele mqtt : %s\n", settings::kMqttFields[i]);
        }
        CHECK(present);
    }
    CHECK(std::strstr(json, "\"mot_de_passe\":{\"type\":\"secret\"") != nullptr);
    CHECK(std::strstr(json, "\"defaut\":1883") != nullptr);
    CHECK(std::strstr(json, "\"defaut\":\"homeassistant\"") != nullptr);
    CHECK(std::strstr(json, "\"libelle\":") != nullptr);
    CHECK_EQ(settings::mqtt_model_json(json, 64U), 0U);
    const std::size_t vlen = settings::mqtt_json(saved, json, sizeof(json));
    CHECK(vlen > 0U);
    CHECK(std::strstr(json, "s3cret") == nullptr);
    CHECK(std::strstr(json, "\"mot_de_passe\":\"\"") != nullptr);
    CHECK(std::strstr(json, "\"mot_de_passe_defini\":true") != nullptr);
    CHECK(std::strstr(json, "\"hote\":\"broker.lan\"") != nullptr);
    CHECK(std::strstr(json, "\"periode_s\":60") != nullptr);
    CHECK_EQ(settings::mqtt_json(saved, json, 32U), 0U);
    // kMqttFields covers the form exactly: 8 names, none duplicated.
    CHECK_EQ(settings::kMqttFieldCount, 8U);
}

}  // namespace

void run_mqtt_tests() {
    std::printf("mqtt (MQ-01..MQ-10)\n");
    test_topics();
    test_discovery();
    test_keys_consistency();
    test_state();
    test_publisher();
    test_config();
    test_form();
}
