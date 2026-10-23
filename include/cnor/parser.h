/**
 * @file parser.h
 * @brief Tokens to a ::cnor_node_t tree (::cnor_parser_t).
 *
 * Recursive descent with one token of lookahead. Statements are found through
 * a table keyed by their leading keyword, expressions by a precedence table, so
 * a new construct is one rule function and one table entry.
 *
 * The parser does not resolve names and does not check types; both are the
 * compiler's business. String escapes are applied here, so the tree holds the
 * final bytes of every literal.
 *
 * A failure returns ::lh_null and leaves the reason in ::cnor_parser_get_diag.
 */

#ifndef CNOR_PARSER_H
#define CNOR_PARSER_H

#include <cnor/arena.h>
#include <cnor/ast.h>
#include <cnor/diag.h>
#include <cnor/lexer.h>
#include <lh/compiler/extern/c.h>
#include <lh/str/view.h>

/**
 * @struct cnor_parser
 * @brief Parser state. Every field is written by ::cnor_parser_init.
 */
struct cnor_parser
{
    cnor_lexer_t lexer;   /**< Token source. */
    cnor_token_t token;   /**< Current token. */
    cnor_arena_t *arena;  /**< Where nodes go; borrowed. */
    lh_str_view_t src;    /**< Source text; borrowed. */
    cnor_diag_t diag;     /**< Why parsing stopped. */
};
/** @copybrief cnor_parser */
typedef struct cnor_parser cnor_parser_t;

LH_COMPILER_EXTERN_C_BEGIN

/**
 * @brief Prepare @p self to read @p src into @p arena.
 *
 * Names and literal text are copied into @p arena, so the tree outlives @p src.
 */
void
cnor_parser_init(cnor_parser_t *self, lh_str_view_t src, cnor_arena_t *arena);

/**
 * @brief Parse a whole file: functions, native declarations and top-level
 *        statements, in source order.
 *
 * @return The first top-level node (the rest are chained through `next`), or
 *         ::lh_null on failure. An empty file is ::cnor_error_empty_block.
 */
cnor_node_t *
cnor_parser_parse(cnor_parser_t *self);

/** @brief Why parsing stopped, or ::cnor_error_none. */
const cnor_diag_t *
cnor_parser_get_diag(const cnor_parser_t *self);

/** @brief The built-in type @p name spells, or ::cnor_type_none. */
cnor_type_t
cnor_parser_type_of(lh_str_view_t name);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_PARSER_H */
