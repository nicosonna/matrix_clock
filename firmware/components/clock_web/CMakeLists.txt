# Network and web server (ESP-IDF): Wi-Fi station supervised by the pure
# `network` component, access point + captive portal, DNS, HTTP server serving
# the embedded pages, mDNS, inbound notification channel. No business logic here.
idf_component_register(
    SRCS "src/network.cpp" "src/web_server.cpp"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "www/wifi.html"
    REQUIRES alerts
    PRIV_REQUIRES network clock_board esp_wifi esp_netif esp_event esp_http_server esp_timer
                  nvs_flash esp_app_format lwip log espressif__mdns
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
)
