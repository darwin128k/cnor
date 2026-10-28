cmake_minimum_required(VERSION 3.12)
project(cnor
        VERSION 0.1.0
        DESCRIPTION "cnor: a small script language built on lh"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(CNOR_SANITIZERS "Instrument cnor, lh and executables with ASan and UBSan" OFF)
option(CNOR_BUILD_FUZZ "Build the deterministic image mutation runner" OFF)
option(CNOR_LIBFUZZER "Build the Clang libFuzzer image target" OFF)
if (CNOR_SANITIZERS OR CNOR_LIBFUZZER)
    if (NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR WIN32)
        message(FATAL_ERROR "Sanitizer builds require GCC or Clang on a Unix host")
    endif ()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
                        -fno-omit-frame-pointer -g)
    # CMake 3.12 has no add_link_options.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif ()
if (CNOR_LIBFUZZER)
    if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CNOR_LIBFUZZER requires Clang")
    endif ()
    add_compile_options(-fsanitize=fuzzer-no-link)
endif ()

option(CNOR_BUILD_TESTS "Build tests" OFF)
option(CNOR_BUILD_BENCH "Build the benchmark runner (bench/)" OFF)
option(CNOR_BUILD_EXAMPLES "Build embedding examples (examples/)" OFF)
# The compiler turns a source into an image; the machine runs the image. A
# host that ships the machine alone needs no compiler: the image is the
# boundary between them, and the script is compiled on the desktop.
option(CNOR_BUILD_COMPILER "Build the compiler (the front end) into the library" ON)
# The command line is for a cnor checkout built on its own; a project that
# embeds cnor as a subdirectory gets the library only, unless it asks.
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(cnor_build_cli_default ON)
else ()
    set(cnor_build_cli_default OFF)
endif ()
option(CNOR_BUILD_CLI "Build the cnor command line (cli/)" ${cnor_build_cli_default})
option(CNOR_VM_COMPUTED_GOTO
        "Dispatch VM instructions through a label table where the compiler supports it (GCC, Clang); OFF forces the portable switch"
        ON)
option(CNOR_IPO "Build cnor with link-time optimization where the toolchain supports it" OFF)
set(CNOR_PGO OFF CACHE STRING
        "Profile-guided optimization (GCC, Clang): OFF, GENERATE (instrument) or USE (the profile)")
set_property(CACHE CNOR_PGO PROPERTY STRINGS OFF GENERATE USE)
set(CNOR_PGO_DIR "${CMAKE_BINARY_DIR}/pgo" CACHE PATH
        "Where CNOR_PGO=GENERATE writes the profile and USE reads it")

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

# The machine, the image it reads, and what both need.
set(CNOR_VM_SOURCES
        ${CNOR_SRC_PATH}/cnor/cnor.c
        ${CNOR_SRC_PATH}/cnor/error.c
        ${CNOR_SRC_PATH}/cnor/diag.c
        ${CNOR_SRC_PATH}/cnor/arena.c
        ${CNOR_SRC_PATH}/cnor/ast.c
        ${CNOR_SRC_PATH}/cnor/native.c
        ${CNOR_SRC_PATH}/cnor/op.c
        ${CNOR_SRC_PATH}/cnor/builtin.c
        ${CNOR_SRC_PATH}/cnor/byte/reader.c
        ${CNOR_SRC_PATH}/cnor/image/header.c
        ${CNOR_SRC_PATH}/cnor/image/view.c
        ${CNOR_SRC_PATH}/cnor/image/names.c
        ${CNOR_SRC_PATH}/cnor/image/verify.c
        ${CNOR_SRC_PATH}/cnor/vm/trace.c
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
        ${CNOR_SRC_PATH}/cnor/vm/op/array.c
        ${CNOR_SRC_PATH}/cnor/vm/op/dict.c
        ${CNOR_SRC_PATH}/cnor/vm/op/collection.c
        ${CNOR_SRC_PATH}/cnor/vm/op/error.c
        ${CNOR_SRC_PATH}/cnor/vm/op/builtin.c
        ${CNOR_SRC_PATH}/cnor/vm/op/operand.c
)

