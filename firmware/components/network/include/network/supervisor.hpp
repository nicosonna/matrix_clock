#pragma once
/**
 * @file supervisor.hpp
 * @brief Wi-Fi supervisor: attempts, portal fallback, retries — and the
 *        power-cut extension of spec-reseau-portail.md, section 2.
 *
 * Behaviour inherited from the charger and ThermoSync:
 *  - **no credentials** => access point + captive portal;
 *  - credentials => `attempts` attempts of `attempt_ms`, access point off;
 *  - every attempt failed => **fallback**: access point + portal, station
 *    **idle** (a station retrying in the background hops the radio channel
 *    and drops the access point's clients — seen on the charger);
 *  - in fallback, a **retry** of `retry_window_ms` every `retry_period_ms`,
 *    access point kept;
 *  - link lost once connected => a new series of attempts.
 *
 * Added here:
 *  - **boot grace**: for `boot_grace_ms` after power-up, failures retry with a
 *    growing delay and **never** raise the portal — the router boots slower
 *    than the board. Two authentication failures end the grace early (a wrong
 *    passphrase does not fix itself by waiting).
 *  - **association without a DHCP lease** never leads to the portal by itself:
 *    being associated proves the credentials, so the portal would be useless.
 *    The station waits and retries (`lease_retry_ms`, growing); after
 *    `lease_desperate_ms` the access point and portal are raised **as well**,
 *    because something is wrong on the router side and the page must be
 *    reachable.
 *
 * Pure logic: it returns a **plan** (access point, portal, station) that the
 * ESP-IDF layer applies. No ESP-IDF dependency, host-testable.
 */

#include <cstdint>

namespace network {

/// Supervisor state.
enum class WifiState : uint8_t {
    NoCredentials = 0U,  ///< No network configured: portal.
    BootGrace,           ///< Attempts with growing delay, no portal.
    Connecting,          ///< Series of attempts, access point off.
    WaitingLease,        ///< Associated, no IP: retrying DHCP, no portal.
    LeaseDesperate,      ///< Still no IP after a long time: portal raised too.
    Connected,           ///< Connected to the router with an IP.
    Fallback,            ///< Fallback: portal, station idle.
    Retrying,            ///< Fallback with a retry in progress (portal kept).
};

/// What the ESP-IDF layer must apply.
struct WifiPlan {
    bool ap_on;            ///< Access point broadcast.
    bool portal_on;        ///< Captive DNS and redirection to the Wi-Fi page.
    bool sta_connect;      ///< Station connecting / connected.
    uint32_t attempt_id;   ///< Changes on each new attempt: re-issue `connect`.
    bool request_reboot;   ///< true once: the recovery reboot is due.
};

/// What the ESP-IDF layer observed since the last step.
struct LinkStatus {
    bool associated;  ///< Associated to the access point (STA_CONNECTED).
    bool has_ip;      ///< DHCP lease obtained (STA_GOT_IP).
};

/// Cause of the last disconnection reported by the driver.
enum class FailCause : uint8_t {
    None = 0U,
    ApNotFound,     ///< NO_AP_FOUND: the network is not there.
    AuthFailed,     ///< AUTH_FAIL, HANDSHAKE_TIMEOUT: wrong passphrase.
    Other,
};

/// Supervisor settings (spec-reseau-portail.md, section 2).
struct WifiSupervisorConfig {
    uint8_t attempts;              ///< Attempts before fallback (charger: 3).
    uint32_t attempt_ms;           ///< Duration of one attempt (charger: 10 s).
    uint32_t retry_period_ms;      ///< Between two retries in fallback.
    uint32_t retry_window_ms;      ///< Duration of a retry in fallback.
    uint32_t boot_grace_ms;        ///< Grace after power-up; 0 = none (ThermoSync behaviour).
    uint32_t grace_backoff_ms;     ///< First delay between grace attempts, doubled each time.
    uint32_t grace_backoff_max_ms; ///< Cap of the grace delay.
    uint32_t lease_ms;             ///< Wait for a DHCP lease after association.
    uint32_t lease_retry_ms;       ///< First retry delay while waiting for a lease, doubled.
    uint32_t lease_retry_max_ms;   ///< Cap of the lease retry delay.
    uint32_t lease_desperate_ms;   ///< Associated without IP for this long: portal raised too.
    uint32_t reboot_without_ip_ms; ///< No IP for this long: request a reboot once; 0 = never.
};

/// @return 3 x 10 s, retry 30 s / 15 min, grace 5 min (10 s -> 60 s), lease 20 s,
///         lease retry 60 s -> 5 min, desperate at 30 min, reboot after 24 h.
WifiSupervisorConfig default_wifi_supervisor_config() noexcept;

class WifiSupervisor {
public:
    /**
     * @param cfg             Settings; zero values are replaced by the defaults.
     * @param has_credentials Is a network configured?
     * @param now_ms          Start time.
     */
    WifiSupervisor(const WifiSupervisorConfig& cfg, bool has_credentials, uint32_t now_ms) noexcept;

    /**
     * @brief Advances the supervision.
     * @param now_ms Current time (ms, 32-bit wrap tolerated).
     * @param link   What the driver reports right now.
     * @param cause  Disconnection cause seen since the last step (None if nothing).
     * @return Plan to apply.
     */
    WifiPlan step(uint32_t now_ms, LinkStatus link, FailCause cause) noexcept;

    /// Compatibility with the inherited behaviour: `link_up` = has an IP.
    WifiPlan step(uint32_t now_ms, bool link_up) noexcept {
        return step(now_ms, LinkStatus{link_up, link_up}, FailCause::None);
    }

    WifiState state() const noexcept { return state_; }
    const WifiSupervisorConfig& config() const noexcept { return cfg_; }

private:
    void new_attempt(uint32_t now_ms) noexcept;
    void start_attempts(uint32_t now_ms) noexcept;
    void enter_fallback(uint32_t now_ms) noexcept;
    void enter_waiting_lease(uint32_t now_ms) noexcept;
    bool in_grace(uint32_t now_ms) const noexcept;
    WifiPlan plan() const noexcept;

    WifiSupervisorConfig cfg_;
    WifiState state_;
    uint8_t attempt_;
    uint32_t since_ms_;
    uint32_t attempt_id_;
    uint32_t boot_ms_;
    bool has_credentials_;
    bool associated_once_;   ///< Credentials proven good at least once.
    uint8_t auth_failures_;
    uint32_t backoff_ms_;    ///< Current grace delay.
    bool pausing_;           ///< In grace: waiting between two attempts (station idle).
    uint32_t lease_wait_ms_; ///< Current lease retry delay.
    uint32_t lease_since_ms_;///< Start of the association-without-IP episode.
    uint32_t no_ip_since_ms_;
    bool reboot_requested_;
    bool reboot_pending_;
};

}  // namespace network
