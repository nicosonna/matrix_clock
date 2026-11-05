/**
 * @file forms.cpp
 * @brief Form decoding (see forms.hpp).
 */
#include "settings/forms.hpp"

#include <cstdio>
#include <cstring>

#include "homeassistant/values.hpp"
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
const char* const kAirFields[] = {"actif", "adresse", "grandeur_pilote", "seuil_eco2_bon", "seuil_eco2_moyen",
                                  "seuil_eco2_mauvais", "seuil_eco2_tres_mauvais", "seuil_tvoc_bon", "seuil_tvoc_moyen",
                                  "seuil_tvoc_mauvais", "seuil_tvoc_tres_mauvais", "hysteresis_eco2", "hysteresis_tvoc",
                                  "confirmation_s", "logo_air", "decalage_temperature"};
const std::size_t kAirFieldCount = sizeof(kAirFields) / sizeof(kAirFields[0]);
const char* const kHaFields[] = {"actif", "securise", "hote", "port", "jeton", "effacer_jeton", "notifications",
                                 "ping_s", "pong_ms", "meteo_actif", "latitude", "longitude", "meteo_periode_s"};
const std::size_t kHaFieldCount = sizeof(kHaFields) / sizeof(kHaFields[0]);
const char* const kHaValueFields[] = {"actif", "nom", "mode", "entite", "attribut", "gabarit", "type", "decimales",
                                      "unite", "icone", "peremption_s"};
const std::size_t kHaValueFieldCount = sizeof(kHaValueFields) / sizeof(kHaValueFields[0]);
const char* const kMqttFields[] = {"actif", "hote", "port", "utilisateur", "mot_de_passe", "effacer_mdp", "prefixe",
                                   "periode_s"};
const std::size_t kMqttFieldCount = sizeof(kMqttFields) / sizeof(kMqttFields[0]);
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
        case display::SceneKind::Air: return "Air";
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
                                      display::SceneKind::Test, display::SceneKind::Air};
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


// ------------------------------------------------------------------ air form

const char* air_logo_name(AirLogoMode m) noexcept {
    switch (m) {
        case AirLogoMode::None: return "Aucun";
        case AirLogoMode::Scene: return "Scene";
        case AirLogoMode::IconZone:
        default: return "ZoneIcone";
    }
}

bool air_logo_from_name(const char* s, AirLogoMode& m) noexcept {
    if (s == nullptr) {
        return false;
    }
    const AirLogoMode all[] = {AirLogoMode::None, AirLogoMode::IconZone, AirLogoMode::Scene};
    for (AirLogoMode cand : all) {
        if (same(s, air_logo_name(cand))) {
            m = cand;
            return true;
        }
    }
    return false;
}

