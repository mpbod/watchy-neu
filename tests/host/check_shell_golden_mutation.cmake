if(NOT DEFINED TEST_EXE OR NOT DEFINED GOLDEN OR NOT DEFINED MUTATED)
  message(FATAL_ERROR "TEST_EXE, GOLDEN, and MUTATED are required")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${GOLDEN}" "${MUTATED}"
                RESULT_VARIABLE copy_result)
if(copy_result)
  message(FATAL_ERROR "could not copy golden")
endif()

execute_process(
  COMMAND python3 -c
    "import pathlib,sys; p=pathlib.Path(sys.argv[1]); b=bytearray(p.read_bytes()); b[-1]^=1; p.write_bytes(b)"
    "${MUTATED}"
  RESULT_VARIABLE mutate_result)
if(mutate_result)
  message(FATAL_ERROR "could not mutate golden")
endif()

execute_process(COMMAND "${TEST_EXE}" --compare-menu "${MUTATED}"
                RESULT_VARIABLE compare_result)
if(compare_result EQUAL 0)
  message(FATAL_ERROR "mutated shell golden was accepted")
endif()
message(STATUS "mutated shell golden rejected as expected")
