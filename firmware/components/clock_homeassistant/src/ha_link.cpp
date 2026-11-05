/**
 * @file ha_link.cpp
 * @brief Home Assistant link task.
 */
#include "clock_homeassistant/ha_link.hpp"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "clock_settings/store.hpp"
#include "homeassistant/reassembler.hpp"
#include "homeassistant/weather.hpp"
#include "settings/model.hpp"

namespace clock_homeassistant {

namespace {

constexpr char kTag[] = "ha";
constexpr uint32_t kStepMs = 100U;
constexpr UBaseType_t kTaskPriority = 4U;
constexpr uint32_t kTaskStack = 8192U;
constexpr int kSendTimeoutMs = 2000;
constexpr int kNetworkTimeoutMs = 10000;
constexpr int kWeatherTimeoutMs = 10000;
constexpr std::size_t kWeatherBodyMax = 1024U;
constexpr uint32_t kWeatherStartupDelayMs = 20000U;  ///< Let the network settle before the first request.

SemaphoreHandle_t s_mutex = nullptr;
settings::HaSettings s_cfg{};           // the token must outlive the client: lives here
homeassistant::WsClient* s_client = nullptr;
homeassistant::Reassembler s_reassembler;
esp_websocket_client_handle_t s_ws = nullptr;
NotificationHandler s_on_notification = nullptr;
LinkSnapshot s_snapshot{};
uint32_t s_weather_last_ms = 0U;
bool s_weather_valid = false;
int16_t s_weather_deci = 0;
uint32_t s_weather_received_ms = 0U;
char s_weather_body[kWeatherBodyMax + 1U];

uint32_t now_ms() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

bool lock() noexcept {
    return (s_mutex != nullptr) && (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE);
}

void unlock() noexcept {
    (void)xSemaphoreGive(s_mutex);
}

void ws_event(void* /*arg*/, esp_event_base_t /*base*/, int32_t event_id, void* event_data) {
    const esp_websocket_event_data_t* d = static_cast<const esp_websocket_event_data_t*>(event_data);
    const uint32_t now = now_ms();
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(kTag, "socket connected");
            if (lock()) {
                s_reassembler.reset();
                s_client->on_connected(now);
                unlock();
            }
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_ERROR:
        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGW(kTag, "socket %s", (event_id == WEBSOCKET_EVENT_ERROR) ? "error" : "closed");
            if (lock()) {
                s_reassembler.reset();
                s_client->on_disconnected(now);
                unlock();
            }
            break;
        case WEBSOCKET_EVENT_DATA:
            if ((d != nullptr) && (d->op_code == 0x01) && (d->data_len > 0)) {  // text frame
                if (lock()) {
                    if (s_reassembler.feed(d->data_ptr, static_cast<std::size_t>(d->data_len),
                                           static_cast<std::size_t>(d->payload_offset),
                                           static_cast<std::size_t>(d->payload_len))) {
                        s_client->on_message(s_reassembler.message(), s_reassembler.length(), now);
                    }
                    unlock();
                }
            }
            break;
        default:
            break;
    }
}

void close_socket() noexcept {
    if (s_ws != nullptr) {
        (void)esp_websocket_client_stop(s_ws);
        (void)esp_websocket_client_destroy(s_ws);
        s_ws = nullptr;
    }
}

bool open_socket() noexcept {
    close_socket();
    static char uri[160];
    if (settings::ha_uri(s_cfg, uri, sizeof(uri)) == 0U) {
        return false;
    }
    esp_websocket_client_config_t cfg = {};
    cfg.uri = uri;
    cfg.buffer_size = static_cast<int>(homeassistant::kMessageMax);
    cfg.network_timeout_ms = kNetworkTimeoutMs;
    cfg.disable_auto_reconnect = true;  // the pure client decides when to retry
    cfg.task_stack = 6144;
    if (s_cfg.secure) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    s_ws = esp_websocket_client_init(&cfg);
    if (s_ws == nullptr) {
        ESP_LOGE(kTag, "client init failed");
        return false;
    }
    if (esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event, nullptr) != ESP_OK) {
        close_socket();
        return false;
    }
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ESP_LOGE(kTag, "client start failed");
        close_socket();
        return false;
    }
    ESP_LOGI(kTag, "connecting to %s", uri);
    return true;
}

