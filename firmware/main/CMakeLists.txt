idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES display brightness alerts clock_board clock_display clock_hal clock_web app_update esp_timer
)

# Same warnings as the host build: an implicit conversion must not pass.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
