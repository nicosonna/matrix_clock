/**
 * @file test_alerts.cpp
 * @brief Host tests of the pure `alerts` component — for now the inbound
 *        notification decoder (ALR-15 / HA-13 / WEB-06 share it).
 */
#include "test_support.hpp"

#include <cstring>

#include "alerts/notification.hpp"

namespace {

using alerts::DecodeStatus;
using alerts::Notification;

std::size_t slen(const char* s) { return std::strlen(s); }

void test_key() {
    CHECK(alerts::is_valid_key("garage_ouvert"));
    CHECK(alerts::is_valid_key("a1"));
    CHECK(!alerts::is_valid_key(""));
    CHECK(!alerts::is_valid_key(nullptr));
    CHECK(!alerts::is_valid_key("Garage"));     // capitals
    CHECK(!alerts::is_valid_key("gar age"));
    CHECK(!alerts::is_valid_key("g-1"));
    CHECK(alerts::is_valid_key("abcdefghijklmnopqrstuvwx"));   // 24
    CHECK(!alerts::is_valid_key("abcdefghijklmnopqrstuvwxy"));  // 25
}

void test_decode_full() {
    Notification n{};
    const char* body = "cle=garage&titre=GARAGE&texte=Porte+ouverte+depuis+10+min&priorite=2&duree_s=60&rappel_s=120&persistante=1&effacer=0";
    CHECK_ENUM(alerts::decode_form(body, slen(body), n), DecodeStatus::Ok);
    CHECK(std::strcmp(n.key, "garage") == 0);
    CHECK(std::strcmp(n.title, "GARAGE") == 0);
    CHECK(std::strcmp(n.text, "Porte ouverte depuis 10 min") == 0);
    CHECK_EQ(n.priority, 2);
    CHECK_EQ(n.duration_s, 60);
    CHECK_EQ(n.repeat_s, 120);
    CHECK(n.persistent);
    CHECK(!n.clear);
    CHECK_EQ(n.defaults_used, 0);
}

void test_decode_defaults_and_bounds() {
    Notification n{};
    // Only the key: every other field takes its default.
    const char* minimal = "cle=x";
    CHECK_ENUM(alerts::decode_form(minimal, slen(minimal), n), DecodeStatus::Ok);
    CHECK_EQ(n.priority, alerts::kPriorityDefault);
    CHECK_EQ(n.duration_s, alerts::kDurationDefault);
    CHECK_EQ(n.repeat_s, alerts::kRepeatDefault);
    CHECK(!n.persistent);
    CHECK(!n.clear);
    CHECK_EQ(n.text[0], 0);
    CHECK_EQ(n.defaults_used, 0);

    // Out of bounds: replaced by the default and counted.
    const char* oob = "cle=x&priorite=9&duree_s=0&rappel_s=5&persistante=peut-etre&effacer=2";
    CHECK_ENUM(alerts::decode_form(oob, slen(oob), n), DecodeStatus::Ok);
    CHECK_EQ(n.priority, alerts::kPriorityDefault);
    CHECK_EQ(n.duration_s, alerts::kDurationDefault);
    CHECK_EQ(n.repeat_s, alerts::kRepeatDefault);
    CHECK(!n.persistent);
    CHECK(!n.clear);
    CHECK_EQ(n.defaults_used, 5);

    // Non-numeric and huge numbers.
    const char* bad = "cle=x&priorite=abc&duree_s=99999999";
    CHECK_ENUM(alerts::decode_form(bad, slen(bad), n), DecodeStatus::Ok);
    CHECK_EQ(n.defaults_used, 2);

    // Boolean spellings.
    const char* bools = "cle=x&persistante=oui&effacer=true";
    CHECK_ENUM(alerts::decode_form(bools, slen(bools), n), DecodeStatus::Ok);
    CHECK(n.persistent);
    CHECK(n.clear);
    const char* offs = "cle=x&persistante=non&effacer=";
    CHECK_ENUM(alerts::decode_form(offs, slen(offs), n), DecodeStatus::Ok);
    CHECK(!n.persistent);
    CHECK(!n.clear);
    CHECK_EQ(n.defaults_used, 0);

    // Text truncated to kTextMax, title to kTitleMax (counted).
    char longbody[400];
    std::strcpy(longbody, "cle=x&titre=ABCDEFGHIJKLMNOPQRSTUVWXYZ&texte=");
    const std::size_t prefix = std::strlen(longbody);
    for (std::size_t i = prefix; i < prefix + 200U; ++i) {
        longbody[i] = 'T';
    }
    longbody[prefix + 200U] = '\0';
    CHECK_ENUM(alerts::decode_form(longbody, slen(longbody), n), DecodeStatus::Ok);
    CHECK_EQ(std::strlen(n.text), alerts::kTextMax);
    CHECK_EQ(std::strlen(n.title), alerts::kTitleMax);
    CHECK_EQ(n.defaults_used, 2);  // both truncations counted
}

void test_decode_rejections() {
    Notification n{};
    const char* nokey = "texte=hello&priorite=1";
    CHECK_ENUM(alerts::decode_form(nokey, slen(nokey), n), DecodeStatus::KeyMissing);
    const char* empty = "cle=&texte=hello";
    CHECK_ENUM(alerts::decode_form(empty, slen(empty), n), DecodeStatus::KeyInvalid);
    const char* caps = "cle=Garage";
    CHECK_ENUM(alerts::decode_form(caps, slen(caps), n), DecodeStatus::KeyInvalid);
    const char* toolong = "cle=abcdefghijklmnopqrstuvwxyz";
    CHECK_ENUM(alerts::decode_form(toolong, slen(toolong), n), DecodeStatus::KeyInvalid);
    const char* badenc = "cle=ga%ZZ";
    CHECK_ENUM(alerts::decode_form(badenc, slen(badenc), n), DecodeStatus::KeyMissing);
    CHECK_ENUM(alerts::decode_form(nullptr, 0U, n), DecodeStatus::KeyMissing);
    CHECK_ENUM(alerts::decode_form("", 0U, n), DecodeStatus::KeyMissing);

    {
        AllocGuard guard;
        const char* body = "cle=x&texte=no+alloc";
        (void)alerts::decode_form(body, slen(body), n);
    }
    CHECK_EQ(g_alloc_count, 0);
}

}  // namespace

void run_alerts_tests() {
    test_key();
    test_decode_full();
    test_decode_defaults_and_bounds();
    test_decode_rejections();
}
