/**
 * @file mqtt.cpp
 * @brief MQTT discovery, state and publish policy (see mqtt.hpp).
 */
#include "homeassistant/mqtt.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace homeassistant {

namespace {

constexpr char kBase[] = "horloge";          ///< Common prefix of the clock's topics.
constexpr char kDeviceName[] = "Horloge";    ///< Device name and manufacturer.
constexpr char kIdPrefix[] = "horloge_";     ///< `unique_id` and `identifiers` prefix.
constexpr char kOn[] = "ON";
constexpr char kOff[] = "OFF";
constexpr char kMeasurement[] = "measurement";
constexpr char kDiagnostic[] = "diagnostic";
constexpr char kLevelOptions[] = "[\"excellent\",\"bon\",\"moyen\",\"mauvais\",\"tres_mauvais\"]";
constexpr uint32_t kMsPerS = 1000U;
constexpr int32_t kCenti = 100;
constexpr int32_t kDeci = 10;

enum class Kind : uint8_t { Sensor = 0U, Binary };

/// One entity; nullptr = key absent from the discovery message.
struct Entity {
    Kind kind;
    const char* object;       ///< Stable object id (`temperature`).
    const char* name;         ///< Displayed name, relative to the device.
    const char* key;          ///< Key in the state JSON.
    const char* device_class;
    const char* unit;
    const char* state_class;
    const char* category;     ///< `entity_category`.
    const char* icon;
    int8_t precision;         ///< `suggested_display_precision`, -1 = absent.
    const char* options;      ///< JSON list for an `enum`.
};

/**
 * @brief The entities, in publication order (spec-home-assistant.md section 6 bis).
 * @note Add an entity here **and** its key in `mqtt_state_json()`: MQ-03 checks
 *       that every `value_template` reads a key that is really published and
 *       that every published key is read.
 */
constexpr std::array<Entity, 10> kEntities{{
    {Kind::Sensor, "temperature", "Température", "temperature", "temperature", "°C", kMeasurement, nullptr, nullptr, 1, nullptr},
    {Kind::Sensor, "humidite", "Humidité", "humidite", "humidity", "%", kMeasurement, nullptr, nullptr, 1, nullptr},
    {Kind::Sensor, "eco2", "eCO2", "eco2", "carbon_dioxide", "ppm", kMeasurement, nullptr, nullptr, 0, nullptr},
    {Kind::Sensor, "tvoc", "TVOC", "tvoc", "volatile_organic_compounds_parts", "ppb", kMeasurement, nullptr, nullptr, 0, nullptr},
    {Kind::Sensor, "aqi", "Indice de qualité d'air", "aqi", "aqi", nullptr, kMeasurement, nullptr, nullptr, 0, nullptr},
    {Kind::Sensor, "niveau", "Niveau d'air", "niveau", "enum", nullptr, nullptr, nullptr, "mdi:air-filter", -1, kLevelOptions},
    {Kind::Binary, "capteur_defaut", "Capteur d'air", "capteur_defaut", "problem", nullptr, nullptr, kDiagnostic, nullptr, -1, nullptr},
    {Kind::Binary, "prechauffage", "Préchauffage", "prechauffage", "running", nullptr, nullptr, kDiagnostic, "mdi:heat-wave", -1, nullptr},
    {Kind::Sensor, "luminosite_brute", "Lumière ambiante", "luminosite_brute", nullptr, nullptr, kMeasurement, kDiagnostic, "mdi:brightness-6", 0, nullptr},
    {Kind::Binary, "heure_sure", "Heure synchronisée", "heure_sure", "connectivity", nullptr, nullptr, kDiagnostic, "mdi:clock-check", -1, nullptr},
}};

bool is_digit(char c) noexcept {
    return (c >= '0') && (c <= '9');
}
bool is_lower(char c) noexcept {
    return (c >= 'a') && (c <= 'z');
}
bool is_upper(char c) noexcept {
    return (c >= 'A') && (c <= 'Z');
}
bool is_printable(char c) noexcept {
    return (c >= ' ') && (c <= '~');
}

/// DNS name or IPv4: `[A-Za-z0-9.-]`, no dot or dash at an edge.
bool host_valid(const char* h) noexcept {
    const std::size_t n = std::strlen(h);
    if (n == 0U) {
        return false;
    }
    if ((h[0] == '.') || (h[0] == '-') || (h[n - 1U] == '.') || (h[n - 1U] == '-')) {
        return false;
    }
    for (std::size_t i = 0U; i < n; ++i) {
        const char c = h[i];
        if (!(is_digit(c) || is_lower(c) || is_upper(c) || (c == '.') || (c == '-'))) {
            return false;
        }
    }
    return true;
}

bool printable_text(const char* s) noexcept {
    for (std::size_t i = 0U; s[i] != '\0'; ++i) {
        if (!is_printable(s[i])) {
            return false;
        }
    }
    return true;
}

/// Topic prefix: `[a-z0-9_-/]`, not empty, no `/` at an edge, no wildcard.
bool prefix_valid(const char* p) noexcept {
    const std::size_t n = std::strlen(p);
    if ((n == 0U) || (p[0] == '/') || (p[n - 1U] == '/')) {
        return false;
    }
    for (std::size_t i = 0U; i < n; ++i) {
        const char c = p[i];
        if (!(is_digit(c) || is_lower(c) || (c == '_') || (c == '-') || (c == '/'))) {
            return false;
        }
    }
    return true;
}

/// Bounded writer: the first overflow freezes everything, `finish()` then returns 0.
class Writer {
public:
    Writer(char* out, std::size_t size) noexcept : out_(out), size_(size), pos_(0U), ok_((out != nullptr) && (size > 0U)) {
        if (ok_) {
            out_[0] = '\0';
        }
    }

