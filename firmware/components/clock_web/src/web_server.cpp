/**
 * @file web_server.cpp
 * @brief HTTP server: embedded pages, settings API, captive portal probes,
 *        `/api/info` and `/api/etat`, inbound notification channel, OTA.
 */
#include "clock_web/web.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "alerts/notification.hpp"
#include "brightness/brightness.hpp"
#include "clock_board/board.hpp"
#include "clock_settings/store.hpp"
#include "network/credentials.hpp"
#include "settings/forms.hpp"
#include "settings/json.hpp"

// Embedded pages (EMBED_TXTFILES adds a trailing NUL).
extern const char kWifiStart[] asm("_binary_wifi_html_start");
extern const char kWifiEnd[] asm("_binary_wifi_html_end");
extern const char kAppStart[] asm("_binary_app_html_start");
extern const char kAppEnd[] asm("_binary_app_html_end");

namespace clock_web {

namespace {

constexpr char kTag[] = "web";
constexpr char kPortalUrl[] = "http://192.168.4.1/wifi";
constexpr size_t kFormMax = 256U;               ///< Wi-Fi form body (bytes).
constexpr size_t kSettingsFormMax = 1024U;      ///< Settings forms (scenes and curve are text).
constexpr size_t kNotificationFormMax = 1024U;  ///< Notification body.
constexpr size_t kScanJsonMax = 2048U;
constexpr size_t kHttpClientsMax = 4U;
constexpr size_t kOtaChunk = 4096U;
constexpr uint32_t kRebootDelayMs = 500U;

httpd_handle_t s_server = nullptr;
Hooks s_hooks{};

size_t embedded_size(const char* start, const char* end) {
    return static_cast<size_t>(end - start) - 1U;  // without the trailing NUL
}

esp_err_t send_text(httpd_req_t* req, const char* status, const char* text) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, text);
}

esp_err_t send_json(httpd_req_t* req, const char* json, size_t len) {
    if (len == 0U) {
        return send_text(req, "500 Internal Server Error", "{\"ok\":false,\"incomplet\":true}");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, static_cast<ssize_t>(len));
}

/// `{"ok":false,"champ":"...","raison":"..."}` as a 400.
esp_err_t send_form_error(httpd_req_t* req, const settings::FormError& err) {
    char body[320];
    char field[64];
    char reason[160];
    (void)network::json_escape(err.field, std::strlen(err.field), field, sizeof(field));
    (void)network::json_escape(err.reason, std::strlen(err.reason), reason, sizeof(reason));
    (void)std::snprintf(body, sizeof(body), "{\"ok\":false,\"champ\":\"%s\",\"raison\":\"%s\"}", field, reason);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
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

// ------------------------------------------------------------------- pages

esp_err_t handle_wifi_page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kWifiStart, static_cast<ssize_t>(embedded_size(kWifiStart, kWifiEnd)));
}

/// GET /, /affichage, /luminosite, /systeme: the single-page app, routed by path client-side.
esp_err_t handle_app_page(httpd_req_t* req) {
    if (detail::portal_active() && (std::strcmp(req->uri, "/") == 0)) {
        return handle_wifi_page(req);  // in portal mode the root is the Wi-Fi page
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, kAppStart, static_cast<ssize_t>(embedded_size(kAppStart, kAppEnd)));
}

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

// ------------------------------------------------------------------- Wi-Fi API

esp_err_t handle_wifi_model(httpd_req_t* req) {
    char body[96];
    return send_json(req, body, network::model_json(body, sizeof(body)));
}

esp_err_t handle_scan(httpd_req_t* req) {
    static char body[kScanJsonMax];
    return send_json(req, body, detail::scan_json(body, sizeof(body)));
}

esp_err_t handle_wifi_save(httpd_req_t* req) {
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
        pass[0] = '\0';
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
                     "Enregistré. L'horloge se connecte à votre réseau ; le point d'accès s'éteindra une fois connectée.");
}

