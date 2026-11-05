# Pure network logic — NO ESP-IDF dependency (host-testable).
# Taken from ThermoSync's `reseau` component (itself the behaviour of the
# Ev_charger), translated to English at copy time, plus the power-cut
# extension (boot grace, DHCP-less association) of spec-reseau-portail.md.
idf_component_register(
    SRCS
        "src/credentials.cpp"
        "src/captive_dns.cpp"
        "src/supervisor.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
