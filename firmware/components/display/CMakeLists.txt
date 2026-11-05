# Pure display logic — NO ESP-IDF dependency (host-testable).
idf_component_register(
    SRCS
        "src/frame_buffer.cpp"
        "src/panel_map.cpp"
        "src/current_limit.cpp"
        "src/render.cpp"
        "src/fonts.cpp"
        "src/text.cpp"
        "src/clock_scene.cpp"
        "src/scroll.cpp"
        "src/scheduler.cpp"
        "src/test_patterns.cpp"
    INCLUDE_DIRS "include"
)

# Same warnings as the host build (CLAUDE.md): an implicit conversion or a
# shadowed variable must not pass.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