esp_err_t handle_wifi_forget(httpd_req_t* req) {
    if (!detail::forget_credentials()) {
        return send_text(req, "500 Internal Server Error", "Effacement impossible.");
    }
    return send_text(req, "200 OK", "Réseau oublié. Le point d'accès reste disponible.");
}

// ------------------------------------------------------------ settings API

esp_err_t handle_display_model(httpd_req_t* req) {
    static char body[settings::kJsonMax];
    return send_json(req, body, settings::display_model_json(body, sizeof(body)));
}

esp_err_t handle_display_get(httpd_req_t* req) {
    static char body[settings::kJsonMax];
    return send_json(req, body, settings::display_json(clock_settings::get().display, body, sizeof(body)));
}

esp_err_t handle_display_post(httpd_req_t* req) {
    static char body[kSettingsFormMax + 1U];
    size_t got = 0U;
    if (!read_body(req, body, kSettingsFormMax, got)) {
        return send_text(req, "400 Bad Request", "Formulaire vide, incomplet ou trop long.");
    }
    const settings::DisplaySettings current = clock_settings::get().display;
    settings::DisplaySettings next = current;
    settings::FormError err{};
    if (!settings::apply_display_form(body, got, current, next, err)) {
        return send_form_error(req, err);
    }
    if (!clock_settings::save_display(next)) {
        return send_text(req, "500 Internal Server Error", "{\"ok\":false,\"raison\":\"enregistrement impossible\"}");
    }
    return send_text(req, "200 OK", "{\"ok\":true}");
}

esp_err_t handle_display_test(httpd_req_t* req) {
    char body[kFormMax + 1U];
    size_t got = 0U;
    if (!read_body(req, body, kFormMax, got)) {
        return send_text(req, "400 Bad Request", "motif=... attendu.");
    }
    char name[16];
    size_t n = 0U;
    if (!network::form_field(body, got, "motif", name, sizeof(name), n)) {
        return send_text(req, "400 Bad Request", "motif=... attendu.");
    }
    if ((s_hooks.test_pattern == nullptr) || !s_hooks.test_pattern(name)) {
        return send_text(req, "400 Bad Request", "Motif inconnu : aucun, pixel, colonne, diagonale, plein, chenillard.");
    }
    return send_text(req, "200 OK", "{\"ok\":true}");
}

esp_err_t handle_brightness_model(httpd_req_t* req) {
    static char body[settings::kJsonMax];
    return send_json(req, body, settings::brightness_model_json(body, sizeof(body)));
}

esp_err_t handle_brightness_get(httpd_req_t* req) {
    static char body[settings::kJsonMax];
    return send_json(req, body, settings::brightness_json(clock_settings::get().brightness, body, sizeof(body)));
}

esp_err_t handle_brightness_post(httpd_req_t* req) {
    static char body[kSettingsFormMax + 1U];
    size_t got = 0U;
    if (!read_body(req, body, kSettingsFormMax, got)) {
        return send_text(req, "400 Bad Request", "Formulaire vide, incomplet ou trop long.");
    }
    const brightness::Config current = clock_settings::get().brightness;
    brightness::Config next = current;
    settings::FormError err{};
    if (!settings::apply_brightness_form(body, got, current, next, err)) {
        return send_form_error(req, err);
    }
    if (!clock_settings::save_brightness(next)) {
        return send_text(req, "500 Internal Server Error", "{\"ok\":false,\"raison\":\"enregistrement impossible\"}");
    }
    return send_text(req, "200 OK", "{\"ok\":true}");
}

/// GET /api/luminosite/brut: live reading for the wizard.
esp_err_t handle_brightness_raw(httpd_req_t* req) {
    uint16_t raw = 0U;
    bool valid = false;
    int mv = 0;
    if ((s_hooks.ldr_raw == nullptr) || !s_hooks.ldr_raw(raw, valid, mv)) {
        return send_text(req, "503 Service Unavailable", "{\"ok\":false}");
    }
    char body[96];
    const int n = std::snprintf(body, sizeof(body), "{\"brut\":%u,\"valide\":%s,\"mv\":%d}", static_cast<unsigned>(raw),
                                valid ? "true" : "false", mv);
    return send_json(req, body, (n > 0) ? static_cast<size_t>(n) : 0U);
}