/// One Open-Meteo request; the value stays invalid on any failure.
void fetch_weather(uint32_t now) noexcept {
    static char url[192];
    if (homeassistant::weather_url(s_cfg.weather, url, sizeof(url)) == 0U) {
        s_weather_valid = false;
        return;
    }
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = kWeatherTimeoutMs;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        s_weather_valid = false;
        return;
    }
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        int total = 0;
        while (static_cast<std::size_t>(total) < kWeatherBodyMax) {
            const int r = esp_http_client_read(client, s_weather_body + total, static_cast<int>(kWeatherBodyMax) - total);
            if (r <= 0) {
                break;
            }
            total += r;
        }
        s_weather_body[total] = '\0';
        int16_t deci = 0;
        if ((status == 200) && homeassistant::decode_weather(s_weather_body, static_cast<std::size_t>(total), deci)) {
            s_weather_deci = deci;
            s_weather_received_ms = now;
            ok = true;
        } else {
            ESP_LOGW(kTag, "weather: HTTP %d, %d bytes, undecodable", status, total);
        }
    } else {
        ESP_LOGW(kTag, "weather: request failed");
    }
    (void)esp_http_client_cleanup(client);
    s_weather_valid = ok;
    if (ok) {
        ESP_LOGI(kTag, "weather: %d.%d C", static_cast<int>(s_weather_deci / 10), static_cast<int>((s_weather_deci < 0 ? -s_weather_deci : s_weather_deci) % 10));
    }
}

void publish_snapshot(uint32_t now) noexcept {
    static LinkSnapshot snap;
    std::memset(&snap, 0, sizeof(snap));
    snap.enabled = s_cfg.enabled;
    snap.state = s_client->state();
    std::strncpy(snap.state_name, s_client->state_name(), sizeof(snap.state_name) - 1U);
    std::strncpy(snap.error, s_client->last_error(), sizeof(snap.error) - 1U);
    snap.stats = s_client->stats();
    snap.frames_dropped = s_reassembler.dropped();
    for (std::size_t i = 0U; i < homeassistant::kValuesMax; ++i) {
        const homeassistant::ValueConfig& c = s_client->value_config(i);
        const homeassistant::ValueState& st = s_client->value(i);
        ValueSnapshot& v = snap.values[i];
        v.enabled = c.enabled;
        v.type = c.type;
        v.decimals = c.decimals;
        v.icon = c.icon;
        v.valid = c.enabled && s_client->value_valid(i, now);
        v.number = st.number;
        v.boolean = st.boolean;
        std::strncpy(v.name, c.name, sizeof(v.name) - 1U);
        std::strncpy(v.unit, c.unit, sizeof(v.unit) - 1U);
        std::strncpy(v.error, st.error, sizeof(v.error) - 1U);
        if (v.valid && (c.type == homeassistant::ValueType::Number)) {
            (void)homeassistant::format_number(st.number, c.decimals, c.unit, v.text, sizeof(v.text));
        } else if (v.valid && (c.type == homeassistant::ValueType::Boolean)) {
            std::strncpy(v.text, st.boolean ? "ON" : "OFF", sizeof(v.text) - 1U);
        } else {
            std::strncpy(v.text, st.text, sizeof(v.text) - 1U);
        }
    }
    // Weather fallback replaces value 1 while it is unavailable (EX-35).
    snap.weather_enabled = s_cfg.weather.enabled;
    snap.weather_valid = s_cfg.weather.enabled && s_weather_valid;
    snap.weather_deci = s_weather_deci;
    snap.weather_age_s = s_weather_valid ? ((now - s_weather_received_ms) / 1000U) : 0U;
    if (snap.weather_valid && !snap.values[0].valid) {
        ValueSnapshot& v = snap.values[0];
        v.enabled = true;
        v.valid = true;
        v.from_weather = true;
        v.type = homeassistant::ValueType::Number;
        v.number = s_weather_deci;
        v.decimals = 1U;
        if (v.unit[0] == '\0') {
            std::strncpy(v.unit, "`", sizeof(v.unit) - 1U);
        }
        if (v.icon == homeassistant::ValueIcon::None) {
            v.icon = homeassistant::ValueIcon::Thermometer;
        }
        (void)homeassistant::format_number(v.number, 1U, v.unit, v.text, sizeof(v.text));
    }
    if (lock()) {
        s_snapshot = snap;
        unlock();
    }
}

