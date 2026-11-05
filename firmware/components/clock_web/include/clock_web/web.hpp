#pragma once
/**
 * @file web.hpp
 * @brief Network (Wi-Fi station + access point / captive portal) and HTTP server.
 *
 * The **decisions** (attempts, grace, lease, fallback, portal) live in the pure
 * `network::WifiSupervisor`, tested on the PC; this component only applies its
 * plan: Wi-Fi mode, connection, captive DNS, credentials in NVS. The HTTP
 * server serves the embedded Wi-Fi page, the portal detection probes, and the
 * **inbound notification channel** (`POST /api/notification`) that the hub,
 * Node-RED or a script can use.
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
 * @brief Starts NVS, the network and the HTTP server.
 * @param on_notification Called for every valid `POST /api/notification`.
 * @return WebStatus::Ok, or the first failed step (logged).
 * @note Non-blocking: Wi-Fi, DNS and HTTP run in their own tasks.
 */
WebStatus start(NotificationHandler on_notification) noexcept;

/// @return A copy of the current network state (safe from any task).
NetworkInfo info() noexcept;

namespace detail {
bool start_network() noexcept;
bool portal_active() noexcept;
bool save_credentials(const char* ssid, const char* pass) noexcept;
bool forget_credentials() noexcept;
std::size_t scan_json(char* out, std::size_t out_size) noexcept;
NotificationHandler notification_handler() noexcept;
}  // namespace detail

}  // namespace clock_web
