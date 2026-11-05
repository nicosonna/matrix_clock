/**
 * @file web_server.cpp
 * @brief HTTP server: embedded Wi-Fi page, captive portal probes, `/api/info`,
 *        inbound notification channel.
 */
#include "clock_web/web.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "alerts/notification.hpp"
#include "clock_board/board.hpp"
#include "network/credentials.hpp"

// Embedded page (EMBED_TXTFILES adds a trailing NUL).
extern const char kWifiStart[] asm("_binary_wifi_html_start");
extern const char kWifiEnd[] asm("_binary_wifi_html_end");

namespace clock_web {

namespace {

constexpr char kTag[] = "web";
constexpr char kPortalUrl[] = "http://192.168.4.1/wifi";
constexpr size_t kFormMax = 256U;           ///< Wi-Fi form body (bytes).
constexpr size_t kNotificationFormMax = 1024U;  ///< Notification body: 128-char text, x3 once encoded, plus fields.
constexpr size_t kScanJsonMax = 2048U;
constexpr size_t kHttpClientsMax = 4U;

httpd_handle_t s_server = nullptr;
NotificationHandler s_on_notification = nullptr;

size_t embedded_size(const char* start, const char* end) {
    return static_cast<size_t>(end - start) - 1U;  // without the trailing NUL
}

esp_err_t send_text(httpd_req_t* req, const char* status, const char* text) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, text);
}

/// Reads a bounded body into @p body (NUL-terminated); false = too big / incomplete.
bool read_body(httpd_req_t* req, char* body, size_t max, size_t& got) {
    got = 0U;
    if ((req->content_len == 0U) || (req->content_len > max)) {
        return false;
    }
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            return false;
        }
        got += static_cast<size_t>(r);
    }
    body[got] = '\0';
    return true;
}

/// GET /wifi (and, until the dashboard exists, GET /): the Wi-Fi page.
esp_err_t handle_wifi_page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kWifiStart, static_cast<ssize_t>(embedded_size(kWifiStart, kWifiEnd)));
}

/// Redirect to the portal: OS connectivity probes, unknown URLs in portal mode.
esp_err_t redirect_to_portal(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", kPortalUrl);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t handle_not_found(httpd_req_t* req, httpd_err_code_t /*err*/) {
    if (detail::portal_active()) {
        return redirect_to_portal(req);
    }
    return send_text(req, "404 Not Found", "Page introuvable");
}

/// GET /api/wifi/modele: credential bounds, single source (firmware).
esp_err_t handle_model(httpd_req_t* req) {
    char body[96];
    const size_t n = network::model_json(body, sizeof(body));
    if (n == 0U) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, static_cast<ssize_t>(n));
}

/// GET /api/wifi/scan: visible networks.
esp_err_t handle_scan(httpd_req_t* req) {
    static char body[kScanJsonMax];
    const size_t n = detail::scan_json(body, sizeof(body));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, static_cast<ssize_t>(n));
}

/// POST /api/wifi: saves the credentials (urlencoded form).
esp_err_t handle_save(httpd_req_t* req) {
    char body[kFormMax + 1U];
    size_t got = 0U;
    if (!read_body(req, body, kFormMax, got)) {
        return send_text(req, "400 Bad Request", "Formulaire vide, incomplet ou trop long.");
    }
    char ssid[network::kSsidMax + 1U] = {};
    char pass[network::kPassMax + 1U] = {};
    size_t ssid_len = 0U;
    size_t pass_len = 0U;
    if (!network::form_field(body, got, "ssid", ssid, sizeof(ssid), ssid_len)) {
        return send_text(req, "400 Bad Request", "Nom du réseau absent ou trop long.");
    }
    if (!network::form_field(body, got, "password", pass, sizeof(pass), pass_len)) {
        pass[0] = '\0';  // field absent: open network
        pass_len = 0U;
    }
    switch (network::validate(ssid, ssid_len, pass, pass_len)) {
        case network::CredStatus::Ok:
            break;
        case network::CredStatus::SsidEmpty:
            return send_text(req, "400 Bad Request", "Le nom du réseau est vide.");
        case network::CredStatus::SsidTooLong:
            return send_text(req, "400 Bad Request", "Le nom du réseau est trop long.");
        case network::CredStatus::PassTooShort:
        case network::CredStatus::PassTooLong:
            return send_text(req, "400 Bad Request", "Le mot de passe doit faire de 8 à 63 caractères.");
        case network::CredStatus::PassInvalidChar:
        default:
            return send_text(req, "400 Bad Request", "Le mot de passe contient un caractère non autorisé.");
    }
    if (!detail::save_credentials(ssid, pass)) {
        return send_text(req, "500 Internal Server Error", "Enregistrement impossible.");
    }
    return send_text(req, "200 OK",
                     "Enregistré. L'horloge se connecte à votre réseau ; le point d'accès "
                     "s'éteindra une fois connectée.");
}

/// POST /api/wifi/oublier: erases the credentials, back to the portal.
esp_err_t handle_forget(httpd_req_t* req) {
    if (!detail::forget_credentials()) {
        return send_text(req, "500 Internal Server Error", "Effacement impossible.");
    }
    return send_text(req, "200 OK", "Réseau oublié. Le point d'accès reste disponible.");
}