bool apply_air_form(const char* body, std::size_t len, const AirSettings& current, AirSettings& out,
                    FormError& err) noexcept {
    AirSettings a = current;
    char v[kFieldMax];
    int8_t r = 0;
    int32_t i = 0;

    if ((r = fetch(body, len, "actif", v, sizeof(v))) < 0) {
        return fail(err, "actif", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, a.enabled)) {
        return fail(err, "actif", "0 ou 1");
    }
    if ((r = fetch(body, len, "adresse", v, sizeof(v))) < 0) {
        return fail(err, "adresse", kBadEncoding);
    }
    if (r > 0) {
        if (same(v, "0x53")) {
            a.ens160_address = air_quality::kEns160AddressHigh;
        } else if (same(v, "0x52")) {
            a.ens160_address = air_quality::kEns160AddressLow;
        } else {
            return fail(err, "adresse", "0x52 ou 0x53");
        }
    }
    if ((r = fetch(body, len, "grandeur_pilote", v, sizeof(v))) < 0) {
        return fail(err, "grandeur_pilote", kBadEncoding);
    }
    if ((r > 0) && !air_quality::pilot_from_name(v, a.levels.pilot)) {
        return fail(err, "grandeur_pilote", "Aqi, Eco2 ou Tvoc");
    }
    struct U16Field {
        const char* name;
        uint16_t* target;
        int32_t lo;
        int32_t hi;
        const char* reason;
    };
    const U16Field u16[] = {
        {"seuil_eco2_bon", &a.levels.eco2_thresholds[0], 1, 65535, "ppm, 1 à 65535"},
        {"seuil_eco2_moyen", &a.levels.eco2_thresholds[1], 1, 65535, "ppm, 1 à 65535"},
        {"seuil_eco2_mauvais", &a.levels.eco2_thresholds[2], 1, 65535, "ppm, 1 à 65535"},
        {"seuil_eco2_tres_mauvais", &a.levels.eco2_thresholds[3], 1, 65535, "ppm, 1 à 65535"},
        {"seuil_tvoc_bon", &a.levels.tvoc_thresholds[0], 1, 65535, "ppb, 1 à 65535"},
        {"seuil_tvoc_moyen", &a.levels.tvoc_thresholds[1], 1, 65535, "ppb, 1 à 65535"},
        {"seuil_tvoc_mauvais", &a.levels.tvoc_thresholds[2], 1, 65535, "ppb, 1 à 65535"},
        {"seuil_tvoc_tres_mauvais", &a.levels.tvoc_thresholds[3], 1, 65535, "ppb, 1 à 65535"},
        {"hysteresis_eco2", &a.levels.hysteresis_eco2, 0, 65535, "ppm, 0 à 65535"},
        {"hysteresis_tvoc", &a.levels.hysteresis_tvoc, 0, 65535, "ppb, 0 à 65535"},
        {"confirmation_s", &a.levels.confirmation_s, 0, air_quality::kConfirmationMaxS, "de 0 à 3600 s"},
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
    if ((r = fetch(body, len, "logo_air", v, sizeof(v))) < 0) {
        return fail(err, "logo_air", kBadEncoding);
    }
    if ((r > 0) && !air_logo_from_name(v, a.logo)) {
        return fail(err, "logo_air", "Aucun, ZoneIcone ou Scene");
    }
    if ((r = fetch(body, len, "decalage_temperature", v, sizeof(v))) < 0) {
        return fail(err, "decalage_temperature", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, -air_quality::kOffsetMaxCenti, air_quality::kOffsetMaxCenti, i)) {
            return fail(err, "decalage_temperature", "centièmes de degré, -1000 à 1000");
        }
        a.temperature_offset_centi = static_cast<int16_t>(i);
    }
    if (!air_quality::is_valid(a.levels)) {
        return fail(err, "seuil_eco2_bon", "seuils strictement croissants, hystérésis plus petite que l'écart entre deux seuils");
    }
    if (!is_valid(a)) {
        return fail(err, "", "réglages incohérents");
    }
    out = a;
    return true;
}


// ------------------------------------------------------------------ Home Assistant forms

namespace {

/// Copies a bounded text field; false (with err) if present but malformed or too long.
bool text_field(const char* body, std::size_t len, const char* name, char* dst, std::size_t dst_size, bool& present,
                FormError& err) noexcept {
    char v[kFieldMax];
    const int8_t r = fetch(body, len, name, v, sizeof(v));
    present = r > 0;
    if (r < 0) {
        return fail(err, name, kBadEncoding);
    }
    if (r > 0) {
        if (std::strlen(v) >= dst_size) {
            return fail(err, name, "trop long");
        }
        std::strncpy(dst, v, dst_size - 1U);
        dst[dst_size - 1U] = '\0';
    }
    return true;
}

}  // namespace

