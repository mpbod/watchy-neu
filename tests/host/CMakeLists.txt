cmake_minimum_required(VERSION 3.16)
project(watchy_host_tests LANGUAGES C CXX)
enable_testing()

include(../../sdk/cmake/WatchyPackage.cmake)

add_executable(watchy_core_tests
  test_core.c
  ../../components/watchy_core/src/wpk.c
  ../../components/watchy_core/src/runtime.c
  ../../components/watchy_core/src/refresh_policy.c
  ../../components/watchy_core/src/transition_compositor.c
  ../../components/watchy_core/src/transition_executor.c
  ../../components/watchy_core/src/transition_policy.c
  ../../components/watchy_core/src/watchdog.c
)
target_include_directories(watchy_core_tests PRIVATE
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_core_tests PRIVATE c_std_11)
target_compile_options(watchy_core_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_core_tests COMMAND watchy_core_tests)

add_executable(watchy_sdk_c_tests
  test_sdk_c.c
)
target_include_directories(watchy_sdk_c_tests PRIVATE
  ../../sdk/include
)
target_compile_features(watchy_sdk_c_tests PRIVATE c_std_11)
target_compile_options(watchy_sdk_c_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_sdk_c_tests COMMAND watchy_sdk_c_tests)

add_executable(watchy_sdk_cpp_tests
  test_sdk_cpp.cpp
)
target_include_directories(watchy_sdk_cpp_tests PRIVATE
  ../../sdk/include
)
target_compile_features(watchy_sdk_cpp_tests PRIVATE cxx_std_17)
target_compile_options(watchy_sdk_cpp_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_sdk_cpp_tests COMMAND watchy_sdk_cpp_tests)

add_executable(watchy_digital_sample_tests
  test_digital_sample.cpp
)
target_include_directories(watchy_digital_sample_tests PRIVATE
  ../../sdk/include
  ../../samples/common
)
target_compile_features(watchy_digital_sample_tests PRIVATE cxx_std_17)
target_compile_options(watchy_digital_sample_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_digital_sample_tests COMMAND watchy_digital_sample_tests)

add_executable(watchy_hardware_sample_tests
  test_hardware_sample.cpp
  ../../components/watchy_packages/src/package_policy.c
  ../../components/watchy_packages/src/package_format.c
  ../../components/watchy_core/src/transition_policy.c
)
target_include_directories(watchy_hardware_sample_tests PRIVATE
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../components/watchy_hal/include
  ../../sdk/include
  ../../samples/common
)
target_compile_features(watchy_hardware_sample_tests PRIVATE cxx_std_17)
target_compile_options(watchy_hardware_sample_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_hardware_sample_tests COMMAND watchy_hardware_sample_tests)

add_executable(watchy_hal_tests
  test_hal.c
  ../../components/watchy_core/src/transition_compositor.c
  ../../components/watchy_core/src/transition_executor.c
  ../../components/watchy_hal/src/framebuffer.c
  ../../components/watchy_hal/src/buttons_policy.c
  ../../components/watchy_hal/src/battery_policy.c
  ../../components/watchy_hal/src/power_policy.c
  ../../components/watchy_hal/src/display_policy.c
  ../../components/watchy_hal/src/rtc_calendar.c
  ../../components/watchy_hal/src/radios_policy.c
  ../../components/watchy_hal/src/storage_policy.c
)
target_include_directories(watchy_hal_tests PRIVATE
  ../../components/watchy_core/include
  ../../components/watchy_hal/include
  ../../sdk/include
)
target_compile_features(watchy_hal_tests PRIVATE c_std_11)
target_compile_options(watchy_hal_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_hal_tests COMMAND watchy_hal_tests)

add_executable(watchy_packages_tests
  test_packages.c
  ../../components/watchy_packages/src/package_format.c
  ../../components/watchy_packages/src/elf_validate.c
  ../../components/watchy_packages/src/package_validate.c
  ../../components/watchy_packages/src/package_state.c
  ../../components/watchy_packages/src/package_runtime.c
  ../../components/watchy_packages/src/package_install.c
  ../../components/watchy_packages/src/package_policy.c
  ../../components/watchy_packages/src/sha256.c
  ../../components/watchy_core/src/wpk.c
  ../../components/watchy_core/src/runtime.c
  ../../components/watchy_core/src/transition_policy.c
)
target_include_directories(watchy_packages_tests PRIVATE
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_packages_tests PRIVATE c_std_11)
target_compile_options(watchy_packages_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_packages_tests COMMAND watchy_packages_tests)

add_executable(watchy_shell_tests
  test_shell.c
  ../../components/watchy_shell/src/settings_policy.c
  ../../components/watchy_shell/src/shell_policy.c
  ../../components/watchy_core/src/transition_policy.c
  ../../components/watchy_hal/src/framebuffer.c
  ../../components/watchy_packages/src/package_format.c
)
watchy_package_add_ui_sources(watchy_shell_tests)
target_include_directories(watchy_shell_tests PRIVATE
  ../../components/watchy_shell/include
  ../../components/watchy_hal/include
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_shell_tests PRIVATE c_std_11)
target_compile_options(watchy_shell_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_shell_tests COMMAND watchy_shell_tests)

add_executable(watchy_watchface_action_tests
  test_watchface_action.c
  ../../components/watchy_shell/src/watchface_action_policy.c
)
target_include_directories(watchy_watchface_action_tests PRIVATE
  ../../components/watchy_shell/include
  ../../components/watchy_hal/include
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_watchface_action_tests PRIVATE c_std_11)
target_compile_options(watchy_watchface_action_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_watchface_action_tests COMMAND watchy_watchface_action_tests)

