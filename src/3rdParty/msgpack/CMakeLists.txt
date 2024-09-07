cmake_minimum_required(VERSION 3.16)
PROJECT (msgpack-c C)

list (APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")
set (prefix ${CMAKE_INSTALL_PREFIX})
set (exec_prefix "\${prefix}")
set (libdir "\${exec_prefix}/lib")
set (includedir "\${prefix}/include")

option (MSGPACK_32BIT "32bit compile" OFF)

INCLUDE(TestBigEndian)
TEST_BIG_ENDIAN(BIGENDIAN)
if (BIGENDIAN)
    set(MSGPACK_ENDIAN_BIG_BYTE 1)
    set(MSGPACK_ENDIAN_LITTLE_BYTE 0)
else ()
    set(MSGPACK_ENDIAN_BIG_BYTE 0)
        set(MSGPACK_ENDIAN_LITTLE_BYTE 1)
endif ()

configure_file (
    cmake/sysdep.h.in
    include/msgpack/sysdep.h
    @ONLY
)

configure_file (
    cmake/pack_template.h.in
    include/msgpack/pack_template.h
    @ONLY
)

if (APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
    if ("${isSystemDir}" STREQUAL "-1")
        set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
    endif ()
endif ()

if (MSGPACK_32BIT)
   if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
      set (CMAKE_C_FLAGS "-m32 ${CMAKE_C_FLAGS}")
      set (CMAKE_EXE_LINKER_FLAGS "-m32 ${CMAKE_EXE_LINKER_FLAGS}")
   elseif ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
      set (CMAKE_C_FLAGS "-m32 ${CMAKE_C_FLAGS}")
      set (CMAKE_EXE_LINKER_FLAGS "-m32 ${CMAKE_EXE_LINKER_FLAGS}")
   endif ()
endif ()

if (MSGPACK_CHAR_SIGN)
   set (CMAKE_C_FLAGS "-f${MSGPACK_CHAR_SIGN}-char ${CMAKE_C_FLAGS}")
endif ()

include(Files.cmake)

configure_file(
    msgpack-c.pc.in
    msgpack-c.pc
    @ONLY
)

add_library(msgpack-c STATIC
    ${msgpack-c_SOURCES}
    ${msgpack-c_HEADERS}
)

target_include_directories(msgpack-c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include/msgpack>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
   set_property (TARGET msgpack-c APPEND_STRING PROPERTY COMPILE_FLAGS " -Wall -Wextra" )
endif ()

if ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
    set_property (TARGET msgpack-c APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-mismatched-tags")
endif ()
