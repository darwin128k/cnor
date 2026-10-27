if (CNOR_BUILD_FUZZ)
    add_executable(cnor_image_mutate mutate.c image.c
                   ${CNOR_SRC_PATH}/cnor/image/writer.c
                   ${CNOR_SRC_PATH}/cnor/byte/writer.c)
    target_include_directories(cnor_image_mutate PRIVATE ${CNOR_SRC_PATH})
    target_link_libraries(cnor_image_mutate PRIVATE cnor)
    if (CNOR_BUILD_TESTS)
        add_test(NAME cnor_image_mutations COMMAND cnor_image_mutate)
        set_tests_properties(cnor_image_mutations PROPERTIES TIMEOUT 60)
        if (CNOR_BUILD_COMPILER AND TARGET cnor_cli)
            foreach (seed IN ITEMS control collections errors)
                set(image "${CMAKE_CURRENT_BINARY_DIR}/${seed}.tbc")
                add_custom_command(OUTPUT "${image}"
                    COMMAND $<TARGET_FILE:cnor_cli> compile
                            "${CMAKE_CURRENT_SOURCE_DIR}/seeds/${seed}.cn" -o "${image}"
                    DEPENDS cnor_cli "${CMAKE_CURRENT_SOURCE_DIR}/seeds/${seed}.cn"
                    VERBATIM)
                add_custom_target(cnor_fuzz_seed_${seed} ALL DEPENDS "${image}")
                add_test(NAME cnor_image_mutations_${seed} COMMAND cnor_image_mutate "${image}")
                set_tests_properties(cnor_image_mutations_${seed} PROPERTIES TIMEOUT 60)
            endforeach ()
        endif ()
    endif ()
endif ()

if (CNOR_LIBFUZZER)
    add_executable(cnor_image_fuzz image.c)
    target_link_libraries(cnor_image_fuzz PRIVATE cnor "-fsanitize=fuzzer")
endif ()
