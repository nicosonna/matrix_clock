# ESP-IDF adapter for the WS2812B panel: espressif/led_strip (managed component,
# see idf_component.yml), SPI backend with DMA by default — the classic ESP32's
# RMT has no DMA and 256 LEDs mean 7.7 ms of interrupt-fed transmission per frame.
idf_component_register(
    SRCS "src/led_panel.cpp"
    INCLUDE_DIRS "include"
    # led_strip is REQUIRES (not PRIV_REQUIRES): led_panel.hpp includes led_strip.h.
    REQUIRES display clock_board espressif__led_strip
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
