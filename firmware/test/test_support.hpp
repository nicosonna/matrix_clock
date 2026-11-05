#pragma once
/**
 * @file test_support.hpp
 * @brief Host test harness shared by every test file.
 *
 * **No `assert()`**: it vanishes as soon as `NDEBUG` is defined, which would
 * let safety tests pass silently. Checks are counted, a failure prints the line
 * and the values without stopping, and `main` returns non-zero on any failure.
 *
 * **Allocation guard**: the global `operator new` is replaced in test_main.cpp.
 * While an AllocGuard is alive, every allocation is counted; a test that must
 * not allocate checks the counter stayed at zero (CLAUDE.md: no dynamic
 * allocation after init).
 */

#include <cstdint>
#include <cstdio>
#include <type_traits>

/// Total number of checks executed.
extern uint32_t g_checks;
/// Number of failed checks.
extern uint32_t g_failures;
/// Allocations seen while the guard was armed.
extern uint32_t g_alloc_count;
/// Whether allocations are currently counted.
extern bool g_alloc_guard_armed;

/// Checks a condition; records and reports a failure without stopping.
void check(bool condition, const char* what, int line);

/// Compares two 64-bit integers and prints both on mismatch.
void check_eq(int64_t got, int64_t expected, const char* what, int line);

/// Compares two `enum class` values (no implicit conversion to an integer).
template <typename E>
void check_enum(E got, E expected, const char* what, int line) {
    ++g_checks;
    if (got != expected) {
        ++g_failures;
        std::printf("  FAIL line %d: %s (got %ld, expected %ld)\n", line, what,
                    static_cast<long>(static_cast<std::underlying_type_t<E>>(got)),
                    static_cast<long>(static_cast<std::underlying_type_t<E>>(expected)));
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(got, exp) check_eq(static_cast<int64_t>(got), static_cast<int64_t>(exp), #got, __LINE__)
#define CHECK_ENUM(got, exp) check_enum((got), (exp), #got, __LINE__)

/// RAII: counts heap allocations for the duration of a scope.
struct AllocGuard {
    AllocGuard() noexcept {
        g_alloc_count = 0U;
        g_alloc_guard_armed = true;
    }
    ~AllocGuard() noexcept { g_alloc_guard_armed = false; }
    AllocGuard(const AllocGuard&) = delete;
    AllocGuard& operator=(const AllocGuard&) = delete;
};

/// Test suites, defined in their own files.
void run_display_tests();
void run_text_tests();
void run_scenes_tests();
void run_brightness_tests();
void run_network_tests();
void run_alerts_tests();
