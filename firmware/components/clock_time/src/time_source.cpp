/**
 * @file time_source.cpp
 * @brief SNTP + pure local time.
 */
#include "clock_time/time_source.hpp"

#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "timekeeping/tz.hpp"
#include "timekeeping/validity.hpp"

namespace clock_time {

namespace {

constexpr char kTag[] = "time";

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
timekeeping::TzRule s_rule{};          // UTC until start()
timekeeping::SyncValidity s_validity;  // never synced until the first callback
uint32_t s_sync_count = 0U;

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

/// Called by the SNTP task on every successful synchronisation.
void on_sync(timeval* tv) {
    taskENTER_CRITICAL(&s_mux);
    s_validity.mark_synced(now_ms());
    ++s_sync_count;
    const uint32_t count = s_sync_count;
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(kTag, "SNTP synchronised (%lu): epoch %lld", static_cast<unsigned long>(count),
             static_cast<long long>((tv != nullptr) ? tv->tv_sec : 0));
}

}  // namespace

bool set_timezone(const char* posix_tz) noexcept {
    timekeeping::TzRule rule{};
    if (!timekeeping::parse_posix_tz(posix_tz, rule)) {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    s_rule = rule;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

esp_err_t start(const char* ntp_server, const char* posix_tz) noexcept {
    const bool tz_ok = set_timezone(posix_tz);
    if (!tz_ok) {
        ESP_LOGE(kTag, "time-zone rule \"%s\" rejected: showing UTC", (posix_tz != nullptr) ? posix_tz : "");
        (void)set_timezone("UTC0");
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server);
    cfg.sync_cb = on_sync;
    cfg.start = true;
    cfg.wait_for_sync = false;  // nobody blocks on it: the display shows --:-- meanwhile
    const esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "SNTP init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(kTag, "SNTP started (%s), rule %s", ntp_server, tz_ok ? posix_tz : "UTC0");
    return tz_ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

Snapshot now() noexcept {
    Snapshot s{};
    const int64_t epoch = static_cast<int64_t>(time(nullptr));
    taskENTER_CRITICAL(&s_mux);
    const timekeeping::TzRule rule = s_rule;
    const uint32_t t = now_ms();
    s.sure = s_validity.sure(t);
    s.age_ms = s_validity.age_ms(t);
    taskEXIT_CRITICAL(&s_mux);
    s.epoch_utc = epoch;
    s.local = timekeeping::to_local(epoch, rule);
    return s;
}

}  // namespace clock_time
