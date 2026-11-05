/**
 * @file ws_client.cpp
 * @brief Home Assistant WebSocket client state machine.
 */
#include "homeassistant/ws_client.hpp"

#include <cstdio>
#include <cstring>

#include "homeassistant/json_reader.hpp"

namespace homeassistant {

namespace {

bool elapsed(uint32_t now, uint32_t until) noexcept {
    return static_cast<int32_t>(now - until) >= 0;
}

bool span_equals(const JsonSpan& v, const char* literal) noexcept {
    // Compares a JSON string value (with quotes) to a literal.
    const std::size_t n = std::strlen(literal);
    return json_is_string(v) && (v.len == n + 2U) && (std::memcmp(v.data + 1, literal, n) == 0);
}

/// Appends `key=` + percent-encoded value to a form body. false if it does not fit.
bool form_append(char* body, std::size_t size, std::size_t& w, const char* key, const char* value) noexcept {
    const std::size_t kl = std::strlen(key);
    if ((w + kl + 2U) >= size) {
        return false;
    }
    if (w > 0U) {
        body[w++] = '&';
    }
    std::memcpy(body + w, key, kl);
    w += kl;
    body[w++] = '=';
    static const char kHex[] = "0123456789ABCDEF";
    for (std::size_t i = 0U; value[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        const bool plain = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) ||
                           (c == '_') || (c == '-') || (c == '.');
        if (plain) {
            if ((w + 1U) >= size) {
                return false;
            }
            body[w++] = static_cast<char>(c);
        } else {
            if ((w + 3U) >= size) {
                return false;
            }
            body[w++] = '%';
            body[w++] = kHex[c >> 4U];
            body[w++] = kHex[c & 0x0FU];
        }
    }
    body[w] = '\0';
    return true;
}

}  // namespace

WsConfig default_ws_config() noexcept {
    WsConfig c{};
    c.enabled = false;
    c.token = "";
    c.notifications = true;
    c.ping_s = kPingDefaultS;
    c.pong_ms = kPongDefaultMs;
    return c;
}

bool is_valid(const WsConfig& c) noexcept {
    if ((c.ping_s < kPingMinS) || (c.ping_s > kPingMaxS) || (c.pong_ms < kPongMinMs) || (c.pong_ms > kPongMaxMs)) {
        return false;
    }
    if (c.token == nullptr) {
        return false;
    }
    const std::size_t n = std::strlen(c.token);
    if (n > kTokenMax) {
        return false;
    }
    for (std::size_t i = 0U; i < n; ++i) {
        if ((c.token[i] < '!') || (c.token[i] > '~')) {
            return false;  // printable ASCII without spaces
        }
    }
    return !c.enabled || (n > 0U);
}

WsClient::WsClient(const WsConfig& config, const ValueList& values) noexcept
    : config_(is_valid(config) ? config : default_ws_config()), values_(values), states_{}, sub_ids_{},
      state_(LinkState::Disabled), next_id_(1U), events_id_(0U), ping_id_(0U), next_sub_(0U), auth_pending_(false),
      events_pending_(false), ping_pending_(false), awaiting_pong_(false), ping_sent_ms_(0U), last_ping_ms_(0U),
      retry_at_ms_(0U), backoff_ms_(kBackoffFirstMs), connect_request_(false), close_request_(false),
      link_down_since_ms_(0U), link_ever_up_(false), acked_subs_(0U), stats_{}, error_{}, notification_{},
      notification_pending_(false) {
    if (!is_valid(values_)) {
        values_ = default_value_list();
    }
    if (config_.enabled) {
        state_ = LinkState::Waiting;
        retry_at_ms_ = 0U;  // connect at once
    }
}

void WsClient::configure(const WsConfig& config, const ValueList& values, uint32_t now_ms) noexcept {
    const bool was_connected = (state_ == LinkState::Authenticating) || (state_ == LinkState::Subscribing) ||
                               (state_ == LinkState::Running) || (state_ == LinkState::Connecting);
    config_ = is_valid(config) ? config : default_ws_config();
    values_ = is_valid(values) ? values : default_value_list();
    for (ValueState& s : states_) {
        s = ValueState{};
    }
    if (was_connected) {
        close_request_ = true;
    }
    backoff_ms_ = kBackoffFirstMs;
    error_[0] = '\0';
    if (config_.enabled) {
        state_ = LinkState::Waiting;
        retry_at_ms_ = now_ms;  // right away
    } else {
        state_ = LinkState::Disabled;
    }
    link_down_since_ms_ = now_ms;
    link_ever_up_ = false;
}

const char* WsClient::state_name() const noexcept {
    switch (state_) {
        case LinkState::Disabled: return "disabled";
        case LinkState::Waiting: return "waiting";
        case LinkState::Connecting: return "connecting";
        case LinkState::Authenticating: return "authenticating";
        case LinkState::Subscribing: return "subscribing";
        case LinkState::Running: return "running";
        case LinkState::TokenRejected: return "token_rejected";
        default: return "?";
    }
}

std::size_t WsClient::enabled_count() const noexcept {
    std::size_t n = 0U;
    for (const ValueConfig& v : values_) {
        n += v.enabled ? 1U : 0U;
    }
    return n;
}

void WsClient::reset_link(uint32_t now_ms) noexcept {
    next_id_ = 1U;
    next_sub_ = 0U;
    acked_subs_ = 0U;
    auth_pending_ = false;
    events_pending_ = false;
    ping_pending_ = false;
    awaiting_pong_ = false;
    events_id_ = 0U;
    ping_id_ = 0U;
    for (uint32_t& id : sub_ids_) {
        id = 0U;
    }
    if (link_ever_up_ && (state_ == LinkState::Running)) {
        link_down_since_ms_ = now_ms;
    }
}

void WsClient::schedule_retry(uint32_t now_ms, bool token_rejected) noexcept {
    if (token_rejected) {
        state_ = LinkState::TokenRejected;
        retry_at_ms_ = now_ms + kTokenRejectedBackoffMs;
        return;
    }
    state_ = LinkState::Waiting;
    retry_at_ms_ = now_ms + backoff_ms_;
    backoff_ms_ = (backoff_ms_ * 2U > kBackoffMaxMs) ? kBackoffMaxMs : backoff_ms_ * 2U;
}

void WsClient::on_connected(uint32_t now_ms) noexcept {
    if (!config_.enabled) {
        close_request_ = true;
        return;
    }
    (void)now_ms;
    reset_link(now_ms);
    state_ = LinkState::Authenticating;
}

void WsClient::on_disconnected(uint32_t now_ms) noexcept {
    if ((state_ == LinkState::Disabled) || (state_ == LinkState::Waiting) || (state_ == LinkState::TokenRejected)) {
        return;
    }
    if (state_ == LinkState::Running) {
        link_down_since_ms_ = now_ms;
    }
    ++stats_.disconnections;
    reset_link(now_ms);
    schedule_retry(now_ms, false);
}

void WsClient::step(uint32_t now_ms) noexcept {
    switch (state_) {
        case LinkState::Waiting:
        case LinkState::TokenRejected:
            if (elapsed(now_ms, retry_at_ms_)) {
                state_ = LinkState::Connecting;
                connect_request_ = true;
                retry_at_ms_ = now_ms + kBackoffMaxMs;  // a connect that never answers is retried too
            }
            break;
        case LinkState::Connecting:
        case LinkState::Authenticating:
        case LinkState::Subscribing:
            if (elapsed(now_ms, retry_at_ms_)) {
                // Stuck (no auth_required, no results): close and retry.
                close_request_ = true;
                schedule_retry(now_ms, false);
            }
            break;
        case LinkState::Running:
            if (awaiting_pong_) {
                if (elapsed(now_ms, ping_sent_ms_ + config_.pong_ms)) {
                    ++stats_.pong_misses;
                    link_down_since_ms_ = now_ms;
                    close_request_ = true;
                    ++stats_.disconnections;
                    reset_link(now_ms);
                    schedule_retry(now_ms, false);
                }
            } else if (elapsed(now_ms, last_ping_ms_ + static_cast<uint32_t>(config_.ping_s) * 1000U)) {
                ping_pending_ = true;
                last_ping_ms_ = now_ms;
            } else {
                // nothing due
            }
            break;
        case LinkState::Disabled:
        default:
            break;
    }
}

bool WsClient::take_connect_request() noexcept {
    const bool r = connect_request_;
    connect_request_ = false;
    return r;
}

bool WsClient::take_close_request() noexcept {
    const bool r = close_request_;
    close_request_ = false;
    return r;
}

std::size_t WsClient::next_outgoing(char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (size == 0U)) {
        return 0U;
    }
    int n = 0;
    if (auth_pending_) {
        auth_pending_ = false;
        n = std::snprintf(out, size, "{\"type\":\"auth\",\"access_token\":\"%s\"}", config_.token);
    } else if (state_ == LinkState::Subscribing) {
        while ((next_sub_ < kValuesMax) && !values_[next_sub_].enabled) {
            ++next_sub_;
        }
        if (next_sub_ < kValuesMax) {
            char tmpl[kTemplateOutMax];
            if (build_template(values_[next_sub_], tmpl, sizeof(tmpl)) == 0U) {
                ++next_sub_;
                return next_outgoing(out, size);  // skip an unbuildable entry (one level: bounded by kValuesMax)
            }
            sub_ids_[next_sub_] = next_id_;
            n = std::snprintf(out, size,
                              "{\"id\":%lu,\"type\":\"render_template\",\"template\":\"%s\",\"report_errors\":true}",
                              static_cast<unsigned long>(next_id_), tmpl);
            ++next_id_;
            ++next_sub_;
        } else if (events_pending_) {
            events_pending_ = false;
            events_id_ = next_id_;
            n = std::snprintf(out, size, "{\"id\":%lu,\"type\":\"subscribe_events\",\"event_type\":\"%s\"}",
                              static_cast<unsigned long>(next_id_), kNotificationEvent);
            ++next_id_;
        } else {
            return 0U;
        }
    } else if (ping_pending_ && (state_ == LinkState::Running)) {
        ping_pending_ = false;
        ping_id_ = next_id_;
        n = std::snprintf(out, size, "{\"id\":%lu,\"type\":\"ping\"}", static_cast<unsigned long>(next_id_));
        ++next_id_;
        awaiting_pong_ = true;
        ping_sent_ms_ = last_ping_ms_;
    } else {
        return 0U;
    }
    if ((n <= 0) || (static_cast<std::size_t>(n) >= size)) {
        out[0] = '\0';
        return 0U;
    }
    return static_cast<std::size_t>(n);
}

