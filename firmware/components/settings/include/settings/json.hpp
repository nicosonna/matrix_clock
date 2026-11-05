#pragma once
/**
 * @file json.hpp
 * @brief JSON views of the settings: the **model** (bounds, defaults, choices,
 *        help — what the page builds its forms and wizards from) and the
 *        **current values**.
 *
 * Every function returns the length written, or 0 if the buffer is too small
 * (the caller answers "incomplete", never a cut document).
 */

#include <cstddef>

#include "settings/model.hpp"

namespace settings {

/// Buffer size that fits every model and every values document.
inline constexpr std::size_t kJsonMax = 4096U;

std::size_t display_model_json(char* out, std::size_t out_size) noexcept;
std::size_t brightness_model_json(char* out, std::size_t out_size) noexcept;
std::size_t system_model_json(char* out, std::size_t out_size) noexcept;
std::size_t air_model_json(char* out, std::size_t out_size) noexcept;
/// Link + weather fields, plus `valeur` (the sub-model of one tracked value) and `valeurs_max`.
std::size_t ha_model_json(char* out, std::size_t out_size) noexcept;

std::size_t display_json(const DisplaySettings& s, char* out, std::size_t out_size) noexcept;
std::size_t brightness_json(const brightness::Config& c, char* out, std::size_t out_size) noexcept;
std::size_t system_json(const SystemSettings& s, char* out, std::size_t out_size) noexcept;
std::size_t air_json(const AirSettings& s, char* out, std::size_t out_size) noexcept;
/// The token is never written back: `jeton_defini` says whether one is stored.
std::size_t ha_json(const HaSettings& s, char* out, std::size_t out_size) noexcept;

}  // namespace settings
