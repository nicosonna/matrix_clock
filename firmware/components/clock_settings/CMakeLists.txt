# Settings store (ESP-IDF): versioned NVS blobs per domain, a copy shared with
# every task under a mutex, a change counter the display task polls to apply
# new settings without a reboot. Validation is the pure `settings` component's.
idf_component_register(
    SRCS "src/store.cpp"
    INCLUDE_DIRS "include"
    REQUIRES settings air_quality homeassistant
    PRIV_REQUIRES nvs_flash log
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
)
