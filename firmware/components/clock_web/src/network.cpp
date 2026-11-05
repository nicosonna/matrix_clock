/**
 * @file network.cpp
 * @brief Wi-Fi station supervised by network::WifiSupervisor, access point +
 *        captive portal (like the Ev_charger), credentials in NVS, mDNS.
 */
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "clock_board/board.hpp"
#include "clock_web/web.hpp"
#include "network/captive_dns.hpp"
#include "network/credentials.hpp"
#include "network/supervisor.hpp"

namespace clock_web::detail {

namespace {

constexpr char kTag[] = "net";
constexpr char kNvsNamespace[] = "clk_wifi";
constexpr char kNvsSsid[] = "ssid";
constexpr char kNvsPass[] = "pass";
constexpr uint8_t kApChannel = 1U;
constexpr uint8_t kApMaxClients = 4U;
constexpr uint32_t kSupervisePeriodMs = 1000U;
constexpr time_t kDnsWaitS = 1;
constexpr uint16_t kDnsPort = 53U;
constexpr uint16_t kScanMax = 16U;
const std::array<uint8_t, 4> kApIp{192U, 168U, 4U, 1U};
constexpr uint32_t kNetTaskStack = 4096U;
constexpr uint32_t kDnsTaskStack = 3072U;
constexpr UBaseType_t kNetTaskPriority = 3U;
constexpr UBaseType_t kDnsTaskPriority = 2U;

std::atomic<bool> s_associated{false};
std::atomic<bool> s_sta_up{false};
std::atomic<bool> s_portal{false};
std::atomic<bool> s_creds_changed{false};
std::atomic<uint8_t> s_cause{0U};  ///< network::FailCause latched since the last step.
std::atomic<uint8_t> s_state{0U};  ///< network::WifiState for info().
SemaphoreHandle_t s_mutex = nullptr;

/// Current credentials and texts (protected by s_mutex).
char s_ssid[network::kSsidMax + 1U] = {};
char s_pass[network::kPassMax + 1U] = {};
char s_ap_ssid[33] = {};
char s_ap_pass[network::kApPassLen + 1U] = {};
char s_ip[16] = {};

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

bool lock() {
    return (s_mutex != nullptr) && (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE);
}

void unlock() {
    (void)xSemaphoreGive(s_mutex);
}

/// Loads the credentials from NVS (the portal writes them there).
void load_credentials() {
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    nvs_handle_t h = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_ssid);
        if (nvs_get_str(h, kNvsSsid, s_ssid, &n) != ESP_OK) {
            s_ssid[0] = '\0';
        }
        n = sizeof(s_pass);
        if (nvs_get_str(h, kNvsPass, s_pass, &n) != ESP_OK) {
            s_pass[0] = '\0';
        }
        nvs_close(h);
    }
}

bool has_credentials() {
    return s_ssid[0] != '\0';
}

network::FailCause cause_from_reason(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
            return network::FailCause::ApNotFound;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return network::FailCause::AuthFailed;
        default:
            return network::FailCause::Other;
    }
}

void on_event(void* /*arg*/, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_CONNECTED) {
            s_associated = true;
            ESP_LOGI(kTag, "associated to the access point, waiting for a DHCP lease");
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            const wifi_event_sta_disconnected_t* ev = static_cast<const wifi_event_sta_disconnected_t*>(data);
            s_associated = false;
            s_sta_up = false;
            s_cause = static_cast<uint8_t>(cause_from_reason(ev->reason));
            ESP_LOGI(kTag, "station disconnected (reason %u)", static_cast<unsigned>(ev->reason));
            // No reconnection here: the supervisor decides.
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            ESP_LOGI(kTag, "a client joined the access point");
        } else {
            // other events: nothing to do
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            const ip_event_got_ip_t* ev = static_cast<const ip_event_got_ip_t*>(data);
            if (lock()) {
                (void)std::snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
                unlock();
            }
            s_sta_up = true;
            ESP_LOGI(kTag, "connected: page at http://" IPSTR "/ (and http://%s.local/)", IP2STR(&ev->ip_info.ip),
                     CONFIG_CLOCK_MDNS_HOSTNAME);
        } else if (id == IP_EVENT_STA_LOST_IP) {
            s_sta_up = false;
            ESP_LOGW(kTag, "IP address lost");
        } else {
            // nothing
        }
    } else {
        // nothing
    }
}