bool apply_ha_form(const char* body, std::size_t len, const HaSettings& current, HaSettings& out,
                   FormError& err) noexcept {
    HaSettings h = current;
    char v[kFieldMax];
    int8_t r = 0;
    int32_t i = 0;
    bool present = false;

    if ((r = fetch(body, len, "actif", v, sizeof(v))) < 0) {
        return fail(err, "actif", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, h.enabled)) {
        return fail(err, "actif", "0 ou 1");
    }
    if ((r = fetch(body, len, "securise", v, sizeof(v))) < 0) {
        return fail(err, "securise", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, h.secure)) {
        return fail(err, "securise", "0 ou 1");
    }
    if (!text_field(body, len, "hote", h.host, sizeof(h.host), present, err)) {
        return false;
    }
    if (present && (h.host[0] != '\0') && !is_valid_host(h.host)) {
        return fail(err, "hote", "adresse IP ou nom DNS, lettres, chiffres, points et tirets");
    }
    if ((r = fetch(body, len, "port", v, sizeof(v))) < 0) {
        return fail(err, "port", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, 1, 65535, i)) {
            return fail(err, "port", "de 1 à 65535");
        }
        h.port = static_cast<uint16_t>(i);
    }
    // Token: a 512-character field needs its own buffer; empty = keep the current one.
    {
        char token[homeassistant::kTokenMax + 2U];
        std::size_t n = 0U;
        if (network::form_field(body, len, "jeton", token, sizeof(token), n)) {
            if (n > 0U) {
                for (std::size_t k = 0U; k < n; ++k) {
                    if ((token[k] < '!') || (token[k] > '~')) {
                        return fail(err, "jeton", "caractères ASCII imprimables sans espace");
                    }
                }
                std::strncpy(h.token, token, homeassistant::kTokenMax);
                h.token[homeassistant::kTokenMax] = '\0';
            }
        } else {
            // Absent (kept), or present but too long / malformed.
            char probe[8];
            if (fetch(body, len, "jeton", probe, sizeof(probe)) < 0) {
                // fetch says malformed when the value exceeds its small buffer too: tell them apart.
                std::size_t m = 0U;
                static char big[homeassistant::kTokenMax + 2U];
                if (!network::form_field(body, len, "jeton", big, sizeof(big), m)) {
                    return fail(err, "jeton", "jeton trop long (512 caractères) ou mal encodé");
                }
            }
        }
    }
    if ((r = fetch(body, len, "effacer_jeton", v, sizeof(v))) < 0) {
        return fail(err, "effacer_jeton", kBadEncoding);
    }
    bool clear_token = false;
    if ((r > 0) && !parse_bool(v, clear_token)) {
        return fail(err, "effacer_jeton", "0 ou 1");
    }
    if (clear_token) {
        h.token[0] = '\0';
    }
    if ((r = fetch(body, len, "notifications", v, sizeof(v))) < 0) {
        return fail(err, "notifications", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, h.notifications)) {
        return fail(err, "notifications", "0 ou 1");
    }
    if ((r = fetch(body, len, "ping_s", v, sizeof(v))) < 0) {
        return fail(err, "ping_s", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, homeassistant::kPingMinS, homeassistant::kPingMaxS, i)) {
            return fail(err, "ping_s", "de 10 à 300 s");
        }
        h.ping_s = static_cast<uint16_t>(i);
    }
    if ((r = fetch(body, len, "pong_ms", v, sizeof(v))) < 0) {
        return fail(err, "pong_ms", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, homeassistant::kPongMinMs, homeassistant::kPongMaxMs, i)) {
            return fail(err, "pong_ms", "de 2000 à 30000 ms");
        }
        h.pong_ms = static_cast<uint16_t>(i);
    }
    if ((r = fetch(body, len, "meteo_actif", v, sizeof(v))) < 0) {
        return fail(err, "meteo_actif", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, h.weather.enabled)) {
        return fail(err, "meteo_actif", "0 ou 1");
    }
    if ((r = fetch(body, len, "latitude", v, sizeof(v))) < 0) {
        return fail(err, "latitude", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, -homeassistant::kLatitudeMax, homeassistant::kLatitudeMax, i)) {
            return fail(err, "latitude", "de -90 à 90 degrés (dix-millièmes)");
        }
        h.weather.latitude_e4 = i;
    }
    if ((r = fetch(body, len, "longitude", v, sizeof(v))) < 0) {
        return fail(err, "longitude", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, -homeassistant::kLongitudeMax, homeassistant::kLongitudeMax, i)) {
            return fail(err, "longitude", "de -180 à 180 degrés (dix-millièmes)");
        }
        h.weather.longitude_e4 = i;
    }
    if ((r = fetch(body, len, "meteo_periode_s", v, sizeof(v))) < 0) {
        return fail(err, "meteo_periode_s", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, homeassistant::kWeatherPeriodMinS, homeassistant::kWeatherPeriodMaxS, i)) {
            return fail(err, "meteo_periode_s", "de 300 à 3600 s");
        }
        h.weather.period_s = static_cast<uint16_t>(i);
    }
    if (h.enabled && (h.host[0] == '\0')) {
        return fail(err, "hote", "une liaison active a besoin d'une adresse");
    }
    if (h.enabled && (h.token[0] == '\0')) {
        return fail(err, "jeton", "une liaison active a besoin d'un jeton");
    }
    if (!is_valid(h)) {
        return fail(err, "", "réglages incohérents");
    }
    out = h;
    return true;
}

