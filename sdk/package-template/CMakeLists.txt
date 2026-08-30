cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(watchy_package)

include(elf_loader)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WatchyPackage.cmake)
watchy_project_so(watchy_package)