/// Access point: WPA2 with the menuconfig password, else derived from the MAC.
bool configure_ap() {
    std::array<uint8_t, 6> mac{};
    (void)esp_read_mac(mac.data(), ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = {};
    const int ssid_len = std::snprintf(reinterpret_cast<char*>(ap.ap.ssid), sizeof(ap.ap.ssid),
                                       "Horloge-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = static_cast<uint8_t>((ssid_len > 0) ? ssid_len : 0);
    ap.ap.channel = kApChannel;
    ap.ap.max_connection = kApMaxClients;

    const char* configured = CONFIG_CLOCK_WIFI_AP_PASSWORD;
    std::array<char, network::kApPassLen + 1U> derived{};
    const char* pass = configured;
    if (std::strlen(configured) < network::kPassMin) {
        if (std::strlen(configured) > 0U) {
            ESP_LOGW(kTag, "access point password too short (< 8): ignored");
        }
        network::derive_ap_password(mac, derived);
        pass = derived.data();
    }
    std::strncpy(reinterpret_cast<char*>(ap.ap.password), pass, sizeof(ap.ap.password) - 1U);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
        return false;
    }
    if (lock()) {
        std::strncpy(s_ap_ssid, reinterpret_cast<const char*>(ap.ap.ssid), sizeof(s_ap_ssid) - 1U);
        std::strncpy(s_ap_pass, pass, sizeof(s_ap_pass) - 1U);
        unlock();
    }
    ESP_LOGI(kTag, "access point %s (WPA2, password %s): page at http://192.168.4.1/",
             reinterpret_cast<const char*>(ap.ap.ssid), pass);
    return true;
}

bool configure_sta() {
    wifi_config_t sta = {};
    if (!lock()) {
        return false;
    }
    std::strncpy(reinterpret_cast<char*>(sta.sta.ssid), s_ssid, sizeof(sta.sta.ssid) - 1U);
    std::strncpy(reinterpret_cast<char*>(sta.sta.password), s_pass, sizeof(sta.sta.password) - 1U);
    unlock();
    return esp_wifi_set_config(WIFI_IF_STA, &sta) == ESP_OK;
}

/// Captive DNS server: answers only while the portal is active.
void dns_task(void* /*arg*/) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(kTag, "captive DNS: no socket");
        vTaskDelete(nullptr);
        return;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kDnsPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ESP_LOGE(kTag, "captive DNS: port 53 unavailable");
        close(sock);
        vTaskDelete(nullptr);
        return;
    }
    timeval wait = {};
    wait.tv_sec = kDnsWaitS;
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    ESP_LOGI(kTag, "captive DNS ready (port 53)");
    static uint8_t rx[network::kDnsMaxMessage];
    static uint8_t tx[network::kDnsMaxMessage + 16U];
    for (;;) {
        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        const int n = recvfrom(sock, rx, sizeof(rx), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if ((n <= 0) || !s_portal) {
            continue;  // outside the portal: hijack nothing
        }
        const size_t out = network::build_captive_response(rx, static_cast<size_t>(n), kApIp, tx, sizeof(tx));
        if (out > 0U) {
            (void)sendto(sock, tx, out, 0, reinterpret_cast<sockaddr*>(&from), from_len);
        }
    }
}

