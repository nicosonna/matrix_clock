/**
 * @file json.cpp
 * @brief Model and values JSON.
 */
#include "settings/json.hpp"

#include "settings/forms.hpp"
#include "settings/json_writer.hpp"
#include "timekeeping/tz.hpp"

namespace settings {

namespace {

/// Common head: `"name":{"type":..,"libelle":..`
bool head(JsonWriter& w, const char* name, const char* type, const char* label) noexcept {
    return w.key(name) && w.begin_object() && w.field("type", type) && w.field("libelle", label);
}

/// `"name":{"type":"entier","libelle":..,"unite":..,"min":..,"max":..,"defaut":..,"aide":".."}`
bool int_field(JsonWriter& w, const char* name, const char* label, const char* unit, int64_t lo, int64_t hi, int64_t def,
               const char* help) noexcept {
    return head(w, name, "entier", label) && w.field("unite", unit) && w.field("min", lo) && w.field("max", hi) &&
           w.field("defaut", def) && w.field("aide", help) && w.end_object();
}

/// Minutes since midnight, shown as HH:MM by the page.
bool time_field(JsonWriter& w, const char* name, const char* label, int64_t def, const char* help) noexcept {
    return head(w, name, "heure", label) && w.field("min", static_cast<int64_t>(0)) &&
           w.field("max", static_cast<int64_t>(brightness::kMinutesPerDay - 1U)) && w.field("defaut", def) &&
           w.field("aide", help) && w.end_object();
}

bool bool_field(JsonWriter& w, const char* name, const char* label, bool def, const char* help) noexcept {
    return head(w, name, "booleen", label) && w.field("defaut", def) && w.field("aide", help) && w.end_object();
}

bool choice_field(JsonWriter& w, const char* name, const char* label, const char* const* values, std::size_t count,
                  const char* def, const char* help) noexcept {
    if (!(head(w, name, "choix", label) && w.key("valeurs") && w.begin_array())) {
        return false;
    }
    for (std::size_t i = 0U; i < count; ++i) {
        if (!w.str(values[i])) {
            return false;
        }
    }
    return w.end_array() && w.field("defaut", def) && w.field("aide", help) && w.end_object();
}

/// `type` is "texte", or a structured text the page edits with its own widget ("scenes", "courbe").
bool text_field(JsonWriter& w, const char* name, const char* type, const char* label, int64_t max_len, const char* def,
                const char* help) noexcept {
    return head(w, name, type, label) && w.field("max", max_len) && w.field("defaut", def) && w.field("aide", help) &&
           w.end_object();
}

std::size_t finish(JsonWriter& w) noexcept {
    return w.ok() ? w.length() : 0U;
}

}  // namespace

std::size_t display_model_json(char* out, std::size_t out_size) noexcept {
    JsonWriter w(out, out_size);
    const DisplaySettings d = default_display_settings();
    char scenes[256];
    (void)scenes_to_text(d.scenes, scenes, sizeof(scenes));
    const char* const axes[] = {"Colonne", "Ligne"};
    const char* const origins[] = {"HautGauche", "HautDroite", "BasGauche", "BasDroite"};
    const char* const widths[] = {"0", "8"};
    const char* const kinds[] = {"Heure", "Date", "Valeur", "DeuxLignes", "Defilement", "Nuit", "Test"};
    bool ok = w.begin_object() && w.key("champs") && w.begin_object();
    ok = ok && choice_field(w, "axe_premier", "Premier axe du ruban", axes, 2U, axis_name(d.map.first_axis),
                            "Sens du ruban depuis l'origine : colonnes de 8 ou rangées de 32.");
    ok = ok && bool_field(w, "serpentin", "Serpentin", d.map.serpentine, "Une ligne sur deux parcourue à l'envers.");
    ok = ok && choice_field(w, "origine", "Origine (LED 0)", origins, 4U, origin_name(d.map.origin), "Coin où se trouve la LED 0.");
    ok = ok && choice_field(w, "largeur_module", "Largeur des modules", widths, 2U, (d.map.module_width == 0U) ? "0" : "8",
                            "0 = dalle entière, 8 = quatre modules 8×8 chaînés.");
    ok = ok && int_field(w, "budget_ma", "Budget de courant", "mA", display::kMinBudgetMa, display::kMaxBudgetMa, d.budget_ma,
                         "Courant maximal de la dalle. L'image est écrêtée pour ne jamais le dépasser.");
    ok = ok && bool_field(w, "clignotement", "Deux-points clignotants", d.colon_blink, "Deux-points clignotant à 1 Hz.");
    ok = ok && bool_field(w, "barre_secondes", "Barre des secondes", d.seconds_bar, "Barre des secondes sur la dernière ligne.");
    ok = ok && int_field(w, "vitesse_defilement", "Vitesse de défilement", "px/s", display::kScrollSpeedMin, display::kScrollSpeedMax, d.scroll_speed_px_s,
                         "Vitesse des textes défilants, pixels par seconde.");
    ok = ok && text_field(w, "scenes", "scenes", "Rotation des scènes", 255, scenes,
                          "Scènes affichées tour à tour, avec leur durée en secondes (0 = jusqu'à la fin du texte).");
    ok = ok && w.end_object();
    ok = ok && w.key("types_scene") && w.begin_array();
    for (const char* k : kinds) {
        ok = ok && w.str(k);
    }
    ok = ok && w.end_array() && w.end_object();
    return finish(w);
}

std::size_t brightness_model_json(char* out, std::size_t out_size) noexcept {
    JsonWriter w(out, out_size);
    const brightness::Config c = brightness::default_config();
    char curve[128];
    (void)curve_to_text(c.curve, curve, sizeof(curve));
    const char* const modes[] = {"Auto", "Manuel", "Eteint"};
    bool ok = w.begin_object() && w.key("champs") && w.begin_object();
    ok = ok && choice_field(w, "mode", "Mode", modes, 3U, brightness_mode_name(c.mode),
                            "Auto suit le capteur ; Manuel fixe le niveau ; Eteint coupe la dalle.");
    ok = ok && int_field(w, "lum_manuelle", "Niveau manuel", "/255", 0, 255, c.manual_level, "Niveau en mode Manuel.");
    ok = ok && int_field(w, "lum_min", "Niveau minimal (nuit)", "/255", 0, 255, c.min_level, "Plancher de nuit (0 = extinction totale autorisée).");
    ok = ok && int_field(w, "lum_max", "Niveau maximal (jour)", "/255", 0, 255, c.max_level, "Plafond de jour.");
    ok = ok && int_field(w, "pas_lum", "Pas de luminosité", "niveaux", brightness::kStepMin, brightness::kStepMax, c.step,
                         "Zone morte : le niveau ne bouge que par pas entiers.");
    ok = ok && int_field(w, "montee_s", "Temps de montée", "s", 1, brightness::kRiseMax, c.rise_s, "Constante de temps vers plus clair.");
    ok = ok && int_field(w, "descente_s", "Temps de descente", "s", 1, brightness::kFallMax, c.fall_s,
                         "Constante de temps vers plus sombre (lente : une ombre qui passe ne compte pas).");
    ok = ok && int_field(w, "compensation_counts", "Auto-éclairage du capteur", "comptes", -brightness::kCompensationMax, brightness::kCompensationMax,
                         c.compensation_counts, "Comptes ADC que la dalle ajoute au capteur à pleine luminosité (wizard).");
    ok = ok && bool_field(w, "extinction_active", "Extinction programmée", c.off_schedule, "Éteindre la dalle sur une plage horaire.");
    ok = ok && time_field(w, "extinction_debut", "Début de l'extinction", c.off_start_min,
                          "Heure à laquelle la dalle s'éteint (heure locale).");
    ok = ok && time_field(w, "extinction_fin", "Fin de l'extinction", c.off_end_min,
                          "Heure à laquelle la dalle se rallume ; peut être le lendemain.");
    ok = ok && text_field(w, "courbe", "courbe", "Courbe mesure → niveau", 127, curve,
                          "Niveau de la dalle pour chaque mesure du capteur (2 à 6 points, mesures croissantes). Le wizard la remplit.");
    ok = ok && w.end_object() && w.end_object();
    return finish(w);
}

std::size_t system_model_json(char* out, std::size_t out_size) noexcept {
    JsonWriter w(out, out_size);
    const SystemSettings s = default_system_settings();
    std::size_t count = 0U;
    const timekeeping::NamedZone* zones = timekeeping::named_zones(count);
    bool ok = w.begin_object() && w.key("champs") && w.begin_object();
    ok = ok && head(w, "fuseau", "choix", "Fuseau horaire") && w.key("valeurs") && w.begin_array();
    for (std::size_t i = 0U; ok && (i < count); ++i) {
        ok = w.str(zones[i].name);
    }
    ok = ok && w.end_array() && w.field("defaut", s.timezone) &&
         w.field("aide", "Fuseau horaire : l'heure d'été est automatique. Appliqué sans redémarrage.") && w.end_object();
    ok = ok && text_field(w, "serveur_ntp", "texte", "Serveur de temps (NTP)", static_cast<int64_t>(kHostMax), s.ntp_server,
                          "Serveur de temps. Pris en compte au prochain démarrage.");
    ok = ok && w.end_object() && w.end_object();
    return finish(w);
}

std::size_t display_json(const DisplaySettings& s, char* out, std::size_t out_size) noexcept {
    JsonWriter w(out, out_size);
    char scenes[256];
    (void)scenes_to_text(s.scenes, scenes, sizeof(scenes));
    bool ok = w.begin_object();
    ok = ok && w.field("axe_premier", axis_name(s.map.first_axis)) && w.field("serpentin", s.map.serpentine) &&
         w.field("origine", origin_name(s.map.origin)) && w.field("largeur_module", static_cast<int64_t>(s.map.module_width)) &&
         w.field("budget_ma", static_cast<int64_t>(s.budget_ma)) && w.field("clignotement", s.colon_blink) &&
         w.field("barre_secondes", s.seconds_bar) && w.field("vitesse_defilement", static_cast<int64_t>(s.scroll_speed_px_s)) &&
         w.field("scenes", scenes) && w.end_object();
    return finish(w);
}

std::size_t brightness_json(const brightness::Config& c, char* out, std::size_t out_size) noexcept {
    JsonWriter w(out, out_size);
    char curve[128];
    (void)curve_to_text(c.curve, curve, sizeof(curve));
    bool ok = w.begin_object();
    ok = ok && w.field("mode", brightness_mode_name(c.mode)) && w.field("lum_manuelle", static_cast<int64_t>(c.manual_level)) &&
         w.field("lum_min", static_cast<int64_t>(c.min_level)) && w.field("lum_max", static_cast<int64_t>(c.max_level)) &&
         w.field("pas_lum", static_cast<int64_t>(c.step)) && w.field("montee_s", static_cast<int64_t>(c.rise_s)) &&
         w.field("descente_s", static_cast<int64_t>(c.fall_s)) &&
         w.field("compensation_counts", static_cast<int64_t>(c.compensation_counts)) &&
         w.field("extinction_active", c.off_schedule) && w.field("extinction_debut", static_cast<int64_t>(c.off_start_min)) &&
         w.field("extinction_fin", static_cast<int64_t>(c.off_end_min)) && w.field("courbe", curve) && w.end_object();
    return finish(w);
}

std::size_t system_json(const SystemSettings& s, char* out, std::size_t out_size) noexcept {
    JsonWriter w(out, out_size);
    const bool ok = w.begin_object() && w.field("fuseau", s.timezone) && w.field("serveur_ntp", s.ntp_server) && w.end_object();
    (void)ok;
    return finish(w);
}

}  // namespace settings
