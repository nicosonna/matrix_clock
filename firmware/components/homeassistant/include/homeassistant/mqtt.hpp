#pragma once
/**
 * @file mqtt.hpp
 * @brief MQTT publication to Home Assistant: settings, discovery, state, publish policy.
 *
 * Ported from ThermoSync `domotique/home_assistant.hpp` + `mqtt_config.hpp`
 * (same conventions, `horloge` instead of `thermosync`, the clock's own
 * entities). Follows Home Assistant **MQTT discovery**:
 * - one retained configuration message per entity on
 *   `<prefix>/<platform>/horloge_<id>/<object>/config`, all attached to the
 *   same `device`;
 * - a common **availability** topic (`horloge/<id>/disponible`, `online` /
 *   `offline`), the `offline` left as the broker's last will;
 * - **one** JSON state message (`horloge/<id>/etat`) read by every entity
 *   through its `value_template`;
 * - the discovery is sent again when Home Assistant restarts
 *   (`<prefix>/status` = `online`).
 *
 * MQTT is **publish only**: nothing is read from the broker besides that
 * status announcement. An invalid measurement is published `null` (entity
 * *unknown* in Home Assistant), never a stale or invented value
 * (spec-home-assistant.md section 6 bis).
 *
 * Pure logic: no ESP-IDF dependency, host-tested (MQ-01 .. MQ-10).
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include "air_quality/levels.hpp"
#include "air_quality/sensor.hpp"

namespace homeassistant {

// ------------------------------------------------------------------ settings

inline constexpr std::size_t kMqttHostMax = 64U;
inline constexpr std::size_t kMqttUserMax = 64U;
inline constexpr std::size_t kMqttPasswordMax = 64U;
inline constexpr std::size_t kMqttPrefixMax = 32U;
inline constexpr uint16_t kMqttPortDefault = 1883U;
inline constexpr char kMqttPrefixDefault[] = "homeassistant";
inline constexpr uint16_t kMqttPeriodMinS = 5U;
inline constexpr uint16_t kMqttPeriodMaxS = 600U;
inline constexpr uint16_t kMqttPeriodDefaultS = 30U;

/// MQTT link settings (page /mqtt). Untrusted input: validated by `validate()`.
struct MqttConfig {
    bool enabled;
    char host[kMqttHostMax + 1U];          ///< DNS name or IPv4 of the broker.
    uint16_t port;                         ///< 1883 by default, no TLS (local network).
    char user[kMqttUserMax + 1U];          ///< Empty = anonymous.
    char password[kMqttPasswordMax + 1U];  ///< Write-only on the page.
    char prefix[kMqttPrefixMax + 1U];      ///< Discovery prefix (`homeassistant`).
    uint16_t period_s;                     ///< State publication period.
};

enum class MqttConfigStatus : uint8_t {
    Ok = 0U,
    HostEmpty,             ///< Link enabled without a broker.
    HostInvalid,           ///< Character outside `[A-Za-z0-9.-]`, or dot/dash at an edge.
    PortInvalid,           ///< Port 0.
    UserInvalid,           ///< Non printable ASCII.
    PasswordInvalid,       ///< Non printable ASCII.
    PasswordWithoutUser,   ///< Refused by MQTT 3.1.1.
    PrefixInvalid,         ///< Empty, character outside `[a-z0-9_-/]`, or `/` at an edge.
    PeriodInvalid,         ///< Outside kMqttPeriodMinS..kMqttPeriodMaxS.
};

/// @return Link disabled, port 1883, prefix `homeassistant`, 30 s.
MqttConfig default_mqtt_config() noexcept;
/// @return `Ok` or the first defect. A disabled link may keep an empty host.
MqttConfigStatus validate(const MqttConfig& c) noexcept;
bool is_valid(const MqttConfig& c) noexcept;
/// French reason for the page, "" for Ok.
const char* mqtt_status_reason(MqttConfigStatus s) noexcept;

// ------------------------------------------------------------------ identity and topics

inline constexpr char kMqttOnline[] = "online";
inline constexpr char kMqttOffline[] = "offline";
inline constexpr std::size_t kMqttIdSize = 13U;          ///< 12 hex digits + NUL.
inline constexpr std::size_t kMqttTopicMax = 128U;
inline constexpr std::size_t kMqttDiscoveryMax = 1024U;
inline constexpr std::size_t kMqttStateMax = 512U;

/// The clock as a Home Assistant device.
struct DeviceIdentity {
    std::array<char, kMqttIdSize> id;   ///< MAC in lowercase hex (`1020ba5c7df4`).
    std::array<char, 18> mac;           ///< Readable MAC (`10:20:ba:5c:7d:f4`).
    std::array<char, 24> version;       ///< Firmware version.
    std::array<char, 24> model;         ///< Board name (`horloge-v1`).
    std::array<char, 48> url;           ///< Local page (`http://10.0.0.5/`), empty if unknown.
};

/**
 * @brief Builds the identity.
 * @param mac     Station MAC address.
 * @param version Firmware version (NUL-terminated).
 * @param model   Board name (NUL-terminated).
 * @param ip      IPv4 as text, or nullptr/empty if unknown.
 * @param out     Identity produced.
 * @return false if a string does not fit.
 */