/// Applies the supervisor's plan: mode, connection, portal.
void apply_plan(const network::WifiPlan& p, network::WifiPlan& applied, bool& first) {
    const wifi_mode_t mode = p.ap_on ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    if (first || (p.ap_on != applied.ap_on)) {
        // STA kept even without a connection: the portal's scan needs it.
        (void)esp_wifi_set_mode(mode);
        ESP_LOGI(kTag, "access point %s", p.ap_on ? "on" : "off");
    }
    if (first || (p.portal_on != applied.portal_on)) {
        s_portal = p.portal_on;
        ESP_LOGI(kTag, "captive portal %s", p.portal_on ? "active" : "inactive");
    }
    const bool new_attempt = p.sta_connect && (first || !applied.sta_connect || (p.attempt_id != applied.attempt_id));
    if (new_attempt) {
        (void)configure_sta();
        (void)esp_wifi_disconnect();
        (void)esp_wifi_connect();
        ESP_LOGI(kTag, "connecting to \"%s\" (attempt %lu)", s_ssid, static_cast<unsigned long>(p.attempt_id));
    } else if (!p.sta_connect && (first || applied.sta_connect)) {
        // Station IDLE in fallback: a retrying station hops the channel and
        // the access point drops its clients (seen on the charger).
        (void)esp_wifi_disconnect();
    } else {
        // nothing to change
    }
    if (p.request_reboot) {
        ESP_LOGW(kTag, "no IP address for too long: recovery reboot");
        vTaskDelay(pdMS_TO_TICKS(200U));
        esp_restart();
    }
    applied = p;
    first = false;
}

const char* state_name(network::WifiState s) {
    switch (s) {
        case network::WifiState::NoCredentials: return "no_credentials";
        case network::WifiState::BootGrace: return "boot_grace";
        case network::WifiState::Connecting: return "connecting";
        case network::WifiState::WaitingLease: return "waiting_lease";
        case network::WifiState::LeaseDesperate: return "lease_desperate";
        case network::WifiState::Connected: return "connected";
        case network::WifiState::Fallback: return "fallback";
        case network::WifiState::Retrying: return "retrying";
        default: return "?";
    }
}

/// Wi-Fi supervision task, period 1 s.
void net_task(void* /*arg*/) {
    network::WifiSupervisor sup(network::default_wifi_supervisor_config(), has_credentials(), now_ms());
    network::WifiPlan applied{};
    bool first = true;
    network::WifiState last_state = sup.state();
    ESP_LOGI(kTag, "supervisor: %s", state_name(last_state));
    for (;;) {
        if (s_creds_changed.exchange(false)) {
            sup = network::WifiSupervisor(network::default_wifi_supervisor_config(), has_credentials(), now_ms());
            ESP_LOGI(kTag, "credentials changed: %s", has_credentials() ? "new connection" : "back to the portal");
        }
        const network::LinkStatus link{s_associated.load(), s_sta_up.load()};
        const network::FailCause cause = static_cast<network::FailCause>(s_cause.exchange(0U));
        const network::WifiPlan p = sup.step(now_ms(), link, cause);
        if (sup.state() != last_state) {
            last_state = sup.state();
            ESP_LOGI(kTag, "supervisor: %s", state_name(last_state));
        }
        s_state = static_cast<uint8_t>(sup.state());
        apply_plan(p, applied, first);
        vTaskDelay(pdMS_TO_TICKS(kSupervisePeriodMs));
    }
}

bool start_wifi() {
    if ((esp_netif_create_default_wifi_ap() == nullptr) || (esp_netif_create_default_wifi_sta() == nullptr)) {
        return false;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) {
        return false;
    }
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);  // our credentials live in our NVS namespace
    if ((esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, nullptr) != ESP_OK) ||
        (esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_event, nullptr) != ESP_OK)) {
        return false;
    }
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
        return false;
    }
    if (!configure_ap()) {
        return false;
    }
    if (has_credentials() && !configure_sta()) {
        return false;
    }
    return esp_wifi_start() == ESP_OK;
}

void start_mdns() {
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(kTag, "mDNS not started");
        return;
    }
    (void)mdns_hostname_set(CONFIG_CLOCK_MDNS_HOSTNAME);
    (void)mdns_instance_name_set("Horloge");
    (void)mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    ESP_LOGI(kTag, "mDNS: %s.local", CONFIG_CLOCK_MDNS_HOSTNAME);
}

}  // namespace

