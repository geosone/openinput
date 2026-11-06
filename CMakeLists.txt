# SPDX-License-Identifier: GPL-2.0-or-later
#
# openinput: keyboards and mice as events in HID usages (C90, polled) - USB
# HID on eusbcore, PS/2 - for 32/64-bit kernels such as GonWare's GDOS.
#
# Needs OpenASPI's host services and eusbcore: a parent project's
# OpenASPI::host and EUSBCORE::core, else OPENASPI_SOURCE_DIR (default:
# ../OpenAspi) and the submodule third_party/eusbcore (or ../eusbcore).

cmake_minimum_required(VERSION 3.20)
project(openinput LANGUAGES C)

if(NOT TARGET OpenASPI::host)
  set(OPENASPI_SOURCE_DIR "" CACHE PATH "OpenASPI source tree")
  if(NOT OPENASPI_SOURCE_DIR)
    foreach(_d OpenAspi OpenASPI)
      if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../${_d}/CMakeLists.txt")
        set(OPENASPI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${_d}")
      endif()
    endforeach()
  endif()
  if(NOT OPENASPI_SOURCE_DIR)
    message(FATAL_ERROR "openinput: set OPENASPI_SOURCE_DIR to an OpenASPI checkout")
  endif()
  add_subdirectory("${OPENASPI_SOURCE_DIR}" openaspi EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET EUSBCORE::core)
  set(EUSBCORE_SOURCE_DIR "" CACHE PATH "eusbcore source tree")
  if(NOT EUSBCORE_SOURCE_DIR)
    foreach(_d "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eusbcore" "${CMAKE_CURRENT_SOURCE_DIR}/../eusbcore")
      if(NOT EUSBCORE_SOURCE_DIR AND EXISTS "${_d}/CMakeLists.txt")
        set(EUSBCORE_SOURCE_DIR "${_d}")
      endif()
    endforeach()
  endif()
  if(NOT EUSBCORE_SOURCE_DIR)
    message(FATAL_ERROR "openinput: eusbcore not found (git submodule update --init)")
  endif()
  add_subdirectory("${EUSBCORE_SOURCE_DIR}" eusbcore EXCLUDE_FROM_ALL)
endif()

add_library(openinput STATIC src/event.c src/tables.c src/usbhid.c src/ps2.c src/virtio.c src/sermouse.c src/hidparse.c)
target_include_directories(openinput PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(openinput PUBLIC EUSBCORE::core OpenASPI::host)
set_target_properties(openinput PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_library(OpenInput::input ALIAS openinput)

if(PROJECT_IS_TOP_LEVEL)
  target_compile_options(openinput PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
