# Pure brightness logic — NO ESP-IDF dependency (host-testable).
idf_component_register(
    SRCS
        "src/filters.cpp"
        "src/brightness.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
