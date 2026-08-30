cmake_minimum_required(VERSION 3.16)
project(watchy_host_tests LANGUAGES C CXX)
enable_testing()

add_executable(watchy_core_tests
  test_core.c
  ../../components/watchy_core/src/wpk.c
  ../../components/watchy_core/src/runtime.c
  ../../components/watchy_core/src/refresh_policy.c
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

add_executable(watchy_hal_tests
  test_hal.c
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
  ../../components/watchy_shell/src/ui.c
  ../../components/watchy_hal/src/framebuffer.c
  ../../components/watchy_packages/src/package_format.c
)
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
