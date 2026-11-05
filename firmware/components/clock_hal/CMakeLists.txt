# ESP-IDF adapters for the sensors: photoresistor on ADC1 (oneshot + calibration).
# Later: I2C bus for the ENS160 + AHT21 module, NVS.
# NAME: `clock_hal`, never `hal` — a project component named `hal` silently replaces
# ESP-IDF's own and nothing compiles any more.
idf_component_register(
    SRCS "src/ldr_adc.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc clock_board brightness
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
