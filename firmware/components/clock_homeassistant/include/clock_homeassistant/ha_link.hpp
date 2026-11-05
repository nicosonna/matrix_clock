#pragma once
/**
 * @file ha_link.hpp
 * @brief Home Assistant link task: WebSocket transport + Open-Meteo fallback.
 *
 * All decisions live in the pure `homeassistant::WsClient`; this task only
 * opens/closes the socket when asked, feeds complete messages in, sends what
 * the client wants out, and publishes a snapshot for the display and the page.
 */

#include <cstddef>
#include <cstdint>

#include "alerts/notification.hpp"
#include "esp_err.h"
#include "homeassistant/values.hpp"
#include "homeassistant/ws_client.hpp"

namespace clock_homeassistant {

/// Receives a decoded notification; returns false if it could not be queued.
using NotificationHandler = bool (*)(const alerts::Notification&);

/// One tracked value as the display needs it.
struct ValueSnapshot {
    bool enabled;
    bool valid;                    ///< With expiry applied.
    homeassistant::ValueType type;
    int32_t number;
    uint8_t decimals;
    bool boolean;
    char text[homeassistant::kTextMax + 1U];   ///< Raw result (text values) or formatted number.
    char name[homeassistant::kNameMax + 1U];
    char unit[homeassistant::kUnitMax + 1U];
    homeassistant::ValueIcon icon;
    char error[homeassistant::kErrorMax + 1U];
    bool from_weather;             ///< Value 1 replaced by the Open-Meteo fallback.
};

struct LinkSnapshot {
    bool enabled;
    homeassistant::LinkState state;
    char state_name[20];
    char error[homeassistant::kErrorMax + 1U];
    homeassistant::WsStats stats;
    uint32_t frames_dropped;
    ValueSnapshot values[homeassistant::kValuesMax];
    bool weather_enabled;
    bool weather_valid;
    int16_t weather_deci;
    uint32_t weather_age_s;
};

/// Starts the task (after the settings store and the network). Never blocks the caller.
esp_err_t start(NotificationHandler on_notification) noexcept;

/// Copies the current state into @p out (safe from any task; ~3 KB, keep @p out static).
void snapshot(LinkSnapshot& out) noexcept;

}  // namespace clock_homeassistant
