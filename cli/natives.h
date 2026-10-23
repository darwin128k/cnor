/**
 * @file natives.h
 * @brief The natives the command line gives every script.
 *
 * | Native | Does |
 * |---|---|
 * | `print(...)` | writes its arguments to the standard output, separated by one space |
 * | `println(...)` | the same, then a newline |
 * | `eprint(...)` | `print` to the standard error |
 * | `eprintln(...)` | `println` to the standard error |
 *
 * Each takes 0 to ::CNOR_ARGC_MAX arguments of any kind, written as
 * `text.h` describes, and returns `none`. A failed write is
 * ::cnor_error_native_fail.
 */

#ifndef CNOR_CLI_NATIVES_H
#define CNOR_CLI_NATIVES_H

#include <cnor/native.h>
#include <stdio.h>

/**
 * @struct cnor_cli_streams
 * @brief Where the natives write: the machine's `ctx`.
 */
struct cnor_cli_streams
{
    FILE *out; /**< `print`, `println`. */
    FILE *err; /**< `eprint`, `eprintln`. */
};
/** @copybrief cnor_cli_streams */
typedef struct cnor_cli_streams cnor_cli_streams_t;

/** @brief The table, for the compiler and the machine; `ctx` is a ::cnor_cli_streams_t. */
const cnor_native_table_t *
cnor_cli_natives(void);

#endif /* CNOR_CLI_NATIVES_H */
