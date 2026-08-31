get_filename_component(WATCHY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)
idf_component_register(
  SRCS "package.cpp" "renderer.cpp" "project_so_bridge.c"
       "${WATCHY_ROOT}/first_party/watchfaces/common/src/face.cpp"
       "${WATCHY_ROOT}/sdk/ui/src/ui_draw.c"
       "${WATCHY_ROOT}/sdk/ui/generated/watchy_fonts.c"
  INCLUDE_DIRS "${WATCHY_ROOT}/sdk/include"
                "${WATCHY_ROOT}/sdk/ui/include"
                "${WATCHY_ROOT}/sdk/ui/generated"
                "${WATCHY_ROOT}/first_party/watchfaces/common/include")
