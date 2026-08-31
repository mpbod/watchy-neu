# Watchy package project template for ESP-IDF 5.5.0 and elf_loader 1.3.3.
# Include after project() and include(elf_loader), then call watchy_project_so().

set(WATCHY_PACKAGE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(watchy_package_add_ui_sources target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "watchy_package_add_ui_sources requires an existing target: ${target}")
  endif()

  get_filename_component(watchy_sdk_dir "${WATCHY_PACKAGE_CMAKE_DIR}/.." ABSOLUTE)
  target_sources(${target} PRIVATE
    "${watchy_sdk_dir}/ui/src/ui_draw.c"
    "${watchy_sdk_dir}/ui/generated/watchy_fonts.c"
  )
  target_include_directories(${target} PUBLIC
    "${watchy_sdk_dir}/include"
    "${watchy_sdk_dir}/ui/include"
    "${watchy_sdk_dir}/ui/generated"
  )
  target_compile_features(${target} PRIVATE c_std_11)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
  )
endfunction()

function(watchy_project_so package_name)
  if(NOT IDF_TARGET STREQUAL "esp32")
    message(FATAL_ERROR "Watchy 2.0 packages must target esp32")
  endif()

  idf_component_get_property(package_component main COMPONENT_LIB)
  get_filename_component(watchy_sdk_dir "${WATCHY_PACKAGE_CMAKE_DIR}/.." ABSOLUTE)
  target_sources(${package_component} PRIVATE
    "${watchy_sdk_dir}/runtime/package_memory.cpp"
  )
  target_compile_features(${package_component} PRIVATE cxx_std_17)
  target_compile_options(${package_component} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fno-builtin
  )

  # The 1.3.3 project_so macro discovers C bridge files itself. The actual C++
  # package is pulled from main's archive by the bridge's entry-point reference.
  set(watchy_linker "${watchy_sdk_dir}/ld/watchy_package_linker.o")
  set(ELF_LIBS
    "${CMAKE_BINARY_DIR}/esp-idf/main/libmain.a"
    "-Wl,-T,${watchy_linker}"
  )
  project_so(${package_name})

  if(NOT DEFINED WATCHY_PACKAGE_TOOL)
    get_filename_component(WATCHY_PACKAGE_TOOL "${watchy_sdk_dir}/../tools/watchy_pkg.py" ABSOLUTE)
  endif()
  if(NOT EXISTS "${WATCHY_PACKAGE_TOOL}")
    message(FATAL_ERROR "Set WATCHY_PACKAGE_TOOL to tools/watchy_pkg.py")
  endif()
  add_custom_command(TARGET so POST_BUILD
    COMMAND ${PYTHON} ${WATCHY_PACKAGE_TOOL} finalize-elf
            --elf ${CMAKE_BINARY_DIR}/${package_name}.so
    COMMENT "Set package ELF entry point to watchy_package_entry"
    VERBATIM
  )
endfunction()
