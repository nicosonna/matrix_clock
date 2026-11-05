# ESP-IDF adapter for the time: SNTP provides the UTC epoch, the pure
# `timekeeping` component turns it into local time and decides whether it can
# be trusted. Nothing here touches the libc's TZ.
idf_component_register(
    SRCS "src/time_source.cpp"
    INCLUDE_DIRS "include"
    REQUIRES timekeeping
    PRIV_REQUIRES esp_netif lwip esp_timer log
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
)