bool apply_ha_value_form(const char* body, std::size_t len, std::size_t index, const HaSettings& current,
                         HaSettings& out, FormError& err) noexcept {
    if (index >= homeassistant::kValuesMax) {
        return fail(err, "", "index de valeur hors de 0..7");
    }
    HaSettings h = current;
    homeassistant::ValueConfig& val = h.values[index];
    char v[kFieldMax];
    int8_t r = 0;
    int32_t i = 0;
    bool present = false;

    if ((r = fetch(body, len, "actif", v, sizeof(v))) < 0) {
        return fail(err, "actif", kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, val.enabled)) {
        return fail(err, "actif", "0 ou 1");
    }
    if (!text_field(body, len, "nom", val.name, sizeof(val.name), present, err)) {
        return false;
    }
    if ((r = fetch(body, len, "mode", v, sizeof(v))) < 0) {
        return fail(err, "mode", kBadEncoding);
    }
    if ((r > 0) && !homeassistant::value_mode_from_name(v, val.mode)) {
        return fail(err, "mode", "Entite ou Gabarit");
    }
    if (!text_field(body, len, "entite", val.entity, sizeof(val.entity), present, err)) {
        return false;
    }
    if (present && (val.entity[0] != '\0') && !homeassistant::is_valid_entity(val.entity)) {
        return fail(err, "entite", "domaine.nom en minuscules, chiffres et _ (ex. sensor.temperature)");
    }
    if (!text_field(body, len, "attribut", val.attribute, sizeof(val.attribute), present, err)) {
        return false;
    }
    if (!text_field(body, len, "gabarit", val.tmpl, sizeof(val.tmpl), present, err)) {
        return false;
    }
    if (present && (val.tmpl[0] != '\0') && !homeassistant::is_valid_template(val.tmpl)) {
        return fail(err, "gabarit", "ASCII imprimable, 128 caractères");
    }
    if ((r = fetch(body, len, "type", v, sizeof(v))) < 0) {
        return fail(err, "type", kBadEncoding);
    }
    if ((r > 0) && !homeassistant::value_type_from_name(v, val.type)) {
        return fail(err, "type", "Nombre, Texte ou Booleen");
    }
    if ((r = fetch(body, len, "decimales", v, sizeof(v))) < 0) {
        return fail(err, "decimales", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, 0, homeassistant::kDecimalsMax, i)) {
            return fail(err, "decimales", "de 0 à 2");
        }
        val.decimals = static_cast<uint8_t>(i);
    }
    if (!text_field(body, len, "unite", val.unit, sizeof(val.unit), present, err)) {
        return false;
    }
    if ((r = fetch(body, len, "icone", v, sizeof(v))) < 0) {
        return fail(err, "icone", kBadEncoding);
    }
    if ((r > 0) && !homeassistant::value_icon_from_name(v, val.icon)) {
        return fail(err, "icone", "icône inconnue");
    }
    if ((r = fetch(body, len, "peremption_s", v, sizeof(v))) < 0) {
        return fail(err, "peremption_s", kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, homeassistant::kExpiryMinS, homeassistant::kExpiryMaxS, i)) {
            return fail(err, "peremption_s", "de 30 à 3600 s");
        }
        val.expiry_s = static_cast<uint16_t>(i);
    }
    if (!homeassistant::is_valid(val)) {
        return fail(err, val.mode == homeassistant::ValueMode::Template ? "gabarit" : "entite",
                    "une valeur active a besoin d'une entité (ou d'un gabarit) valide");
    }
    if (!is_valid(h)) {
        return fail(err, "", "réglages incohérents");
    }
    out = h;
    return true;
}

// ------------------------------------------------------------------ MQTT form

