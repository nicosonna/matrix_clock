#pragma once
/**
 * @file mqtt_link.hpp
 * @brief MQTT publication task: esp-mqtt transport around the pure `homeassistant::MqttPublisher`.
 *
 * The publisher decides (what, when, in which order); this task starts and
 * stops the client, publishes what is asked, and feeds broker events back.
 * Publish only: the single subscription is Home Assistant's own status topic.
 */

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "homeassistant/mqtt.hpp"

namespace clock_homeassistant {

/// Fills @p out with what the clock measures right now (called from the MQTT task).
using StateProvider = void (*)(homeassistant::MqttState& out);

struct MqttSnapshot {
    bool enabled;
    homeassistant::MqttLinkState state;
    const char* state_name;
    homeassistant::MqttStats stats;
    uint32_t publish_failures;      ///< esp-mqtt refused a publish (offline queue full, not connected).
    char device_id[homeassistant::kMqttIdSize];
    char last_error[48];
};

/// Starts the task (after the settings store and the network). Never blocks the caller.
esp_err_t mqtt_start(StateProvider provider) noexcept;
/// Current state for the page and `/api/etat` (safe from any task).
void mqtt_snapshot(MqttSnapshot& out) noexcept;
/// Page button: publish the discovery again (no-op when not connected).
void mqtt_request_discovery() noexcept;

}  // namespace clock_homeassistant