    void text(const char* s) noexcept {
        if (!ok_) {
            return;
        }
        if (s == nullptr) {
            ok_ = false;
            return;
        }
        const std::size_t n = std::strlen(s);
        if ((pos_ + n) >= size_) {
            ok_ = false;
            return;
        }
        std::memcpy(out_ + pos_, s, n);
        pos_ += n;
        out_[pos_] = '\0';
    }

    /// `"s"` — @p s comes from this file or from a validated field (no escaping needed).
    void quoted(const char* s) noexcept {
        text("\"");
        text(s);
        text("\"");
    }

    /// `,"key":"value"`
    void field(const char* key, const char* value) noexcept {
        text(",\"");
        text(key);
        text("\":");
        quoted(value);
    }

    void integer(int64_t v) noexcept {
        std::array<char, 24> tmp{};
        const int n = std::snprintf(tmp.data(), tmp.size(), "%" PRId64, v);
        if ((n < 0) || (static_cast<std::size_t>(n) >= tmp.size())) {
            ok_ = false;
            return;
        }
        text(tmp.data());
    }

    /// Fixed point: @p v in 10^-decimals units; the sign is handled apart (-5 deci ⇒ "-0.5").
    void fixed(int32_t v, uint8_t decimals) noexcept {
        int32_t div = 1;
        for (uint8_t i = 0U; i < decimals; ++i) {
            div *= kDeci;
        }
        const bool negative = v < 0;
        const uint32_t a = negative ? static_cast<uint32_t>(-v) : static_cast<uint32_t>(v);
        const uint32_t d = static_cast<uint32_t>(div);
        if (negative) {
            text("-");
        }
        std::array<char, 24> tmp{};
        int n;
        if (decimals == 0U) {
            n = std::snprintf(tmp.data(), tmp.size(), "%" PRIu32, a);
        } else {
            n = std::snprintf(tmp.data(), tmp.size(), "%" PRIu32 ".%0*" PRIu32, a / d, static_cast<int>(decimals), a % d);
        }
        if ((n < 0) || (static_cast<std::size_t>(n) >= tmp.size())) {
            ok_ = false;
            return;
        }
        text(tmp.data());
    }

