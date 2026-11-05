# Pure air-quality logic (ENS160 + AHT21) — NO ESP-IDF dependency (host-testable).
# The I2C bus is an injected interface; the sensor is a non-blocking state machine.
idf_component_register(
    SRCS
        "src/aht21.cpp"
        "src/ens160.cpp"
        "src/sensor.cpp"
        "src/levels.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
