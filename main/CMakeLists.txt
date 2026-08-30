idf_component_register(
    SRCS
        "app_hooks.c"
        "main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        watchy_hal
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
