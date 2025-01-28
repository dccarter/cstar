
cmake_minimum_required(VERSION 3.16)
project (yaml C)

set (YAML_VERSION_MAJOR 0)
set (YAML_VERSION_MINOR 2)
set (YAML_VERSION_PATCH 5)
set (YAML_VERSION_STRING "${YAML_VERSION_MAJOR}.${YAML_VERSION_MINOR}.${YAML_VERSION_PATCH}")

option(BUILD_SHARED_LIBS "Build libyaml as a shared library" OFF)
set(YAML_STATIC_LIB_NAME "yaml" CACHE STRING "Base name of static library output")

#
# Output directories for a build tree
#
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

#
# Install relative directories
#
if(NOT DEFINED INSTALL_LIB_DIR)
  set(INSTALL_LIB_DIR lib)
endif()
if(NOT DEFINED INSTALL_BIN_DIR)
  set(INSTALL_BIN_DIR bin)
endif()
if(NOT DEFINED INSTALL_INCLUDE_DIR)
  set(INSTALL_INCLUDE_DIR include)
endif()
if(NOT DEFINED INSTALL_CMAKE_DIR)
  set(INSTALL_CMAKE_DIR cmake)
endif()

#
# Build library
#
set(SRCS
  src/api.c
  src/dumper.c
  src/emitter.c
  src/loader.c
  src/parser.c
  src/reader.c
  src/scanner.c
  src/writer.c
  )

set(config_h ${CMAKE_CURRENT_BINARY_DIR}/include/config.h)
configure_file(
  cmake/config.h.in
  ${config_h}
  )

add_library(yaml ${SRCS})

if(NOT BUILD_SHARED_LIBS)
  set_target_properties(yaml
    PROPERTIES OUTPUT_NAME ${YAML_STATIC_LIB_NAME}
  )
endif()

set_target_properties(yaml
  PROPERTIES DEFINE_SYMBOL YAML_DECLARE_EXPORT
  )

target_compile_definitions(yaml
  PRIVATE HAVE_CONFIG_H
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:YAML_DECLARE_STATIC>
    $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_WARNINGS>
  )

target_include_directories(yaml PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${INSTALL_INCLUDE_DIR}>
  )
