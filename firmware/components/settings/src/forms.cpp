/**
 * @file forms.cpp
 * @brief Form decoding (see forms.hpp).
 */
#include "settings/forms.hpp"

#include <cstdio>
#include <cstring>

#include "network/credentials.hpp"
#include "timekeeping/tz.hpp"

namespace settings {

const char* const kDisplayFields[] = {"axe_premier", "serpentin", "origine", "largeur_module", "budget_ma",
                                      "clignotement", "barre_secondes", "vitesse_defilement", "scenes"};
const std::size_t kDisplayFieldCount = sizeof(kDisplayFields) / sizeof(kDisplayFields[0]);

const char* const kBrightnessFields[] = {"mode", "lum_manuelle", "lum_min", "lum_max", "pas_lum", "montee_s", "descente_s",
                                         "compensation_counts", "extinction_active", "extinction_debut", "extinction_fin",
                                         "courbe"};
const std::size_t kBrightnessFieldCount = sizeof(kBrightnessFields) / sizeof(kBrightnessFields[0]);

const char* const kSystemFields[] = {"fuseau", "serveur_ntp"};
const std::size_t kSystemFieldCount = sizeof(kSystemFields) / sizeof(kSystemFields[0]);

namespace {

constexpr std::size_t kFieldMax = 256U;

bool same(const char* a, const char* b) noexcept {
    return std::strcmp(a, b) == 0;
}

/// Parses an integer in [lo, hi]; false otherwise.
bool parse_int(const char* s, int32_t lo, int32_t hi, int32_t& out) noexcept {
    if ((s == nullptr) || (s[0] == '\0')) {
        return false;
    }
    bool neg = false;
    std::size_t i = 0U;
    if (s[0] == '-') {
        neg = true;
        i = 1U;
    }
    int64_t v = 0;
    std::size_t digits = 0U;
    for (; s[i] != '\0'; ++i) {
        if ((s[i] < '0') || (s[i] > '9') || (digits >= 9U)) {
            return false;
        }
        v = v * 10 + (s[i] - '0');
        ++digits;
    }
    if (digits == 0U) {
        return false;
    }
    if (neg) {
        v = -v;
    }
    if ((v < lo) || (v > hi)) {
        return false;
    }
    out = static_cast<int32_t>(v);
    return true;
}

bool parse_bool(const char* s, bool& out) noexcept {
    if (same(s, "1") || same(s, "true") || same(s, "oui") || same(s, "on")) {
        out = true;
        return true;
    }
    if (same(s, "0") || same(s, "false") || same(s, "non") || same(s, "off") || same(s, "")) {
        out = false;
        return true;
    }
    return false;
}

/// Fetches a field; returns 0 = absent, 1 = present, -1 = malformed.
int8_t fetch(const char* body, std::size_t len, const char* name, char* out, std::size_t out_size) noexcept {
    std::size_t n = 0U;
    if (network::form_field(body, len, name, out, out_size, n)) {
        return 1;
    }
    // Absent, or present but malformed / too long: look for "name=" to tell.
    const std::size_t nl = std::strlen(name);
    std::size_t i = 0U;
    while (i + nl < len) {
        if (((i == 0U) || (body[i - 1U] == '&')) && (std::strncmp(body + i, name, nl) == 0) && (body[i + nl] == '=')) {
            return -1;
        }
        ++i;
    }
    return 0;
}

bool fail(FormError& err, const char* field, const char* reason) noexcept {
    err.field = field;
    err.reason = reason;
    return false;
}

constexpr char kBadEncoding[] = "champ mal encodé ou trop long";

}  // namespace

// ----------------------------------------------------------------- enumerations

const char* axis_name(display::FirstAxis a) noexcept {
    return (a == display::FirstAxis::Row) ? "Ligne" : "Colonne";
}

const char* origin_name(display::Origin o) noexcept {
    switch (o) {
        case display::Origin::TopRight: return "HautDroite";
        case display::Origin::BottomLeft: return "BasGauche";
        case display::Origin::BottomRight: return "BasDroite";
        case display::Origin::TopLeft:
        default: return "HautGauche";
    }
}

const char* scene_kind_name(display::SceneKind k) noexcept {
    switch (k) {
        case display::SceneKind::Date: return "Date";
        case display::SceneKind::Value: return "Valeur";
        case display::SceneKind::TwoLines: return "DeuxLignes";
        case display::SceneKind::Scroll: return "Defilement";
        case display::SceneKind::Night: return "Nuit";
        case display::SceneKind::Test: return "Test";
        case display::SceneKind::Clock:
        default: return "Heure";
    }
}

const char* brightness_mode_name(brightness::Mode m) noexcept {
    switch (m) {
        case brightness::Mode::Manual: return "Manuel";
        case brightness::Mode::Off: return "Eteint";
        case brightness::Mode::Auto:
        default: return "Auto";
    }
}

bool axis_from_name(const char* s, display::FirstAxis& a) noexcept {
    if (same(s, "Colonne")) {
        a = display::FirstAxis::Column;
        return true;
    }
    if (same(s, "Ligne")) {
        a = display::FirstAxis::Row;
        return true;
    }
    return false;
}

bool origin_from_name(const char* s, display::Origin& o) noexcept {
    const display::Origin all[] = {display::Origin::TopLeft, display::Origin::TopRight, display::Origin::BottomLeft,
                                   display::Origin::BottomRight};
    for (display::Origin cand : all) {
        if (same(s, origin_name(cand))) {
            o = cand;
            return true;
        }
    }
    return false;
}

bool scene_kind_from_name(const char* s, display::SceneKind& k) noexcept {
    const display::SceneKind all[] = {display::SceneKind::Clock, display::SceneKind::Date, display::SceneKind::Value,
                                      display::SceneKind::TwoLines, display::SceneKind::Scroll, display::SceneKind::Night,
                                      display::SceneKind::Test};
    for (display::SceneKind cand : all) {
        if (same(s, scene_kind_name(cand))) {
            k = cand;
            return true;
        }
    }
    return false;
}

bool brightness_mode_from_name(const char* s, brightness::Mode& m) noexcept {
    const brightness::Mode all[] = {brightness::Mode::Auto, brightness::Mode::Manual, brightness::Mode::Off};
    for (brightness::Mode cand : all) {
        if (same(s, brightness_mode_name(cand))) {
            m = cand;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ scenes text

std::size_t scenes_to_text(const display::SceneList& scenes, char* out, std::size_t out_size) noexcept {
    if ((out == nullptr) || (out_size == 0U)) {
        return 0U;
    }
    std::size_t w = 0U;
    out[0] = '\0';
    for (uint8_t i = 0U; i < scenes.count; ++i) {
        const display::SceneEntry& e = scenes.entries[i];
        char item[48];
        int n = 0;
        if (e.kind == display::SceneKind::Value) {
            n = std::snprintf(item, sizeof(item), "%s%s%u:%u:%u", (i > 0U) ? "," : "", scene_kind_name(e.kind),
                              static_cast<unsigned>(e.value_index), static_cast<unsigned>(e.duration_s), e.enabled ? 1U : 0U);
        } else {
            n = std::snprintf(item, sizeof(item), "%s%s:%u:%u", (i > 0U) ? "," : "", scene_kind_name(e.kind),
                              static_cast<unsigned>(e.duration_s), e.enabled ? 1U : 0U);
        }
        if ((n <= 0) || ((w + static_cast<std::size_t>(n) + 1U) > out_size)) {
            out[0] = '\0';
            return 0U;
        }
        std::memcpy(out + w, item, static_cast<std::size_t>(n) + 1U);
        w += static_cast<std::size_t>(n);
    }
    return w;
}

bool scenes_from_text(const char* text, display::SceneList& out) noexcept {
    if (text == nullptr) {
        return false;
    }
    display::SceneList list{};
    const char* p = text;
    while (*p != '\0') {
        if (list.count >= display::kSceneMax) {
            return false;
        }
        // kind[index]
        char kind[16];
        std::size_t k = 0U;
        while ((*p != '\0') && (*p != ':') && (*p != ',')) {
            if (k + 1U >= sizeof(kind)) {
                return false;
            }
            kind[k++] = *p++;
        }
        kind[k] = '\0';
        uint8_t index = 0U;
        if ((k > 0U) && (kind[k - 1U] >= '0') && (kind[k - 1U] <= '9')) {
            index = static_cast<uint8_t>(kind[k - 1U] - '0');
            kind[k - 1U] = '\0';
        }
        display::SceneKind sk = display::SceneKind::Clock;
        if (!scene_kind_from_name(kind, sk) || (*p != ':')) {
            return false;
        }
        ++p;
        char num[8];
        std::size_t n = 0U;
        while ((*p != '\0') && (*p != ':')) {
            if (n + 1U >= sizeof(num)) {
                return false;
            }
            num[n++] = *p++;
        }
        num[n] = '\0';
        int32_t duration = 0;
        if (!parse_int(num, 0, 3600, duration) || (*p != ':')) {
            return false;
        }
        ++p;
        n = 0U;
        while ((*p != '\0') && (*p != ',')) {
            if (n + 1U >= sizeof(num)) {
                return false;
            }
            num[n++] = *p++;
        }
        num[n] = '\0';
        bool enabled = false;
        if (!parse_bool(num, enabled)) {
            return false;
        }
        list.entries[list.count] = display::SceneEntry{sk, index, static_cast<uint16_t>(duration), enabled};
        ++list.count;
        if (*p == ',') {
            ++p;
            if (*p == '\0') {
                return false;  // trailing comma
            }
        }
    }
    if (!display::is_valid(list)) {
        return false;
    }
    out = list;
    return true;
}

// ------------------------------------------------------------------- curve text

std::size_t curve_to_text(const brightness::Curve& curve, char* out, std::size_t out_size) noexcept {
    if ((out == nullptr) || (out_size == 0U)) {
        return 0U;
    }
    std::size_t w = 0U;
    out[0] = '\0';
    for (uint8_t i = 0U; i < curve.count; ++i) {
        char item[24];
        const int n = std::snprintf(item, sizeof(item), "%s%u:%u", (i > 0U) ? "," : "",
                                    static_cast<unsigned>(curve.points[i].raw), static_cast<unsigned>(curve.points[i].level));
        if ((n <= 0) || ((w + static_cast<std::size_t>(n) + 1U) > out_size)) {
            out[0] = '\0';
            return 0U;
        }
        std::memcpy(out + w, item, static_cast<std::size_t>(n) + 1U);
        w += static_cast<std::size_t>(n);
    }
    return w;
}

bool curve_from_text(const char* text, brightness::Curve& out) noexcept {
    if (text == nullptr) {
        return false;
    }
    brightness::Curve c{};
    const char* p = text;
    while (*p != '\0') {
        if (c.count >= brightness::kCurvePointsMax) {
            return false;
        }
        char num[8];
        std::size_t n = 0U;
        while ((*p != '\0') && (*p != ':')) {
            if (n + 1U >= sizeof(num)) {
                return false;
            }
            num[n++] = *p++;
        }
        num[n] = '\0';
        int32_t raw = 0;
        if (!parse_int(num, 0, brightness::kRawMax, raw) || (*p != ':')) {
            return false;
        }
        ++p;
        n = 0U;
        while ((*p != '\0') && (*p != ',')) {
            if (n + 1U >= sizeof(num)) {
                return false;
            }
            num[n++] = *p++;
        }
        num[n] = '\0';
        int32_t level = 0;
        if (!parse_int(num, 0, 255, level)) {
            return false;
        }
        c.points[c.count] = brightness::CurvePoint{static_cast<uint16_t>(raw), static_cast<uint8_t>(level)};
        ++c.count;
        if (*p == ',') {
            ++p;
            if (*p == '\0') {
                return false;
            }
        }
    }
    if (!brightness::is_valid(c)) {
        return false;
    }
    out = c;
    return true;
}

// ------------------------------------------------------------------ the forms

bool apply_display_form(const char* body, std::size_t len, const DisplaySettings& current, DisplaySettings& out,
                        FormError& err) noexcept {
    DisplaySettings s = current;
    char v[kFieldMax];
    int8_t r = 0;

    if ((r = fetch(body, len, "axe_premier", v, sizeof(v))) < 0) {
        return fail(err, "axe_premier", kBadEncoding);
    }
    if ((r > 0) && !axis_from_name(v, s.map.first_axis)) {
        return fail(err, "axe_premier", "Colonne ou Ligne");
    }
    if ((r = fetch(body, len, "serpentin", v, sizeof(v))) < 0) {
        return fail(err, "serpentin", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, s.map.serpentine)) {
        return fail(err, "serpentin", "0 ou 1");
    }
    if ((r = fetch(body, len, "origine", v, sizeof(v))) < 0) {
        return fail(err, "origine", kBadEncoding);
    }
    if ((r > 0) && !origin_from_name(v, s.map.origin)) {
        return fail(err, "origine", "HautGauche, HautDroite, BasGauche ou BasDroite");
    }
    int32_t i = 0;
    if ((r = fetch(body, len, "largeur_module", v, sizeof(v))) < 0) {
        return fail(err, "largeur_module", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, 0, 8, i) || ((i != 0) && (i != display::kChainedModuleWidth))) {
            return fail(err, "largeur_module", "0 (dalle entière) ou 8 (modules chaînés)");
        }
        s.map.module_width = static_cast<uint8_t>(i);
    }
    if ((r = fetch(body, len, "budget_ma", v, sizeof(v))) < 0) {
        return fail(err, "budget_ma", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, display::kMinBudgetMa, display::kMaxBudgetMa, i)) {
            return fail(err, "budget_ma", "doit valoir entre 400 et 15000 mA");
        }
        s.budget_ma = static_cast<uint16_t>(i);
    }
    if ((r = fetch(body, len, "clignotement", v, sizeof(v))) < 0) {
        return fail(err, "clignotement", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, s.colon_blink)) {
        return fail(err, "clignotement", "0 ou 1");
    }
    if ((r = fetch(body, len, "barre_secondes", v, sizeof(v))) < 0) {
        return fail(err, "barre_secondes", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, s.seconds_bar)) {
        return fail(err, "barre_secondes", "0 ou 1");
    }
    if ((r = fetch(body, len, "vitesse_defilement", v, sizeof(v))) < 0) {
        return fail(err, "vitesse_defilement", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, display::kScrollSpeedMin, display::kScrollSpeedMax, i)) {
            return fail(err, "vitesse_defilement", "de 5 à 60 pixels par seconde");
        }
        s.scroll_speed_px_s = static_cast<uint16_t>(i);
    }
    if ((r = fetch(body, len, "scenes", v, sizeof(v))) < 0) {
        return fail(err, "scenes", kBadEncoding);
    }
    if ((r > 0) && !scenes_from_text(v, s.scenes)) {
        return fail(err, "scenes", "liste Type:secondes:actif, au plus 8, Valeur0..7");
    }
    if (!is_valid(s)) {
        return fail(err, "", "réglages incohérents");
    }
    out = s;
    return true;
}

bool apply_brightness_form(const char* body, std::size_t len, const brightness::Config& current,
                           brightness::Config& out, FormError& err) noexcept {
    brightness::Config c = current;
    char v[kFieldMax];
    int8_t r = 0;
    int32_t i = 0;

    if ((r = fetch(body, len, "mode", v, sizeof(v))) < 0) {
        return fail(err, "mode", kBadEncoding);
    }
    if ((r > 0) && !brightness_mode_from_name(v, c.mode)) {
        return fail(err, "mode", "Auto, Manuel ou Eteint");
    }
    struct U8Field {
        const char* name;
        uint8_t* target;
        int32_t lo;
        int32_t hi;
        const char* reason;
    };
    const U8Field u8[] = {
        {"lum_manuelle", &c.manual_level, 0, 255, "de 0 à 255"},
        {"lum_min", &c.min_level, 0, 255, "de 0 à 255"},
        {"lum_max", &c.max_level, 0, 255, "de 0 à 255"},
        {"pas_lum", &c.step, brightness::kStepMin, brightness::kStepMax, "de 1 à 32"},
    };
    for (const U8Field& f : u8) {
        if ((r = fetch(body, len, f.name, v, sizeof(v))) < 0) {
            return fail(err, f.name, kBadEncoding);
        }
        if (r > 0) {
            if (!parse_int(v, f.lo, f.hi, i)) {
                return fail(err, f.name, f.reason);
            }
            *f.target = static_cast<uint8_t>(i);
        }
    }
    struct U16Field {
        const char* name;
        uint16_t* target;
        int32_t lo;
        int32_t hi;
        const char* reason;
    };
    const U16Field u16[] = {
        {"montee_s", &c.rise_s, 1, brightness::kRiseMax, "de 1 à 300 s"},
        {"descente_s", &c.fall_s, 1, brightness::kFallMax, "de 1 à 600 s"},
        {"extinction_debut", &c.off_start_min, 0, brightness::kMinutesPerDay - 1, "minutes depuis minuit, 0 à 1439"},
        {"extinction_fin", &c.off_end_min, 0, brightness::kMinutesPerDay - 1, "minutes depuis minuit, 0 à 1439"},
    };
    for (const U16Field& f : u16) {
        if ((r = fetch(body, len, f.name, v, sizeof(v))) < 0) {
            return fail(err, f.name, kBadEncoding);
        }
        if (r > 0) {
            if (!parse_int(v, f.lo, f.hi, i)) {
                return fail(err, f.name, f.reason);
            }
            *f.target = static_cast<uint16_t>(i);
        }
    }
    if ((r = fetch(body, len, "compensation_counts", v, sizeof(v))) < 0) {
        return fail(err, "compensation_counts", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, -brightness::kCompensationMax, brightness::kCompensationMax, i)) {
            return fail(err, "compensation_counts", "de -4095 à 4095 comptes");
        }
        c.compensation_counts = static_cast<int16_t>(i);
    }
    if ((r = fetch(body, len, "extinction_active", v, sizeof(v))) < 0) {
        return fail(err, "extinction_active", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, c.off_schedule)) {
        return fail(err, "extinction_active", "0 ou 1");
    }
    if ((r = fetch(body, len, "courbe", v, sizeof(v))) < 0) {
        return fail(err, "courbe", kBadEncoding);
    }
    if ((r > 0) && !curve_from_text(v, c.curve)) {
        return fail(err, "courbe", "2 à 6 points mesure:niveau, mesures strictement croissantes (0 à 4095)");
    }
    if (c.min_level > c.max_level) {
        return fail(err, "lum_min", "le plancher doit rester sous le plafond");
    }
    if (!brightness::is_valid(c)) {
        return fail(err, "", "réglages incohérents");
    }
    out = c;
    return true;
}

