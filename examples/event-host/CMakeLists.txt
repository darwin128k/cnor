add_executable(cnor_event_host main.c ../../bench/clock.c)
target_include_directories(cnor_event_host PRIVATE ../../bench)
target_link_libraries(cnor_event_host PRIVATE cnor)

# Generate with the desktop compiler; a VM-only build consumes this same file.
if (CNOR_BUILD_COMPILER AND TARGET cnor_cli)
    set(event_image "${CMAKE_CURRENT_BINARY_DIR}/thermostat.cnb")
    add_custom_command(OUTPUT "${event_image}"
            COMMAND $<TARGET_FILE:cnor_cli> compile
                    "${CMAKE_CURRENT_SOURCE_DIR}/thermostat.cn" -o "${event_image}"
            DEPENDS cnor_cli "${CMAKE_CURRENT_SOURCE_DIR}/thermostat.cn"
            VERBATIM)
    add_custom_target(cnor_event_image ALL DEPENDS "${event_image}")
    if (CNOR_BUILD_TESTS)
        add_test(NAME cnor_event_host
                COMMAND cnor_event_host "${event_image}")
    endif ()
endif ()
