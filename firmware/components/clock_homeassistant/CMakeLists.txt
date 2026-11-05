# Home Assistant transport (ESP-IDF): WebSocket client task around the pure
# `homeassistant::WsClient`, Open-Meteo fallback over HTTPS, esp-mqtt publication task
# around the pure `homeassistant::MqttPublisher`.
idf_component_register(
    SRCS "src/ha_link.cpp" "src/mqtt_link.cpp"
    INCLUDE_DIRS "include"
    REQUIRES homeassistant settings clock_settings alerts air_quality
    PRIV_REQUIRES esp_websocket_client esp_http_client esp-tls mbedtls esp_timer log mqtt esp_app_format esp_netif clock_board
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
)
