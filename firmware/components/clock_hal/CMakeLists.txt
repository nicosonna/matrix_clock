# ESP-IDF adapters for the sensors: photoresistor on ADC1 (oneshot + calibration),
# I2C master bus (ENS160 + AHT21) with recovery of a stuck bus.
# NAME: `clock_hal`, never `hal` — a project component named `hal` silently replaces
# ESP-IDF's own and nothing compiles any more.
idf_component_register(
    SRCS "src/ldr_adc.cpp" "src/i2c_bus.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc esp_driver_i2c esp_driver_gpio clock_board brightness air_quality
    PRIV_REQUIRES log esp_rom
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
