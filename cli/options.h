/**
 * @file options.h
 * @brief The command line, parsed (::cnor_cli_options_t), and the exit codes.
 */

#ifndef CNOR_CLI_OPTIONS_H
#define CNOR_CLI_OPTIONS_H

#include "args.h"

#include <cnor/types.h>
#include <lh/bool.h>
#include <lh/char.h>
#include <lh/numeric/fixed/types.h>

/** @brief Value stack of a run, in values (`--stack`). */
#define CNOR_CLI_DEFAULT_STACK_SLOTS 65536U
/** @brief Nested calls allowed (`--depth`). */
#define CNOR_CLI_DEFAULT_MAX_DEPTH 4096U
/** @brief String pool, in bytes (`--pool`). */
#define CNOR_CLI_DEFAULT_STR_POOL_SIZE (16U * 1024U * 1024U)
/** @brief Instruction budget; 0 is unlimited (`--max-steps`). */
#define CNOR_CLI_DEFAULT_MAX_STEPS 0U

/**
 * @enum cnor_cli_exit
 * @brief What the process returns.
 */
enum cnor_cli_exit
{
    cnor_cli_exit_ok = 0,      /**< Done. */
    cnor_cli_exit_run = 1,     /**< The script failed while running (step limit included). */
    cnor_cli_exit_usage = 2,   /**< The command line is wrong. */
    cnor_cli_exit_compile = 3, /**< The source does not compile, or the image does not load. */
    cnor_cli_exit_io = 4       /**< A file could not be read or written. */
};
/** @copybrief cnor_cli_exit */
typedef enum cnor_cli_exit cnor_cli_exit_t;

/**
 * @enum cnor_cli_command
 * @brief What was asked for.
 */
enum cnor_cli_command
{
    cnor_cli_command_run = 0, /**< `run`: compile a source, then run it. */
    cnor_cli_command_compile, /**< `compile`: write the image of a source. */
    cnor_cli_command_exec,    /**< `exec`: load an image and run it. */
    cnor_cli_command_help,    /**< `--help`, `help`. */
    cnor_cli_command_version  /**< `--version`. */
};
/** @copybrief cnor_cli_command */
typedef enum cnor_cli_command cnor_cli_command_t;

/**
 * @struct cnor_cli_options
 * @brief Everything the command line says.
 */
struct cnor_cli_options
{
    cnor_cli_command_t command;     /**< The subcommand. */
    const lh_char_t *input;         /**< Source or image path; `-` is the standard input. */
    const lh_char_t *output;        /**< `compile -o`, or ::lh_null for the default. */
    lh_bool_t lines;                /**< Keep the line table (`--no-lines` clears it). */
    cnor_step_count_t max_steps;    /**< `--max-steps`. */
    cnor_stack_slots_t stack_slots; /**< `--stack`. */
    cnor_call_depth_t max_depth;    /**< `--depth`. */
    lh_u32_t str_pool_size;         /**< `--pool`. */
};
/** @copybrief cnor_cli_options */
typedef struct cnor_cli_options cnor_cli_options_t;

/**
 * @brief Parse @p args into @p out.
 * @return ::cnor_cli_exit_ok, or ::cnor_cli_exit_usage after a message on the
 *         standard error.
 */
cnor_cli_exit_t
cnor_cli_options_parse(cnor_cli_options_t *out, const cnor_cli_args_t *args);

/** @brief The `--help` text. */
void
cnor_cli_options_print_help(void);

#endif /* CNOR_CLI_OPTIONS_H */
