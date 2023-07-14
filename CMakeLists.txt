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

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

include(CxyUtils)
include(FetchContent)
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
cmake_policy(POP)

include_directories(src/cxy)

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

set(CXY_LANG_SOURCES
        src/cxy/lang/ast.c
        src/cxy/lang/capture.c
        src/cxy/lang/codegen.c
        src/cxy/lang/encode.c
        src/cxy/lang/decode.c
        src/cxy/lang/eval.c
        src/cxy/lang/node.c
        src/cxy/lang/lexer.c
        src/cxy/lang/operator.c
        src/cxy/lang/parser.c
        src/cxy/lang/semantics.c
        src/cxy/lang/scope.c
        src/cxy/lang/strings.c
        src/cxy/lang/ttable.c
        src/cxy/lang/types.c
        src/cxy/lang/visitor.c)

set(CXY_LANG_NODE_SOURCES
        src/cxy/lang/nodes/while.c
        src/cxy/lang/nodes/builtins.c
        src/cxy/lang/nodes/block.c
        src/cxy/lang/nodes/tuple.c
        src/cxy/lang/nodes/array.c
        src/cxy/lang/nodes/cast.c
        src/cxy/lang/nodes/switch.c
        src/cxy/lang/nodes/ternary.c
        src/cxy/lang/nodes/module.c
        src/cxy/lang/nodes/variables.c
        src/cxy/lang/nodes/closure.c
        src/cxy/lang/nodes/comptime.c
        src/cxy/lang/nodes/binary.c
        src/cxy/lang/nodes/member.c
        src/cxy/lang/nodes/if.c
        src/cxy/lang/nodes/function.c
        src/cxy/lang/nodes/unary.c
        src/cxy/lang/nodes/ccode.c
        src/cxy/lang/nodes/range.c
        src/cxy/lang/nodes/generics.c
        src/cxy/lang/nodes/assign.c
        src/cxy/lang/nodes/index.c
        src/cxy/lang/nodes/type.c
        src/cxy/lang/nodes/enum.c
        src/cxy/lang/nodes/define.c
        src/cxy/lang/nodes/path.c
        src/cxy/lang/nodes/strexpr.c
        src/cxy/lang/nodes/forstmt.c
        src/cxy/lang/nodes/struct.c
        src/cxy/lang/nodes/literals.c
        src/cxy/lang/nodes/call.c
        src/cxy/lang/nodes/attributes.c
        src/cxy/lang/nodes/return.c
        src/cxy/lang/nodes/new.c
        src/cxy/lang/nodes/macro.c)

set(CXY_OPERATIONS_SOURCES
        src/cxy/lang/operations/dump.c)

set(CXY_DRIVER_SOURCES
        src/cxy/driver/cc.c
        src/cxy/driver/driver.c
        src/cxy/driver/options.c
        src/cxy/driver/stages.c
        src/cxy/driver/stats.c)

add_executable(amalgamate
        src/tools/amalgamate.c
        src/cxy/core/utils.c)

CxyAmalgamate(epilogue
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
            src/cxy/lang/runtime/c-stubs.h
            src/cxy/lang/runtime/epilogue.h
            src/cxy/lang/runtime/coro/tina.h
            src/cxy/lang/runtime/evloop/ae.h
            src/cxy/lang/runtime/async.h)

CxyAmalgamate(prologue
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
            src/cxy/lang/runtime/prologue.h)

CxyAmalgamate(runtime
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
            src/cxy/lang/runtime/runtime.c
            src/cxy/lang/runtime/coro/tina.c
            src/cxy/lang/runtime/evloop/ae.c
            src/cxy/lang/runtime/async.c)

CxyAmalgamate(builtins
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
            src/cxy/lang/runtime/builtins.cxy)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/stdlib)
set(CXY_STD_LIB_SOURCES
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/io.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/json.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/map.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/str.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/vector.cxy)

add_custom_target(
        cxy-stdlib ALL
        COMMAND cp -rf ${CXY_STD_LIB_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/stdlib/
        DEPENDS ${CXY_STD_LIB_SOURCES}
)

add_executable(cxy
        src/cxy/driver/main.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/builtins.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/epilogue.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/prologue.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/runtime.c
        ${CXY_3RD_PARTY_SOURCES}
        ${CXY_CORE_SOURCES}
        ${CXY_LANG_SOURCES}
        ${CXY_LANG_NODE_SOURCES}
        ${CXY_DRIVER_SOURCES}
        ${CXY_OPERATIONS_SOURCES})

target_compile_definitions(cxy PRIVATE
        -DCXY_VERSION="${CXY_VERSION}"
        -DCXY_SOURCE_LANG_DIR="${CMAKE_SOURCE_DIR}/src/cxy/lang")

target_link_libraries(cxy msgpack-c)
add_dependencies(cxy cxy-stdlib)
