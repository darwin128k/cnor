# Tests of the cnor command line: the program run on the scripts here, its
# exit status and output checked by check.cmake. Registered with the other
# tests when the command line is built (CNOR_BUILD_CLI). When cross-compiling,
# CMAKE_CROSSCOMPILING_EMULATOR runs it (wine for a Windows build).

set(cnor_cli_data ${CMAKE_CURRENT_SOURCE_DIR})
set(cnor_cli_work ${CMAKE_CURRENT_BINARY_DIR}/work)
file(MAKE_DIRECTORY ${cnor_cli_work})
string(REPLACE ";" "|" cnor_cli_emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")

# cnor_cli_test(NAME CODE <status> ARGS <arg>... [STDIN <file>] [COPY <from> <to>]
#               [STDOUT_FILE <file>] [STDOUT_REGEX <re>] [STDERR_REGEX <re>]
#               [STDERR_EMPTY] [SETUP <fixture>] [REQUIRES <fixture>])
function(cnor_cli_test name)
    cmake_parse_arguments(t "STDERR_EMPTY"
            "CODE;STDIN;STDOUT_FILE;STDOUT_REGEX;STDERR_REGEX;SETUP;REQUIRES" "ARGS;COPY" ${ARGN})
    string(REPLACE ";" "|" args "${t_ARGS}")
    set(defines
            -DCNOR=$<TARGET_FILE:cnor_cli>
            "-DEMULATOR=${cnor_cli_emulator}"
            -DWORKDIR=${cnor_cli_work}
            -DCODE=${t_CODE}
            "-DARGS=${args}")
    foreach (key STDIN STDOUT_FILE STDOUT_REGEX STDERR_REGEX)
        if (DEFINED t_${key})
            list(APPEND defines "-D${key}=${t_${key}}")
        endif ()
    endforeach ()
    if (t_COPY)
        string(REPLACE ";" "|" copy "${t_COPY}")
        list(APPEND defines "-DCOPY=${copy}")
    endif ()
    if (t_STDERR_EMPTY)
        list(APPEND defines -DSTDERR_EMPTY=ON)
    endif ()
    add_test(NAME cnor_cli_${name}
            COMMAND ${CMAKE_COMMAND} ${defines} -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
    if (t_SETUP)
        set_tests_properties(cnor_cli_${name} PROPERTIES FIXTURES_SETUP ${t_SETUP})
    endif ()
    if (t_REQUIRES)
        set_tests_properties(cnor_cli_${name} PROPERTIES FIXTURES_REQUIRED ${t_REQUIRES})
    endif ()
endfunction()

# -- information ---------------------------------------------------------------

cnor_cli_test(version CODE 0 ARGS --version
        STDOUT_REGEX "^cnor [0-9]+\\.[0-9]+\\.[0-9]+ \\(image format v[0-9]+\\)\n$" STDERR_EMPTY)
cnor_cli_test(help CODE 0 ARGS --help STDOUT_REGEX "usage: cnor run" STDERR_EMPTY)

# -- run -----------------------------------------------------------------------

cnor_cli_test(run CODE 0 ARGS run ${cnor_cli_data}/hello.cn
        STDOUT_FILE ${cnor_cli_data}/hello.out STDERR_EMPTY)
cnor_cli_test(run_without_command CODE 0 ARGS ${cnor_cli_data}/hello.cn
        STDOUT_FILE ${cnor_cli_data}/hello.out STDERR_EMPTY)
cnor_cli_test(run_none_prints_nothing CODE 0 ARGS run ${cnor_cli_data}/quiet.cn
        STDOUT_FILE ${cnor_cli_data}/quiet.out STDERR_EMPTY)
cnor_cli_test(run_streams CODE 0 ARGS run ${cnor_cli_data}/streams.cn
        STDOUT_FILE ${cnor_cli_data}/streams.out STDERR_REGEX "^err 2\n$")
cnor_cli_test(run_stdin CODE 0 ARGS run - STDIN ${cnor_cli_data}/hello.cn
        STDOUT_FILE ${cnor_cli_data}/hello.out STDERR_EMPTY)
cnor_cli_test(run_utf8_file_name CODE 0 ARGS run "héllo wörld.cn"
        COPY ${cnor_cli_data}/hello.cn "héllo wörld.cn"
        STDOUT_FILE ${cnor_cli_data}/hello.out STDERR_EMPTY)
if (WIN32)
    cnor_cli_test(run_windows_path CODE 0 ARGS "..\\work\\hello copy.cn"
            COPY ${cnor_cli_data}/hello.cn "hello copy.cn"
            STDOUT_FILE ${cnor_cli_data}/hello.out STDERR_EMPTY)
endif ()

# -- failures ------------------------------------------------------------------

cnor_cli_test(compile_error CODE 3 ARGS run ${cnor_cli_data}/compile_error.cn
        STDERR_REGEX "compile_error\\.cn:3: unknown name: missing\n$")
cnor_cli_test(runtime_error CODE 1 ARGS run ${cnor_cli_data}/runtime_error.cn
        STDOUT_FILE ${cnor_cli_data}/runtime_error.out
        STDERR_REGEX "runtime_error\\.cn:5: division by zero\n$")
cnor_cli_test(runtime_error_stdin CODE 1 ARGS run - STDIN ${cnor_cli_data}/runtime_error.cn
        STDOUT_FILE ${cnor_cli_data}/runtime_error.out
        STDERR_REGEX "^<stdin>:5: division by zero\n$")
cnor_cli_test(step_limit CODE 1 ARGS run ${cnor_cli_data}/count.cn --max-steps 1000
        STDERR_REGEX "count\\.cn:[34]: instruction budget exhausted\n$")
cnor_cli_test(step_limit_enough CODE 0 ARGS run ${cnor_cli_data}/count.cn --max-steps 1000000
        STDOUT_FILE ${cnor_cli_data}/count.out STDERR_EMPTY)

# -- compile, then exec --------------------------------------------------------

cnor_cli_test(compile CODE 0 ARGS compile ${cnor_cli_data}/hello.cn -o hello.tbc
        STDERR_EMPTY SETUP cnor_cli_images)
cnor_cli_test(compile_runtime_error CODE 0
        ARGS compile ${cnor_cli_data}/runtime_error.cn -o runtime_error.tbc
        STDERR_EMPTY SETUP cnor_cli_images)
cnor_cli_test(compile_without_lines CODE 0
        ARGS compile ${cnor_cli_data}/runtime_error.cn -o no_lines.tbc --no-lines
        STDERR_EMPTY SETUP cnor_cli_images)
cnor_cli_test(exec CODE 0 ARGS exec hello.tbc
        STDOUT_FILE ${cnor_cli_data}/hello.out STDERR_EMPTY REQUIRES cnor_cli_images)
cnor_cli_test(exec_stdin CODE 0 ARGS exec - STDIN ${cnor_cli_work}/hello.tbc
        STDOUT_FILE ${cnor_cli_data}/hello.out STDERR_EMPTY REQUIRES cnor_cli_images)
cnor_cli_test(exec_runtime_error CODE 1 ARGS exec runtime_error.tbc
        STDOUT_FILE ${cnor_cli_data}/runtime_error.out
        STDERR_REGEX "runtime_error\\.cn:5: division by zero\n$" REQUIRES cnor_cli_images)
cnor_cli_test(exec_without_lines CODE 1 ARGS exec no_lines.tbc
        STDERR_REGEX "runtime_error\\.cn: division by zero\n$" REQUIRES cnor_cli_images)
cnor_cli_test(exec_not_an_image CODE 3 ARGS exec ${cnor_cli_data}/hello.cn
        STDERR_REGEX "hello\\.cn: [a-z]")
cnor_cli_test(compile_reports_errors CODE 3
        ARGS compile ${cnor_cli_data}/compile_error.cn -o compile_error.tbc
        STDERR_REGEX "compile_error\\.cn:3: unknown name: missing\n$")

# -- bad command lines ---------------------------------------------------------

cnor_cli_test(no_arguments CODE 2 STDERR_REGEX "missing command")
cnor_cli_test(unknown_command CODE 2 ARGS frobnicate STDERR_REGEX "unknown command: frobnicate")
cnor_cli_test(unknown_option CODE 2 ARGS run ${cnor_cli_data}/hello.cn --frob
        STDERR_REGEX "unknown option: --frob")
cnor_cli_test(missing_file CODE 2 ARGS run STDERR_REGEX "missing file")
cnor_cli_test(missing_value CODE 2 ARGS run ${cnor_cli_data}/hello.cn --max-steps
        STDERR_REGEX "missing value for: --max-steps")
cnor_cli_test(bad_number CODE 2 ARGS run ${cnor_cli_data}/hello.cn --stack 12x
        STDERR_REGEX "expected a positive number: 12x")
cnor_cli_test(zero_stack CODE 2 ARGS run ${cnor_cli_data}/hello.cn --stack 0
        STDERR_REGEX "expected a positive number: 0")
cnor_cli_test(number_too_large CODE 2 ARGS run ${cnor_cli_data}/hello.cn --max-steps 4294967296
        STDERR_REGEX "expected a number: 4294967296")
cnor_cli_test(two_files CODE 2 ARGS run ${cnor_cli_data}/hello.cn ${cnor_cli_data}/quiet.cn
        STDERR_REGEX "more than one file")
cnor_cli_test(compile_stdin_needs_output CODE 2 ARGS compile - STDIN ${cnor_cli_data}/hello.cn
        STDERR_REGEX "needs -o")
cnor_cli_test(no_such_file CODE 4 ARGS run no_such_file.cn
        STDERR_REGEX "no_such_file\\.cn: cannot read the file")
