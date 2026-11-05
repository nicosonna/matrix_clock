# Host build (PC) of the pure logic + tests. Independent of ESP-IDF.
# Run: bash firmware/test/run_host_tests.sh [--coverage]
cmake_minimum_required(VERSION 3.16)
project(matrix_clock_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLOCK_COVERAGE "Instrument for gcov" OFF)

set(DISPLAY ${CMAKE_CURRENT_SOURCE_DIR}/../components/display)
set(BRIGHT ${CMAKE_CURRENT_SOURCE_DIR}/../components/brightness)
set(NET ${CMAKE_CURRENT_SOURCE_DIR}/../components/network)
set(ALERTS ${CMAKE_CURRENT_SOURCE_DIR}/../components/alerts)
set(TIME ${CMAKE_CURRENT_SOURCE_DIR}/../components/timekeeping)
set(SET ${CMAKE_CURRENT_SOURCE_DIR}/../components/settings)
set(AIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/air_quality)
set(HA ${CMAKE_CURRENT_SOURCE_DIR}/../components/homeassistant)

add_executable(test_clock
    test_main.cpp
    test_display.cpp
    test_text.cpp
    test_scenes.cpp
    test_brightness.cpp
    test_network.cpp
    test_alerts.cpp
    test_alert_engine.cpp
    test_timekeeping.cpp
    test_settings.cpp
    test_air_quality.cpp
    test_homeassistant.cpp
    test_mqtt.cpp
    ${HA}/src/json_reader.cpp
    ${HA}/src/values.cpp
    ${HA}/src/reassembler.cpp
    ${HA}/src/ws_client.cpp
    ${HA}/src/weather.cpp
    ${HA}/src/mqtt.cpp
    ${AIR}/src/aht21.cpp
    ${AIR}/src/ens160.cpp
    ${AIR}/src/sensor.cpp
    ${AIR}/src/levels.cpp
    ${DISPLAY}/src/air_logo.cpp
    ${DISPLAY}/src/icon_zone.cpp
    ${DISPLAY}/src/value_icons.cpp
    ${SET}/src/json_writer.cpp
    ${SET}/src/model.cpp
    ${SET}/src/forms.cpp
    ${SET}/src/json.cpp
    ${TIME}/src/tz.cpp
    ${TIME}/src/local_time.cpp
    ${TIME}/src/validity.cpp
    ${ALERTS}/src/notification.cpp
    ${ALERTS}/src/queue.cpp
    ${ALERTS}/src/rules.cpp
    ${BRIGHT}/src/filters.cpp
    ${BRIGHT}/src/brightness.cpp
    ${NET}/src/credentials.cpp
    ${NET}/src/captive_dns.cpp
    ${NET}/src/supervisor.cpp
    ${DISPLAY}/src/frame_buffer.cpp
    ${DISPLAY}/src/panel_map.cpp
    ${DISPLAY}/src/current_limit.cpp
    ${DISPLAY}/src/render.cpp
    ${DISPLAY}/src/fonts.cpp
    ${DISPLAY}/src/text.cpp
    ${DISPLAY}/src/clock_scene.cpp
    ${DISPLAY}/src/scroll.cpp
    ${DISPLAY}/src/scheduler.cpp
    ${DISPLAY}/src/test_patterns.cpp
)
target_include_directories(test_clock PRIVATE
    ${DISPLAY}/include
    ${BRIGHT}/include
    ${NET}/include
    ${ALERTS}/include
    ${TIME}/include
    ${SET}/include
    ${AIR}/include
    ${HA}/include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_clock PRIVATE
    CLOCK_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    # Embedded pages, checked against the settings model (no orphan field).
    CLOCK_WWW="${CMAKE_CURRENT_SOURCE_DIR}/../components/clock_web/www")

# Safety standard (CLAUDE.md): no exceptions, no RTTI, strict warnings as errors —
# an implicit conversion or a shadowed variable must not pass.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_clock PRIVATE
        -fno-exceptions -fno-rtti
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion
        -Wshadow -Wold-style-cast -Wuseless-cast
        -Wdouble-promotion -Wfloat-equal
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Werror
    )
    if(CLOCK_COVERAGE)
        target_compile_options(test_clock PRIVATE --coverage -O0)
        target_link_options(test_clock PRIVATE --coverage)
    endif()
endif()

enable_testing()
add_test(NAME test_clock COMMAND test_clock)
