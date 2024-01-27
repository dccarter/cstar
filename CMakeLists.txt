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

include(CxyUtils)
include(FetchContent)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

set(CMAKE_WARN_DEPRECATED OFF)
# Fetch message-pack library
set(MSGPACK_BUILD_TESTS OFF)
set(MSGPACK_BUILD_EXAMPLES OFF)
set(MSGPACK_ENABLE_STATIC ON)
set(MSGPACK_ENABLE_SHARED OFF)
set(MSGPACK_GEN_COVERAGE OFF)

cmake_policy(PUSH)
set(CMAKE_POLICY_DEFAULT_CMP0048 "NEW")
set(CMAKE_POLICY_DEFAULT_CMP0077 "NEW")
FetchContent_Declare(
        msgpack-c
        GIT_REPOSITORY https://github.com/msgpack/msgpack-c
        GIT_TAG c-6.0.0
)
FetchContent_MakeAvailable(msgpack-c)

set(BUILD_TESTING OFF)
FetchContent_Declare(
        libyaml
        GIT_REPOSITORY https://github.com/yaml/libyaml.git
        GIT_TAG master
)
FetchContent_MakeAvailable(libyaml)

cmake_policy(POP)

include_directories(src/cxy)

add_executable(amalgamate
        src/tools/amalgamate.c
        src/cxy/core/utils.c)

target_compile_definitions(amalgamate PRIVATE -DNO_EXEC_UTIL)

CxyAmalgamate(builtins
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
        src/cxy/lang/builtins.cxy)


set(CXY_3RD_PARTY_SOURCES
        src/cxy/3rdparty/cJSON.c)

set(CXY_CORE_SOURCES
        src/cxy/core/args.c
        src/cxy/core/array.c
        src/cxy/core/e4c.c
        src/cxy/core/format.c
        src/cxy/core/hash.c
        src/cxy/core/htable.c
        src/cxy/core/log.c
        src/cxy/core/mempool.c
        src/cxy/core/sb.c
        src/cxy/core/strpool.c
        src/cxy/core/utils.c)

set(CXY_3RD_PARTY_SOURCES
        src/cxy/3rdParty/cJSON.c)

set(CXY_FRONTEND_SOURCES
        src/cxy/lang/frontend/ast.c
        src/cxy/lang/frontend/capture.c
        src/cxy/lang/frontend/encode.c
        src/cxy/lang/frontend/decode.c
        src/cxy/lang/frontend/lexer.c
        src/cxy/lang/frontend/operator.c
        src/cxy/lang/frontend/parser.c
        src/cxy/lang/frontend/strings.c
        src/cxy/lang/frontend/ttable.c
        src/cxy/lang/frontend/types.c
        src/cxy/lang/frontend/visitor.c)

set(CXY_MIDDLE_SOURCES
        src/cxy/lang/middle/builtins.c
        src/cxy/lang/middle/macro.c
        src/cxy/lang/middle/scope.c

        src/cxy/lang/middle/check/array.c
        src/cxy/lang/middle/check/assign.c
        src/cxy/lang/middle/check/binary.c
        src/cxy/lang/middle/check/builtins.c
        src/cxy/lang/middle/check/call.c
        src/cxy/lang/middle/check/cast.c
        src/cxy/lang/middle/check/check.c
        src/cxy/lang/middle/check/class.c
        src/cxy/lang/middle/check/closure.c
        src/cxy/lang/middle/check/defaults.c
        src/cxy/lang/middle/check/enum.c
        src/cxy/lang/middle/check/for.c
        src/cxy/lang/middle/check/function.c
        src/cxy/lang/middle/check/generics.c
        src/cxy/lang/middle/check/if.c
        src/cxy/lang/middle/check/index.c
        src/cxy/lang/middle/check/interface.c
        src/cxy/lang/middle/check/match.c
        src/cxy/lang/middle/check/member.c
        src/cxy/lang/middle/check/module.c
        src/cxy/lang/middle/check/new.c
        src/cxy/lang/middle/check/node.c
        src/cxy/lang/middle/check/path.c
        src/cxy/lang/middle/check/struct.c
        src/cxy/lang/middle/check/switch.c
        src/cxy/lang/middle/check/tuple.c
        src/cxy/lang/middle/check/type.c
        src/cxy/lang/middle/check/unary.c
        src/cxy/lang/middle/check/variables.c

        src/cxy/lang/middle/eval/binary.c
        src/cxy/lang/middle/eval/comptime.c
        src/cxy/lang/middle/eval/enum.c
        src/cxy/lang/middle/eval/eval.c
        src/cxy/lang/middle/eval/for.c
        src/cxy/lang/middle/eval/index.c
        src/cxy/lang/middle/eval/literal.c
        src/cxy/lang/middle/eval/member.c
        src/cxy/lang/middle/eval/path.c
        src/cxy/lang/middle/eval/unary.c

        src/cxy/lang/middle/bind/bind.c
        src/cxy/lang/middle/bind/discover.c
        src/cxy/lang/middle/bind/resolve.c

        src/cxy/lang/middle/dump/json.c
        src/cxy/lang/middle/dump/yaml.c

        src/cxy/lang/middle/shake/closure.c
        src/cxy/lang/middle/shake/shake.c)

set(CXY_LANG_BACKEND_SOURCES
        src/cxy/lang/backend/llvm/array.cpp
        src/cxy/lang/backend/llvm/backend.cpp
        src/cxy/lang/backend/llvm/binary.cpp
        src/cxy/lang/backend/llvm/generate.cpp
        src/cxy/lang/backend/llvm/context.cpp
        src/cxy/lang/backend/llvm/for.cpp
        src/cxy/lang/backend/llvm/simplify.c)

set(CXY_DRIVER_SOURCES
        src/cxy/driver/driver.c
        src/cxy/driver/options.c
        src/cxy/driver/stages.c
        src/cxy/driver/stats.c)

add_executable(cxy
        src/cxy/driver/main.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/src/builtins.c
        ${CXY_3RD_PARTY_SOURCES}
        ${CXY_CORE_SOURCES}
        ${CXY_FRONTEND_SOURCES}
        ${CXY_MIDDLE_SOURCES}
        ${CXY_LANG_BACKEND_SOURCES}
        ${CXY_DRIVER_SOURCES})

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs
        Analysis
        Core
        TransformUtils
)

message(STATUS ${llvm_libs})
target_link_libraries(cxy msgpack-c yaml ${llvm_libs})
target_compile_definitions(cxy PRIVATE
        -DCXY_VERSION="${CXY_VERSION}")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/stdlib)
set(CXY_STD_LIB_SOURCES
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/dns
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/c.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/io.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/json.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/map.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/net.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/str.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/tcp.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/vector.cxy)

add_custom_target(
        cxy-stdlib ALL
        COMMAND cp -rf ${CXY_STD_LIB_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/stdlib/
        DEPENDS ${CXY_STD_LIB_SOURCES}
)

add_dependencies(cxy cxy-stdlib)