void ha_task(void* /*arg*/) {
    static homeassistant::WsClient client(settings::ws_config(s_cfg), s_cfg.values);
    s_client = &client;
    uint32_t applied_version = 0U;
    homeassistant::LinkState last_state = homeassistant::LinkState::Disabled;
    const uint32_t started = now_ms();
    s_weather_last_ms = started - (static_cast<uint32_t>(s_cfg.weather.period_s) * 1000U) + kWeatherStartupDelayMs;
    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t version = clock_settings::version();
        bool reconfigured = false;
        if (version != applied_version) {
            applied_version = version;
            if (lock()) {
                clock_settings::ha(s_cfg);
                client.configure(settings::ws_config(s_cfg), s_cfg.values, now);
                unlock();
            }
            reconfigured = true;
            ESP_LOGI(kTag, "settings applied: link %s, host %s", s_cfg.enabled ? "on" : "off", s_cfg.host);
        }
        bool want_close = false;
        bool want_connect = false;
        static char out[homeassistant::kOutgoingMax];
        if (lock()) {
            client.step(now);
            want_close = client.take_close_request();
            want_connect = client.take_connect_request();
            unlock();
        }
        if (want_close || (reconfigured && !s_cfg.enabled)) {
            close_socket();
        }
        if (want_connect) {
            if (!open_socket()) {
                if (lock()) {
                    client.on_disconnected(now);
                    unlock();
                }
            }
        }
        // Outgoing messages, one at a time (the socket send happens outside the lock).
        for (uint32_t guard = 0U; guard < 12U; ++guard) {
            std::size_t len = 0U;
            if (lock()) {
                len = client.next_outgoing(out, sizeof(out));
                unlock();
            }
            if (len == 0U) {
                break;
            }
            if ((s_ws == nullptr) || !esp_websocket_client_is_connected(s_ws) ||
                (esp_websocket_client_send_text(s_ws, out, static_cast<int>(len), pdMS_TO_TICKS(kSendTimeoutMs)) < 0)) {
                ESP_LOGW(kTag, "send failed: closing");
                close_socket();
                if (lock()) {
                    client.on_disconnected(now);
                    unlock();
                }
                break;
            }
        }
        // Inbound notification.
        alerts::Notification n{};
        bool have = false;
        if (lock()) {
            have = client.take_notification(n);
            unlock();
        }
        if (have && (s_on_notification != nullptr)) {
            (void)s_on_notification(n);
        }
        // Weather fallback.
        if (s_cfg.weather.enabled) {
            if ((now - s_weather_last_ms) >= (static_cast<uint32_t>(s_cfg.weather.period_s) * 1000U)) {
                s_weather_last_ms = now;
                fetch_weather(now);
            }
        } else {
            s_weather_valid = false;
        }
        if (lock()) {
            const homeassistant::LinkState st = client.state();
            if (st != last_state) {
                last_state = st;
                ESP_LOGI(kTag, "link %s%s%s", client.state_name(), (client.last_error()[0] != '\0') ? ": " : "",
                         client.last_error());
            }
            unlock();
        }
        publish_snapshot(now);
        vTaskDelay(pdMS_TO_TICKS(kStepMs));
    }
}

}  // namespace

esp_err_t start(NotificationHandler on_notification) noexcept {
    s_on_notification = on_notification;
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }
    clock_settings::ha(s_cfg);
    if (xTaskCreatePinnedToCore(ha_task, "ha", kTaskStack, nullptr, kTaskPriority, nullptr, 0) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void snapshot(LinkSnapshot& out) noexcept {
    if (lock()) {
        out = s_snapshot;
        unlock();
    }
}

}  // namespace clock_homeassistant
