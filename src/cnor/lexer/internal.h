/**
 * @file internal.h
 * @brief Library-private helpers shared by the lexer's translation units.
 */

#ifndef CNOR_SRC_LEXER_INTERNAL_H
#define CNOR_SRC_LEXER_INTERNAL_H

#include <cnor/lexer.h>
#include <lh/char.h>

/** @brief The character at @p offset, or 0 past the end of the source. */
lh_char_t
cnor_lexer_char_at(const cnor_lexer_t *self, cnor_source_offset_t offset);

/** @brief Record a failure at @p pos. @return Always ::lh_bool_false. */
lh_bool_t
cnor_lexer_fail(cnor_lexer_t *self, cnor_error_t code, cnor_source_offset_t pos);

/**
 * @brief Fill @p out. Any kind but newline, indent and dedent marks the
 *        current line as carrying tokens.
 * @return Always ::lh_bool_true.
 */
lh_bool_t
cnor_lexer_emit(cnor_lexer_t *self, cnor_token_t *out, cnor_token_kind_t kind,
                cnor_source_offset_t start, cnor_line_t line);

/**
 * @brief Read the indentation of a fresh line.
 *
 * Skips blank and comment-only lines. May push an indent level (and emit
 * ::cnor_token_indent into @p out, setting @p emitted) or queue dedents.
 *
 * @return ::lh_bool_false on a bad dedent or too many levels.
 */
lh_bool_t
cnor_lexer_read_line_start(cnor_lexer_t *self, cnor_token_t *out, lh_bool_t *emitted);

/** @brief Scan a string literal starting at the opening quote. */
lh_bool_t
cnor_lexer_scan_string(cnor_lexer_t *self, cnor_token_t *out);

/** @brief Scan an integer or real literal starting at its first digit. */
lh_bool_t
cnor_lexer_scan_number(cnor_lexer_t *self, cnor_token_t *out);

/** @brief Scan an identifier or keyword starting at its first character. */
lh_bool_t
cnor_lexer_scan_word(cnor_lexer_t *self, cnor_token_t *out);

/** @brief Scan punctuation; fails with ::cnor_error_bad_character otherwise. */
lh_bool_t
cnor_lexer_scan_punct(cnor_lexer_t *self, cnor_token_t *out);

/** @brief Whether @p c may start an identifier: a letter or `_`. */
lh_bool_t
cnor_lexer_is_word_start(lh_char_t c);

/** @brief Whether @p c may continue an identifier. */
lh_bool_t
cnor_lexer_is_word_part(lh_char_t c);

#endif /* CNOR_SRC_LEXER_INTERNAL_H */
