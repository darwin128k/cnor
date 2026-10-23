/**
 * @file command.h
 * @brief The subcommands: `run`, `compile` and `exec`.
 *
 * Diagnostics go to the standard error as `file:line: message` (`file:
 * message` when the line is unknown), after whatever the script printed so
 * far. `run` and `exec` print the program's result on the standard output,
 * followed by a newline, unless it is `none`.
 */

#ifndef CNOR_CLI_COMMAND_H
#define CNOR_CLI_COMMAND_H

#include "options.h"

/** @brief Compile the source, then run it. */
cnor_cli_exit_t
cnor_cli_run(const cnor_cli_options_t *options);

/** @brief Compile the source and write its image. */
cnor_cli_exit_t
cnor_cli_compile(const cnor_cli_options_t *options);

/** @brief Load the image and run it. */
cnor_cli_exit_t
cnor_cli_exec(const cnor_cli_options_t *options);

#endif /* CNOR_CLI_COMMAND_H */
