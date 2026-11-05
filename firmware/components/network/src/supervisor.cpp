/**
 * @file supervisor.cpp
 * @brief Wi-Fi supervisor (see supervisor.hpp).
 */
#include "network/supervisor.hpp"

namespace network {

namespace {
/// Authentication failures that end the boot grace: the passphrase is wrong.
constexpr uint8_t kAuthFailuresToFallback = 2U;
}  // namespace

WifiSupervisorConfig default_wifi_supervisor_config() noexcept {
    return WifiSupervisorConfig{
        /*attempts              */ 3U,
        /*attempt_ms            */ 10000U,
        /*retry_period_ms       */ 15U * 60U * 1000U,
        /*retry_window_ms       */ 30000U,
        /*boot_grace_ms         */ 5U * 60U * 1000U,
        /*grace_backoff_ms      */ 10000U,
        /*grace_backoff_max_ms  */ 60000U,
        /*lease_ms              */ 20000U,
        /*lease_retry_ms        */ 60000U,
        /*lease_retry_max_ms    */ 5U * 60U * 1000U,
        /*lease_desperate_ms    */ 30U * 60U * 1000U,
        /*reboot_without_ip_ms  */ 24U * 60U * 60U * 1000U,
    };
}

WifiSupervisor::WifiSupervisor(const WifiSupervisorConfig& cfg, bool has_credentials,
                               uint32_t now_ms) noexcept
    : cfg_(cfg), state_(WifiState::NoCredentials), attempt_(0U), since_ms_(now_ms), attempt_id_(0U),
      boot_ms_(now_ms), has_credentials_(has_credentials), associated_once_(false), auth_failures_(0U),
      backoff_ms_(cfg.grace_backoff_ms), pausing_(false), lease_wait_ms_(cfg.lease_retry_ms),
      lease_since_ms_(now_ms), no_ip_since_ms_(now_ms), reboot_requested_(false), reboot_pending_(false) {
    const WifiSupervisorConfig d = default_wifi_supervisor_config();
    if (cfg_.attempts == 0U) {
        cfg_.attempts = d.attempts;
    }
    if (cfg_.attempt_ms == 0U) {
        cfg_.attempt_ms = d.attempt_ms;
    }
    if (cfg_.retry_period_ms == 0U) {
        cfg_.retry_period_ms = d.retry_period_ms;
    }
    if (cfg_.retry_window_ms == 0U) {
        cfg_.retry_window_ms = d.retry_window_ms;
    }
    // boot_grace_ms = 0 is a legitimate choice (no grace), as is reboot = 0.
    if (cfg_.grace_backoff_ms == 0U) {
        cfg_.grace_backoff_ms = d.grace_backoff_ms;
    }
    if (cfg_.grace_backoff_max_ms == 0U) {
        cfg_.grace_backoff_max_ms = d.grace_backoff_max_ms;
    }
    if (cfg_.lease_ms == 0U) {
        cfg_.lease_ms = d.lease_ms;
    }
    if (cfg_.lease_retry_ms == 0U) {
        cfg_.lease_retry_ms = d.lease_retry_ms;
    }
    if (cfg_.lease_retry_max_ms == 0U) {
        cfg_.lease_retry_max_ms = d.lease_retry_max_ms;
    }
    if (cfg_.lease_desperate_ms == 0U) {
        cfg_.lease_desperate_ms = d.lease_desperate_ms;
    }
    backoff_ms_ = cfg_.grace_backoff_ms;
    lease_wait_ms_ = cfg_.lease_retry_ms;
    if (has_credentials) {
        if (cfg_.boot_grace_ms != 0U) {
            state_ = WifiState::BootGrace;
            new_attempt(now_ms);
        } else {
            start_attempts(now_ms);
        }
    }
}

bool WifiSupervisor::in_grace(uint32_t now_ms) const noexcept {
    return (now_ms - boot_ms_) < cfg_.boot_grace_ms;
}

void WifiSupervisor::new_attempt(uint32_t now_ms) noexcept {
    since_ms_ = now_ms;
    ++attempt_id_;
    pausing_ = false;
}

void WifiSupervisor::start_attempts(uint32_t now_ms) noexcept {
    state_ = WifiState::Connecting;
    attempt_ = 1U;
    new_attempt(now_ms);
}

void WifiSupervisor::enter_fallback(uint32_t now_ms) noexcept {
    state_ = WifiState::Fallback;
    since_ms_ = now_ms;
    pausing_ = false;
}

void WifiSupervisor::enter_waiting_lease(uint32_t now_ms) noexcept {
    state_ = WifiState::WaitingLease;
    since_ms_ = now_ms;
    lease_since_ms_ = now_ms;
    lease_wait_ms_ = cfg_.lease_retry_ms;
    pausing_ = false;
}

WifiPlan WifiSupervisor::plan() const noexcept {
    WifiPlan p{false, false, false, attempt_id_, reboot_pending_};
    switch (state_) {
        case WifiState::BootGrace:
            p.sta_connect = !pausing_;
            break;
        case WifiState::Connecting:
        case WifiState::Connected:
        case WifiState::WaitingLease:
            p.sta_connect = true;
            break;
        case WifiState::LeaseDesperate:
            p.ap_on = true;
            p.portal_on = true;
            p.sta_connect = true;
            break;
        case WifiState::Retrying:
            p.ap_on = true;
            p.portal_on = true;
            p.sta_connect = true;
            break;
        case WifiState::Fallback:
        case WifiState::NoCredentials:
        default:
            p.ap_on = true;
            p.portal_on = true;
            break;
    }
    return p;
}

WifiPlan WifiSupervisor::step(uint32_t now_ms, LinkStatus link, FailCause cause) noexcept {
    const uint32_t elapsed = now_ms - since_ms_;  // unsigned: right after a wrap too
    reboot_pending_ = false;

    if (link.associated) {
        associated_once_ = true;
        auth_failures_ = 0U;
    }
    if (cause == FailCause::AuthFailed) {
        if (auth_failures_ < 255U) {
            ++auth_failures_;
        }
    }
    const bool auth_hopeless = !associated_once_ && (auth_failures_ >= kAuthFailuresToFallback);

    // Recovery reboot: too long without an IP while credentials exist.
    if (link.has_ip || !has_credentials_) {
        no_ip_since_ms_ = now_ms;
        reboot_requested_ = false;
    } else if ((cfg_.reboot_without_ip_ms != 0U) && !reboot_requested_ &&
               ((now_ms - no_ip_since_ms_) >= cfg_.reboot_without_ip_ms)) {
        reboot_requested_ = true;
        reboot_pending_ = true;
        no_ip_since_ms_ = now_ms;  // once per period
    }

    switch (state_) {
        case WifiState::BootGrace:
            if (link.has_ip) {
                state_ = WifiState::Connected;
            } else if (auth_hopeless) {
                enter_fallback(now_ms);  // waiting will not fix a wrong passphrase
            } else if (link.associated && (elapsed >= cfg_.lease_ms)) {
                enter_waiting_lease(now_ms);
            } else if (!in_grace(now_ms)) {
                start_attempts(now_ms);  // grace over: the inherited behaviour
            } else if (pausing_) {
                if (elapsed >= backoff_ms_) {
                    backoff_ms_ = (backoff_ms_ * 2U > cfg_.grace_backoff_max_ms) ? cfg_.grace_backoff_max_ms
                                                                                  : backoff_ms_ * 2U;
                    new_attempt(now_ms);
                }
            } else if ((elapsed >= cfg_.attempt_ms) && !link.associated) {
                pausing_ = true;  // station idle for the current backoff
                since_ms_ = now_ms;
            } else {
                // attempt in progress, or associated and waiting for the lease
            }
            break;

        case WifiState::Connecting:
            if (link.has_ip) {
                state_ = WifiState::Connected;
            } else if (auth_hopeless) {
                enter_fallback(now_ms);
            } else if (link.associated && (elapsed >= cfg_.lease_ms)) {
                enter_waiting_lease(now_ms);  // credentials proven: never the portal for this
            } else if ((elapsed >= cfg_.attempt_ms) && !link.associated) {
                if (attempt_ >= cfg_.attempts) {
                    enter_fallback(now_ms);  // fallback: portal, station idle
                } else {
                    ++attempt_;
                    new_attempt(now_ms);
                }
            } else {
                // attempt in progress
            }
            break;

        case WifiState::WaitingLease:
        case WifiState::LeaseDesperate:
            if (link.has_ip) {
                state_ = WifiState::Connected;
            } else if (!link.associated && (elapsed >= cfg_.attempt_ms)) {
                // Association lost for longer than a reconnection takes: a new series.
                // (Right after new_attempt() the station is briefly not associated.)
                start_attempts(now_ms);
            } else {
                if (elapsed >= lease_wait_ms_) {
                    // Re-issue the connection so DHCP starts again.
                    lease_wait_ms_ = (lease_wait_ms_ * 2U > cfg_.lease_retry_max_ms) ? cfg_.lease_retry_max_ms
                                                                                     : lease_wait_ms_ * 2U;
                    new_attempt(now_ms);
                }
                if ((state_ == WifiState::WaitingLease) &&
                    ((now_ms - lease_since_ms_) >= cfg_.lease_desperate_ms)) {
                    state_ = WifiState::LeaseDesperate;  // page reachable again, retries continue
                }
            }
            break;

        case WifiState::Connected:
            if (!link.has_ip) {
                start_attempts(now_ms);  // link lost: a new series (no grace: the router was up a minute ago)
            }
            break;

        case WifiState::Fallback:
            if (link.has_ip) {
                state_ = WifiState::Connected;  // spontaneous return (should not happen)
            } else if (elapsed >= cfg_.retry_period_ms) {
                state_ = WifiState::Retrying;
                new_attempt(now_ms);
            } else {
                // waiting for the next retry
            }
            break;

        case WifiState::Retrying:
            if (link.has_ip) {
                state_ = WifiState::Connected;
            } else if (elapsed >= cfg_.retry_window_ms) {
                if (link.associated) {
                    enter_waiting_lease(now_ms);  // the network is back, only DHCP is missing
                } else {
                    enter_fallback(now_ms);
                }
            } else {
                // retry in progress
            }
            break;

        case WifiState::NoCredentials:
        default:
            break;
    }
    return plan();
}

}  // namespace network
