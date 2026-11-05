#pragma once
/**
 * @file web.hpp
 * @brief Network (Wi-Fi station + access point / captive portal) and HTTP server.
 *
 * The **decisions** (attempts, grace, lease, fallback, portal) live in the pure
 * `network::WifiSupervisor`, tested on the PC; this component only applies its
 * plan: Wi-Fi mode, connection, captive DNS, credentials in NVS. The HTTP
 * server serves the embedded pages, the settings API (models and values from
 * the pure `settings` component, persistence in `clock_settings`), the OS
 * portal probes, the OTA upload, and the **inbound notification channel**
 * (`POST /api/notification`) that the hub, Node-RED or a script can use.
 *
 * @note A network failure never stops the display: start() logs and returns a
 *       status, no abort().
 */

#include <cstddef>
#include <cstdint>

#include "alerts/notification.hpp"

namespace clock_web {

/// Result of starting the network and the server.
enum class WebStatus : uint8_t {
    Ok = 0U,     ///< Network and HTTP server started.
    NvsError,    ///< NVS unavailable (required by Wi-Fi).
    NetError,    ///< Network stack or Wi-Fi failed.
    HttpError,   ///< HTTP server not started.
};

/// Receives a decoded notification; returns false if it could not be queued.
using NotificationHandler = bool (*)(const alerts::Notification&);

/// What the application lends to the server (all optional but the first).
struct Hooks {
    NotificationHandler on_notification;
    /// Writes the dashboard JSON (`/api/etat`); returns 0 if the buffer is too small.
    std::size_t (*status_json)(char* out, std::size_t size);
    /// Shows a commissioning pattern by name (`aucun`, `pixel`, `colonne`, `diagonale`, `plein`, `chenillard`).
    bool (*test_pattern)(const char* name);
    /// Live photoresistor reading for the brightness wizard.
    bool (*ldr_raw)(uint16_t& raw, bool& valid, int& millivolts);
    /// OTA progress for the panel (0..100, active false when done or failed).
    void (*ota_progress)(uint8_t percent, bool active);
    /// Raw AHT21 temperature (before the offset) for the self-heating wizard; false if invalid.
    bool (*air_raw_temperature)(int16_t& centi);
    /// Page button of /mqtt: publish the discovery again.
    void (*mqtt_discovery)();
};

/// Snapshot of the network state for the display and `/api/info`.
struct NetworkInfo {
    bool portal;            ///< Captive portal active (access point up).
    bool connected;         ///< Station has an IP.
    const char* state;      ///< Supervisor state name.
    char ap_ssid[33];       ///< Access point SSID.
    char ap_password[11];   ///< Access point password (shown on the panel in portal mode).
    char ip[16];            ///< Station IPv4 as text, empty when not connected.
};

/**
 * @brief Starts NVS, the settings store, the network and the HTTP server.
 * @return WebStatus::Ok, or the first failed step (logged).
 * @note Non-blocking: Wi-Fi, DNS and HTTP run in their own tasks.
 */
WebStatus start(const Hooks& hooks) noexcept;

/// @return A copy of the current network state (safe from any task).
NetworkInfo info() noexcept;

namespace detail {
bool start_network() noexcept;
bool portal_active() noexcept;
bool save_credentials(const char* ssid, const char* pass) noexcept;
bool forget_credentials() noexcept;
std::size_t scan_json(char* out, std::size_t out_size) noexcept;
}  // namespace detail

}  // namespace clock_web