# The front end: the lexer, the parser, the compiler, and the writers that
# put an image on disk. None of it is needed to run an image.
set(CNOR_COMPILER_SOURCES
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
        ${CNOR_SRC_PATH}/cnor/image/writer.c
        ${CNOR_SRC_PATH}/cnor/compiler/compiler.c
        ${CNOR_SRC_PATH}/cnor/compiler/emit.c
        ${CNOR_SRC_PATH}/cnor/compiler/scope.c
        ${CNOR_SRC_PATH}/cnor/compiler/constant.c
        ${CNOR_SRC_PATH}/cnor/compiler/rules.c
        ${CNOR_SRC_PATH}/cnor/compiler/builtin.c
        ${CNOR_SRC_PATH}/cnor/compiler/expr.c
        ${CNOR_SRC_PATH}/cnor/compiler/logic.c
        ${CNOR_SRC_PATH}/cnor/compiler/call.c
${CNOR_SRC_PATH}/cnor/compiler/localfn.c
        ${CNOR_SRC_PATH}/cnor/compiler/stmt.c
        ${CNOR_SRC_PATH}/cnor/compiler/control_if.c
        ${CNOR_SRC_PATH}/cnor/compiler/control_loop.c
        ${CNOR_SRC_PATH}/cnor/compiler/control_match.c
        ${CNOR_SRC_PATH}/cnor/compiler/control_try.c
        ${CNOR_SRC_PATH}/cnor/compiler/function.c
        ${CNOR_SRC_PATH}/cnor/compiler/path.c
        ${CNOR_SRC_PATH}/cnor/compiler/module.c
        ${CNOR_SRC_PATH}/cnor/compiler/named.c
        ${CNOR_SRC_PATH}/cnor/compiler/type.c
        ${CNOR_SRC_PATH}/cnor/compiler/order.c
)

if (CNOR_BUILD_COMPILER)
    set(CNOR_SOURCES ${CNOR_VM_SOURCES} ${CNOR_COMPILER_SOURCES})
else ()
    set(CNOR_SOURCES ${CNOR_VM_SOURCES})
endif ()

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
# The dispatch loop runs at a speed that depends on where its hot blocks sit in
# 64-byte lines (docs/architecture.md, "Where the loop starts"): with the
# switch, one 22-byte block that every instruction goes through ran the
# benchmarks up to 35% slower when it straddled two lines. The function, its
# loop heads and every block reached only by a jump start on a line, so
# neither unrelated code nor the position of a block before them moves them.
include(CheckCCompilerFlag)
set(CNOR_DISPATCH_ALIGN_FLAGS "")
foreach (CNOR_ALIGN_FLAG IN ITEMS -falign-functions=64 -falign-loops=64 -falign-jumps=64)
    string(MAKE_C_IDENTIFIER "CNOR_HAS${CNOR_ALIGN_FLAG}" CNOR_ALIGN_VAR)
    check_c_compiler_flag(${CNOR_ALIGN_FLAG} ${CNOR_ALIGN_VAR})
    if (${CNOR_ALIGN_VAR})
        list(APPEND CNOR_DISPATCH_ALIGN_FLAGS ${CNOR_ALIGN_FLAG})
    endif ()
endforeach ()
if (CNOR_DISPATCH_ALIGN_FLAGS)
    set_source_files_properties(${CNOR_SRC_PATH}/cnor/vm/dispatch.c PROPERTIES
            COMPILE_OPTIONS "${CNOR_DISPATCH_ALIGN_FLAGS}")
endif ()

# Optional optimizations, all OFF by default; bench/README.md has what they
# measured. Link-time optimization lets the compiler inline across cnor's
# sources and lh's (lh builds that way in Release on its own).
if (CNOR_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT cnor_ipo_supported OUTPUT cnor_ipo_output LANGUAGES C)
    if (cnor_ipo_supported)
        set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else ()
        message(WARNING "CNOR_IPO: not supported here: ${cnor_ipo_output}")
    endif ()
endif ()

# Profile-guided optimization: configure with GENERATE, build, run a workload
# (bench/cnor_bench does), then configure the same build directory with USE and
# build again. GCC names the profile after the object files, so both builds
# must share the build directory; Clang reads CNOR_PGO_DIR/default.profdata,
# merged from the raw profiles with llvm-profdata.
if (NOT CNOR_PGO STREQUAL "OFF")
    if (NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "CNOR_PGO needs GCC or Clang")
    endif ()
    if (CNOR_PGO STREQUAL "GENERATE")
        target_compile_options(${PROJECT_NAME} PRIVATE "-fprofile-generate=${CNOR_PGO_DIR}")
        # Whatever links cnor links the profiling runtime too.
        target_link_libraries(${PROJECT_NAME} PUBLIC "-fprofile-generate=${CNOR_PGO_DIR}")
    elseif (CNOR_PGO STREQUAL "USE")
        target_compile_options(${PROJECT_NAME} PRIVATE "-fprofile-use=${CNOR_PGO_DIR}")
        if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${PROJECT_NAME} PRIVATE
                    -fprofile-correction -Wno-missing-profile)
        endif ()
    else ()
        message(FATAL_ERROR "CNOR_PGO must be OFF, GENERATE or USE, not ${CNOR_PGO}")
    endif ()
endif ()

if (CNOR_BUILD_CLI)
    add_subdirectory(cli)
endif ()

if (CNOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (CNOR_BUILD_EXAMPLES)
    add_subdirectory(examples/event-host)
endif ()

if (CNOR_BUILD_FUZZ OR CNOR_LIBFUZZER)
    add_subdirectory(fuzz)
endif ()

if (CNOR_BUILD_BENCH AND CNOR_BUILD_COMPILER)
    # The benchmark compiles the programs it runs.
    add_subdirectory(bench)
endif ()
