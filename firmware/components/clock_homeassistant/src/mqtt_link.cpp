/**
 * @file mqtt_link.cpp
 * @brief MQTT publication task (see mqtt_link.hpp).
 *
 * Two contexts: the esp-mqtt event handler (its own task) only forwards
 * events to the publisher under the mutex; the `mqtt` task runs the
 * publisher's actions every 250 ms. Static buffers only; esp-mqtt allocates
 * its own at creation and reconfiguration (user-initiated).
 */
#include "clock_homeassistant/mqtt_link.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "clock_board/board.hpp"
#include "clock_settings/store.hpp"

namespace clock_homeassistant {

namespace {

constexpr char kTag[] = "mqtt";
constexpr uint32_t kStepMs = 250U;
constexpr UBaseType_t kTaskPriority = 3U;
constexpr uint32_t kTaskStack = 4096U;
constexpr BaseType_t kCore = 0;
constexpr int kQosReliable = 1;
constexpr int kQosFast = 0;
constexpr int kRetain = 1;
constexpr int kKeepaliveS = 30;
constexpr int kReconnectMs = 10000;
constexpr int kNetworkTimeoutMs = 5000;
constexpr int kBufferSize = 1536;   ///< A discovery message (< 1 KB) must fit whole.
constexpr std::size_t kUriMax = 96U;
constexpr std::size_t kIpMax = 16U;
constexpr std::size_t kClientIdMax = 32U;

SemaphoreHandle_t s_mutex = nullptr;
esp_mqtt_client_handle_t s_client = nullptr;
StateProvider s_provider = nullptr;
homeassistant::MqttPublisher* s_pub = nullptr;
homeassistant::DeviceIdentity s_ident{};
uint32_t s_publish_failures = 0U;
char s_last_error[48] = {};

// Buffers of the mqtt task (the only user).
std::array<char, homeassistant::kMqttTopicMax> s_topic{};
std::array<char, homeassistant::kMqttDiscoveryMax> s_payload{};
std::array<char, homeassistant::kMqttStateMax> s_state{};
// Strings handed to esp-mqtt (it copies them, kept stable anyway).
std::array<char, kUriMax> s_uri{};
std::array<char, homeassistant::kMqttTopicMax> s_availability{};
std::array<char, kClientIdMax> s_client_id{};
homeassistant::MqttConfig s_client_cfg{};

uint32_t now_ms() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

bool lock() noexcept {
    return (s_mutex != nullptr) && (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE);
}

void unlock() noexcept {
    (void)xSemaphoreGive(s_mutex);
}

void set_error(const char* text) noexcept {
    std::strncpy(s_last_error, text, sizeof(s_last_error) - 1U);
    s_last_error[sizeof(s_last_error) - 1U] = '\0';
}

bool current_ip(char* out, std::size_t size) noexcept {
    esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    if ((n == nullptr) || (esp_netif_get_ip_info(n, &ip) != ESP_OK) || (ip.ip.addr == 0U)) {
        return false;
    }
    const int len = std::snprintf(out, size, IPSTR, IP2STR(&ip.ip));
    return (len > 0) && (static_cast<std::size_t>(len) < size);
}

/// Rebuilds the identity (the IP may change between two connections).
bool refresh_identity(homeassistant::DeviceIdentity& out) noexcept {
    std::array<uint8_t, 6> mac{};
    if (esp_read_mac(mac.data(), ESP_MAC_WIFI_STA) != ESP_OK) {
        return false;
    }
    std::array<char, kIpMax> ip{};
    const bool has_ip = current_ip(ip.data(), ip.size());
    return homeassistant::build_identity(mac, esp_app_get_description()->version, clock_board::kName,
                                         has_ip ? ip.data() : nullptr, out);
}

void mqtt_event(void* /*arg*/, esp_event_base_t /*base*/, int32_t id, void* data) {
    const esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(data);
    switch (static_cast<esp_mqtt_event_id_t>(id)) {
        case MQTT_EVENT_CONNECTED: {
            std::array<char, homeassistant::kMqttTopicMax> t{};
            if (lock()) {
                if (homeassistant::ha_status_topic(s_pub->config().prefix, t.data(), t.size()) > 0U) {
                    (void)esp_mqtt_client_subscribe(ev->client, t.data(), kQosReliable);
                }
                s_pub->on_connected();
                s_last_error[0] = '\0';
                unlock();
            }
            ESP_LOGI(kTag, "connected to the broker");
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            if (lock()) {
                if (s_pub->connected()) {
                    ESP_LOGW(kTag, "disconnected: the client retries by itself");
                }
                s_pub->on_disconnected();
                unlock();
            }
            break;
        case MQTT_EVENT_DATA:
            // Only whole, short messages matter (the status announcement).
            if ((ev->topic != nullptr) && (ev->data != nullptr) && (ev->data_len == ev->total_data_len) &&
                (ev->topic_len > 0) && (ev->data_len >= 0)) {
                if (lock()) {
                    s_pub->on_message(ev->topic, static_cast<std::size_t>(ev->topic_len), ev->data,
                                      static_cast<std::size_t>(ev->data_len));
                    unlock();
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            if (lock()) {
                if ((ev->error_handle != nullptr) && (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)) {
                    set_error("connexion refusée (utilisateur / mot de passe ?)");
                } else {
                    set_error("broker injoignable");
                }
                unlock();
            }
            break;
        default:
            break;
    }
}

bool publish(const char* topic, const char* payload, std::size_t len, int qos) noexcept {
    const int r = esp_mqtt_client_publish(s_client, topic, payload, static_cast<int>(len), qos, kRetain);
    if (r < 0) {
        ++s_publish_failures;
        return false;
    }
    return true;
}

/// Stops the client (the publisher asked for it).
void stop_client() noexcept {
    if (s_client != nullptr) {
        (void)esp_mqtt_client_stop(s_client);
    }
}

/// (Re)configures and starts the client with the publisher's settings.
void start_client(const homeassistant::MqttConfig& cfg) noexcept {
    if (!refresh_identity(s_ident)) {
        ESP_LOGE(kTag, "device identity unreadable: no MQTT");
        return;
    }
    const int nu = std::snprintf(s_uri.data(), s_uri.size(), "mqtt://%s:%u", cfg.host, static_cast<unsigned>(cfg.port));
    const int nc = std::snprintf(s_client_id.data(), s_client_id.size(), "horloge_%s", s_ident.id.data());
    if ((nu < 0) || (static_cast<std::size_t>(nu) >= s_uri.size()) || (nc < 0) ||
        (static_cast<std::size_t>(nc) >= s_client_id.size()) ||
        (homeassistant::mqtt_topic(homeassistant::MqttTopic::Availability, s_ident, s_availability.data(),
                                   s_availability.size()) == 0U)) {
        ESP_LOGE(kTag, "settings too long: no MQTT");
        return;
    }
    s_client_cfg = cfg;
    esp_mqtt_client_config_t mc{};
    mc.broker.address.uri = s_uri.data();
    mc.credentials.client_id = s_client_id.data();
    mc.credentials.username = (s_client_cfg.user[0] != '\0') ? s_client_cfg.user : nullptr;
    mc.credentials.authentication.password = (s_client_cfg.password[0] != '\0') ? s_client_cfg.password : nullptr;
    mc.session.last_will.topic = s_availability.data();
    mc.session.last_will.msg = homeassistant::kMqttOffline;
    mc.session.last_will.qos = kQosReliable;
    mc.session.last_will.retain = kRetain;
    mc.session.keepalive = kKeepaliveS;
    mc.network.reconnect_timeout_ms = kReconnectMs;
    mc.network.timeout_ms = kNetworkTimeoutMs;
    mc.buffer.size = kBufferSize;
    mc.buffer.out_size = kBufferSize;

    esp_err_t err = ESP_OK;
    if (s_client == nullptr) {
        s_client = esp_mqtt_client_init(&mc);
        if (s_client == nullptr) {
            ESP_LOGE(kTag, "client not created");
            return;
        }
        err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event, nullptr);
    } else {
        err = esp_mqtt_set_config(s_client, &mc);
    }
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(s_client);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "start failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(kTag, "publishing to %s as %s, prefix %s, every %u s", s_uri.data(), s_client_id.data(), cfg.prefix,
             static_cast<unsigned>(cfg.period_s));
}

void publish_discovery() noexcept {
    (void)refresh_identity(s_ident);  // the configuration_url follows the IP
    uint32_t ok = 0U;
    const std::size_t n = homeassistant::mqtt_entity_count();
    for (std::size_t i = 0U; i < n; ++i) {
        const std::size_t len = homeassistant::mqtt_discovery(i, s_ident, s_client_cfg.prefix, s_topic.data(),
                                                              s_topic.size(), s_payload.data(), s_payload.size());
        if ((len > 0U) && publish(s_topic.data(), s_payload.data(), len, kQosReliable)) {
            ++ok;
        }
    }
    ESP_LOGI(kTag, "discovery published: %lu/%u entities, device %s", static_cast<unsigned long>(ok),
             static_cast<unsigned>(n), s_ident.id.data());
}

void publish_state(const homeassistant::MqttState& st) noexcept {
    const std::size_t len = homeassistant::mqtt_state_json(st, s_state.data(), s_state.size());
    if ((len == 0U) ||
        (homeassistant::mqtt_topic(homeassistant::MqttTopic::State, s_ident, s_topic.data(), s_topic.size()) == 0U)) {
        ESP_LOGE(kTag, "state not published (buffer)");
        return;
    }
    (void)publish(s_topic.data(), s_state.data(), len, kQosFast);
}

void task(void* /*arg*/) {
    uint32_t applied_version = 0U;
    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t version = clock_settings::version();
        homeassistant::MqttState st{};
        if (s_provider != nullptr) {
            s_provider(st);
        }
        if (lock()) {
            if (version != applied_version) {
                applied_version = version;
                s_pub->configure(clock_settings::mqtt());
            }
            s_pub->update_state(st, now);
            // Execute the publisher's actions one at a time; each publish needs the current settings/identity.
            for (uint8_t guard = 0U; guard < 8U; ++guard) {
                const homeassistant::MqttAction a = s_pub->next_action(now);
                if (a == homeassistant::MqttAction::None) {
                    break;
                }
                switch (a) {
                    case homeassistant::MqttAction::PublishOffline:
                        if (s_client != nullptr) {
                            (void)publish(s_availability.data(), homeassistant::kMqttOffline,
                                          std::strlen(homeassistant::kMqttOffline), kQosReliable);
                        }
                        break;
                    case homeassistant::MqttAction::Disconnect:
                        stop_client();
                        break;
                    case homeassistant::MqttAction::Connect:
                        start_client(s_pub->config());
                        break;
                    case homeassistant::MqttAction::PublishOnline:
                        (void)publish(s_availability.data(), homeassistant::kMqttOnline,
                                      std::strlen(homeassistant::kMqttOnline), kQosReliable);
                        break;
                    case homeassistant::MqttAction::PublishDiscovery:
                        publish_discovery();
                        break;
                    case homeassistant::MqttAction::PublishState:
                        publish_state(s_pub->state());
                        break;
                    case homeassistant::MqttAction::None:
                    default:
                        break;
                }
            }
            unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(kStepMs));
    }
}

}  // namespace

esp_err_t mqtt_start(StateProvider provider) noexcept {
    static homeassistant::MqttPublisher publisher;
    s_pub = &publisher;
    s_provider = provider;
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(task, "mqtt", kTaskStack, nullptr, kTaskPriority, nullptr, kCore) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void mqtt_snapshot(MqttSnapshot& out) noexcept {
    out = MqttSnapshot{};
    out.state_name = homeassistant::mqtt_link_state_name(homeassistant::MqttLinkState::Disabled);
    if ((s_pub == nullptr) || !lock()) {
        return;
    }
    out.enabled = s_pub->config().enabled;
    out.state = s_pub->link_state();
    out.state_name = homeassistant::mqtt_link_state_name(out.state);
    out.stats = s_pub->stats();
    out.publish_failures = s_publish_failures;
    std::memcpy(out.device_id, s_ident.id.data(), sizeof(out.device_id));
    std::memcpy(out.last_error, s_last_error, sizeof(out.last_error));
    unlock();
}

void mqtt_request_discovery() noexcept {
    if ((s_pub != nullptr) && lock()) {
        s_pub->request_discovery();
        unlock();
    }
}

}  // namespace clock_homeassistant