void WsClient::handle_result(uint32_t id, const char* json, std::size_t len) noexcept {
    const char* const success_path[] = {"success"};
    JsonSpan success{};
    const bool ok = json_find(json, len, success_path, 1U, success) && json_is_true(success);
    for (std::size_t i = 0U; i < kValuesMax; ++i) {
        if ((sub_ids_[i] != 0U) && (sub_ids_[i] == id)) {
            if (!ok) {
                const char* const msg_path[] = {"error", "message"};
                JsonSpan msg{};
                if (json_find(json, len, msg_path, 2U, msg)) {
                    (void)json_string(msg, states_[i].error, sizeof(states_[i].error));
                } else {
                    (void)std::snprintf(states_[i].error, sizeof(states_[i].error), "subscription refused");
                }
                states_[i].valid = false;
            } else {
                states_[i].error[0] = '\0';
            }
            ++acked_subs_;
            break;
        }
    }
    if ((id == events_id_) && (events_id_ != 0U) && !ok) {
        (void)std::snprintf(error_, sizeof(error_), "event subscription refused");
    }
    // All expected acknowledgements in: running.
    const std::size_t expected = enabled_count() + (config_.notifications ? 1U : 0U);
    std::size_t acked = acked_subs_;
    if ((id == events_id_) && (events_id_ != 0U)) {
        acked = static_cast<std::size_t>(acked_subs_) + 1U;
        acked_subs_ = static_cast<uint8_t>(acked);
        events_id_ = events_id_ | 0x80000000U;  // mark acknowledged (ids are small)
    }
    if ((state_ == LinkState::Subscribing) && (acked >= expected) && (next_sub_ >= kValuesMax) && !events_pending_) {
        state_ = LinkState::Running;
        link_ever_up_ = true;
        ++stats_.connections;
        backoff_ms_ = kBackoffFirstMs;
    }
}

