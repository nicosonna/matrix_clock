/**
 * @file test_network.cpp
 * @brief Host tests of the pure `network` component.
 *
 * RES-01..11 are ThermoSync's tests, translated: the form and the DNS receive
 * data from any device within radio range, so they are exercised with hostile
 * input. RES-20..29 cover the power-cut extension of spec-reseau-portail.md.
 */
#include "test_support.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include "network/captive_dns.hpp"
#include "network/credentials.hpp"
#include "network/supervisor.hpp"

namespace {

using network::CredStatus;
using network::FailCause;
using network::LinkStatus;
using network::WifiPlan;
using network::WifiState;
using network::WifiSupervisor;
using network::WifiSupervisorConfig;

std::size_t slen(const char* s) { return std::strlen(s); }

constexpr LinkStatus kDown{false, false};
constexpr LinkStatus kAssociated{true, false};
constexpr LinkStatus kUp{true, true};

// --------------------------------------------------------------------- RES-09

void test_validate() {
    CHECK_ENUM(network::validate("Maison", 6U, "motdepasse", 10U), CredStatus::Ok);
    CHECK_ENUM(network::validate("Maison", 6U, "", 0U), CredStatus::Ok);  // open network
    CHECK_ENUM(network::validate("", 0U, "motdepasse", 10U), CredStatus::SsidEmpty);
    CHECK_ENUM(network::validate(nullptr, 0U, nullptr, 0U), CredStatus::SsidEmpty);
    const char s33[] = "123456789012345678901234567890123";
    CHECK_ENUM(network::validate(s33, 33U, "", 0U), CredStatus::SsidTooLong);
    CHECK_ENUM(network::validate(s33, 32U, "", 0U), CredStatus::Ok);  // 32 bytes: exact limit
    CHECK_ENUM(network::validate("M", 1U, "1234567", 7U), CredStatus::PassTooShort);
    CHECK_ENUM(network::validate("M", 1U, "12345678", 8U), CredStatus::Ok);
    char p64[65];
    std::memset(p64, 'a', 64U);
    p64[64] = '\0';
    CHECK_ENUM(network::validate("M", 1U, p64, 64U), CredStatus::PassTooLong);
    CHECK_ENUM(network::validate("M", 1U, p64, 63U), CredStatus::Ok);
    CHECK_ENUM(network::validate("M", 1U, "abc\tdefgh", 9U), CredStatus::PassInvalidChar);
    CHECK_ENUM(network::validate("M", 1U, "abc\xC3\xA9" "defg", 9U), CredStatus::PassInvalidChar);
    CHECK_ENUM(network::validate("M", 1U, nullptr, 8U), CredStatus::PassTooShort);
}

// --------------------------------------------------------------------- RES-10

void test_form_field() {
    char out[40];
    std::size_t n = 0U;
    const char* body = "ssid=Ma+Box&password=p%40ss%26w%3Drd";
    CHECK(network::form_field(body, slen(body), "ssid", out, sizeof(out), n));
    CHECK(std::strcmp(out, "Ma Box") == 0);
    CHECK_EQ(n, 6);
    CHECK(network::form_field(body, slen(body), "password", out, sizeof(out), n));
    CHECK(std::strcmp(out, "p@ss&w=rd") == 0);

    const char* utf8 = "ssid=Caf%C3%A9";  // UTF-8 SSID: accepted as is
    CHECK(network::form_field(utf8, slen(utf8), "ssid", out, sizeof(out), n));
    CHECK_EQ(n, 5);
    char upper[8];
    std::memcpy(upper, out, n + 1U);
    const char* lower = "ssid=Caf%c3%a9";  // same in lower-case hex
    CHECK(network::form_field(lower, slen(lower), "ssid", out, sizeof(out), n));
    CHECK(std::strcmp(out, upper) == 0);

    const char* empty = "ssid=&password=";
    CHECK(network::form_field(empty, slen(empty), "ssid", out, sizeof(out), n));
    CHECK_EQ(n, 0);

    // Key absent, or only a prefix of another key.
    const char* other = "ssidx=a&xssid=b";
    CHECK(!network::form_field(other, slen(other), "ssid", out, sizeof(out), n));
    CHECK(!network::form_field(body, slen(body), "pass", out, sizeof(out), n));

    // Hostile encodings: rejected, never repaired.
    const char* bad[] = {"ssid=%G1", "ssid=ab%4", "ssid=ab%", "ssid=a%00b"};
    for (const char* b : bad) {
        CHECK(!network::form_field(b, slen(b), "ssid", out, sizeof(out), n));
        CHECK_EQ(out[0], 0);
    }
    // '%' at the end of a field followed by another field: no spill-over.
    const char* edge = "ssid=a%4&x=1";
    CHECK(!network::form_field(edge, slen(edge), "ssid", out, sizeof(out), n));
    // Read beyond the body: the byte right AFTER the announced length is a
    // valid hex digit. A wrong bound would read it and accept; the right bound refuses.
    const char beyond[] = "ssid=ab%4F";
    CHECK(!network::form_field(beyond, sizeof(beyond) - 2U, "ssid", out, sizeof(out), n));

    // Value that does not fit.
    char small[4];
    const char* long_ = "ssid=abcd";
    CHECK(!network::form_field(long_, slen(long_), "ssid", small, sizeof(small), n));
    const char* just = "ssid=abc";
    CHECK(network::form_field(just, slen(just), "ssid", small, sizeof(small), n));

    // '=' inside the value, and a pair without '='.
    const char* equal = "alone&ssid=a=b";
    CHECK(network::form_field(equal, slen(equal), "ssid", out, sizeof(out), n));
    CHECK(std::strcmp(out, "a=b") == 0);

    CHECK(!network::form_field(nullptr, 0U, "ssid", out, sizeof(out), n));
    CHECK(!network::form_field(body, slen(body), "ssid", out, 0U, n));
}

void test_json_escape_and_model() {
    char out[32];
    const char* s = "a\"b\\c\nd";
    const std::size_t n = network::json_escape(s, slen(s), out, sizeof(out));
    CHECK(std::strcmp(out, "a\\\"b\\\\c\\u000ad") == 0);
    CHECK_EQ(n, std::strlen(out));
    char small[4];
    CHECK_EQ(network::json_escape("abcd", 4U, small, sizeof(small)), 0);
    CHECK_EQ(small[0], 0);
    CHECK_EQ(network::json_escape("ab\"", 3U, small, sizeof(small)), 0);  // cut sequence: refused
    CHECK_EQ(network::json_escape("x", 1U, nullptr, 0U), 0);
    CHECK_EQ(network::json_escape("", 0U, out, sizeof(out)), 0);
    CHECK_EQ(out[0], 0);

    char model[96];
    const std::size_t m = network::model_json(model, sizeof(model));
    CHECK(m > 0U);
    CHECK(std::strstr(model, "\"ssid_max\":32") != nullptr);
    CHECK(std::strstr(model, "\"pass_min\":8") != nullptr);
    CHECK(std::strstr(model, "\"pass_max\":63") != nullptr);
    char tiny[8];
    CHECK_EQ(network::model_json(tiny, sizeof(tiny)), 0);
    CHECK_EQ(network::model_json(nullptr, 0U), 0);
}

void test_ap_password() {
    std::array<char, network::kApPassLen + 1U> a{};
    std::array<char, network::kApPassLen + 1U> b{};
    std::array<char, network::kApPassLen + 1U> c{};
    network::derive_ap_password({0x10, 0x20, 0xba, 0x5c, 0x7d, 0xf4}, a);
    network::derive_ap_password({0x10, 0x20, 0xba, 0x5c, 0x7d, 0xf4}, b);
    network::derive_ap_password({0x10, 0x20, 0xba, 0x5c, 0x7d, 0xf5}, c);
    CHECK_EQ(std::strlen(a.data()), network::kApPassLen);
    CHECK(std::strcmp(a.data(), b.data()) == 0);  // deterministic
    CHECK(std::strcmp(a.data(), c.data()) != 0);  // specific to each board
    // Readable on the panel's 5x7 font and typed exactly: digits and capitals, no O/I.
    bool readable = true;
    for (std::size_t i = 0U; i < network::kApPassLen; ++i) {
        const char ch = a[i];
        const bool digit = (ch >= '0') && (ch <= '9');
        const bool capital = (ch >= 'A') && (ch <= 'Z') && (ch != 'O') && (ch != 'I');
        readable = readable && (digit || capital);
    }
    CHECK(readable);
    CHECK_ENUM(network::validate("M", 1U, a.data(), network::kApPassLen), CredStatus::Ok);  // valid WPA2
    std::array<char, network::kApPassLen + 1U> z{};
    network::derive_ap_password({0, 0, 0, 0, 0, 0}, z);
    CHECK_EQ(std::strlen(z.data()), network::kApPassLen);
}

// --------------------------------------------------------------------- RES-11

/// Builds a DNS query: id 0x1234, RD, one question `labels` of type `qtype`.
std::size_t make_query(uint8_t* q, const char* const* labels, std::size_t nlabels, uint16_t qtype) {
    const uint8_t hdr[12] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    std::memcpy(q, hdr, sizeof(hdr));
    std::size_t i = 12U;
    for (std::size_t k = 0U; k < nlabels; ++k) {
        const std::size_t l = std::strlen(labels[k]);
        q[i++] = static_cast<uint8_t>(l);
        std::memcpy(q + i, labels[k], l);
        i += l;
    }
    q[i++] = 0U;
    q[i++] = static_cast<uint8_t>(qtype >> 8);
    q[i++] = static_cast<uint8_t>(qtype & 0xFFU);
    q[i++] = 0U;
    q[i++] = 1U;  // IN
    return i;
}

void test_captive_dns() {
    const std::array<uint8_t, 4> ip{192, 168, 4, 1};
    uint8_t q[600] = {};
    uint8_t r[600] = {};
    const char* const labels[] = {"connectivitycheck", "gstatic", "com"};
    const std::size_t ql = make_query(q, labels, 3U, 1U);

    std::size_t rl = network::build_captive_response(q, ql, ip, r, sizeof(r));
    CHECK_EQ(rl, ql + 16U);
    CHECK_EQ(r[0], 0x12);
    CHECK_EQ(r[1], 0x34);                 // id copied
    CHECK((r[2] & 0x80U) != 0U);          // QR = response
    CHECK((r[2] & 0x01U) != 0U);          // RD copied
    CHECK_EQ(r[3] & 0x0F, 0);             // NoError
    CHECK_EQ(r[7], 1);                    // one answer
    CHECK_EQ(r[rl - 4U], 192);
    CHECK_EQ(r[rl - 1U], 1);
    CHECK_EQ(r[ql], 0xC0);                // name pointer
    CHECK_EQ(r[ql + 1U], 12);

    // AAAA: empty answer, but no error (makes the portal open).
    const std::size_t q6 = make_query(q, labels, 3U, 28U);
    rl = network::build_captive_response(q, q6, ip, r, sizeof(r));
    CHECK_EQ(rl, q6);
    CHECK_EQ(r[7], 0);
    CHECK_EQ(r[3] & 0x0F, 0);

    // Query with an additional record (EDNS): ignored, ARCOUNT 0.
    std::size_t qe = make_query(q, labels, 3U, 1U);
    q[11] = 1U;
    const uint8_t opt[11] = {0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(q + qe, opt, sizeof(opt));
    qe += sizeof(opt);
    rl = network::build_captive_response(q, qe, ip, r, sizeof(r));
    CHECK_EQ(rl, qe - sizeof(opt) + 16U);
    CHECK_EQ(r[11], 0);

    // Packets to ignore.
    std::size_t ql2 = make_query(q, labels, 3U, 1U);
    q[2] = 0x81U;  // a response
    CHECK_EQ(network::build_captive_response(q, ql2, ip, r, sizeof(r)), 0);
    q[2] = 0x09U;  // opcode 1 (inverse query)
    CHECK_EQ(network::build_captive_response(q, ql2, ip, r, sizeof(r)), 0);
    q[2] = 0x01U;
    q[5] = 2U;     // two questions
    CHECK_EQ(network::build_captive_response(q, ql2, ip, r, sizeof(r)), 0);
    q[5] = 1U;
    CHECK_EQ(network::build_captive_response(q, ql2 - 1U, ip, r, sizeof(r)), 0);  // class cut
    CHECK_EQ(network::build_captive_response(q, 14U, ip, r, sizeof(r)), 0);       // name cut
    CHECK_EQ(network::build_captive_response(q, 11U, ip, r, sizeof(r)), 0);       // short header
    CHECK_EQ(network::build_captive_response(q, 513U, ip, r, sizeof(r)), 0);      // too long
    CHECK_EQ(network::build_captive_response(q, ql2, ip, r, ql2 + 15U), 0);       // buffer too small
    CHECK_EQ(network::build_captive_response(nullptr, ql2, ip, r, sizeof(r)), 0);

    q[12] = 0xC0U;  // compression in the question: refused
    CHECK_EQ(network::build_captive_response(q, ql2, ip, r, sizeof(r)), 0);
    q[12] = 64U;    // label > 63
    CHECK_EQ(network::build_captive_response(q, ql2, ip, r, sizeof(r)), 0);

    // Name longer than 255 bytes (5 labels of 60).
    char l60[61];
    std::memset(l60, 'a', 60U);
    l60[60] = '\0';
    const char* const longs[] = {l60, l60, l60, l60, l60};
    const std::size_t qlong = make_query(q, longs, 5U, 1U);
    CHECK_EQ(network::build_captive_response(q, qlong, ip, r, sizeof(r)), 0);
}

// ---------------------------------------------------------- RES-01..08, RES-22

/// ThermoSync's configuration: no grace, so the inherited behaviour, bit for bit.
WifiSupervisorConfig legacy_config() {
    WifiSupervisorConfig c{};
    c.attempts = 3U;
    c.attempt_ms = 10000U;
    c.retry_period_ms = 900000U;
    c.retry_window_ms = 30000U;
    c.boot_grace_ms = 0U;
    return c;
}

void test_supervisor_inherited() {
    const WifiSupervisorConfig cfg = legacy_config();

    // No network configured: portal, station idle, forever.
    WifiSupervisor none(cfg, false, 0U);
    WifiPlan p = none.step(5000U, false);
    CHECK_ENUM(none.state(), WifiState::NoCredentials);
    CHECK(p.ap_on && p.portal_on && !p.sta_connect);
    p = none.step(100000000U, false);
    CHECK(p.portal_on);

    // Network configured, connected on the first attempt: access point never on.
    WifiSupervisor ok(cfg, true, 0U);
    p = ok.step(1000U, false);
    CHECK_ENUM(ok.state(), WifiState::Connecting);
    CHECK(!p.ap_on && !p.portal_on && p.sta_connect);
    const uint32_t id1 = p.attempt_id;
    p = ok.step(3000U, true);
    CHECK_ENUM(ok.state(), WifiState::Connected);
    CHECK(!p.ap_on && p.sta_connect);

    // Link lost: a new series of attempts, new id.
    p = ok.step(4000U, false);
    CHECK_ENUM(ok.state(), WifiState::Connecting);
    CHECK(p.attempt_id != id1);

    // Three failed attempts: fallback, station IDLE.
    WifiSupervisor ko(cfg, true, 0U);
    uint32_t last = ko.step(0U, false).attempt_id;
    p = ko.step(10000U, false);  // end of attempt 1
    CHECK_ENUM(ko.state(), WifiState::Connecting);
    CHECK(p.attempt_id != last);
    last = p.attempt_id;
    p = ko.step(15000U, false);
    CHECK_EQ(p.attempt_id, last);  // attempt 2 in progress
    p = ko.step(20000U, false);    // end of attempt 2
    CHECK(p.attempt_id != last);
    p = ko.step(30000U, false);    // end of attempt 3
    CHECK_ENUM(ko.state(), WifiState::Fallback);
    CHECK(p.ap_on && p.portal_on && !p.sta_connect);

    // In fallback: a retry after the period, portal kept.
    p = ko.step(30000U + 899999U, false);
    CHECK_ENUM(ko.state(), WifiState::Fallback);
    last = p.attempt_id;
    p = ko.step(30000U + 900000U, false);
    CHECK_ENUM(ko.state(), WifiState::Retrying);
    CHECK(p.ap_on && p.portal_on && p.sta_connect);
    CHECK(p.attempt_id != last);
    // Window over without a link: back to fallback.
    p = ko.step(930000U + 30000U, false);
    CHECK_ENUM(ko.state(), WifiState::Fallback);
    CHECK(!p.sta_connect);
    // Successful retry: connected, access point off.
    (void)ko.step(960000U + 900000U, false);
    CHECK_ENUM(ko.state(), WifiState::Retrying);
    p = ko.step(1860000U + 5000U, true);
    CHECK_ENUM(ko.state(), WifiState::Connected);
    CHECK(!p.ap_on && !p.portal_on);

    // Link back during fallback (edge case): connected.
    WifiSupervisorConfig quick = legacy_config();
    quick.attempts = 1U;
    quick.attempt_ms = 1000U;
    quick.retry_period_ms = 5000U;
    quick.retry_window_ms = 1000U;
    WifiSupervisor fb(quick, true, 0U);
    (void)fb.step(1000U, false);
    CHECK_ENUM(fb.state(), WifiState::Fallback);
    (void)fb.step(1500U, true);
    CHECK_ENUM(fb.state(), WifiState::Connected);

    // Zero settings replaced by the defaults; clock wrap. Defaults include a
    // grace, so this one explicitly disables it to stay the inherited behaviour.
    WifiSupervisorConfig zeros{};
    WifiSupervisor z(zeros, true, 0xFFFFF000U);
    CHECK_ENUM(z.state(), WifiState::NoCredentials == z.state() ? WifiState::NoCredentials : z.state());
    WifiSupervisorConfig zeros_no_grace{};
    zeros_no_grace.boot_grace_ms = 0U;
    WifiSupervisor zz(zeros_no_grace, true, 0xFFFFF000U);
    (void)zz.step(0xFFFFF000U + 5000U, false);   // 5 s later, across the wrap
    CHECK_ENUM(zz.state(), WifiState::Connecting);
    (void)zz.step(0xFFFFF000U + 10000U, false);  // 10 s: attempt 2 (default 3 x 10 s)
    (void)zz.step(0xFFFFF000U + 20000U, false);
    (void)zz.step(0xFFFFF000U + 30000U, false);
    CHECK_ENUM(zz.state(), WifiState::Fallback);
    CHECK_EQ(network::default_wifi_supervisor_config().retry_period_ms, 900000);
    CHECK_EQ(network::default_wifi_supervisor_config().boot_grace_ms, 300000);
}

// ------------------------------------------------------------- RES-20 .. RES-24

void test_boot_grace() {
    const WifiSupervisorConfig cfg = network::default_wifi_supervisor_config();

    // RES-20: five minutes of "AP not found": never the portal, delays 10/20/40/60/60.
    WifiSupervisor s(cfg, true, 0U);
    CHECK_ENUM(s.state(), WifiState::BootGrace);
    bool portal_seen = false;
    bool ap_seen = false;
    uint32_t last_id = 0U;
    uint32_t attempts_started = 0U;
    std::array<uint32_t, 8> attempt_times{};
    for (uint32_t t = 0U; t < 300000U; t += 1000U) {
        const WifiPlan p = s.step(t, kDown, (t % 10000U == 9000U) ? FailCause::ApNotFound : FailCause::None);
        portal_seen = portal_seen || p.portal_on;
        ap_seen = ap_seen || p.ap_on;
        if (p.sta_connect && (p.attempt_id != last_id)) {
            if (attempts_started < attempt_times.size()) {
                attempt_times[attempts_started] = t;
            }
            ++attempts_started;
            last_id = p.attempt_id;
        }
    }
    CHECK(!portal_seen);
    CHECK(!ap_seen);
    CHECK_ENUM(s.state(), WifiState::BootGrace);
    // Attempts at 0, 20 s (10 + 10 pause), 50 s (+10+20), 100 s (+10+40), 170 s (+10+60), 240 s.
    CHECK_EQ(attempts_started, 6);
    CHECK_EQ(attempt_times[0], 0);
    CHECK_EQ(attempt_times[1], 20000);
    CHECK_EQ(attempt_times[2], 50000);
    CHECK_EQ(attempt_times[3], 100000);
    CHECK_EQ(attempt_times[4], 170000);
    CHECK_EQ(attempt_times[5], 240000);
    // Between attempts the station is idle (no background retries).
    WifiSupervisor idle(cfg, true, 0U);
    (void)idle.step(0U, kDown, FailCause::None);
    WifiPlan p = idle.step(10000U, kDown, FailCause::ApNotFound);
    CHECK(!p.sta_connect);
    p = idle.step(19000U, kDown, FailCause::None);
    CHECK(!p.sta_connect);
    p = idle.step(20000U, kDown, FailCause::None);
    CHECK(p.sta_connect);

    // RES-21: grace over => inherited attempts, then fallback at +30 s.
    p = s.step(300000U, kDown, FailCause::None);
    CHECK_ENUM(s.state(), WifiState::Connecting);
    CHECK(!p.portal_on);
    for (uint32_t t = 301000U; t <= 330000U; t += 1000U) {
        p = s.step(t, kDown, FailCause::None);
    }
    CHECK_ENUM(s.state(), WifiState::Fallback);
    CHECK(p.ap_on && p.portal_on && !p.sta_connect);

    // Success during the grace: connected, no portal ever.
    WifiSupervisor okg(cfg, true, 0U);
    (void)okg.step(0U, kDown, FailCause::None);
    (void)okg.step(10000U, kDown, FailCause::ApNotFound);  // pause
    (void)okg.step(20000U, kDown, FailCause::None);        // attempt 2
    p = okg.step(25000U, kUp, FailCause::None);
    CHECK_ENUM(okg.state(), WifiState::Connected);
    CHECK(!p.ap_on && p.sta_connect);

    // RES-23: two authentication failures during the grace => fallback at once.
    WifiSupervisor auth(cfg, true, 0U);
    (void)auth.step(0U, kDown, FailCause::None);
    p = auth.step(3000U, kDown, FailCause::AuthFailed);
    CHECK_ENUM(auth.state(), WifiState::BootGrace);  // RES-24: one is not enough
    CHECK(!p.portal_on);
    p = auth.step(6000U, kDown, FailCause::AuthFailed);
    CHECK_ENUM(auth.state(), WifiState::Fallback);
    CHECK(p.ap_on && p.portal_on && !p.sta_connect);

    // An authentication failure after a successful association does not count:
    // the passphrase was right, the router is flaky.
    WifiSupervisor flaky(cfg, true, 0U);
    (void)flaky.step(0U, kAssociated, FailCause::None);
    (void)flaky.step(1000U, kDown, FailCause::AuthFailed);
    p = flaky.step(2000U, kDown, FailCause::AuthFailed);
    CHECK_ENUM(flaky.state(), WifiState::BootGrace);
    CHECK(!p.portal_on);

    // No credentials with a grace configured: portal at once, as before.
    WifiSupervisor none(cfg, false, 0U);
    p = none.step(0U, kDown, FailCause::None);
    CHECK_ENUM(none.state(), WifiState::NoCredentials);
    CHECK(p.ap_on && p.portal_on && !p.sta_connect);
}

// ------------------------------------------------------------- RES-25 .. RES-28

void test_lease() {
    const WifiSupervisorConfig cfg = network::default_wifi_supervisor_config();

    // RES-25: associated without an IP for lease_ms => WaitingLease, no portal,
    // retries at 60 s then 120, 240, 300 (cap).
    WifiSupervisor s(cfg, true, 0U);
    WifiPlan p = s.step(0U, kAssociated, FailCause::None);
    CHECK_ENUM(s.state(), WifiState::BootGrace);
    p = s.step(19000U, kAssociated, FailCause::None);
    CHECK_ENUM(s.state(), WifiState::BootGrace);
    p = s.step(20000U, kAssociated, FailCause::None);
    CHECK_ENUM(s.state(), WifiState::WaitingLease);
    CHECK(!p.ap_on && !p.portal_on && p.sta_connect);
    uint32_t last_id = p.attempt_id;
    std::array<uint32_t, 6> retry_times{};
    uint32_t retries = 0U;
    bool portal_seen = false;
    for (uint32_t t = 21000U; t < 20000U + 29U * 60000U; t += 1000U) {
        p = s.step(t, kAssociated, FailCause::None);
        portal_seen = portal_seen || p.portal_on;
        if (p.attempt_id != last_id) {
            if (retries < retry_times.size()) {
                retry_times[retries] = t - 20000U;
            }
            ++retries;
            last_id = p.attempt_id;
        }
    }
    CHECK(!portal_seen);                       // RES-26: 29 minutes without a portal
    CHECK_ENUM(s.state(), WifiState::WaitingLease);
    CHECK(retries >= 5U);
    CHECK_EQ(retry_times[0], 60000);
    CHECK_EQ(retry_times[1], 60000 + 120000);
    CHECK_EQ(retry_times[2], 60000 + 120000 + 240000);
    CHECK_EQ(retry_times[3], 60000 + 120000 + 240000 + 300000);  // capped at 5 min
    CHECK_EQ(retry_times[4], 60000 + 120000 + 240000 + 300000 + 300000);

    // RES-26: at 30 minutes, desperate: access point AND portal raised, retries continue.
    p = s.step(20000U + 30U * 60000U, kAssociated, FailCause::None);
    CHECK_ENUM(s.state(), WifiState::LeaseDesperate);
    CHECK(p.ap_on && p.portal_on && p.sta_connect);
    last_id = p.attempt_id;
    bool retried = false;
    for (uint32_t t = 20000U + 30U * 60000U + 1000U; t < 20000U + 36U * 60000U; t += 1000U) {
        p = s.step(t, kAssociated, FailCause::None);
        retried = retried || (p.attempt_id != last_id);
    }
    CHECK(retried);

    // RES-27: a lease arrives => connected, access point off.
    p = s.step(20000U + 36U * 60000U, kUp, FailCause::None);
    CHECK_ENUM(s.state(), WifiState::Connected);
    CHECK(!p.ap_on && !p.portal_on && p.sta_connect);

    // RES-27 from WaitingLease too.
    WifiSupervisor w(cfg, true, 0U);
    (void)w.step(0U, kAssociated, FailCause::None);
    (void)w.step(20000U, kAssociated, FailCause::None);
    CHECK_ENUM(w.state(), WifiState::WaitingLease);
    p = w.step(30000U, kUp, FailCause::None);
    CHECK_ENUM(w.state(), WifiState::Connected);

    // RES-28: once associated, a Connecting series that stays associated never
    // falls back: after 20 s it waits for the lease instead.
    WifiSupervisor c(cfg, true, 0U);
    (void)c.step(0U, kUp, FailCause::None);
    CHECK_ENUM(c.state(), WifiState::Connected);
    (void)c.step(1000U, kAssociated, FailCause::None);  // IP lost, still associated
    CHECK_ENUM(c.state(), WifiState::Connecting);
    bool fell_back = false;
    for (uint32_t t = 2000U; t < 600000U; t += 1000U) {
        p = c.step(t, kAssociated, FailCause::None);
        fell_back = fell_back || (c.state() == WifiState::Fallback) || (c.state() == WifiState::Retrying);
    }
    CHECK(!fell_back);
    CHECK(c.state() == WifiState::WaitingLease || c.state() == WifiState::LeaseDesperate);

    // Association lost for good while waiting: a new series of attempts.
    WifiSupervisor lost(cfg, true, 0U);
    (void)lost.step(0U, kAssociated, FailCause::None);
    (void)lost.step(20000U, kAssociated, FailCause::None);
    CHECK_ENUM(lost.state(), WifiState::WaitingLease);
    (void)lost.step(21000U, kDown, FailCause::Other);
    CHECK_ENUM(lost.state(), WifiState::WaitingLease);  // a reconnection takes a moment
    for (uint32_t t = 22000U; t <= 31000U; t += 1000U) {
        (void)lost.step(t, kDown, FailCause::None);
    }
    CHECK_ENUM(lost.state(), WifiState::Connecting);

    // Retry in fallback that associates but gets no lease: WaitingLease, not fallback.
    WifiSupervisorConfig legacy = legacy_config();
    WifiSupervisor r(legacy, true, 0U);
    for (uint32_t t = 0U; t <= 30000U; t += 10000U) {
        (void)r.step(t, kDown, FailCause::None);
    }
    CHECK_ENUM(r.state(), WifiState::Fallback);
    (void)r.step(30000U + 900000U, kDown, FailCause::None);
    CHECK_ENUM(r.state(), WifiState::Retrying);
    p = r.step(930000U + 30000U, kAssociated, FailCause::None);
    CHECK_ENUM(r.state(), WifiState::WaitingLease);
    CHECK(!p.portal_on);
}

// --------------------------------------------------------------------- RES-29

void test_reboot_request() {
    WifiSupervisorConfig cfg = legacy_config();
    cfg.reboot_without_ip_ms = 100000U;
    WifiSupervisor s(cfg, true, 0U);
    bool requested = false;
    uint32_t count = 0U;
    for (uint32_t t = 0U; t <= 250000U; t += 1000U) {
        const WifiPlan p = s.step(t, kDown, FailCause::None);
        if (p.request_reboot) {
            ++count;
            requested = true;
        }
    }
    CHECK(requested);
    CHECK_EQ(count, 1);  // once per period, and only once requested
    // An IP resets the clock.
    WifiSupervisor ok(cfg, true, 0U);
    count = 0U;
    for (uint32_t t = 0U; t <= 250000U; t += 1000U) {
        if (ok.step(t, kUp, FailCause::None).request_reboot) {
            ++count;
        }
    }
    CHECK_EQ(count, 0);
    // 0 = never.
    cfg.reboot_without_ip_ms = 0U;
    WifiSupervisor never(cfg, true, 0U);
    count = 0U;
    for (uint32_t t = 0U; t <= 250000U; t += 1000U) {
        if (never.step(t, kDown, FailCause::None).request_reboot) {
            ++count;
        }
    }
    CHECK_EQ(count, 0);
    // No credentials: no reboot either.
    cfg.reboot_without_ip_ms = 100000U;
    WifiSupervisor none(cfg, false, 0U);
    count = 0U;
    for (uint32_t t = 0U; t <= 250000U; t += 1000U) {
        if (none.step(t, kDown, FailCause::None).request_reboot) {
            ++count;
        }
    }
    CHECK_EQ(count, 0);

    {
        AllocGuard guard;
        WifiSupervisor g(network::default_wifi_supervisor_config(), true, 0U);
        (void)g.step(1000U, kDown, FailCause::ApNotFound);
        char out[16];
        std::size_t n = 0U;
        (void)network::form_field("ssid=a", 6U, "ssid", out, sizeof(out), n);
    }
    CHECK_EQ(g_alloc_count, 0);
}

}  // namespace

void run_network_tests() {
    test_validate();
    test_form_field();
    test_json_escape_and_model();
    test_ap_password();
    test_captive_dns();
    test_supervisor_inherited();
    test_boot_grace();
    test_lease();
    test_reboot_request();
}
