idf_component_register(
    SRCS
        "src/elf_validate.c"
        "src/host_caps.c"
        "src/idf_runtime.c"
        "src/package_format.c"
        "src/package_install.c"
        "src/package_policy.c"
        "src/package_runtime.c"
        "src/package_state.c"
        "src/package_validate.c"
        "src/sha256.c"
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
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Wframe-larger-than=2048
    -fstack-usage
)

# A real Xtensa ET_DYN fixture is a dependency in the native CMake component
# graph. PlatformIO's SCons bridge exposes the same target for the explicit
# validation smoke command. It is not flashed or exposed as a sample package.
set(WATCHY_SMOKE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../tests/fixtures/package_smoke.c")
set(WATCHY_SMOKE_LINKER "${CMAKE_CURRENT_LIST_DIR}/../../tests/fixtures/package_smoke.ld")
set(WATCHY_SMOKE_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/watchy_package_smoke.o")
set(WATCHY_SMOKE_SO "${CMAKE_CURRENT_BINARY_DIR}/watchy_package_smoke.so")
add_custom_command(
    OUTPUT "${WATCHY_SMOKE_SO}"
    COMMAND ${CMAKE_C_COMPILER} -c -fPIC -fvisibility=hidden
            -ffunction-sections -fdata-sections
            -I${CMAKE_CURRENT_LIST_DIR}/../../sdk/include
            "${WATCHY_SMOKE_SOURCE}" -o "${WATCHY_SMOKE_OBJECT}"
    COMMAND ${CMAKE_C_COMPILER} -shared -fPIC -nostdlib -nostartfiles -static-libgcc
            -Wl,-e,watchy_package_entry -Wl,-T,${WATCHY_SMOKE_LINKER}
            -Wl,--gc-sections -Wl,--allow-shlib-undefined
            -o "${WATCHY_SMOKE_SO}" "${WATCHY_SMOKE_OBJECT}"
    DEPENDS "${WATCHY_SMOKE_SOURCE}" "${WATCHY_SMOKE_LINKER}"
    VERBATIM
)
add_custom_target(watchy_package_xtensa_smoke ALL DEPENDS "${WATCHY_SMOKE_SO}")
add_dependencies(${COMPONENT_LIB} watchy_package_xtensa_smoke)
