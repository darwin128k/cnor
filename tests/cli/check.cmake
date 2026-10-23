# Run the cnor command line once and check what it did; see CMakeLists.txt.
#
#   CNOR          the program
#   EMULATOR      what runs it when cross-compiling, items separated by `|`
#   WORKDIR       the working directory
#   ARGS          its arguments, separated by `|`
#   STDIN         a file for the standard input
#   COPY          `from|to`: copy a file first (to a name the test needs)
#   CODE          the exit status expected
#   STDOUT_FILE   the exact standard output expected
#   STDOUT_REGEX  a pattern the standard output must match
#   STDERR_REGEX  a pattern the standard error must match
#   STDERR_EMPTY  when set, the standard error must be empty
#
# Carriage returns are dropped from both outputs first: on Windows the
# standard streams are text streams.

foreach (required CNOR WORKDIR CODE)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "check.cmake: ${required} is not set")
    endif ()
endforeach ()

string(REPLACE "|" ";" emulator "${EMULATOR}")
string(REPLACE "|" ";" args "${ARGS}")

if (DEFINED COPY)
    string(REPLACE "|" ";" copy "${COPY}")
    list(GET copy 0 copy_from)
    list(GET copy 1 copy_to)
    configure_file("${copy_from}" "${WORKDIR}/${copy_to}" COPYONLY)
endif ()

set(input)
if (DEFINED STDIN)
    set(input INPUT_FILE "${STDIN}")
endif ()

execute_process(
        COMMAND ${emulator} "${CNOR}" ${args}
        WORKING_DIRECTORY "${WORKDIR}"
        ${input}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
)
string(REPLACE "\r" "" out "${out}")
string(REPLACE "\r" "" err "${err}")

set(failures)
if (NOT code STREQUAL CODE)
    list(APPEND failures "exit status ${code}, expected ${CODE}")
endif ()
if (DEFINED STDOUT_FILE)
    file(READ "${STDOUT_FILE}" expected)
    string(REPLACE "\r" "" expected "${expected}")
    if (NOT out STREQUAL expected)
        list(APPEND failures "standard output differs from ${STDOUT_FILE}")
    endif ()
endif ()
if (DEFINED STDOUT_REGEX AND NOT out MATCHES "${STDOUT_REGEX}")
    list(APPEND failures "standard output does not match `${STDOUT_REGEX}`")
endif ()
if (DEFINED STDERR_REGEX AND NOT err MATCHES "${STDERR_REGEX}")
    list(APPEND failures "standard error does not match `${STDERR_REGEX}`")
endif ()
if (STDERR_EMPTY AND NOT err STREQUAL "")
    list(APPEND failures "standard error is not empty")
endif ()

if (failures)
    string(REPLACE ";" "\n  " failures "${failures}")
    string(REPLACE "|" " " shown "${ARGS}")
    message(FATAL_ERROR "cnor ${shown}:\n  ${failures}\n"
            "--- standard output ---\n${out}--- standard error ---\n${err}")
endif ()
