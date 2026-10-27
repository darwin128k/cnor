/**
 * @file main.c
 * @brief `cnor`, the command line: compile and run scripts, write and run
 *        images.
 *
 * Usage: see ::cnor_cli_options_print_help, or README.md.
 */

#include "args.h"
#include "command.h"
#include "options.h"
#include "repl.h"

#include <cnor/cnor.h>
#include <cnor/image.h>
#include <lh/version.h>
#include <stdio.h>

static void
cnor_cli_print_version(void)
{
    const lh_version_t *version = cnor_get_version();

    printf("cnor %u.%u.%u (image format v%u)\n", (unsigned) lh_version_get_major(version),
           (unsigned) lh_version_get_minor(version), (unsigned) lh_version_get_patch(version),
           (unsigned) CNOR_IMAGE_VERSION);
}

static cnor_cli_exit_t
cnor_cli_dispatch(const cnor_cli_options_t *options)
{
    switch (options->command)
    {
    case cnor_cli_command_compile:
        return cnor_cli_compile(options);
    case cnor_cli_command_exec:
        return cnor_cli_exec(options);
    case cnor_cli_command_repl:
        return cnor_cli_repl(options);
    case cnor_cli_command_help:
        cnor_cli_options_print_help();
        return cnor_cli_exit_ok;
    case cnor_cli_command_version:
        cnor_cli_print_version();
        return cnor_cli_exit_ok;
    case cnor_cli_command_run:
    default:
        return cnor_cli_run(options);
    }
}

int
main(int argc, char **argv)
{
    cnor_cli_args_t args;
    cnor_cli_options_t options;
    cnor_cli_exit_t code;

    if (!cnor_cli_args_init(&args, argc, argv))
    {
        fputs("cnor: cannot read the command line\n", stderr);
        return cnor_cli_exit_usage;
    }
    code = cnor_cli_options_parse(&options, &args);
    if (code == cnor_cli_exit_ok)
    {
        code = cnor_cli_dispatch(&options);
    }
    cnor_cli_args_deinit(&args);
    return (int) code;
}
