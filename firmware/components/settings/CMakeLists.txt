# Pure settings model — NO ESP-IDF dependency (host-testable).
# The single source of truth for every bound the web page shows: the model
# JSON served on /api/<domaine>/modele is generated from the same tables the
# form decoder uses, and the host tests check the page against them.
idf_component_register(
    SRCS
        "src/json_writer.cpp"
        "src/model.cpp"
        "src/forms.cpp"
        "src/json.cpp"
    INCLUDE_DIRS "include"
    REQUIRES display brightness network timekeeping air_quality
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
