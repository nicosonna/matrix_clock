/**
 * @file test_main.cpp
 * @brief Host test entry point, harness definitions and the allocation trap.
 */
#include "test_support.hpp"

#include <cstdlib>
#include <new>

uint32_t g_checks = 0U;
uint32_t g_failures = 0U;
uint32_t g_alloc_count = 0U;
bool g_alloc_guard_armed = false;

// Global allocation trap: the firmware must not allocate after init, and the
// only way to enforce that beyond code review is to count allocations in the
// tests. The replacement never throws (the build is -fno-exceptions).
namespace {
void* traced_alloc(std::size_t n) {
    if (g_alloc_guard_armed) {
        ++g_alloc_count;
    }
    void* p = std::malloc((n == 0U) ? 1U : n);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}
}  // namespace

void* operator new(std::size_t n) { return traced_alloc(n); }
void* operator new[](std::size_t n) { return traced_alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL line %d: %s\n", line, what);
    }
}

void check_eq(int64_t got, int64_t expected, const char* what, int line) {
    ++g_checks;
    if (got != expected) {
        ++g_failures;
        std::printf("  FAIL line %d: %s (got %lld, expected %lld)\n", line, what,
                    static_cast<long long>(got), static_cast<long long>(expected));
    }
}

int main() {
    std::printf("-- display: frame buffer, panel mapping, gamma, current limit, render\n");
    run_display_tests();
    std::printf("-- display: fonts, text, transliteration, clock and date scenes\n");
    run_text_tests();
    std::printf("-- display: scrolling, scene scheduler, test patterns\n");
    run_scenes_tests();
    std::printf("-- brightness: median, asymmetric average, curve, dead band, fault, schedule, wizard\n");
    run_brightness_tests();
    std::printf("-- network: credentials, portal form, captive DNS, supervisor (grace, lease, reboot)\n");
    run_network_tests();
    std::printf("-- alerts: inbound notification decoder\n");
    run_alerts_tests();
    run_alert_engine_tests();
    std::printf("-- timekeeping: POSIX rules, local time with DST, calendar, validity\n");
    run_timekeeping_tests();
    std::printf("-- settings: JSON writer, forms (all or nothing), models, page orphans\n");
    run_settings_tests();
    std::printf("-- air quality: AHT21 CRC/decoding, ENS160 registers, measurement loop, levels, logo\n");
    run_air_quality_tests();
    std::printf("-- home assistant: JSON reader, values, reassembly, WebSocket client, weather\n");
    run_homeassistant_tests();
    run_mqtt_tests();

    if (g_failures == 0U) {
        std::printf("OK: %lu checks passed.\n", static_cast<unsigned long>(g_checks));
        return 0;
    }
    std::printf("FAILED: %lu/%lu checks in error.\n", static_cast<unsigned long>(g_failures),
                static_cast<unsigned long>(g_checks));
    return 1;
}
