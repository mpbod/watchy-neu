idf_component_register(
    SRCS
        "src/elf_validate.c"
        "src/host_caps.c"
        "src/idf_runtime.c"
        "src/package_format.c"
        "src/package_install.c"
        "src/package_runtime.c"
        "src/package_state.c"
        "src/package_validate.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        watchy_core
        watchy_hal
        elf_loader
        mbedtls
        nvs_flash
        esp_timer
        esp_system
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
