/**
 * @file values.cpp
 * @brief Tracked values.
 */
#include "homeassistant/values.hpp"

#include <cstdio>
#include <cstring>

namespace homeassistant {

namespace {

bool same(const char* a, const char* b) noexcept {
    return std::strcmp(a, b) == 0;
}

bool printable(char c) noexcept {
    return (c >= ' ') && (c <= '~');
}

/// Appends @p s to @p out at @p w, JSON-escaping quotes and backslashes. false if it does not fit.
bool append_escaped(const char* s, char* out, std::size_t size, std::size_t& w) noexcept {
    for (std::size_t i = 0U; s[i] != '\0'; ++i) {
        const char c = s[i];
        const bool esc = (c == '"') || (c == '\\');
        if ((w + (esc ? 2U : 1U) + 1U) > size) {
            return false;
        }
        if (esc) {
            out[w++] = '\\';
        }
        out[w++] = c;
    }
    return true;
}

bool append_raw(const char* s, char* out, std::size_t size, std::size_t& w) noexcept {
    const std::size_t n = std::strlen(s);
    if ((w + n + 1U) > size) {
        return false;
    }
    std::memcpy(out + w, s, n);
    w += n;
    return true;
}

}  // namespace

ValueConfig default_value_config() noexcept {
    ValueConfig v{};
    v.enabled = false;
    v.mode = ValueMode::Entity;
    v.type = ValueType::Number;
    v.decimals = 1U;
    v.icon = ValueIcon::None;
    v.expiry_s = kExpiryDefaultS;
    return v;
}

ValueList default_value_list() noexcept {
    ValueList l{};
    for (ValueConfig& v : l) {
        v = default_value_config();
    }
    return l;
}

bool is_valid_entity(const char* entity) noexcept {
    if (entity == nullptr) {
        return false;
    }
    std::size_t n = 0U;
    bool dot = false;
    for (; entity[n] != '\0'; ++n) {
        const char c = entity[n];
        const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_') || (c == '.');
        if (!ok || (n >= kEntityMax)) {
            return false;
        }
        dot = dot || (c == '.');
    }
    return (n > 0U) && dot && (entity[0] != '.') && (entity[n - 1U] != '.');
}

bool is_valid_template(const char* tmpl) noexcept {
    if (tmpl == nullptr) {
        return false;
    }
    std::size_t n = 0U;
    for (; tmpl[n] != '\0'; ++n) {
        if (!printable(tmpl[n]) || (n >= kTemplateMax)) {
            return false;
        }
    }
    return n > 0U;
}

bool is_valid(const ValueConfig& v) noexcept {
    if ((std::strlen(v.name) > kNameMax) || (std::strlen(v.unit) > kUnitMax) || (v.decimals > kDecimalsMax)) {
        return false;
    }
    if ((v.expiry_s < kExpiryMinS) || (v.expiry_s > kExpiryMaxS)) {
        return false;
    }
    if ((v.type != ValueType::Number) && (v.type != ValueType::Text) && (v.type != ValueType::Boolean)) {
        return false;
    }
    if (static_cast<uint8_t>(v.icon) >= static_cast<uint8_t>(ValueIcon::Count)) {
        return false;
    }
    for (std::size_t i = 0U; v.unit[i] != '\0'; ++i) {
        if ((v.unit[i] == '"') || (v.unit[i] == '\\')) {
            return false;
        }
    }
    if (!v.enabled) {
        return true;  // a disabled entry may be half filled
    }
    if (v.mode == ValueMode::Entity) {
        if (!is_valid_entity(v.entity)) {
            return false;
        }
        for (std::size_t i = 0U; v.attribute[i] != '\0'; ++i) {
            const char c = v.attribute[i];
            const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_');
            if (!ok || (i >= kAttributeMax)) {
                return false;
            }
        }
        return true;
    }
    if (v.mode == ValueMode::Template) {
        return is_valid_template(v.tmpl);
    }
    return false;
}

bool is_valid(const ValueList& list) noexcept {
    for (const ValueConfig& v : list) {
        if (!is_valid(v)) {
            return false;
        }
    }
    return true;
}

std::size_t build_template(const ValueConfig& v, char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (size == 0U)) {
        return 0U;
    }
    std::size_t w = 0U;
    bool ok = true;
    if (v.mode == ValueMode::Template) {
        ok = append_escaped(v.tmpl, out, size, w);
    } else if (v.attribute[0] == '\0') {
        ok = append_raw("{{ states('", out, size, w) && append_escaped(v.entity, out, size, w) &&
             append_raw("') }}", out, size, w);
    } else {
        ok = append_raw("{{ state_attr('", out, size, w) && append_escaped(v.entity, out, size, w) &&
             append_raw("','", out, size, w) && append_escaped(v.attribute, out, size, w) &&
             append_raw("') }}", out, size, w);
    }
    if (!ok) {
        out[0] = '\0';
        return 0U;
    }
    out[w] = '\0';
    return w;
}

bool decode_text(const char* text) noexcept {
    return (text != nullptr) && (text[0] != '\0') && !same(text, "unknown") && !same(text, "unavailable") &&
           !same(text, "none") && !same(text, "None") && !same(text, "null");
}

