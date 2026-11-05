# Home Assistant transport (ESP-IDF): WebSocket client task around the pure
# `homeassistant::WsClient`, Open-Meteo fallback over HTTPS. Later: esp-mqtt publication.
idf_component_register(
    SRCS "src/ha_link.cpp"
    INCLUDE_DIRS "include"
    REQUIRES homeassistant settings clock_settings alerts
    PRIV_REQUIRES esp_websocket_client esp_http_client esp-tls mbedtls esp_timer log
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
)
