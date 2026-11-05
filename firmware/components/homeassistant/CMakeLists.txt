# Pure Home Assistant logic — NO ESP-IDF dependency (host-testable):
# allocation-free JSON reader, tracked values, WebSocket client state machine,
# frame reassembly, Open-Meteo fallback, MQTT discovery (publication).
# NAME: never `json` — ESP-IDF has a component of that name (cJSON).
idf_component_register(
    SRCS
        "src/json_reader.cpp"
        "src/values.cpp"
        "src/reassembler.cpp"
        "src/ws_client.cpp"
        "src/weather.cpp"
    INCLUDE_DIRS "include"
    REQUIRES alerts
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
