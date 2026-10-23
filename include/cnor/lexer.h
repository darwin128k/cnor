/**
 * @file lexer.h
 * @brief Source text to tokens, with indentation resolved (::cnor_lexer_t).
 *
 * A pull lexer: ::cnor_lexer_next returns one token per call and never
 * allocates.
 *
 * ## Lines and blocks
 *
 * A logical line that carried tokens ends in ::cnor_token_newline. A line
 * indented deeper than the previous one starts with ::cnor_token_indent; a
 * line indented shallower starts with one ::cnor_token_dedent per closed level.
 * Blank lines and comment lines (`#` to end of line) produce nothing. An open
 * `(` or `[` holds lines together: no newline, indent or dedent inside it.
 * The end of the source closes the last line and every open block.
 *
 * A tab is worth ::CNOR_LEXER_TAB_WIDTH columns and a space one.
 */

#ifndef CNOR_LEXER_H
#define CNOR_LEXER_H

#include <cnor/diag.h>
#include <cnor/lexer/token.h>
#include <cnor/types.h>
#include <lh/bool.h>
#include <lh/compiler/extern/c.h>
#include <lh/str/view.h>

/**
 * @def CNOR_LEXER_MAX_INDENTS
 * @brief How many blocks may be open at once, the outermost level included.
 */
#define CNOR_LEXER_MAX_INDENTS 64U

/**
 * @def CNOR_LEXER_TAB_WIDTH
 * @brief Columns a tab is worth.
 */
#define CNOR_LEXER_TAB_WIDTH 4U

/**
 * @typedef cnor_column_t
 * @brief Indentation width of a line, in columns.
 */
typedef lh_u32_t cnor_column_t;

/**
 * @struct cnor_lexer
 * @brief Lexer state. Every field is written by ::cnor_lexer_init.
 */
struct cnor_lexer
{
    lh_str_view_t src;                             /**< Source; not owned. */
    cnor_source_offset_t pos;                      /**< Next unread byte. */
    cnor_line_t line;                              /**< Line of `pos`. */
    cnor_column_t indents[CNOR_LEXER_MAX_INDENTS]; /**< Open block columns, ascending. */
    lh_u32_t depth;                                /**< Entries of `indents` in use. */
    lh_u32_t brackets;                             /**< Open `(` and `[`. */
    lh_u32_t pending_dedents;                      /**< Dedents owed before the next token. */
    lh_bool_t at_line_start;                       /**< Indentation still to be read. */
    lh_bool_t line_has_tokens;                     /**< The current line produced a token. */
    cnor_diag_t diag;                              /**< Why lexing stopped. */
};
/** @copybrief cnor_lexer */
typedef struct cnor_lexer cnor_lexer_t;

LH_COMPILER_EXTERN_C_BEGIN

/**
 * @brief Point @p self at @p src. Nothing is copied: @p src must outlive it.
 */
void
cnor_lexer_init(cnor_lexer_t *self, lh_str_view_t src);

/**
 * @brief Produce the next token into @p out.
 *
 * @return ::lh_bool_false only when the source cannot be lexed; see
 *         ::cnor_lexer_get_diag. A finished source keeps yielding
 *         ::cnor_token_eof.
 */
lh_bool_t
cnor_lexer_next(cnor_lexer_t *self, cnor_token_t *out);

/** @brief Why the last call failed; ::cnor_error_none when it did not. */
const cnor_diag_t *
cnor_lexer_get_diag(const cnor_lexer_t *self);

/**
 * @brief The keyword @p word spells, or ::cnor_keyword_none.
 */
cnor_keyword_t
cnor_lexer_keyword_of(lh_str_view_t word);

/**
 * @brief The text a token covers inside @p src.
 */
lh_str_view_t
cnor_token_get_text(const cnor_token_t *self, lh_str_view_t src);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_LEXER_H */
