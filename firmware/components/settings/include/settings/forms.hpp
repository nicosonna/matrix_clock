#pragma once
/**
 * @file forms.hpp
 * @brief Form decoding, all or nothing: a field out of bounds refuses the whole
 *        form, names the field and the reason, and nothing is changed.
 *
 * Fields absent from the body keep their current value, so a page can post a
 * domain partially. Field names are French: they are the API, the user sees them.
 */

#include <cstddef>

#include "settings/model.hpp"

namespace settings {

/// Why a form was refused.
struct FormError {
    const char* field;   ///< Field name, or "" when the body itself is unusable.
    const char* reason;  ///< French, for the page.
};

/// Names of the fields of each domain — shared by the decoder, the model JSON
/// and the orphan test against the page.
extern const char* const kDisplayFields[];
extern const std::size_t kDisplayFieldCount;
extern const char* const kBrightnessFields[];
extern const std::size_t kBrightnessFieldCount;
extern const char* const kSystemFields[];
extern const std::size_t kSystemFieldCount;

/**
 * @brief Applies a form to a copy of @p current; writes @p out only if everything is valid.
 * @return true on success; false with @p err filled otherwise (@p out untouched).
 */
bool apply_display_form(const char* body, std::size_t len, const DisplaySettings& current, DisplaySettings& out,
                        FormError& err) noexcept;
bool apply_brightness_form(const char* body, std::size_t len, const brightness::Config& current,
                           brightness::Config& out, FormError& err) noexcept;
bool apply_system_form(const char* body, std::size_t len, const SystemSettings& current, SystemSettings& out,
                       FormError& err) noexcept;

/// Enumerations, as the page shows them (French labels are the API values).
const char* axis_name(display::FirstAxis a) noexcept;
const char* origin_name(display::Origin o) noexcept;
const char* scene_kind_name(display::SceneKind k) noexcept;
const char* brightness_mode_name(brightness::Mode m) noexcept;
bool axis_from_name(const char* s, display::FirstAxis& a) noexcept;
bool origin_from_name(const char* s, display::Origin& o) noexcept;
bool scene_kind_from_name(const char* s, display::SceneKind& k) noexcept;
bool brightness_mode_from_name(const char* s, brightness::Mode& m) noexcept;

/**
 * @brief Scene list as text: `Heure:20:1,Date:5:1,Valeur0:8:1` (kind[index]:seconds:enabled).
 * @return Length written, 0 if the buffer is too small.
 */
std::size_t scenes_to_text(const display::SceneList& scenes, char* out, std::size_t out_size) noexcept;
/// Parses the text form; false if malformed or invalid.
bool scenes_from_text(const char* text, display::SceneList& out) noexcept;

/// Curve as text: `300:8,900:40,2000:160` (raw:level).
std::size_t curve_to_text(const brightness::Curve& curve, char* out, std::size_t out_size) noexcept;
bool curve_from_text(const char* text, brightness::Curve& out) noexcept;

}  // namespace settings
