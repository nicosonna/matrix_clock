# Pure time logic — NO ESP-IDF dependency (host-testable).
# POSIX time-zone rules, UTC epoch -> local civil time with DST, and the
# "is the time trustworthy" rule. The libc's TZ handling is not used: MinGW's
# does not understand POSIX M-rules, so it could not be tested on the host.
idf_component_register(
    SRCS
        "src/tz.cpp"
        "src/local_time.cpp"
        "src/validity.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