bool apply_system_form(const char* body, std::size_t len, const SystemSettings& current, SystemSettings& out,
                       FormError& err) noexcept {
    SystemSettings s = current;
    char v[kFieldMax];
    int8_t r = 0;

    if ((r = fetch(body, len, "fuseau", v, sizeof(v))) < 0) {
        return fail(err, "fuseau", kBadEncoding);
    }
    if (r > 0) {
        if ((timekeeping::posix_for_zone(v) == nullptr) || (std::strlen(v) > kZoneNameMax)) {
            return fail(err, "fuseau", "fuseau inconnu : choisir dans la liste");
        }
        std::strncpy(s.timezone, v, kZoneNameMax);
        s.timezone[kZoneNameMax] = '\0';
    }
    if ((r = fetch(body, len, "serveur_ntp", v, sizeof(v))) < 0) {
        return fail(err, "serveur_ntp", kBadEncoding);
    }
    if (r > 0) {
        if (!is_valid_host(v)) {
            return fail(err, "serveur_ntp", "nom ou adresse IPv4, lettres, chiffres, points et tirets, 63 caractères");
        }
        std::strncpy(s.ntp_server, v, kHostMax);
        s.ntp_server[kHostMax] = '\0';
    }
    if (!is_valid(s)) {
        return fail(err, "", "réglages incohérents");
    }
    out = s;
    return true;
}

}  // namespace settings
