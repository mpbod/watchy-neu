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
