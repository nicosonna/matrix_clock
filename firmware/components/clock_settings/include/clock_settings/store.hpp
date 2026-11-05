#pragma once
/**
 * @file store.hpp
 * @brief Settings persistence and sharing.
 *
 * Each domain is one NVS blob prefixed by a schema version. An unknown version
 * or a blob that fails validation falls back to the defaults and is **flagged**
 * (`replaced_on_boot()`): a corrupted configuration is not repaired, the page
 * says so (architecture.md section 4).
 */

#include <cstdint>

#include "esp_err.h"
#include "settings/model.hpp"

namespace clock_settings {

/// Loads every domain from NVS (defaults where missing or invalid). Call after nvs_flash_init().
esp_err_t init() noexcept;

/// @return A copy of the current settings (safe from any task).
settings::Settings get() noexcept;

/// @return A counter incremented on every change: poll it to re-apply settings.
uint32_t version() noexcept;

/// true if at least one domain fell back to its defaults at boot.
bool replaced_on_boot() noexcept;

/// Validate, persist, publish. false if invalid or NVS failed (nothing changed).
bool save_display(const settings::DisplaySettings& s) noexcept;
bool save_brightness(const brightness::Config& c) noexcept;
bool save_system(const settings::SystemSettings& s) noexcept;
bool save_air(const settings::AirSettings& s) noexcept;

/// Erases every stored setting (Wi-Fi credentials included). The caller reboots.
bool factory_reset() noexcept;

}  // namespace clock_settings
