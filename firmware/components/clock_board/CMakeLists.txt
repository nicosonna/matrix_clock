# Pin profiles, selected at compile time through Kconfig.
# HEADER-ONLY component: everything is `constexpr`, no runtime cost, no allocation.
#
# NAME: prefixed `clock_` like every ESP-IDF adapter of this project, so it can never
# shadow a core ESP-IDF component (a project component named `hal` silently replaces
# ESP-IDF's own `hal` and nothing compiles any more).
idf_component_register(
    INCLUDE_DIRS "include"
)