bool apply_mqtt_form(const char* body, std::size_t len, const homeassistant::MqttConfig& current,
                     homeassistant::MqttConfig& out, FormError& err) noexcept {
    homeassistant::MqttConfig m = current;
    char v[kFieldMax];
    int8_t r = 0;
    int32_t i = 0;
    bool present = false;

    if ((r = fetch(body, len, kMqttFields[0], v, sizeof(v))) < 0) {
        return fail(err, kMqttFields[0], kBadEncoding);
    }
    if ((r > 0) && !parse_bool(v, m.enabled)) {
        return fail(err, kMqttFields[0], "0 ou 1");
    }
    if (!text_field(body, len, kMqttFields[1], m.host, sizeof(m.host), present, err)) {
        return false;
    }
    if ((r = fetch(body, len, kMqttFields[2], v, sizeof(v))) < 0) {
        return fail(err, kMqttFields[2], kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, 1, 65535, i)) {
            return fail(err, kMqttFields[2], "de 1 à 65535");
        }
        m.port = static_cast<uint16_t>(i);
    }
    if (!text_field(body, len, kMqttFields[3], m.user, sizeof(m.user), present, err)) {
        return false;
    }
    // Password: empty = keep the current one.
    char password[homeassistant::kMqttPasswordMax + 1U];
    password[0] = '\0';
    if (!text_field(body, len, kMqttFields[4], password, sizeof(password), present, err)) {
        return false;
    }
    if (present && (password[0] != '\0')) {
        std::strncpy(m.password, password, sizeof(m.password) - 1U);
        m.password[sizeof(m.password) - 1U] = '\0';
    }
    if ((r = fetch(body, len, kMqttFields[5], v, sizeof(v))) < 0) {
        return fail(err, kMqttFields[5], kBadEncoding);
    }
    bool clear_password = false;
    if ((r > 0) && !parse_bool(v, clear_password)) {
        return fail(err, kMqttFields[5], "0 ou 1");
    }
    if (clear_password) {
        m.password[0] = '\0';
    }
    char prefix[homeassistant::kMqttPrefixMax + 1U];
    prefix[0] = '\0';
    if (!text_field(body, len, kMqttFields[6], prefix, sizeof(prefix), present, err)) {
        return false;
    }
    if (present) {
        std::strncpy(m.prefix, (prefix[0] != '\0') ? prefix : homeassistant::kMqttPrefixDefault, sizeof(m.prefix) - 1U);
        m.prefix[sizeof(m.prefix) - 1U] = '\0';
    }
    if ((r = fetch(body, len, kMqttFields[7], v, sizeof(v))) < 0) {
        return fail(err, kMqttFields[7], kBadEncoding);
    }
    if (r > 0) {
        if (!parse_int(v, homeassistant::kMqttPeriodMinS, homeassistant::kMqttPeriodMaxS, i)) {
            return fail(err, kMqttFields[7], "de 5 à 600 s");
        }
        m.period_s = static_cast<uint16_t>(i);
    }
    // One validation, one reason, named after the field at fault.
    const homeassistant::MqttConfigStatus st = homeassistant::validate(m);
    if (st != homeassistant::MqttConfigStatus::Ok) {
        const char* field = "";
        switch (st) {
            case homeassistant::MqttConfigStatus::HostEmpty:
            case homeassistant::MqttConfigStatus::HostInvalid: field = kMqttFields[1]; break;
            case homeassistant::MqttConfigStatus::PortInvalid: field = kMqttFields[2]; break;
            case homeassistant::MqttConfigStatus::UserInvalid: field = kMqttFields[3]; break;
            case homeassistant::MqttConfigStatus::PasswordInvalid:
            case homeassistant::MqttConfigStatus::PasswordWithoutUser: field = kMqttFields[4]; break;
            case homeassistant::MqttConfigStatus::PrefixInvalid: field = kMqttFields[6]; break;
            case homeassistant::MqttConfigStatus::PeriodInvalid: field = kMqttFields[7]; break;
            case homeassistant::MqttConfigStatus::Ok:
            default: break;
        }
        return fail(err, field, homeassistant::mqtt_status_reason(st));
    }
    out = m;
    return true;
}

}  // namespace settings