bool start_network() noexcept {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == nullptr) {
        return false;
    }
    load_credentials();
    if (esp_netif_init() != ESP_OK) {
        return false;
    }
    const esp_err_t loop = esp_event_loop_create_default();
    if ((loop != ESP_OK) && (loop != ESP_ERR_INVALID_STATE)) {
        return false;
    }
    if (!start_wifi()) {
        ESP_LOGE(kTag, "Wi-Fi failed to start");
        return false;
    }
    ESP_LOGI(kTag, "Wi-Fi %s", has_credentials() ? "configured: connecting" : "not configured: captive portal");
    start_mdns();
    if ((xTaskCreate(dns_task, "dns", kDnsTaskStack, nullptr, kDnsTaskPriority, nullptr) != pdPASS) ||
        (xTaskCreate(net_task, "net", kNetTaskStack, nullptr, kNetTaskPriority, nullptr) != pdPASS)) {
        ESP_LOGE(kTag, "network tasks not created");
        return false;
    }
    return true;
}

bool portal_active() noexcept {
    return s_portal;
}

bool save_credentials(const char* ssid, const char* pass) noexcept {
    nvs_handle_t h = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = (nvs_set_str(h, kNvsSsid, ssid) == ESP_OK) && (nvs_set_str(h, kNvsPass, pass) == ESP_OK) &&
                    (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    if (!ok || !lock()) {
        return false;
    }
    std::strncpy(s_ssid, ssid, sizeof(s_ssid) - 1U);
    s_ssid[sizeof(s_ssid) - 1U] = '\0';
    std::strncpy(s_pass, pass, sizeof(s_pass) - 1U);
    s_pass[sizeof(s_pass) - 1U] = '\0';
    unlock();
    s_creds_changed = true;
    ESP_LOGI(kTag, "Wi-Fi credentials saved for \"%s\"", ssid);
    return true;
}

bool forget_credentials() noexcept {
    nvs_handle_t h = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    (void)nvs_erase_key(h, kNvsSsid);
    (void)nvs_erase_key(h, kNvsPass);
    const bool ok = nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (lock()) {
        s_ssid[0] = '\0';
        s_pass[0] = '\0';
        unlock();
    }
    s_creds_changed = true;
    ESP_LOGI(kTag, "Wi-Fi credentials erased: back to the portal");
    return ok;
}

std::size_t scan_json(char* out, std::size_t out_size) noexcept {
    if ((out == nullptr) || (out_size < 3U)) {
        return 0U;
    }
    std::size_t w = 0U;
    out[w++] = '[';
    if (esp_wifi_scan_start(nullptr, true) == ESP_OK) {
        static wifi_ap_record_t recs[kScanMax];
        uint16_t count = kScanMax;
        if (esp_wifi_scan_get_ap_records(&count, recs) == ESP_OK) {
            for (uint16_t i = 0U; i < count; ++i) {
                char esc[network::kSsidMax * 6U + 1U];
                const char* ssid = reinterpret_cast<const char*>(recs[i].ssid);
                const size_t ssid_len = strnlen(ssid, network::kSsidMax);
                if ((ssid_len == 0U) || (network::json_escape(ssid, ssid_len, esc, sizeof(esc)) == 0U)) {
                    continue;  // hidden network, or a name we cannot escape
                }
                const int n = std::snprintf(out + w, out_size - w, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"ouvert\":%s}",
                                            (w > 1U) ? "," : "", esc, static_cast<int>(recs[i].rssi),
                                            (recs[i].authmode == WIFI_AUTH_OPEN) ? "true" : "false");
                if ((n < 0) || (static_cast<size_t>(n) >= (out_size - w - 1U))) {
                    break;  // no room left: keep what fits
                }
                w += static_cast<size_t>(n);
            }
        }
    }
    out[w++] = ']';
    out[w] = '\0';
    return w;
}

}  // namespace clock_web::detail

namespace clock_web {

NetworkInfo info() noexcept {
    NetworkInfo i{};
    i.portal = detail::portal_active();
    i.connected = detail::s_sta_up.load();
    i.state = detail::state_name(static_cast<network::WifiState>(detail::s_state.load()));
    if (detail::lock()) {
        std::strncpy(i.ap_ssid, detail::s_ap_ssid, sizeof(i.ap_ssid) - 1U);
        std::strncpy(i.ap_password, detail::s_ap_pass, sizeof(i.ap_password) - 1U);
        std::strncpy(i.ip, detail::s_ip, sizeof(i.ip) - 1U);
        detail::unlock();
    }
    if (!i.connected) {
        i.ip[0] = '\0';
    }
    return i;
}

}  // namespace clock_web
