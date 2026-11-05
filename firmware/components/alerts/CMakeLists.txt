# Pure alert logic — NO ESP-IDF dependency (host-testable).
# For now: the inbound notification channel's decoder, shared by every entry
# point (local HTTP POST, Home Assistant event, hub command). The rule engine
# and the notification queue of spec-alertes.md come next.
idf_component_register(
    SRCS "src/notification.cpp"
    INCLUDE_DIRS "include"
    REQUIRES network
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
