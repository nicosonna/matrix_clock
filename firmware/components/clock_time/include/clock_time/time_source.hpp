#pragma once
/**
 * @file time_source.hpp
 * @brief Time of day for the display: SNTP epoch, pure local-time conversion,
 *        honest validity (EX-02, EX-03).
 *
 * `sure` is false until SNTP has synchronised once, and again when the last
 * synchronisation is older than 48 h. The caller never gets a wall-clock time
 * it should not trust without being told so.
 */

#include <cstdint>

#include "esp_err.h"
#include "timekeeping/local_time.hpp"

namespace clock_time {

/// What the display needs, read from any task.
struct Snapshot {
    bool sure;                    ///< The time can be shown.
    timekeeping::LocalTime local; ///< Local civil time (meaningless when !sure).
    uint32_t age_ms;              ///< Since the last synchronisation, UINT32_MAX if never.
    int64_t epoch_utc;            ///< Raw UTC epoch (seconds).
};

/**
 * @brief Starts SNTP and installs the time-zone rule.
 * @param ntp_server Host name of the NTP server (e.g. "pool.ntp.org").
 * @param posix_tz   POSIX rule, e.g. timekeeping::kEuropeZurich.
 * @return ESP_OK; ESP_ERR_INVALID_ARG if the rule does not parse (UTC is then used);
 *         the SNTP error otherwise.
 * @note Call once the network interfaces exist (after clock_web::start()).
 */
esp_err_t start(const char* ntp_server, const char* posix_tz) noexcept;

/// Replaces the time-zone rule at run time (settings page). false if it does not parse.
bool set_timezone(const char* posix_tz) noexcept;

/// @return The current snapshot.
Snapshot now() noexcept;

}  // namespace clock_time