/// POST /api/luminosite/wizard: brut_nuit, brut_nuit_allume, brut_soir, brut_jour -> curve + compensation.
esp_err_t handle_brightness_wizard(httpd_req_t* req) {
    char body[kFormMax + 1U];
    size_t got = 0U;
    if (!read_body(req, body, kFormMax, got)) {
        return send_text(req, "400 Bad Request", "Quatre mesures attendues.");
    }
    const char* const names[] = {"brut_nuit", "brut_nuit_allume", "brut_soir", "brut_jour"};
    uint16_t values[4] = {};
    for (size_t i = 0U; i < 4U; ++i) {
        char v[8];
        size_t n = 0U;
        if (!network::form_field(body, got, names[i], v, sizeof(v), n) || (n == 0U)) {
            return send_text(req, "400 Bad Request", "Quatre mesures attendues (brut_nuit, brut_nuit_allume, brut_soir, brut_jour).");
        }
        unsigned parsed = 0U;
        if ((std::sscanf(v, "%u", &parsed) != 1) || (parsed > brightness::kRawMax)) {
            return send_text(req, "400 Bad Request", "Mesure hors de 0..4095.");
        }
        values[i] = static_cast<uint16_t>(parsed);
    }
    brightness::Config cfg = clock_settings::get().brightness;
    if (!brightness::build_from_wizard(values[0], values[1], values[2], values[3], cfg)) {
        return send_text(req, "400 Bad Request", "Deux mesures identiques : la courbe ne serait pas monotone. Recommencer dans d'autres conditions.");
    }
    if (!clock_settings::save_brightness(cfg)) {
        return send_text(req, "500 Internal Server Error", "Enregistrement impossible.");
    }
    char reply[160];
    const bool warn = (cfg.compensation_counts > brightness::kCompensationWarnCounts) ||
                      (cfg.compensation_counts < -brightness::kCompensationWarnCounts);
    (void)std::snprintf(reply, sizeof(reply), "{\"ok\":true,\"compensation_counts\":%d,\"deporter\":%s}",
                        static_cast<int>(cfg.compensation_counts), warn ? "true" : "false");
    return send_text(req, "200 OK", reply);
}

esp_err_t handle_system_model(httpd_req_t* req) {
    static char body[settings::kJsonMax];
    return send_json(req, body, settings::system_model_json(body, sizeof(body)));
}

esp_err_t handle_system_get(httpd_req_t* req) {
    static char body[settings::kJsonMax];
    return send_json(req, body, settings::system_json(clock_settings::get().system, body, sizeof(body)));
}

esp_err_t handle_system_post(httpd_req_t* req) {
    char body[kFormMax + 1U];
    size_t got = 0U;
    if (!read_body(req, body, kFormMax, got)) {
        return send_text(req, "400 Bad Request", "Formulaire vide, incomplet ou trop long.");
    }
    const settings::SystemSettings current = clock_settings::get().system;
    settings::SystemSettings next = current;
    settings::FormError err{};
    if (!settings::apply_system_form(body, got, current, next, err)) {
        return send_form_error(req, err);
    }
    if (!clock_settings::save_system(next)) {
        return send_text(req, "500 Internal Server Error", "{\"ok\":false,\"raison\":\"enregistrement impossible\"}");
    }
    return send_text(req, "200 OK", "{\"ok\":true}");
}

void reboot_task(void* /*arg*/) {
    vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));  // let the response leave
    esp_restart();
}

void schedule_reboot() {
    (void)xTaskCreate(reboot_task, "reboot", 2048, nullptr, 5, nullptr);
}

esp_err_t handle_reboot(httpd_req_t* req) {
    const esp_err_t r = send_text(req, "200 OK", "Redémarrage.");
    schedule_reboot();
    return r;
}