bool decode_boolean(const char* text, bool& out) noexcept {
    if (!decode_text(text)) {
        return false;
    }
    const char* const yes[] = {"on", "true", "home", "open", "yes", "True", "1"};
    const char* const no[] = {"off", "false", "not_home", "closed", "no", "False", "0"};
    for (const char* y : yes) {
        if (same(text, y)) {
            out = true;
            return true;
        }
    }
    for (const char* n : no) {
        if (same(text, n)) {
            out = false;
            return true;
        }
    }
    return false;
}

bool decode_number(const char* text, uint8_t decimals, int32_t& out) noexcept {
    if (!decode_text(text) || (decimals > kDecimalsMax)) {
        return false;
    }
    const JsonSpan span{text, std::strlen(text)};
    int32_t v = 0;
    if (!json_to_fixed(span, decimals, v)) {
        return false;
    }
    if ((v > kNumberLimit) || (v < -kNumberLimit)) {
        return false;
    }
    out = v;
    return true;
}

bool decode_result(ValueType type, uint8_t decimals, const JsonSpan& result, ValueState& out) noexcept {
    // Raw text first: string unescaped, or the literal/number as is (bounded).
    if (json_is_string(result)) {
        (void)json_string(result, out.text, sizeof(out.text));
    } else {
        std::size_t n = (result.data == nullptr) ? 0U : result.len;
        if (n > kTextMax) {
            n = kTextMax;
        }
        if (n > 0U) {
            std::memcpy(out.text, result.data, n);
        }
        out.text[n] = '\0';
    }
    bool valid = false;
    switch (type) {
        case ValueType::Number:
            valid = decode_number(out.text, decimals, out.number);
            break;
        case ValueType::Boolean:
            valid = decode_boolean(out.text, out.boolean);
            break;
        case ValueType::Text:
        default:
            valid = decode_text(out.text);
            break;
    }
    out.valid = valid;
    return valid;
}

const char* value_type_name(ValueType t) noexcept {
    switch (t) {
        case ValueType::Text: return "Texte";
        case ValueType::Boolean: return "Booleen";
        case ValueType::Number:
        default: return "Nombre";
    }
}

bool value_type_from_name(const char* s, ValueType& t) noexcept {
    if (s == nullptr) {
        return false;
    }
    const ValueType all[] = {ValueType::Number, ValueType::Text, ValueType::Boolean};
    for (ValueType c : all) {
        if (same(s, value_type_name(c))) {
            t = c;
            return true;
        }
    }
    return false;
}

const char* value_mode_name(ValueMode m) noexcept {
    return (m == ValueMode::Template) ? "Gabarit" : "Entite";
}

bool value_mode_from_name(const char* s, ValueMode& m) noexcept {
    if (s == nullptr) {
        return false;
    }
    if (same(s, "Entite")) {
        m = ValueMode::Entity;
        return true;
    }
    if (same(s, "Gabarit")) {
        m = ValueMode::Template;
        return true;
    }
    return false;
}

const char* value_icon_name(ValueIcon i) noexcept {
    switch (i) {
        case ValueIcon::Thermometer: return "Thermometre";
        case ValueIcon::Drop: return "Goutte";
        case ValueIcon::Bolt: return "Eclair";
        case ValueIcon::Door: return "Porte";
        case ValueIcon::Wind: return "Vent";
        case ValueIcon::Sun: return "Soleil";
        case ValueIcon::Home: return "Maison";
        case ValueIcon::None:
        default: return "Aucune";
    }
}

bool value_icon_from_name(const char* s, ValueIcon& i) noexcept {
    if (s == nullptr) {
        return false;
    }
    for (uint8_t k = 0U; k < static_cast<uint8_t>(ValueIcon::Count); ++k) {
        const ValueIcon c = static_cast<ValueIcon>(k);
        if (same(s, value_icon_name(c))) {
            i = c;
            return true;
        }
    }
    return false;
}

std::size_t format_number(int32_t number, uint8_t decimals, const char* unit, char* out, std::size_t size) noexcept {
    if ((out == nullptr) || (size == 0U) || (decimals > kDecimalsMax)) {
        return 0U;
    }
    int32_t scale = 1;
    for (uint8_t i = 0U; i < decimals; ++i) {
        scale *= 10;
    }
    const bool neg = number < 0;
    const int32_t abs = neg ? -number : number;
    const int32_t whole = abs / scale;
    const int32_t frac = abs % scale;
    int n = 0;
    if (decimals == 0U) {
        n = std::snprintf(out, size, "%s%ld%s", neg ? "-" : "", static_cast<long>(whole), (unit != nullptr) ? unit : "");
    } else if (decimals == 1U) {
        n = std::snprintf(out, size, "%s%ld.%01ld%s", neg ? "-" : "", static_cast<long>(whole), static_cast<long>(frac),
                          (unit != nullptr) ? unit : "");
    } else {
        n = std::snprintf(out, size, "%s%ld.%02ld%s", neg ? "-" : "", static_cast<long>(whole), static_cast<long>(frac),
                          (unit != nullptr) ? unit : "");
    }
    if ((n <= 0) || (static_cast<std::size_t>(n) >= size)) {
        out[0] = '\0';
        return 0U;
    }
    return static_cast<std::size_t>(n);
}

}  // namespace homeassistant
