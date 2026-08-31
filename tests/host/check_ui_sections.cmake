if(NOT DEFINED NM OR NOT DEFINED BINARY)
  message(FATAL_ERROR "check_ui_sections.cmake requires NM and BINARY")
endif()

execute_process(
  COMMAND "${NM}" -g "${BINARY}"
  RESULT_VARIABLE nm_result
  OUTPUT_VARIABLE symbols
  ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
  message(FATAL_ERROR "nm failed (${nm_result}): ${nm_error}")
endif()
if(NOT symbols MATCHES "watchy_font_heros_62_bold")
  message(FATAL_ERROR "referenced Heros 62 strike was unexpectedly removed")
endif()
if(symbols MATCHES "watchy_font_heros_82_bold")
  message(FATAL_ERROR "unreferenced Heros 82 strike was not garbage-collected")
endif()
