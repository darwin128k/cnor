/**
 * @file text.h
 * @brief How the command line writes a value: what `print` and a run's result
 *        show.
 *
 * - `none`, `true`, `false`;
 * - integers in decimal;
 * - reals with the fewest digits that read back as the same value, with a
 *   `.0` when that would look like an integer; `nan`, `inf`, `-inf`;
 * - strings as their bytes, unquoted;
 * - a host pointer as `<ptr>`.
 */

#ifndef CNOR_CLI_TEXT_H
#define CNOR_CLI_TEXT_H

#include <lh/bool.h>
#include <lh/value.h>
#include <stdio.h>

/** @brief Write @p value to @p to. @return ::lh_bool_false on a write error. */
lh_bool_t
cnor_cli_text_write_value(FILE *to, const lh_value_t *value);

#endif /* CNOR_CLI_TEXT_H */
