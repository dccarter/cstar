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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c99-designator -Werror -fno-rtti")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

if (CMAKE_BUILD_TYPE MATCHES "asan")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
endif ()


# Configure path for loading project cmake scripts
set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (LLVM_ROOT_DIR)
    set(CMAKE_PREFIX_PATH
            ${CMAKE_PREFIX_PATH}
            ${LLVM_ROOT_DIR}/lib/cmake/llvm
            ${LLVM_ROOT_DIR}/lib/cmake/clang)
endif ()

option(ENABLE_TESTS "Enable CMake tests" ON)

include(CxyUtils)
include(FetchContent)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED)
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
        GIT_REPOSITORY https://github.com/dccarter/libyaml.git
        GIT_TAG main
)
FetchContent_MakeAvailable(libyaml)

cmake_policy(POP)

include_directories(
        src/cxy
        src/3rdParty
)

add_executable(amalgamate
        src/tools/amalgamate.c
        src/cxy/core/utils.c
)

target_compile_definitions(amalgamate PRIVATE -DNO_EXEC_UTIL)

CxyAmalgamate(builtins
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
        src/cxy/runtime/builtins.cxy)

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
        src/3rdParty/cJSON.c)

set(CXY_FRONTEND_SOURCES
        src/cxy/lang/frontend/ast.c
        src/cxy/lang/frontend/capture.c
        src/cxy/lang/frontend/encode.c
        src/cxy/lang/frontend/defines.c
        src/cxy/lang/frontend/decode.c
        src/cxy/lang/frontend/lexer.c
        src/cxy/lang/frontend/module.c
        src/cxy/lang/frontend/operator.c
        src/cxy/lang/frontend/parser.c
        src/cxy/lang/frontend/strings.c
        src/cxy/lang/frontend/ttable.c
        src/cxy/lang/frontend/types.c
        src/cxy/lang/frontend/visitor.c)

set(CXY_MIDDLE_SOURCES
        src/cxy/lang/middle/builtins.c
        src/cxy/lang/middle/mangle.c

        src/cxy/lang/middle/sema/array.c
        src/cxy/lang/middle/sema/assign.c
        src/cxy/lang/middle/sema/binary.c
        src/cxy/lang/middle/sema/builtins.c
        src/cxy/lang/middle/sema/call.c
        src/cxy/lang/middle/sema/cast.c
        src/cxy/lang/middle/sema/check.c
        src/cxy/lang/middle/sema/class.c
        src/cxy/lang/middle/sema/closure.c
        src/cxy/lang/middle/sema/defaults.c
        src/cxy/lang/middle/sema/enum.c
        src/cxy/lang/middle/sema/for.c
        src/cxy/lang/middle/sema/function.c
        src/cxy/lang/middle/sema/generics.c
        src/cxy/lang/middle/sema/if.c
        src/cxy/lang/middle/sema/index.c
        src/cxy/lang/middle/sema/inheritance.c
        src/cxy/lang/middle/sema/interface.c
        src/cxy/lang/middle/sema/match.c
        src/cxy/lang/middle/sema/member.c
        src/cxy/lang/middle/sema/new.c
        src/cxy/lang/middle/sema/node.c
        src/cxy/lang/middle/sema/path.c
        src/cxy/lang/middle/sema/struct.c
        src/cxy/lang/middle/sema/switch.c
        src/cxy/lang/middle/sema/tuple.c
        src/cxy/lang/middle/sema/type.c
        src/cxy/lang/middle/sema/unary.c
        src/cxy/lang/middle/sema/variables.c

        src/cxy/lang/middle/eval/macro.c
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
        src/cxy/lang/middle/bind/scope.c

        src/cxy/lang/middle/dump/json.c
        src/cxy/lang/middle/dump/yaml.c
        src/cxy/lang/middle/dump/cxy.c

        src/cxy/lang/middle/shake/closure.c
        src/cxy/lang/middle/shake/shake.c

        src/cxy/lang/middle/preprocess/preprocessor.c
        src/cxy/lang/middle/preprocess/evaluate.c

        src/cxy/lang/middle/simplify/stage1.c
        src/cxy/lang/middle/simplify/simplify.c

        src/cxy/lang/middle/mem/sweep.c)

set(CXY_LANG_BACKEND_SOURCES
        src/cxy/lang/backend/llvm/array.cpp
        src/cxy/lang/backend/llvm/binary.cpp
        src/cxy/lang/backend/llvm/context.cpp
        src/cxy/lang/backend/llvm/llvm.cpp
        src/cxy/lang/backend/llvm/generate.cpp
)

set(CXY_DRIVER_SOURCES
        src/cxy/driver/driver.c
        src/cxy/driver/options.c
        src/cxy/driver/stages.c
        src/cxy/driver/stats.c

        src/cxy/driver/c-import/context.cpp
        src/cxy/driver/c-import/import.cpp
)

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
        ${LLVM_TARGETS_TO_BUILD}
        Analysis
        Core
        Linker
        TransformUtils
        CodeGen
)
list(APPEND llvm_libs clangAST clangBasic clangFrontend clangLex clangParse clangSema)
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
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/hash.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/net.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/tcp.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/vector.cxy)

add_custom_target(
        cxy-stdlib ALL
        COMMAND cp -rf ${CXY_STD_LIB_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/stdlib/
        DEPENDS ${CXY_STD_LIB_SOURCES}
)

add_dependencies(cxy cxy-stdlib)

if(NOT TARGET FileCheck)
    # Download the LLVM FileCheck utility for tests.
    set(FILE_CHECK_SRC_PATH "llvmorg-${LLVM_PACKAGE_VERSION}/llvm/utils/FileCheck/FileCheck.cpp")
    set(FILE_CHECK_SRC_URL "https://raw.githubusercontent.com/llvm/llvm-project/${FILE_CHECK_SRC_PATH}")
    set(FILE_CHECK_SRC_FILE "${CMAKE_CURRENT_BINARY_DIR}/${FILE_CHECK_SRC_PATH}")
    if(NOT EXISTS "${FILE_CHECK_SRC_FILE}")
        message(STATUS "Downloading LLVM FileCheck source from ${FILE_CHECK_SRC_URL}")
        file(DOWNLOAD "${FILE_CHECK_SRC_URL}" "${FILE_CHECK_SRC_FILE}")
    endif()
    add_executable(FileCheck ${FILE_CHECK_SRC_FILE})
    llvm_map_components_to_libnames(FILE_CHECK_NEEDED_LIBS filecheck support)
    target_link_libraries(FileCheck ${FILE_CHECK_NEEDED_LIBS})
endif()

add_executable(lex-tester
    tests/utils/lexer.cpp
    src/cxy/lang/frontend/lexer.c
    ${CXY_CORE_SOURCES}
)