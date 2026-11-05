# Network and web server (ESP-IDF): Wi-Fi station supervised by the pure
# `network` component, access point + captive portal, DNS, HTTP server serving
# the embedded pages, mDNS, inbound notification channel. No business logic here.
idf_component_register(
    SRCS "src/network.cpp" "src/web_server.cpp"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "www/wifi.html"
    REQUIRES alerts
    PRIV_REQUIRES network settings clock_settings brightness air_quality homeassistant clock_board esp_wifi esp_netif esp_event esp_http_server
                  esp_timer nvs_flash esp_app_format app_update lwip log espressif__mdns
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
)

# The settings page is served gzipped (65 KB -> 17 KB, budget 60 KB of EX-52): compressed at
# build time by the IDF Python, embedded as a binary, sent with Content-Encoding: gzip.
idf_build_get_property(python PYTHON)
set(APP_HTML "${CMAKE_CURRENT_SOURCE_DIR}/www/app.html")
set(APP_GZ "${CMAKE_CURRENT_BINARY_DIR}/app.html.gz")
add_custom_command(
    OUTPUT "${APP_GZ}"
    COMMAND "${python}" -c "import gzip,sys;open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(),9,mtime=0))" "${APP_HTML}" "${APP_GZ}"
    DEPENDS "${APP_HTML}"
    VERBATIM
)
add_custom_target(clock_web_app_gz DEPENDS "${APP_GZ}")
add_dependencies(${COMPONENT_LIB} clock_web_app_gz)
target_add_binary_data(${COMPONENT_LIB} "${APP_GZ}" BINARY)