bool build_identity(const std::array<uint8_t, 6>& mac, const char* version, const char* model, const char* ip,
                    DeviceIdentity& out) noexcept;

enum class MqttTopic : uint8_t {
    Availability = 0U,  ///< `horloge/<id>/disponible`
    State,              ///< `horloge/<id>/etat`
};

/// @return Length written, 0 if @p out_size is too small.
std::size_t mqtt_topic(MqttTopic t, const DeviceIdentity& ident, char* out, std::size_t out_size) noexcept;
/// `<prefix>/status`, the Home Assistant birth/will topic.
std::size_t ha_status_topic(const char* prefix, char* out, std::size_t out_size) noexcept;
/// @return true for `<prefix>/status` = `online`: send the discovery again.
bool is_ha_online(const char* prefix, const char* topic, std::size_t tlen, const char* payload,
                  std::size_t plen) noexcept;

// ------------------------------------------------------------------ discovery

/// @return Number of entities (discovery messages).
std::size_t mqtt_entity_count() noexcept;
/// @return The `object_id` of entity @p index, nullptr if out of range.
const char* mqtt_entity_object(std::size_t index) noexcept;
/// @return The state JSON key read by entity @p index, nullptr if out of range.
const char* mqtt_entity_key(std::size_t index) noexcept;

/**
 * @brief Produces the discovery message of one entity (publish **retained**, QoS 1).
 * @return Payload length, 0 if @p index is out of range or a buffer is too small.
 */
std::size_t mqtt_discovery(std::size_t index, const DeviceIdentity& ident, const char* prefix, char* topic_out,
                           std::size_t topic_size, char* payload_out, std::size_t payload_size) noexcept;

// ------------------------------------------------------------------ state

/// What the clock publishes (one JSON document).
struct MqttState {
    bool air_enabled;                 ///< false: every air key is null.
    air_quality::AirReading air;
    air_quality::AirLevel level;      ///< Confirmed level (Unknown ⇒ null).
    bool ldr_valid;
    uint16_t ldr_raw;
    bool time_sure;
};

/// @return Length written, 0 if the buffer is too small (`kMqttStateMax` fits).
std::size_t mqtt_state_json(const MqttState& s, char* out, std::size_t out_size) noexcept;
/// `excellent`, `bon`, `moyen`, `mauvais`, `tres_mauvais`; nullptr for Unknown.
const char* mqtt_level_option(air_quality::AirLevel level) noexcept;

// ------------------------------------------------------------------ publisher

/// What the transport must do next (one at a time, in this order of priority).
enum class MqttAction : uint8_t {
    None = 0U,
    PublishOffline,    ///< Retained `offline` on the availability topic (before a stop).
    Disconnect,        ///< Stop the client.
    Connect,           ///< (Re)configure and start the client with the current settings.
    PublishOnline,     ///< Retained `online`.
    PublishDiscovery,  ///< Every entity, retained, QoS 1.
    PublishState,      ///< The state document, retained, QoS 0.
};

struct MqttStats {
    uint32_t connections;
    uint32_t disconnections;
    uint32_t discoveries;   ///< Discovery rounds sent.
    uint32_t states;        ///< State documents sent.
};

enum class MqttLinkState : uint8_t {
    Disabled = 0U,
    Connecting,   ///< Enabled, not (yet) connected.
    Connected,    ///< Session open, discovery published.
};
const char* mqtt_link_state_name(MqttLinkState s) noexcept;

/**
 * @brief Decides what to publish and when; the transport only executes.
 *
 * Inputs: settings, broker events, Home Assistant announcements, the state to
 * publish. Outputs: `next_action()`. No allocation, no time dependency besides
 * the millisecond counter passed in.
 */
class MqttPublisher {
public:
    MqttPublisher() noexcept;

    /// New settings: a running link is stopped (offline first) and restarted if still enabled.
    void configure(const MqttConfig& c) noexcept;
    /// Broker session opened.
    void on_connected() noexcept;
    /// Broker session lost (the transport reconnects by itself).
    void on_disconnected() noexcept;
    /// A message arrived (only `<prefix>/status` matters).
    void on_message(const char* topic, std::size_t tlen, const char* payload, std::size_t plen) noexcept;
    /// New measurements; a level change publishes without waiting for the period.
    void update_state(const MqttState& s, uint32_t now_ms) noexcept;
    /// Page button: send the discovery again.
    void request_discovery() noexcept;

    /// @return The next action, `None` when nothing is pending. Call until `None`.
    MqttAction next_action(uint32_t now_ms) noexcept;

    const MqttConfig& config() const noexcept { return cfg_; }
    const MqttState& state() const noexcept { return state_; }
    bool has_state() const noexcept { return state_known_; }
    bool connected() const noexcept { return connected_; }
    MqttLinkState link_state() const noexcept;
    const MqttStats& stats() const noexcept { return stats_; }

private:
    MqttConfig cfg_;
    MqttState state_;
    MqttStats stats_;
    bool state_known_;
    bool connected_;
    bool client_running_;     ///< A client was started (must be stopped before a reconfigure).
    bool reconfigure_;        ///< configure() pending.
    bool online_pending_;
    bool discovery_pending_;
    bool state_pending_;
    bool discovery_done_;
    uint32_t last_state_ms_;
    air_quality::AirLevel last_level_;
};

}  // namespace homeassistant