/// POST /api/notification: inbound channel (hub, Node-RED, scripts).
esp_err_t handle_notification(httpd_req_t* req) {
    static char body[kNotificationFormMax + 1U];
    size_t got = 0U;
    if (!read_body(req, body, kNotificationFormMax, got)) {
        return send_text(req, "400 Bad Request", "Corps vide, incomplet ou trop long.");
    }
    alerts::Notification n{};
    switch (alerts::decode_form(body, got, n)) {
        case alerts::DecodeStatus::Ok:
            break;
        case alerts::DecodeStatus::KeyMissing:
            return send_text(req, "400 Bad Request", "Champ cle absent.");
        case alerts::DecodeStatus::KeyInvalid:
            return send_text(req, "400 Bad Request", "cle : 1 à 24 caractères parmi a-z, 0-9 et _.");
        case alerts::DecodeStatus::BodyInvalid:
        default:
            return send_text(req, "400 Bad Request", "Formulaire mal encodé.");
    }
    if ((s_on_notification == nullptr) || !s_on_notification(n)) {
        return send_text(req, "503 Service Unavailable", "Notification refusée (file pleine).");
    }
    char reply[96];
    (void)std::snprintf(reply, sizeof(reply), "Notification %s (%u champ(s) remplacé(s) par défaut).",
                        n.clear ? "effacée" : "acceptée", static_cast<unsigned>(n.defaults_used));
    return send_text(req, "200 OK", reply);
}

/// GET /api/info: identity and network state.
esp_err_t handle_info(httpd_req_t* req) {
    const esp_app_desc_t* app = esp_app_get_description();
    const NetworkInfo net = info();
    char body[320];
    const int n = std::snprintf(body, sizeof(body),
                                "{\"projet\":\"%s\",\"version\":\"%s\",\"carte\":\"%s\",\"reseau\":\"%s\","
                                "\"portail\":%s,\"connecte\":%s,\"ip\":\"%s\",\"ap\":\"%s\",\"fonctionnement_s\":%lu}",
                                app->project_name, app->version, clock_board::kName, net.state,
                                net.portal ? "true" : "false", net.connected ? "true" : "false", net.ip, net.ap_ssid,
                                static_cast<unsigned long>(esp_timer_get_time() / 1000000));
    if ((n < 0) || (static_cast<size_t>(n) >= sizeof(body))) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, static_cast<ssize_t>(n));
}

/// Initialises NVS (Wi-Fi, portal credentials).
bool init_nvs() {
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        // Blank or older-format NVS: erasing loses at most the Wi-Fi
        // credentials, which the portal asks again.
        if (nvs_flash_erase() != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

bool start_http() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 16;
    cfg.max_open_sockets = kHttpClientsMax;
    cfg.lru_purge_enable = true;  // browsers keep connections open: recycle the oldest
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        return false;
    }
    static const httpd_uri_t kRoutes[] = {
        {"/", HTTP_GET, handle_wifi_page, nullptr},
        {"/wifi", HTTP_GET, handle_wifi_page, nullptr},
        {"/api/info", HTTP_GET, handle_info, nullptr},
        {"/api/wifi/modele", HTTP_GET, handle_model, nullptr},
        {"/api/wifi/scan", HTTP_GET, handle_scan, nullptr},
        {"/api/wifi", HTTP_POST, handle_save, nullptr},
        {"/api/wifi/oublier", HTTP_POST, handle_forget, nullptr},
        {"/api/notification", HTTP_POST, handle_notification, nullptr},
        // OS connectivity probes: the portal window opens by itself.
        {"/generate_204", HTTP_GET, redirect_to_portal, nullptr},
        {"/gen_204", HTTP_GET, redirect_to_portal, nullptr},
        {"/hotspot-detect.html", HTTP_GET, redirect_to_portal, nullptr},
        {"/library/test/success.html", HTTP_GET, redirect_to_portal, nullptr},
        {"/ncsi.txt", HTTP_GET, redirect_to_portal, nullptr},
        {"/connecttest.txt", HTTP_GET, redirect_to_portal, nullptr},
        {"/canonical.html", HTTP_GET, redirect_to_portal, nullptr},
    };
    for (const httpd_uri_t& r : kRoutes) {
        if (httpd_register_uri_handler(s_server, &r) != ESP_OK) {
            return false;
        }
    }
    return httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, handle_not_found) == ESP_OK;
}

}  // namespace

WebStatus start(NotificationHandler on_notification) noexcept {
    s_on_notification = on_notification;
    if (!init_nvs()) {
        ESP_LOGE(kTag, "NVS unavailable: no network");
        return WebStatus::NvsError;
    }
    if (!detail::start_network()) {
        ESP_LOGE(kTag, "network failed: web page unavailable (the clock keeps running)");
        return WebStatus::NetError;
    }
    if (!start_http()) {
        ESP_LOGE(kTag, "HTTP server failed (the clock keeps running)");
        return WebStatus::HttpError;
    }
    ESP_LOGI(kTag, "HTTP server ready (wifi page %u bytes)", static_cast<unsigned>(embedded_size(kWifiStart, kWifiEnd)));
    return WebStatus::Ok;
}

namespace detail {
NotificationHandler notification_handler() noexcept {
    return s_on_notification;
}
}  // namespace detail

}  // namespace clock_web
