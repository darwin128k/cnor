# The `cnor` command line: compile and run scripts, write and run images.
# Built with CNOR_BUILD_CLI (ON when cnor is the top-level project). The
# target is cnor_cli, because `cnor` is the library; the program is `cnor`.

add_executable(cnor_cli
        main.c
        args.c
        options.c
        command.c
        file.c
        loader.c
        natives.c
        text.c
)
set_target_properties(cnor_cli PROPERTIES OUTPUT_NAME cnor)
target_link_libraries(cnor_cli PRIVATE cnor)
if (WIN32)
    # CommandLineToArgvW, for UTF-8 arguments.
    target_link_libraries(cnor_cli PRIVATE shell32)
endif ()
