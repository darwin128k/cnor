/**
 * @file repl.h
 * @brief The interactive session: read a line, run it, keep what came before.
 */

#ifndef CNOR_CLI_REPL_H
#define CNOR_CLI_REPL_H

#include "options.h"

/**
 * @brief Read lines, compile them with what came before, and run the result.
 *
 * The session keeps the program it has read so far, and every line is run
 * with it: the state of the program is the program itself, so a value a line
 * left in a global is there for the next one.
 */
cnor_cli_exit_t
cnor_cli_repl(const cnor_cli_options_t *options);

#endif /* CNOR_CLI_REPL_H */
