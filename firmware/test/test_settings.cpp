/**
 * @file test_settings.cpp
 * @brief Host tests of the pure `settings` component (WEB-01 .. WEB-05).
 *
 * WEB-04 reads the embedded page (CLOCK_WWW) and checks that every field of
 * every model appears in it, and that every `name="..."` input of the page is a
 * model field: no orphan on either side (the wizard rule of CLAUDE.md).
 */
#include "test_support.hpp"

#include <cstdio>
#include <cstring>

#include "settings/forms.hpp"
#include "settings/json.hpp"
#include "settings/json_writer.hpp"
#include "settings/model.hpp"

namespace {

using settings::DisplaySettings;
using settings::FormError;
using settings::SystemSettings;

std::size_t slen(const char* s) { return std::strlen(s); }

// --------------------------------------------------------------------- WEB-05

void test_json_writer() {
    char buf[128];
    settings::JsonWriter w(buf, sizeof(buf));
    CHECK(w.begin_object());
    CHECK(w.field("a", static_cast<int64_t>(-5)));
    CHECK(w.field("b", "x\"y"));
    CHECK(w.field("c", true));
    CHECK(w.key("d") && w.begin_array() && w.integer(1) && w.integer(2) && w.end_array());
    {
        char small[8];
        settings::JsonWriter t(small, sizeof(small));
        CHECK(t.begin_object());
        CHECK(!t.field("abcdef", "ghijkl"));  // overflow
        CHECK(!t.end_object());
        CHECK_EQ(t.length(), 0);
    }
    {
        char small[64];
        settings::JsonWriter t(small, sizeof(small));
        CHECK(!t.end_object());                 // nothing open
        CHECK(!t.end_array());
        CHECK(!t.key(nullptr));
        CHECK(!t.str(nullptr));
        CHECK(!t.raw(nullptr));
        settings::JsonWriter deep(small, sizeof(small));
        bool ok = true;
        for (uint32_t i = 0U; i < 9U; ++i) {
            ok = deep.begin_array() && ok;
        }
        CHECK(!ok);                               // depth 8 exceeded
        settings::JsonWriter none(nullptr, 0U);
        CHECK(!none.begin_object());
        CHECK_EQ(none.length(), 0);
    }
    CHECK(w.key("e") && w.begin_object() && w.end_object());
    CHECK(w.end_object());
    CHECK(w.ok());
    CHECK(std::strcmp(buf, "{\"a\":-5,\"b\":\"x\\\"y\",\"c\":true,\"d\":[1,2],\"e\":{}}") == 0);
    CHECK_EQ(w.length(), std::strlen(buf));

    // Too small: ok() false, length 0, never a cut document presented as complete.
    char tiny[12];
    settings::JsonWriter t(tiny, sizeof(tiny));
    (void)t.begin_object();
    (void)t.field("longkeyname", "value");
    CHECK(!t.ok());
    CHECK_EQ(t.length(), 0);
    settings::JsonWriter n(nullptr, 0U);
    CHECK(!n.ok());
    // Unbalanced end: refused.
    settings::JsonWriter u(buf, sizeof(buf));
    CHECK(!u.end_object());
}

// --------------------------------------------------------------------- WEB-02/03

void test_display_form() {
    const DisplaySettings cur = settings::default_display_settings();
    DisplaySettings out = cur;
    FormError err{};
    // One bad field: refused, named, nothing written.
    out.budget_ma = 1234U;
    const char* bad = "budget_ma=300&origine=HautDroite";
    CHECK(!settings::apply_display_form(bad, slen(bad), cur, out, err));
    CHECK(std::strcmp(err.field, "budget_ma") == 0);
    CHECK(std::strstr(err.reason, "400") != nullptr);
    CHECK_EQ(out.budget_ma, 1234);  // untouched

    // Valid partial form: only the given fields change.
    const char* ok = "budget_ma=3000&origine=BasDroite&axe_premier=Ligne&serpentin=0&largeur_module=8&vitesse_defilement=30&clignotement=0&barre_secondes=1";
    CHECK(settings::apply_display_form(ok, slen(ok), cur, out, err));
    CHECK_EQ(out.budget_ma, 3000);
    CHECK_ENUM(out.map.origin, display::Origin::BottomRight);
    CHECK_ENUM(out.map.first_axis, display::FirstAxis::Row);
    CHECK(!out.map.serpentine);
    CHECK_EQ(out.map.module_width, 8);
    CHECK_EQ(out.scroll_speed_px_s, 30);
    CHECK(!out.colon_blink);
    CHECK(out.seconds_bar);
    CHECK_EQ(out.scenes.count, cur.scenes.count);  // kept

    // Scenes text, round trip.
    const char* sc = "scenes=Heure:10:1,Date:5:0,Valeur3:8:1,Defilement:0:1";
    CHECK(settings::apply_display_form(sc, slen(sc), cur, out, err));
    CHECK_EQ(out.scenes.count, 4);
    CHECK_ENUM(out.scenes.entries[2].kind, display::SceneKind::Value);
    CHECK_EQ(out.scenes.entries[2].value_index, 3);
    CHECK_EQ(out.scenes.entries[1].duration_s, 5);
    CHECK(!out.scenes.entries[1].enabled);
    CHECK_EQ(out.scenes.entries[3].duration_s, 0);
    char text[256];
    CHECK(settings::scenes_to_text(out.scenes, text, sizeof(text)) > 0U);
    CHECK(std::strcmp(text, "Heure:10:1,Date:5:0,Valeur3:8:1,Defilement:0:1") == 0);
    display::SceneList again{};
    CHECK(settings::scenes_from_text(text, again));
    CHECK_EQ(again.count, 4);

    // Bad scenes: unknown kind, index 8, 9 entries, trailing comma, bad number.
    const char* bads[] = {"scenes=Lune:5:1", "scenes=Valeur8:5:1", "scenes=Heure:5:1,Heure:5:1,Heure:5:1,Heure:5:1,Heure:5:1,Heure:5:1,Heure:5:1,Heure:5:1,Heure:5:1",
                          "scenes=Heure:5:1,", "scenes=Heure:x:1", "scenes=Heure:5", "scenes=Heure:5:2"};
    for (const char* b : bads) {
        CHECK(!settings::apply_display_form(b, slen(b), cur, out, err));
        CHECK(std::strcmp(err.field, "scenes") == 0);
    }
    const char* enc = "budget_ma=%ZZ";
    CHECK(!settings::apply_display_form(enc, slen(enc), cur, out, err));
    CHECK(std::strcmp(err.field, "budget_ma") == 0);
    const char* encs[] = {"axe_premier=%ZZ", "serpentin=%ZZ", "serpentin=2", "origine=%ZZ", "origine=Milieu",
                          "largeur_module=%ZZ", "clignotement=%ZZ", "clignotement=5", "barre_secondes=%ZZ",
                          "barre_secondes=7", "vitesse_defilement=%ZZ", "vitesse_defilement=100", "scenes=%ZZ",
                          "budget_ma=-", "budget_ma=1234567890", "budget_ma=12a"};
    for (const char* b : encs) {
        CHECK(!settings::apply_display_form(b, slen(b), cur, out, err));
        CHECK(err.field[0] != '\0');
    }
    const char* col = "axe_premier=Colonne&origine=BasDroite&serpentin=0";
    CHECK(settings::apply_display_form(col, slen(col), cur, out, err));
    CHECK(out.map.first_axis == display::FirstAxis::Column);
    CHECK(out.map.origin == display::Origin::BottomRight);
    CHECK(!out.map.serpentine);
    char tiny[4];
    CHECK_EQ(settings::scenes_to_text(out.scenes, tiny, sizeof(tiny)), 0);
    CHECK_EQ(settings::scenes_to_text(out.scenes, nullptr, 0U), 0);
    const char* w7 = "largeur_module=7";
    CHECK(!settings::apply_display_form(w7, slen(w7), cur, out, err));
    const char* ax = "axe_premier=Diagonale";
    CHECK(!settings::apply_display_form(ax, slen(ax), cur, out, err));
    CHECK(std::strcmp(err.field, "axe_premier") == 0);
    // Empty body: nothing changes, success.
    DisplaySettings same = cur;
    CHECK(settings::apply_display_form("", 0U, cur, same, err));
    CHECK_EQ(same.budget_ma, cur.budget_ma);
}

void test_brightness_form() {
    const brightness::Config cur = brightness::default_config();
    brightness::Config out = cur;
    FormError err{};
    const char* ok = "mode=Manuel&lum_manuelle=120&lum_min=10&lum_max=200&pas_lum=8&montee_s=5&descente_s=90&compensation_counts=-250&extinction_active=1&extinction_debut=1380&extinction_fin=390&courbe=100:10,2000:200";
    CHECK(settings::apply_brightness_form(ok, slen(ok), cur, out, err));
    CHECK_ENUM(out.mode, brightness::Mode::Manual);
    CHECK_EQ(out.manual_level, 120);
    CHECK_EQ(out.min_level, 10);
    CHECK_EQ(out.max_level, 200);
    CHECK_EQ(out.step, 8);
    CHECK_EQ(out.rise_s, 5);
    CHECK_EQ(out.fall_s, 90);
    CHECK_EQ(out.compensation_counts, -250);
    CHECK(out.off_schedule);
    CHECK_EQ(out.off_start_min, 1380);
    CHECK_EQ(out.off_end_min, 390);
    CHECK_EQ(out.curve.count, 2);
    CHECK_EQ(out.curve.points[1].raw, 2000);
    CHECK(brightness::is_valid(out));

    // All or nothing, with the field named.
    brightness::Config keep = cur;
    const char* bad = "lum_min=250&lum_max=100";
    CHECK(!settings::apply_brightness_form(bad, slen(bad), cur, keep, err));
    CHECK(std::strcmp(err.field, "lum_min") == 0);
    CHECK_EQ(keep.min_level, cur.min_level);
    const char* bads[] = {"pas_lum=0", "pas_lum=33", "montee_s=0", "descente_s=601", "compensation_counts=4096",
                          "extinction_debut=1440", "courbe=100:10", "courbe=100:10,100:20", "courbe=5000:1,6000:2",
                          "courbe=100:300,200:10", "mode=Nuit", "extinction_active=maybe", "lum_manuelle=256"};
    const char* encs[] = {"mode=%ZZ", "lum_manuelle=%ZZ", "lum_min=%ZZ", "montee_s=%ZZ", "compensation_counts=%ZZ",
                          "extinction_active=%ZZ", "courbe=%ZZ", "extinction_debut=%ZZ"};
    for (const char* b : encs) {
        CHECK(!settings::apply_brightness_form(b, slen(b), cur, keep, err));
        CHECK(err.field[0] != '\0');
    }
    char tiny[4];
    CHECK_EQ(settings::curve_to_text(cur.curve, tiny, sizeof(tiny)), 0);
    CHECK_EQ(settings::curve_to_text(cur.curve, nullptr, 0U), 0);
    for (const char* b : bads) {
        CHECK(!settings::apply_brightness_form(b, slen(b), cur, keep, err));
        CHECK(err.field[0] != '\0');
    }
    // Curve round trip including a falling curve (wiring b).
    const char* inv = "courbe=300:255,3500:8";
    CHECK(settings::apply_brightness_form(inv, slen(inv), cur, out, err));
    char text[128];
    CHECK(settings::curve_to_text(out.curve, text, sizeof(text)) > 0U);
    CHECK(std::strcmp(text, "300:255,3500:8") == 0);
}

void test_system_form() {
    const SystemSettings cur = settings::default_system_settings();
    CHECK(settings::is_valid(cur));
    CHECK(std::strcmp(cur.timezone, "Europe/Zurich") == 0);
    SystemSettings out = cur;
    FormError err{};
    const char* ok = "fuseau=America/New_York&serveur_ntp=time.cloudflare.com";
    CHECK(settings::apply_system_form(ok, slen(ok), cur, out, err));
    CHECK(std::strcmp(out.timezone, "America/New_York") == 0);
    CHECK(std::strcmp(out.ntp_server, "time.cloudflare.com") == 0);
    const char* ip = "serveur_ntp=10.10.0.1";
    CHECK(settings::apply_system_form(ip, slen(ip), cur, out, err));
    const char* encs[] = {"fuseau=%ZZ", "serveur_ntp=%ZZ"};
    for (const char* b : encs) {
        CHECK(!settings::apply_system_form(b, slen(b), cur, out, err));
    }
    const char* bads[] = {"fuseau=Mars/Olympus", "fuseau=", "serveur_ntp=", "serveur_ntp=-bad", "serveur_ntp=bad.",
                          "serveur_ntp=a b", "serveur_ntp=http://x"};
    for (const char* b : bads) {
        CHECK(!settings::apply_system_form(b, slen(b), cur, out, err));
    }
    char longhost[80];
    std::memset(longhost, 'a', 79U);
    longhost[79] = '\0';
    CHECK(!settings::is_valid_host(longhost));
    CHECK(settings::is_valid_host("a"));
    CHECK(!settings::is_valid_host(nullptr));
}

// --------------------------------------------------------------------- WEB-04

/// Reads the embedded page into a static buffer (host only).
const char* read_page() {
    static char page[65536];
    std::FILE* f = std::fopen(CLOCK_WWW "/app.html", "rb");
    if (f == nullptr) {
        page[0] = '\0';
        return page;
    }
    const std::size_t n = std::fread(page, 1U, sizeof(page) - 1U, f);
    page[n] = '\0';
    (void)std::fclose(f);
    return page;
}

/// The page builds the form of @p domain from the firmware model: it must carry
/// `data-domaine="<domain>"` and fetch `/api/<domain>/modele`, and no field of
/// that domain may be hard-coded as a static input (a name="..." attribute).
void check_domain_dynamic(const char* page, const char* const* fields, std::size_t count, const char* domain) {
    char needle[64];
    (void)std::snprintf(needle, sizeof(needle), "data-domaine=\"%s\"", domain);
    const bool form = std::strstr(page, needle) != nullptr;
    if (!form) {
        std::printf("  formulaire absent de la page : %s\n", domain);
    }
    CHECK(form);
    CHECK(std::strstr(page, "'/api/' + domaine + '/modele'") != nullptr);
    for (std::size_t i = 0U; i < count; ++i) {
        (void)std::snprintf(needle, sizeof(needle), "name=\"%s\"", fields[i]);
        const bool hard_coded = std::strstr(page, needle) != nullptr;
        if (hard_coded) {
            std::printf("  champ recopie a la main dans la page (%s) : %s\n", domain, fields[i]);
        }
        CHECK(!hard_coded);
    }
}

/// A key or value the page's JavaScript hard-codes (wizards): it must exist in the firmware.
struct PageLiteral {
    const char* literal;   ///< As written in the page.
    const char* needle;    ///< What the model JSON must contain.
};

bool is_model_field(const char* name) {
    const char* const* lists[] = {settings::kDisplayFields, settings::kBrightnessFields, settings::kSystemFields};
    const std::size_t counts[] = {settings::kDisplayFieldCount, settings::kBrightnessFieldCount, settings::kSystemFieldCount};
    const char* const extra[] = {"ssid", "password", "cle", "texte", "priorite", "duree_s", "confirme", "motif",
                                 "brut_nuit", "brut_nuit_allume", "brut_soir", "brut_jour", "firmware"};
    for (std::size_t l = 0U; l < 3U; ++l) {
        for (std::size_t i = 0U; i < counts[l]; ++i) {
            if (std::strcmp(lists[l][i], name) == 0) {
                return true;
            }
        }
    }
    for (const char* e : extra) {
        if (std::strcmp(e, name) == 0) {
            return true;
        }
    }
    return false;
}

void test_page_orphans() {
    const char* page = read_page();
    CHECK(page[0] != '\0');
    if (page[0] == '\0') {
        std::printf("  page introuvable : " CLOCK_WWW "/app.html\n");
        return;
    }
    check_domain_dynamic(page, settings::kDisplayFields, settings::kDisplayFieldCount, "affichage");
    check_domain_dynamic(page, settings::kBrightnessFields, settings::kBrightnessFieldCount, "luminosite");
    check_domain_dynamic(page, settings::kSystemFields, settings::kSystemFieldCount, "systeme");

    // Every name="..." left in the page (uploads, portal) is a known field.
    const char* p = page;
    while ((p = std::strstr(p, "name=\"")) != nullptr) {
        p += 6;
        char name[48];
        std::size_t n = 0U;
        while ((*p != '"') && (*p != '\0') && (n + 1U < sizeof(name))) {
            name[n++] = *p++;
        }
        name[n] = '\0';
        // <meta name="viewport"> is the only name= that is not a form field.
        const bool known = (std::strcmp(name, "viewport") == 0) || is_model_field(name);
        if (!known) {
            std::printf("  champ orphelin dans la page : %s\n", name);
        }
        CHECK(known);
    }

    // The page distributes the fields over its forms by name: every model field of the three domains
    // must appear in exactly one group list, and every listed name must be a model field.
    const char* const* lists[] = {settings::kDisplayFields, settings::kBrightnessFields};
    const std::size_t counts[] = {settings::kDisplayFieldCount, settings::kBrightnessFieldCount};
    const char* groups = std::strstr(page, "var groupes = {");
    CHECK(groups != nullptr);
    const char* groups_end = (groups != nullptr) ? std::strstr(groups, "};") : nullptr;
    CHECK(groups_end != nullptr);
    for (std::size_t l = 0U; l < 2U; ++l) {
        for (std::size_t i = 0U; i < counts[l]; ++i) {
            char needle[64];
            (void)std::snprintf(needle, sizeof(needle), "'%s'", lists[l][i]);
            uint32_t seen = 0U;
            for (const char* p2 = groups; (p2 = std::strstr(p2, needle)) != nullptr; ++p2) {
                if (p2 < groups_end) {
                    ++seen;
                }
            }
            if (seen != 1U) {
                std::printf("  champ %s : %lu fois dans les groupes de la page\n", lists[l][i], static_cast<unsigned long>(seen));
            }
            CHECK_EQ(seen, 1U);
        }
    }
    // Every type the model emits has a widget in the page, and every field carries a label.
    const char* const types[] = {"m.type === 'booleen'", "m.type === 'choix'", "m.type === 'heure'", "m.type === 'entier'",
                                 "m.type === 'scenes'", "m.type === 'courbe'"};
    for (const char* t : types) {
        CHECK(std::strstr(page, t) != nullptr);
    }
    CHECK(std::strstr(page, "type: 'time'") != nullptr);  // hours are edited as HH:MM, never as minutes
    CHECK(std::strstr(page, "lignes.splice(i - 1, 0") != nullptr);  // scenes can be reordered
    {
        static char m[settings::kJsonMax];
        CHECK(settings::brightness_model_json(m, sizeof(m)) > 0U);
        CHECK(std::strstr(m, "\"extinction_debut\":{\"type\":\"heure\"") != nullptr);
        CHECK(std::strstr(m, "\"courbe\":{\"type\":\"courbe\"") != nullptr);
        CHECK(std::strstr(m, "\"unite\":\"s\"") != nullptr);
        for (std::size_t i = 0U; i < settings::kBrightnessFieldCount; ++i) {
            char needle[96];
            (void)std::snprintf(needle, sizeof(needle), "\"%s\":{\"type\":\"", settings::kBrightnessFields[i]);
            const char* f = std::strstr(m, needle);
            CHECK(f != nullptr);
            CHECK((f != nullptr) && (std::strstr(f, "\"libelle\":\"") != nullptr));
        }
        CHECK(settings::display_model_json(m, sizeof(m)) > 0U);
        CHECK(std::strstr(m, "\"scenes\":{\"type\":\"scenes\"") != nullptr);
        for (std::size_t i = 0U; i < settings::kDisplayFieldCount; ++i) {
            char needle[96];
            (void)std::snprintf(needle, sizeof(needle), "\"%s\":{\"type\":\"", settings::kDisplayFields[i]);
            CHECK(std::strstr(m, needle) != nullptr);
        }
        CHECK(settings::system_model_json(m, sizeof(m)) > 0U);
        CHECK(std::strstr(m, "\"fuseau\":{\"type\":\"choix\",\"libelle\":") != nullptr);
    }
    // The mapping wizard hard-codes keys and choice values: each must be in the display model.
    static char model[settings::kJsonMax];
    CHECK(settings::display_model_json(model, sizeof(model)) > 0U);
    const PageLiteral mapping[] = {
        {"origine: 'HautGauche'", "\"origine\":{"},     {"'origine', 'HautDroite'", "\"HautDroite\""},
        {"'origine', 'BasGauche'", "\"BasGauche\""},    {"'origine', 'BasDroite'", "\"BasDroite\""},
        {"'axe_premier', 'Colonne'", "\"Colonne\""},    {"'axe_premier', 'Ligne'", "\"Ligne\""},
        {"'largeur_module', '0'", "\"largeur_module\":{"}, {"'serpentin', '0'", "\"serpentin\":{"},
        
    };
    for (const PageLiteral& l : mapping) {
        const bool in_page = std::strstr(page, l.literal) != nullptr;
        const bool in_model = std::strstr(model, l.needle) != nullptr;
        if (!in_page || !in_model) {
            std::printf("  wizard de correspondance : %s (page %d, modele %d)\n", l.literal, in_page ? 1 : 0, in_model ? 1 : 0);
        }
        CHECK(in_page);
        CHECK(in_model);
    }
    // The brightness wizard posts exactly the four readings the firmware expects.
    const char* const readings[] = {"'brut_nuit'", "'brut_nuit_allume'", "'brut_soir'", "'brut_jour'"};
    for (const char* r : readings) {
        CHECK(std::strstr(page, r) != nullptr);
    }
    CHECK(std::strstr(page, "post('/api/affichage/test', 'motif='") != nullptr);
    CHECK(std::strstr(page, "'/api/luminosite/wizard'") != nullptr);
    CHECK(std::strstr(page, "'/api/luminosite/brut'") != nullptr);
    CHECK(std::strstr(page, "'/api/systeme/usine', 'confirme=OUI'") != nullptr);
    CHECK(std::strstr(page, "'/api/ota'") != nullptr);
    // No external resource, no emoji (UI rules).
    CHECK(std::strstr(page, "https://") == nullptr);
    CHECK(std::strstr(page, "ð") == nullptr);

    // The model JSON names the same fields.
    static char json[settings::kJsonMax];
    CHECK(settings::display_model_json(json, sizeof(json)) > 0U);
    for (std::size_t i = 0U; i < settings::kDisplayFieldCount; ++i) {
        char needle[64];
        (void)std::snprintf(needle, sizeof(needle), "\"%s\":{", settings::kDisplayFields[i]);
        CHECK(std::strstr(json, needle) != nullptr);
    }
    CHECK(settings::brightness_model_json(json, sizeof(json)) > 0U);
    for (std::size_t i = 0U; i < settings::kBrightnessFieldCount; ++i) {
        char needle[64];
        (void)std::snprintf(needle, sizeof(needle), "\"%s\":{", settings::kBrightnessFields[i]);
        CHECK(std::strstr(json, needle) != nullptr);
    }
    CHECK(settings::system_model_json(json, sizeof(json)) > 0U);
    CHECK(std::strstr(json, "\"Europe/Zurich\"") != nullptr);
    CHECK(std::strstr(json, "\"serveur_ntp\":{") != nullptr);
    // Values documents reflect the settings and fit.
    const DisplaySettings d = settings::default_display_settings();
    CHECK(settings::display_json(d, json, sizeof(json)) > 0U);
    CHECK(std::strstr(json, "\"budget_ma\":2000") != nullptr);
    CHECK(settings::brightness_json(brightness::default_config(), json, sizeof(json)) > 0U);
    CHECK(std::strstr(json, "\"courbe\":\"300:8,900:40,2000:160,3600:255\"") != nullptr);
    CHECK(settings::system_json(settings::default_system_settings(), json, sizeof(json)) > 0U);
    char small[64];
    CHECK_EQ(settings::display_model_json(small, sizeof(small)), 0);

    {
        AllocGuard guard;
        DisplaySettings o = d;
        FormError e{};
        (void)settings::apply_display_form("budget_ma=2500", 14U, d, o, e);
        (void)settings::display_json(o, json, sizeof(json));
        (void)settings::brightness_model_json(json, sizeof(json));
    }
    CHECK_EQ(g_alloc_count, 0);
}

}  // namespace

void run_settings_tests() {
    test_json_writer();
    test_display_form();
    test_brightness_form();
    test_system_form();
    test_page_orphans();
}
