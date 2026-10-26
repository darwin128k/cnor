# Tests for cnor: plain C executables on a tiny harness (harness.h), one per
# suite, registered with CTest. Built only with CNOR_BUILD_TESTS=ON.

# The suites of the front end need the compiler; the machine's suite does not.
set(CNOR_TEST_SUITES image_vm)
if (CNOR_BUILD_COMPILER)
    list(APPEND CNOR_TEST_SUITES lexer parser compiler language)
endif ()

foreach (suite ${CNOR_TEST_SUITES})
    add_executable(cnor_test_${suite} test_${suite}.c)
    # Image and VM tests build images by hand with the library-private writer.
    target_include_directories(cnor_test_${suite} PRIVATE ${CNOR_SRC_PATH})
    target_link_libraries(cnor_test_${suite} PRIVATE cnor)
    if (NOT CNOR_BUILD_COMPILER)
        # The writer is the compiler's; the test builds its images with it.
        target_sources(cnor_test_${suite} PRIVATE
                ${CNOR_SRC_PATH}/cnor/image/writer.c
                ${CNOR_SRC_PATH}/cnor/byte/writer.c)
    endif ()
    add_test(NAME cnor_${suite} COMMAND cnor_test_${suite})
endforeach ()

# The command line, run on sample scripts (cli/).
if (TARGET cnor_cli)
    add_subdirectory(cli)
endif ()
