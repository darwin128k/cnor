/**
 * @file args.h
 * @brief The command line as UTF-8 strings (::cnor_cli_args_t).
 *
 * On Windows `main` receives its arguments in the ANSI code page, which cannot
 * hold every file name; there they are read again from the UTF-16 command line
 * and converted to UTF-8. Elsewhere `argv` is used as it is: UTF-8 by
 * convention.
 */

#ifndef CNOR_CLI_ARGS_H
#define CNOR_CLI_ARGS_H

#include <lh/bool.h>
#include <lh/char.h>

/**
 * @struct cnor_cli_args
 * @brief The arguments after the program name.
 */
struct cnor_cli_args
{
    int count;              /**< Number of arguments. */
    lh_char_t **values;     /**< `count` NUL-terminated UTF-8 strings. */
    lh_char_t **allocation; /**< Owned block behind `values`, or ::lh_null. */
};
/** @copybrief cnor_cli_args */
typedef struct cnor_cli_args cnor_cli_args_t;

/**
 * @brief Collect the arguments `main` was given (or, on Windows, their UTF-16
 *        originals).
 * @return ::lh_bool_false when the conversion runs out of memory.
 */
lh_bool_t
cnor_cli_args_init(cnor_cli_args_t *self, int argc, lh_char_t **argv);

/** @brief Release what ::cnor_cli_args_init allocated. */
void
cnor_cli_args_deinit(cnor_cli_args_t *self);

#endif /* CNOR_CLI_ARGS_H */
