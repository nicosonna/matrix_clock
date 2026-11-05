idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES display brightness alerts timekeeping settings air_quality homeassistant clock_homeassistant clock_settings clock_board clock_display clock_hal clock_web clock_time app_update esp_timer
)

# Same warnings as the host build: an implicit conversion must not pass.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)

# Test build for HW-WEB-03 (OTA rollback): the image boots but never confirms itself, so the
# next reset falls back to the previous image. Never for a release:
#   idf.py -B build-norollback -DPROJECT_VER=0.1.0-norollback -DCLOCK_OTA_NEVER_CONFIRM=1 build
if(CLOCK_OTA_NEVER_CONFIRM)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CLOCK_OTA_NEVER_CONFIRM=1)
endif()

# Test build for EX-60 (watchdog): the display task locks up after 120 s; the task watchdog on
# the starved idle task must reset the board, and /api/info must then say so. Never for a release:
#   idf.py -B build-wdt -DPROJECT_VER=0.1.0-wdt -DCLOCK_TEST_LOCKUP_S=120 build
if(CLOCK_TEST_LOCKUP_S)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CLOCK_TEST_LOCKUP_S=${CLOCK_TEST_LOCKUP_S})
endif()