void WsClient::handle_event(uint32_t id, const char* json, std::size_t len, uint32_t now_ms) noexcept {
    for (std::size_t i = 0U; i < kValuesMax; ++i) {
        if ((sub_ids_[i] != 0U) && (sub_ids_[i] == id)) {
            const char* const result_path[] = {"event", "result"};
            JsonSpan result{};
            if (!json_find(json, len, result_path, 2U, result)) {
                // report_errors: {"event":{"error":"...","level":"ERROR"}}
                const char* const err_path[] = {"event", "error"};
                JsonSpan err{};
                if (json_find(json, len, err_path, 2U, err)) {
                    (void)json_string(err, states_[i].error, sizeof(states_[i].error));
                    states_[i].valid = false;
                } else {
                    ++stats_.messages_ignored;
                }
                return;
            }
            const ValueConfig& cfg = values_[i];
            (void)decode_result(cfg.type, cfg.decimals, result, states_[i]);
            states_[i].received_ms = now_ms;
            states_[i].ever_received = true;
            states_[i].error[0] = '\0';
            ++stats_.events;
            return;
        }
    }
    if ((events_id_ != 0U) && ((id == (events_id_ & 0x7FFFFFFFU)))) {
        if (decode_notification(json, len)) {
            ++stats_.notifications;
        } else {
            ++stats_.notifications_rejected;
        }
        return;
    }
    ++stats_.messages_ignored;
}