add_executable(watchy_shell_render_tests
  test_shell_render.c
  ../../components/watchy_shell/src/shell_render.c
  ../../components/watchy_shell/src/settings_policy.c
  ../../components/watchy_shell/src/shell_policy.c
  ../../components/watchy_core/src/transition_policy.c
  ../../components/watchy_hal/src/framebuffer.c
  ../../components/watchy_packages/src/package_format.c
)
watchy_package_add_ui_sources(watchy_shell_render_tests)
target_include_directories(watchy_shell_render_tests PRIVATE
  ../../components/watchy_shell/include
  ../../components/watchy_hal/include
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_shell_render_tests PRIVATE c_std_11)
target_compile_options(watchy_shell_render_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(watchy_shell_render_tests PRIVATE
  WATCHY_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../golden/shell")
add_test(NAME watchy_shell_render_tests COMMAND watchy_shell_render_tests)
add_test(NAME watchy_shell_render_golden_mutation
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:watchy_shell_render_tests>
          -DGOLDEN=${CMAKE_CURRENT_LIST_DIR}/../golden/shell/menu.pbm
          -DMUTATED=${CMAKE_CURRENT_BINARY_DIR}/mutated-menu.pbm
          -P ${CMAKE_CURRENT_LIST_DIR}/check_shell_golden_mutation.cmake)

add_executable(watchy_portal_tests
  test_portal.c
  ../../components/watchy_shell/src/portal_policy.c
  ../../components/watchy_packages/src/package_format.c
)
target_include_directories(watchy_portal_tests PRIVATE
  ../../components/watchy_shell/include
  ../../components/watchy_hal/include
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_portal_tests PRIVATE c_std_11)
target_compile_options(watchy_portal_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME watchy_portal_tests COMMAND watchy_portal_tests)

add_executable(watchy_ui_draw_tests
  test_ui_draw.c
)
watchy_package_add_ui_sources(watchy_ui_draw_tests)
target_compile_features(watchy_ui_draw_tests PRIVATE c_std_11)
target_compile_options(watchy_ui_draw_tests PRIVATE -Wall -Wextra -Werror)
if(CMAKE_NM AND (APPLE OR UNIX))
  if(APPLE)
    target_link_options(watchy_ui_draw_tests PRIVATE -Wl,-dead_strip)
  else()
    target_link_options(watchy_ui_draw_tests PRIVATE -Wl,--gc-sections)
  endif()
  add_custom_command(TARGET watchy_ui_draw_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DNM=${CMAKE_NM}"
            "-DBINARY=$<TARGET_FILE:watchy_ui_draw_tests>"
            -P "${CMAKE_CURRENT_LIST_DIR}/check_ui_sections.cmake"
    VERBATIM
  )
endif()
add_test(NAME watchy_ui_draw_tests COMMAND watchy_ui_draw_tests)

add_executable(watchy_first_party_face_tests
  test_first_party_faces.cpp
  ../../first_party/watchfaces/common/src/face.cpp
  ../../first_party/watchfaces/grid-01/main/renderer.cpp
  ../../first_party/watchfaces/grid-01/main/package.cpp
  ../../first_party/watchfaces/grid-02/main/renderer.cpp
  ../../first_party/watchfaces/grid-02/main/package.cpp
  ../../first_party/watchfaces/grid-03/main/renderer.cpp
  ../../first_party/watchfaces/grid-03/main/package.cpp
  ../../first_party/watchfaces/term-01/main/renderer.cpp
  ../../first_party/watchfaces/term-01/main/package.cpp
  ../../first_party/watchfaces/term-02/main/renderer.cpp
  ../../first_party/watchfaces/term-02/main/package.cpp
  ../../first_party/watchfaces/term-03/main/renderer.cpp
  ../../first_party/watchfaces/term-03/main/package.cpp
)
target_include_directories(watchy_first_party_face_tests PRIVATE
  ../../first_party/watchfaces/common/include
  ../../sdk/include
  ../../components/watchy_packages/include
  ../../sdk/ui/include
  ../../sdk/ui/generated
)
watchy_package_add_ui_sources(watchy_first_party_face_tests)
target_compile_features(watchy_first_party_face_tests PRIVATE cxx_std_17)
target_compile_options(watchy_first_party_face_tests PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fno-threadsafe-statics)
target_compile_definitions(watchy_first_party_face_tests PRIVATE WATCHY_HOST_TEST=1)
target_compile_definitions(watchy_first_party_face_tests PRIVATE
  WATCHY_FACE_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../golden/faces")
add_test(NAME watchy_first_party_face_tests COMMAND watchy_first_party_face_tests)

add_executable(watchy_elf_fixture_validator
  validate_elf_fixture.c
  ../../components/watchy_packages/src/elf_validate.c
)
target_include_directories(watchy_elf_fixture_validator PRIVATE
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_elf_fixture_validator PRIVATE c_std_11)
target_compile_options(watchy_elf_fixture_validator PRIVATE -Wall -Wextra -Werror)

add_executable(watchy_wpk_fixture_validator
  validate_wpk_fixture.c
  ../../components/watchy_packages/src/package_format.c
  ../../components/watchy_packages/src/elf_validate.c
  ../../components/watchy_packages/src/package_validate.c
  ../../components/watchy_packages/src/sha256.c
  ../../components/watchy_core/src/wpk.c
  ../../components/watchy_core/src/runtime.c
)
target_include_directories(watchy_wpk_fixture_validator PRIVATE
  ../../components/watchy_packages/include
  ../../components/watchy_core/include
  ../../sdk/include
)
target_compile_features(watchy_wpk_fixture_validator PRIVATE c_std_11)
target_compile_options(watchy_wpk_fixture_validator PRIVATE -Wall -Wextra -Werror)