esp_err_t handle_factory(httpd_req_t* req) {
    char body[kFormMax + 1U];
    size_t got = 0U;
    char v[8];
    size_t n = 0U;
    if (!read_body(req, body, kFormMax, got) || !network::form_field(body, got, "confirme", v, sizeof(v), n) ||
        (std::strcmp(v, "OUI") != 0)) {
        return send_text(req, "400 Bad Request", "Confirmation attendue : confirme=OUI.");
    }
    if (!clock_settings::factory_reset()) {
        return send_text(req, "500 Internal Server Error", "Effacement impossible.");
    }
    const esp_err_t r = send_text(req, "200 OK", "Réglages d'usine : l'horloge redémarre en portail captif.");
    schedule_reboot();
    return r;
}

/// POST /api/ota: raw image in the body, written to the inactive slot, then reboot (rollback armed).
esp_err_t handle_ota(httpd_req_t* req) {
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        return send_text(req, "500 Internal Server Error", "Aucun emplacement OTA.");
    }
    if ((req->content_len == 0U) || (req->content_len > target->size)) {
        return send_text(req, "400 Bad Request", "Image vide ou plus grande que l'emplacement.");
    }
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, req->content_len, &handle);
    if (err != ESP_OK) {
        return send_text(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    static char chunk[kOtaChunk];
    size_t received = 0U;
    uint8_t last_percent = 255U;
    if (s_hooks.ota_progress != nullptr) {
        s_hooks.ota_progress(0U, true);
    }
    while (received < req->content_len) {
        const size_t want = ((req->content_len - received) < kOtaChunk) ? (req->content_len - received) : kOtaChunk;
        const int r = httpd_req_recv(req, chunk, want);
        if (r <= 0) {
            (void)esp_ota_abort(handle);
            if (s_hooks.ota_progress != nullptr) {
                s_hooks.ota_progress(0U, false);
            }
            return send_text(req, "400 Bad Request", "Transfert interrompu.");
        }
        err = esp_ota_write(handle, chunk, static_cast<size_t>(r));
        if (err != ESP_OK) {
            (void)esp_ota_abort(handle);
            if (s_hooks.ota_progress != nullptr) {
                s_hooks.ota_progress(0U, false);
            }
            return send_text(req, "400 Bad Request", esp_err_to_name(err));
        }
        received += static_cast<size_t>(r);
        const uint8_t percent = static_cast<uint8_t>((received * 100U) / req->content_len);
        if ((percent != last_percent) && (s_hooks.ota_progress != nullptr)) {
            last_percent = percent;
            s_hooks.ota_progress(percent, true);
        }
    }
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        if (s_hooks.ota_progress != nullptr) {
            s_hooks.ota_progress(0U, false);
        }
        return send_text(req, "400 Bad Request", (err == ESP_ERR_OTA_VALIDATE_FAILED) ? "Image invalide." : esp_err_to_name(err));
    }
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        if (s_hooks.ota_progress != nullptr) {
            s_hooks.ota_progress(0U, false);
        }
        return send_text(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    ESP_LOGI(kTag, "OTA: %u bytes written to %s, rebooting", static_cast<unsigned>(received), target->label);
    const esp_err_t r = send_text(req, "200 OK", "Image écrite. Redémarrage ; retour à l'ancienne image si la nouvelle ne confirme pas son démarrage.");
    schedule_reboot();
    return r;
}

// ----------------------------------------------------------- notifications

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
    if ((s_hooks.on_notification == nullptr) || !s_hooks.on_notification(n)) {
        return send_text(req, "503 Service Unavailable", "Notification refusée (file pleine).");
    }
    char reply[96];
    (void)std::snprintf(reply, sizeof(reply), "Notification %s (%u champ(s) remplacé(s) par défaut).",
                        n.clear ? "effacée" : "acceptée", static_cast<unsigned>(n.defaults_used));
    return send_text(req, "200 OK", reply);
}

