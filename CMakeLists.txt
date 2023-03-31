CMAKE_MINIMUM_REQUIRED(VERSION 3.16)
set(CXY_VERSION 0.1.0 CACHE STRING "The version of cxy")
project(cxy
        VERSION ${CXY_VERSION}
        DESCRIPTION "Cxy is a transpiler for a high level programming language that tries to simplify C"
        LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_BUILD_TYPE)
    # default build key is Debug
    set(CMAKE_BUILD_TYPE Debug)
endif ()

set(CMAKE_C_FLAGS_DEBUG "-g")

# Configure path for loading project cmake scripts
set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(ENABLE_TESTS "Enable CMake tests" ON)

include_directories(src/cxy)

set(CXY_CORE_SOURCES
        src/cxy/core/array.c
        src/cxy/core/e4c.c
        src/cxy/core/format.c
        src/cxy/core/hash.c
        src/cxy/core/htable.c
        src/cxy/core/log.c
        src/cxy/core/mempool.c
        src/cxy/core/strpool.c
        src/cxy/core/utils.c)

set(CXY_LANG_SOURCES
        src/cxy/lang/ast.c
        src/cxy/lang/codegen.c
        src/cxy/lang/lexer.c
        src/cxy/lang/parser.c
        src/cxy/lang/tcheck.c
        src/cxy/lang/ttable.c
        src/cxy/lang/types.c
        src/cxy/lang/ccodegen/epilogue.c
        src/cxy/lang/ccodegen/generate.c
        src/cxy/lang/ccodegen/prologue.c)

set(CXY_DRIVER_SOURCES
        src/cxy/driver/driver.c
        src/cxy/driver/options.c)

add_executable(cxy
        src/cxy/driver/main.c
        ${CXY_CORE_SOURCES}
        ${CXY_LANG_SOURCES}
        ${CXY_DRIVER_SOURCES})

target_compile_definitions(cxy PRIVATE
        -DCXY_VERSION="${CXY_VERSION}"
        -DCXY_SOURCE_LANG_DIR="${CMAKE_SOURCE_DIR}/src/cxy/lang")

