# Pure alert logic — NO ESP-IDF dependency (host-testable): the inbound
# notification decoder shared by every entry point (local HTTP POST, Home
# Assistant event), the bounded notification queue with quiet hours, and the
# rule engine of spec-alertes.md.
idf_component_register(
    SRCS "src/notification.cpp" "src/queue.cpp" "src/rules.cpp"
    INCLUDE_DIRS "include"
    REQUIRES network
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
