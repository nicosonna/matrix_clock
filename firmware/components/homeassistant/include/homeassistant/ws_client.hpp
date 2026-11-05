#pragma once
/**
 * @file ws_client.hpp
 * @brief Home Assistant WebSocket client: pure state machine.
 *
 * The transport (`esp_websocket_client` in `clock_homeassistant`, a fake in the
 * tests) reports connection events and complete messages; the client answers
 * with outgoing messages pulled one at a time through next_outgoing(). It
 * decides when to (re)connect, authenticates, subscribes every tracked value
 * (`render_template`) and the notification event, pings, and detects a dead
 * link (no `pong`). Everything is bounded and allocation-free.
 *
 * Dialogue: auth_required → auth → auth_ok → render_template × n →
 * subscribe_events(horloge_notification) → ping every ping_s.
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include "alerts/notification.hpp"
#include "homeassistant/values.hpp"

namespace homeassistant {

inline constexpr std::size_t kTokenMax = 512U;
inline constexpr uint16_t kPingMinS = 10U;
inline constexpr uint16_t kPingMaxS = 300U;
inline constexpr uint16_t kPingDefaultS = 30U;
inline constexpr uint16_t kPongMinMs = 2000U;
inline constexpr uint16_t kPongMaxMs = 30000U;
inline constexpr uint16_t kPongDefaultMs = 10000U;
inline constexpr uint32_t kBackoffFirstMs = 5000U;
inline constexpr uint32_t kBackoffMaxMs = 60000U;
inline constexpr uint32_t kTokenRejectedBackoffMs = 300000U;
/// Outgoing message buffer (template subscriptions are the longest).
inline constexpr std::size_t kOutgoingMax = 512U;
/// Name of the Home Assistant event carrying notifications.
inline constexpr char kNotificationEvent[] = "horloge_notification";

enum class LinkState : uint8_t {
    Disabled = 0U,   ///< Link off in the settings.
    Waiting,         ///< Backoff before the next attempt.
    Connecting,      ///< Transport asked to connect.
    Authenticating,  ///< Socket open, waiting for auth_required / auth_ok.
    Subscribing,     ///< Sending the subscriptions.
    Running,         ///< All subscriptions acknowledged, pinging.
    TokenRejected,   ///< auth_invalid: 5 min before retrying.
};

struct WsConfig {
    bool enabled;
    const char* token;     ///< Long-lived access token (owned by the settings store).
    bool notifications;    ///< Subscribe to the notification event.
    uint16_t ping_s;
    uint16_t pong_ms;
};

WsConfig default_ws_config() noexcept;
bool is_valid(const WsConfig& c) noexcept;

/// Diagnostics for the page.
struct WsStats {
    uint32_t connections;      ///< Successful authentications.
    uint32_t disconnections;
    uint32_t pong_misses;
    uint32_t messages_ignored; ///< Malformed or unexpected messages.
    uint32_t events;           ///< render_template events applied.
    uint32_t notifications;    ///< Notifications accepted.
    uint32_t notifications_rejected;
};

class WsClient {
public:
    WsClient(const WsConfig& config, const ValueList& values) noexcept;

    /// Applies new settings: the link restarts (subscriptions are replayed).
    void configure(const WsConfig& config, const ValueList& values, uint32_t now_ms) noexcept;

    // ----- transport events
    void on_connected(uint32_t now_ms) noexcept;
    void on_disconnected(uint32_t now_ms) noexcept;
    void on_message(const char* json, std::size_t len, uint32_t now_ms) noexcept;

    // ----- polling
    /// Advances timers (backoff, ping, pong). Call every 100 ms or so.
    void step(uint32_t now_ms) noexcept;
    /// true when the transport should open a connection (once per attempt).
    bool take_connect_request() noexcept;
    /// true when the transport should close the socket (dead link, reconfiguration).
    bool take_close_request() noexcept;
    /// Next message to send, 0 if none. Call until it returns 0.
    std::size_t next_outgoing(char* out, std::size_t size) noexcept;

    // ----- state
    LinkState state() const noexcept { return state_; }
    const char* state_name() const noexcept;
    bool link_up() const noexcept { return state_ == LinkState::Running; }
    /// Validity with expiry applied (HA-12): invalid if the link has been down for longer than expiry_s.
    bool value_valid(std::size_t index, uint32_t now_ms) const noexcept;
    const ValueState& value(std::size_t index) const noexcept { return states_[index]; }
    const ValueConfig& value_config(std::size_t index) const noexcept { return values_[index]; }
    const WsStats& stats() const noexcept { return stats_; }
    /// Last error text (auth, template), "" if none.
    const char* last_error() const noexcept { return error_; }
    uint32_t backoff_ms() const noexcept { return backoff_ms_; }

    /// Pending inbound notification: copied out and cleared. false if none.
    bool take_notification(alerts::Notification& out) noexcept;

private:
    enum class Pending : uint8_t { None = 0U, Auth, Subscribe, Events, Ping };

    void reset_link(uint32_t now_ms) noexcept;
    void schedule_retry(uint32_t now_ms, bool token_rejected) noexcept;
    void handle_event(uint32_t id, const char* json, std::size_t len, uint32_t now_ms) noexcept;
    void handle_result(uint32_t id, const char* json, std::size_t len) noexcept;
    bool decode_notification(const char* json, std::size_t len) noexcept;
    std::size_t enabled_count() const noexcept;

    WsConfig config_;
    ValueList values_;
    std::array<ValueState, kValuesMax> states_;
    std::array<uint32_t, kValuesMax> sub_ids_;
    LinkState state_;
    uint32_t next_id_;
    uint32_t events_id_;
    uint32_t ping_id_;
    std::size_t next_sub_;        ///< Next value index to subscribe.
    bool auth_pending_;           ///< auth_required received, auth to send.
    bool events_pending_;
    bool ping_pending_;           ///< Ping to send.
    bool awaiting_pong_;
    uint32_t ping_sent_ms_;
    uint32_t last_ping_ms_;
    uint32_t retry_at_ms_;
    uint32_t backoff_ms_;
    bool connect_request_;
    bool close_request_;
    uint32_t link_down_since_ms_;
    bool link_ever_up_;
    uint8_t acked_subs_;          ///< Subscriptions acknowledged this session.
    WsStats stats_;
    char error_[kErrorMax + 1U];
    alerts::Notification notification_;
    bool notification_pending_;
};

}  // namespace homeassistant