// ------------------------------------------------------------------ status

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
    return send_json(req, body, ((n > 0) && (static_cast<size_t>(n) < sizeof(body))) ? static_cast<size_t>(n) : 0U);
}

esp_err_t handle_status(httpd_req_t* req) {
    static char body[settings::kJsonMax];
    if (s_hooks.status_json == nullptr) {
        return send_text(req, "503 Service Unavailable", "{\"ok\":false}");
    }
    return send_json(req, body, s_hooks.status_json(body, sizeof(body)));
}

bool init_nvs() {
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        if (nvs_flash_erase() != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

bool start_http() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 40;
    cfg.max_open_sockets = kHttpClientsMax;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;  // settings JSON and OTA chunks are static, but the handlers use some stack
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        return false;
    }
    static const httpd_uri_t kRoutes[] = {
        {"/", HTTP_GET, handle_app_page, nullptr},
        {"/affichage", HTTP_GET, handle_app_page, nullptr},
        {"/luminosite", HTTP_GET, handle_app_page, nullptr},
        {"/systeme", HTTP_GET, handle_app_page, nullptr},
        {"/wifi", HTTP_GET, handle_wifi_page, nullptr},
        {"/api/info", HTTP_GET, handle_info, nullptr},
        {"/api/etat", HTTP_GET, handle_status, nullptr},
        {"/api/wifi/modele", HTTP_GET, handle_wifi_model, nullptr},
        {"/api/wifi/scan", HTTP_GET, handle_scan, nullptr},
        {"/api/wifi", HTTP_POST, handle_wifi_save, nullptr},
        {"/api/wifi/oublier", HTTP_POST, handle_wifi_forget, nullptr},
        {"/api/affichage/modele", HTTP_GET, handle_display_model, nullptr},
        {"/api/affichage", HTTP_GET, handle_display_get, nullptr},
        {"/api/affichage", HTTP_POST, handle_display_post, nullptr},
        {"/api/affichage/test", HTTP_POST, handle_display_test, nullptr},
        {"/api/luminosite/modele", HTTP_GET, handle_brightness_model, nullptr},
        {"/api/luminosite", HTTP_GET, handle_brightness_get, nullptr},
        {"/api/luminosite", HTTP_POST, handle_brightness_post, nullptr},
        {"/api/luminosite/brut", HTTP_GET, handle_brightness_raw, nullptr},
        {"/api/luminosite/wizard", HTTP_POST, handle_brightness_wizard, nullptr},
        {"/api/systeme/modele", HTTP_GET, handle_system_model, nullptr},
        {"/api/systeme", HTTP_GET, handle_system_get, nullptr},
        {"/api/systeme", HTTP_POST, handle_system_post, nullptr},
        {"/api/systeme/redemarrer", HTTP_POST, handle_reboot, nullptr},
        {"/api/systeme/usine", HTTP_POST, handle_factory, nullptr},
        {"/api/ota", HTTP_POST, handle_ota, nullptr},
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

WebStatus start(const Hooks& hooks) noexcept {
    s_hooks = hooks;
    if (!init_nvs()) {
        ESP_LOGE(kTag, "NVS unavailable: no network");
        return WebStatus::NvsError;
    }
    if (clock_settings::init() != ESP_OK) {
        ESP_LOGE(kTag, "settings store failed: defaults in RAM");
    }
    if (!detail::start_network()) {
        ESP_LOGE(kTag, "network failed: web page unavailable (the clock keeps running)");
        return WebStatus::NetError;
    }
    if (!start_http()) {
        ESP_LOGE(kTag, "HTTP server failed (the clock keeps running)");
        return WebStatus::HttpError;
    }
    ESP_LOGI(kTag, "HTTP server ready (app %u bytes, wifi page %u bytes)",
             static_cast<unsigned>(embedded_size(kAppStart, kAppEnd)),
             static_cast<unsigned>(embedded_size(kWifiStart, kWifiEnd)));
    return WebStatus::Ok;
}

}  // namespace clock_web