    std::size_t finish() noexcept {
        if (!ok_) {
            if ((out_ != nullptr) && (size_ > 0U)) {
                out_[0] = '\0';
            }
            return 0U;
        }
        return pos_;
    }

private:
    char* out_;
    std::size_t size_;
    std::size_t pos_;
    bool ok_;
};

const char* platform(Kind k) noexcept {
    return (k == Kind::Binary) ? "binary_sensor" : "sensor";
}

/// `availability`, `device` and `origin` blocks shared by every entity.
void common(Writer& w, const DeviceIdentity& ident, const char* availability) noexcept {
    w.field("availability_topic", availability);
    w.field("payload_available", kMqttOnline);
    w.field("payload_not_available", kMqttOffline);
    w.text(",\"device\":{\"identifiers\":[\"");
    w.text(kIdPrefix);
    w.text(ident.id.data());
    w.text("\"],\"connections\":[[\"mac\",\"");
    w.text(ident.mac.data());
    w.text("\"]]");
    w.field("name", kDeviceName);
    w.field("manufacturer", kDeviceName);
    w.field("model", ident.model.data());
    w.field("sw_version", ident.version.data());
    if (ident.url[0] != '\0') {
        w.field("configuration_url", ident.url.data());
    }
    w.text("},\"origin\":{\"name\":");
    w.quoted(kDeviceName);
    w.field("sw_version", ident.version.data());
    w.text("}");
}

void key_null(Writer& w, const char* key) noexcept {
    w.text(",\"");
    w.text(key);
    w.text("\":null");
}

void key_fixed(Writer& w, const char* key, bool valid, int32_t v, uint8_t decimals) noexcept {
    if (!valid) {
        key_null(w, key);
        return;
    }
    w.text(",\"");
    w.text(key);
    w.text("\":");
    w.fixed(v, decimals);
}

void key_binary(Writer& w, const char* key, bool known, bool value) noexcept {
    if (!known) {
        key_null(w, key);
        return;
    }
    w.field(key, value ? kOn : kOff);
}

bool equal(const char* a, std::size_t alen, const char* b) noexcept {
    const std::size_t blen = std::strlen(b);
    return (alen == blen) && (std::memcmp(a, b, alen) == 0);
}

/// centi-°C to deci-°C, rounded half away from zero.
int32_t centi_to_deci(int32_t centi) noexcept {
    const int32_t half = (kCenti / kDeci) / 2;
    return (centi >= 0) ? ((centi + half) / kDeci) : -(((-centi) + half) / kDeci);
}

}  // namespace

// ------------------------------------------------------------------ settings

MqttConfig default_mqtt_config() noexcept {
    MqttConfig c{};
    c.enabled = false;
    c.host[0] = '\0';
    c.port = kMqttPortDefault;
    c.user[0] = '\0';
    c.password[0] = '\0';
    std::strncpy(c.prefix, kMqttPrefixDefault, kMqttPrefixMax);
    c.prefix[kMqttPrefixMax] = '\0';
    c.period_s = kMqttPeriodDefaultS;
    return c;
}

MqttConfigStatus validate(const MqttConfig& c) noexcept {
    // Terminated inside their arrays: never read beyond.
    if ((c.host[kMqttHostMax] != '\0') || (c.user[kMqttUserMax] != '\0') || (c.password[kMqttPasswordMax] != '\0') ||
        (c.prefix[kMqttPrefixMax] != '\0')) {
        return MqttConfigStatus::HostInvalid;
    }
    if (c.host[0] == '\0') {
        if (c.enabled) {
            return MqttConfigStatus::HostEmpty;
        }
    } else if (!host_valid(c.host)) {
        return MqttConfigStatus::HostInvalid;
    } else {
        // host set and valid
    }
    if (c.port == 0U) {
        return MqttConfigStatus::PortInvalid;
    }
    if (!printable_text(c.user)) {
        return MqttConfigStatus::UserInvalid;
    }
    if (!printable_text(c.password)) {
        return MqttConfigStatus::PasswordInvalid;
    }
    if ((c.password[0] != '\0') && (c.user[0] == '\0')) {
        return MqttConfigStatus::PasswordWithoutUser;
    }
    if (!prefix_valid(c.prefix)) {
        return MqttConfigStatus::PrefixInvalid;
    }
    if ((c.period_s < kMqttPeriodMinS) || (c.period_s > kMqttPeriodMaxS)) {
        return MqttConfigStatus::PeriodInvalid;
    }
    return MqttConfigStatus::Ok;
}

bool is_valid(const MqttConfig& c) noexcept {
    return validate(c) == MqttConfigStatus::Ok;
}

