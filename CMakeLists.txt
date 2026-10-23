cmake_minimum_required(VERSION 3.12)
project(cnor
        VERSION 0.1.0
        DESCRIPTION "cnor: a small script language built on lh"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(CNOR_BUILD_TESTS "Build tests" OFF)
option(CNOR_BUILD_BENCH "Build the benchmark runner (bench/)" OFF)
option(CNOR_VM_COMPUTED_GOTO
        "Dispatch VM instructions through a label table where the compiler supports it (GCC, Clang); OFF forces the portable switch"
        ON)

# -- lh -----------------------------------------------------------------------

# lh is the git submodule in lib/lh. Only the library itself is needed here.
set(LH_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(LH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LH_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(LH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/lh)

# -- cnor ---------------------------------------------------------------------

set(CNOR_SRC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(CNOR_INCLUDE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CNOR_SOURCES
        ${CNOR_SRC_PATH}/cnor/cnor.c
        ${CNOR_SRC_PATH}/cnor/error.c
        ${CNOR_SRC_PATH}/cnor/diag.c
        ${CNOR_SRC_PATH}/cnor/arena.c
        ${CNOR_SRC_PATH}/cnor/ast.c
        ${CNOR_SRC_PATH}/cnor/native.c
        ${CNOR_SRC_PATH}/cnor/op.c
        ${CNOR_SRC_PATH}/cnor/byte/reader.c
        ${CNOR_SRC_PATH}/cnor/byte/writer.c
        ${CNOR_SRC_PATH}/cnor/lexer/lexer.c
        ${CNOR_SRC_PATH}/cnor/lexer/indent.c
        ${CNOR_SRC_PATH}/cnor/lexer/scan.c
        ${CNOR_SRC_PATH}/cnor/lexer/punct.c
        ${CNOR_SRC_PATH}/cnor/lexer/keyword.c
        ${CNOR_SRC_PATH}/cnor/parser/parser.c
        ${CNOR_SRC_PATH}/cnor/parser/token.c
        ${CNOR_SRC_PATH}/cnor/parser/literal.c
        ${CNOR_SRC_PATH}/cnor/parser/expr.c
        ${CNOR_SRC_PATH}/cnor/parser/stmt.c
        ${CNOR_SRC_PATH}/cnor/parser/control.c
        ${CNOR_SRC_PATH}/cnor/parser/fn.c
        ${CNOR_SRC_PATH}/cnor/image/header.c
        ${CNOR_SRC_PATH}/cnor/image/view.c
        ${CNOR_SRC_PATH}/cnor/image/names.c
        ${CNOR_SRC_PATH}/cnor/image/verify.c
        ${CNOR_SRC_PATH}/cnor/image/writer.c
        ${CNOR_SRC_PATH}/cnor/compiler/compiler.c
        ${CNOR_SRC_PATH}/cnor/compiler/emit.c
        ${CNOR_SRC_PATH}/cnor/compiler/scope.c
        ${CNOR_SRC_PATH}/cnor/compiler/constant.c
        ${CNOR_SRC_PATH}/cnor/compiler/rules.c
        ${CNOR_SRC_PATH}/cnor/compiler/expr.c
        ${CNOR_SRC_PATH}/cnor/compiler/logic.c
        ${CNOR_SRC_PATH}/cnor/compiler/call.c
        ${CNOR_SRC_PATH}/cnor/compiler/stmt.c
        ${CNOR_SRC_PATH}/cnor/compiler/control_if.c
        ${CNOR_SRC_PATH}/cnor/compiler/control_loop.c
        ${CNOR_SRC_PATH}/cnor/compiler/control_match.c
        ${CNOR_SRC_PATH}/cnor/compiler/function.c
        ${CNOR_SRC_PATH}/cnor/vm/vm.c
        ${CNOR_SRC_PATH}/cnor/vm/load.c
        ${CNOR_SRC_PATH}/cnor/vm/dispatch.c
        ${CNOR_SRC_PATH}/cnor/vm/stack.c
        ${CNOR_SRC_PATH}/cnor/vm/str_pool.c
        ${CNOR_SRC_PATH}/cnor/vm/value.c
        ${CNOR_SRC_PATH}/cnor/vm/op/data.c
        ${CNOR_SRC_PATH}/cnor/vm/op/arith.c
        ${CNOR_SRC_PATH}/cnor/vm/op/compare.c
        ${CNOR_SRC_PATH}/cnor/vm/op/control.c
        ${CNOR_SRC_PATH}/cnor/vm/op/text.c
)

# cnor is a static library only, like lh.
add_library(${PROJECT_NAME} STATIC ${CNOR_SOURCES})
target_include_directories(${PROJECT_NAME}
        PUBLIC ${CNOR_INCLUDE_PATH}
        PRIVATE ${CNOR_SRC_PATH}
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
        CNOR_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        CNOR_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        CNOR_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_link_libraries(${PROJECT_NAME} PUBLIC lh)
if (NOT CNOR_VM_COMPUTED_GOTO)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CNOR_VM_COMPUTED_GOTO=0)
endif ()

if (CNOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (CNOR_BUILD_BENCH)
    add_subdirectory(bench)
endif ()