bool WsClient::decode_notification(const char* json, std::size_t len) noexcept {
    // event.data.{cle,titre,texte,priorite,duree_s,persistante,rappel_s,effacer} -> form body -> alerts decoder.
    const char* const data_path[] = {"event", "data"};
    JsonSpan data{};
    if (!json_find(json, len, data_path, 2U, data)) {
        return false;
    }
    const char* const keys[] = {"cle", "titre", "texte", "priorite", "duree_s", "persistante", "rappel_s", "effacer",
                                "couleur", "icone"};
    char body[alerts::kScratchMax];
    std::size_t w = 0U;
    for (const char* key : keys) {
        const char* const path[] = {key};
        JsonSpan v{};
        if (!json_find(data.data, data.len, path, 1U, v)) {
            continue;
        }
        char text[alerts::kTextMax + 1U];
        if (json_is_string(v)) {
            (void)json_string(v, text, sizeof(text));
        } else {
            std::size_t n = (v.len > alerts::kTextMax) ? alerts::kTextMax : v.len;
            std::memcpy(text, v.data, n);
            text[n] = '\0';
        }
        if (!form_append(body, sizeof(body), w, key, text)) {
            return false;
        }
    }
    alerts::Notification n{};
    if (alerts::decode_form(body, w, n) != alerts::DecodeStatus::Ok) {
        return false;
    }
    notification_ = n;
    notification_pending_ = true;
    return true;
}

void WsClient::on_message(const char* json, std::size_t len, uint32_t now_ms) noexcept {
    if ((json == nullptr) || (len == 0U) || (state_ == LinkState::Disabled)) {
        ++stats_.messages_ignored;
        return;
    }
    const char* const type_path[] = {"type"};
    JsonSpan type{};
    if (!json_find(json, len, type_path, 1U, type) || !json_is_string(type)) {
        ++stats_.messages_ignored;
        return;
    }
    if (span_equals(type, "auth_required")) {
        if (state_ == LinkState::Authenticating) {
            auth_pending_ = true;
            retry_at_ms_ = now_ms + kBackoffMaxMs;
        } else {
            ++stats_.messages_ignored;
        }
        return;
    }
    if (span_equals(type, "auth_ok")) {
        if (state_ == LinkState::Authenticating) {
            state_ = LinkState::Subscribing;
            events_pending_ = config_.notifications;
            next_sub_ = 0U;
            acked_subs_ = 0U;
            error_[0] = '\0';
            retry_at_ms_ = now_ms + kBackoffMaxMs;
            if ((enabled_count() == 0U) && !config_.notifications) {
                state_ = LinkState::Running;  // nothing to subscribe: still a live link
                link_ever_up_ = true;
                ++stats_.connections;
                backoff_ms_ = kBackoffFirstMs;
            }
            last_ping_ms_ = now_ms;
        } else {
            ++stats_.messages_ignored;
        }
        return;
    }
    if (span_equals(type, "auth_invalid")) {
        const char* const msg_path[] = {"message"};
        JsonSpan msg{};
        if (json_find(json, len, msg_path, 1U, msg)) {
            (void)json_string(msg, error_, sizeof(error_));
        } else {
            (void)std::snprintf(error_, sizeof(error_), "auth_invalid");
        }
        close_request_ = true;
        reset_link(now_ms);
        schedule_retry(now_ms, true);
        return;
    }
    const char* const id_path[] = {"id"};
    JsonSpan id_span{};
    int32_t id = 0;
    if (!json_find(json, len, id_path, 1U, id_span) || !json_to_fixed(id_span, 0U, id) || (id <= 0)) {
        ++stats_.messages_ignored;
        return;
    }
    const uint32_t uid = static_cast<uint32_t>(id);
    if (span_equals(type, "result")) {
        handle_result(uid, json, len);
        return;
    }
    if (span_equals(type, "event")) {
        handle_event(uid, json, len, now_ms);
        return;
    }
    if (span_equals(type, "pong")) {
        if (awaiting_pong_ && (uid == ping_id_)) {
            awaiting_pong_ = false;
        } else {
            ++stats_.messages_ignored;
        }
        return;
    }
    ++stats_.messages_ignored;
}

bool WsClient::value_valid(std::size_t index, uint32_t now_ms) const noexcept {
    if (index >= kValuesMax) {
        return false;
    }
    const ValueState& s = states_[index];
    if (!s.valid) {
        return false;
    }
    if (state_ == LinkState::Running) {
        return true;  // the link is alive: a stable value is a good value (debt D6)
    }
    const uint32_t expiry_ms = static_cast<uint32_t>(values_[index].expiry_s) * 1000U;
    return (now_ms - link_down_since_ms_) < expiry_ms;
}

bool WsClient::take_notification(alerts::Notification& out) noexcept {
    if (!notification_pending_) {
        return false;
    }
    out = notification_;
    notification_pending_ = false;
    return true;
}

}  // namespace homeassistant