const char* mqtt_status_reason(MqttConfigStatus s) noexcept {
    switch (s) {
        case MqttConfigStatus::HostEmpty: return "une liaison active a besoin d'une adresse de broker";
        case MqttConfigStatus::HostInvalid: return "adresse IP ou nom DNS, lettres, chiffres, points et tirets";
        case MqttConfigStatus::PortInvalid: return "de 1 à 65535";
        case MqttConfigStatus::UserInvalid: return "caractères ASCII imprimables";
        case MqttConfigStatus::PasswordInvalid: return "caractères ASCII imprimables";
        case MqttConfigStatus::PasswordWithoutUser: return "un mot de passe exige un utilisateur";
        case MqttConfigStatus::PrefixInvalid: return "minuscules, chiffres, _ - et /, sans / au bord";
        case MqttConfigStatus::PeriodInvalid: return "de 5 à 600 s";
        case MqttConfigStatus::Ok:
        default: return "";
    }
}

// ------------------------------------------------------------------ identity and topics

bool build_identity(const std::array<uint8_t, 6>& mac, const char* version, const char* model, const char* ip,
                    DeviceIdentity& out) noexcept {
    out = DeviceIdentity{};
    const int a = std::snprintf(out.id.data(), out.id.size(), "%02x%02x%02x%02x%02x%02x", static_cast<unsigned>(mac[0]),
                                static_cast<unsigned>(mac[1]), static_cast<unsigned>(mac[2]), static_cast<unsigned>(mac[3]),
                                static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
    const int b = std::snprintf(out.mac.data(), out.mac.size(), "%02x:%02x:%02x:%02x:%02x:%02x",
                                static_cast<unsigned>(mac[0]), static_cast<unsigned>(mac[1]), static_cast<unsigned>(mac[2]),
                                static_cast<unsigned>(mac[3]), static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
    if ((a != 12) || (b != 17) || (version == nullptr) || (model == nullptr)) {
        return false;
    }
    const std::size_t lv = std::strlen(version);
    const std::size_t lm = std::strlen(model);
    if ((lv >= out.version.size()) || (lm >= out.model.size())) {
        return false;
    }
    std::memcpy(out.version.data(), version, lv + 1U);
    std::memcpy(out.model.data(), model, lm + 1U);
    if ((ip != nullptr) && (ip[0] != '\0')) {
        const int c = std::snprintf(out.url.data(), out.url.size(), "http://%s/", ip);
        if ((c < 0) || (static_cast<std::size_t>(c) >= out.url.size())) {
            out.url[0] = '\0';
            return false;
        }
    }
    return true;
}

std::size_t mqtt_topic(MqttTopic t, const DeviceIdentity& ident, char* out, std::size_t out_size) noexcept {
    Writer w(out, out_size);
    w.text(kBase);
    w.text("/");
    w.text(ident.id.data());
    w.text((t == MqttTopic::State) ? "/etat" : "/disponible");
    return w.finish();
}

std::size_t ha_status_topic(const char* prefix, char* out, std::size_t out_size) noexcept {
    Writer w(out, out_size);
    w.text(prefix);
    w.text("/status");
    return w.finish();
}

bool is_ha_online(const char* prefix, const char* topic, std::size_t tlen, const char* payload,
                  std::size_t plen) noexcept {
    if ((prefix == nullptr) || (topic == nullptr) || (payload == nullptr)) {
        return false;
    }
    std::array<char, kMqttTopicMax> expected{};
    if (ha_status_topic(prefix, expected.data(), expected.size()) == 0U) {
        return false;
    }
    return equal(topic, tlen, expected.data()) && equal(payload, plen, kMqttOnline);
}

// ------------------------------------------------------------------ discovery

std::size_t mqtt_entity_count() noexcept {
    return kEntities.size();
}

const char* mqtt_entity_object(std::size_t index) noexcept {
    return (index < kEntities.size()) ? kEntities[index].object : nullptr;
}

const char* mqtt_entity_key(std::size_t index) noexcept {
    return (index < kEntities.size()) ? kEntities[index].key : nullptr;
}

std::size_t mqtt_discovery(std::size_t index, const DeviceIdentity& ident, const char* prefix, char* topic_out,
                           std::size_t topic_size, char* payload_out, std::size_t payload_size) noexcept {
    if ((index >= kEntities.size()) || (prefix == nullptr)) {
        return 0U;
    }
    const Entity& e = kEntities[index];

    Writer t(topic_out, topic_size);
    t.text(prefix);
    t.text("/");
    t.text(platform(e.kind));
    t.text("/");
    t.text(kIdPrefix);
    t.text(ident.id.data());
    t.text("/");
    t.text(e.object);
    t.text("/config");
    if (t.finish() == 0U) {
        return 0U;
    }

    std::array<char, kMqttTopicMax> availability{};
    std::array<char, kMqttTopicMax> state{};
    if ((mqtt_topic(MqttTopic::Availability, ident, availability.data(), availability.size()) == 0U) ||
        (mqtt_topic(MqttTopic::State, ident, state.data(), state.size()) == 0U)) {
        return 0U;
    }

    Writer w(payload_out, payload_size);
    w.text("{\"name\":");
    w.quoted(e.name);
    w.text(",\"unique_id\":\"");
    w.text(kIdPrefix);
    w.text(ident.id.data());
    w.text("_");
    w.text(e.object);
    w.text("\",\"has_entity_name\":true");
    w.field("state_topic", state.data());
    w.text(",\"value_template\":\"{{ value_json.");
    w.text(e.key);
    w.text(" }}\"");
    if (e.kind == Kind::Binary) {
        w.field("payload_on", kOn);
        w.field("payload_off", kOff);
    }
    if (e.device_class != nullptr) {
        w.field("device_class", e.device_class);
    }
    if (e.unit != nullptr) {
        w.field("unit_of_measurement", e.unit);
    }
    if (e.state_class != nullptr) {
        w.field("state_class", e.state_class);
    }
    if (e.options != nullptr) {
        w.text(",\"options\":");
        w.text(e.options);
    }
    if (e.precision >= 0) {
        w.text(",\"suggested_display_precision\":");
        w.integer(e.precision);
    }
    if (e.icon != nullptr) {
        w.field("icon", e.icon);
    }
    if (e.category != nullptr) {
        w.field("entity_category", e.category);
    }
    common(w, ident, availability.data());
    w.text("}");
    return w.finish();
}

// ------------------------------------------------------------------ state

const char* mqtt_level_option(air_quality::AirLevel level) noexcept {
    switch (level) {
        case air_quality::AirLevel::Excellent: return "excellent";
        case air_quality::AirLevel::Good: return "bon";
        case air_quality::AirLevel::Moderate: return "moyen";
        case air_quality::AirLevel::Poor: return "mauvais";
        case air_quality::AirLevel::VeryPoor: return "tres_mauvais";
        case air_quality::AirLevel::Unknown:
        default: return nullptr;
    }
}

std::size_t mqtt_state_json(const MqttState& s, char* out, std::size_t out_size) noexcept {
    Writer w(out, out_size);
    const air_quality::AirReading& a = s.air;
    const bool sensor = s.air_enabled && a.present;
    const bool climate = sensor && !a.fault && a.climate_valid;
    const bool air = sensor && !a.fault && a.air_valid;
    const char* level = mqtt_level_option(s.level);
    w.text("{\"heure_sure\":");
    w.quoted(s.time_sure ? kOn : kOff);
    key_fixed(w, "temperature", climate, centi_to_deci(a.temperature_centi), 1U);
    key_fixed(w, "humidite", climate, static_cast<int32_t>(a.humidity_pct10), 1U);
    key_fixed(w, "eco2", air, static_cast<int32_t>(a.eco2_ppm), 0U);
    key_fixed(w, "tvoc", air, static_cast<int32_t>(a.tvoc_ppb), 0U);
    key_fixed(w, "aqi", air, static_cast<int32_t>(a.aqi), 0U);
    if ((level != nullptr) && air) {
        w.field("niveau", level);
    } else {
        key_null(w, "niveau");
    }
    // Sensor disabled on the page: nothing to say about it (null), not "no problem".
    key_binary(w, "capteur_defaut", s.air_enabled, !a.present || a.fault);
    key_binary(w, "prechauffage", sensor && !a.fault, a.warming_up || a.initial_start);
    key_fixed(w, "luminosite_brute", s.ldr_valid, static_cast<int32_t>(s.ldr_raw), 0U);
    w.text("}");
    return w.finish();
}

// ------------------------------------------------------------------ publisher

const char* mqtt_link_state_name(MqttLinkState s) noexcept {
    switch (s) {
        case MqttLinkState::Connecting: return "connecting";
        case MqttLinkState::Connected: return "connected";
        case MqttLinkState::Disabled:
        default: return "disabled";
    }
}

MqttPublisher::MqttPublisher() noexcept
    : cfg_(default_mqtt_config()),
      state_{},
      stats_{},
      state_known_(false),
      connected_(false),
      client_running_(false),
      reconfigure_(false),
      online_pending_(false),
      discovery_pending_(false),
      state_pending_(false),
      discovery_done_(false),
      last_state_ms_(0U),
      last_level_(air_quality::AirLevel::Unknown) {}

void MqttPublisher::configure(const MqttConfig& c) noexcept {
    cfg_ = is_valid(c) ? c : default_mqtt_config();
    reconfigure_ = true;
}

void MqttPublisher::on_connected() noexcept {
    if (!connected_) {
        ++stats_.connections;
    }
    connected_ = true;
    online_pending_ = true;
    discovery_pending_ = true;
    state_pending_ = true;
    discovery_done_ = false;
}

void MqttPublisher::on_disconnected() noexcept {
    if (connected_) {
        ++stats_.disconnections;
    }
    connected_ = false;
    online_pending_ = false;
    discovery_pending_ = false;
    state_pending_ = false;
    discovery_done_ = false;
}

void MqttPublisher::on_message(const char* topic, std::size_t tlen, const char* payload, std::size_t plen) noexcept {
    if (is_ha_online(cfg_.prefix, topic, tlen, payload, plen)) {
        discovery_pending_ = true;
        state_pending_ = true;
    }
}

void MqttPublisher::update_state(const MqttState& s, uint32_t now_ms) noexcept {
    state_ = s;
    state_known_ = true;
    if (s.level != last_level_) {
        last_level_ = s.level;
        state_pending_ = true;  // a level change does not wait for the period
    }
    const uint32_t period_ms = static_cast<uint32_t>(cfg_.period_s) * kMsPerS;
    if ((now_ms - last_state_ms_) >= period_ms) {
        state_pending_ = true;
    }
}

void MqttPublisher::request_discovery() noexcept {
    discovery_pending_ = true;
    state_pending_ = true;
}

MqttAction MqttPublisher::next_action(uint32_t now_ms) noexcept {
    if (reconfigure_) {
        // Order: tell Home Assistant we leave (if we can), stop, start again if still enabled.
        if (client_running_) {
            if (connected_) {
                connected_ = false;
                ++stats_.disconnections;
                return MqttAction::PublishOffline;
            }
            client_running_ = false;
            online_pending_ = false;
            discovery_pending_ = false;
            state_pending_ = false;
            discovery_done_ = false;
            return MqttAction::Disconnect;
        }
        reconfigure_ = false;
        if (cfg_.enabled) {
            client_running_ = true;
            return MqttAction::Connect;
        }
        return MqttAction::None;
    }
    if (!connected_) {
        return MqttAction::None;
    }
    if (online_pending_) {
        online_pending_ = false;
        return MqttAction::PublishOnline;
    }
    if (discovery_pending_) {
        discovery_pending_ = false;
        discovery_done_ = true;
        ++stats_.discoveries;
        return MqttAction::PublishDiscovery;
    }
    if (state_pending_ && state_known_) {
        state_pending_ = false;
        last_state_ms_ = now_ms;
        ++stats_.states;
        return MqttAction::PublishState;
    }
    return MqttAction::None;
}

MqttLinkState MqttPublisher::link_state() const noexcept {
    if (!cfg_.enabled) {
        return MqttLinkState::Disabled;
    }
    return (connected_ && discovery_done_) ? MqttLinkState::Connected : MqttLinkState::Connecting;
}

}  // namespace homeassistant
