/**
 * @file internal.h
 * @brief Library-private: rules and token plumbing shared by the parser's
 *        translation units.
 */

#ifndef CNOR_SRC_PARSER_INTERNAL_H
#define CNOR_SRC_PARSER_INTERNAL_H

#include <cnor/parser.h>

/**
 * @typedef cnor_parser_rule_fn
 * @brief A grammar rule: parses one construct starting at the current token.
 */
typedef cnor_node_t *(*cnor_parser_rule_fn)(cnor_parser_t *self);

/**
 * @struct cnor_node_list
 * @brief Head and tail of a list being built, so appending is O(1).
 */
struct cnor_node_list
{
    cnor_node_t *head;
    cnor_node_t *tail;
};
/** @copybrief cnor_node_list */
typedef struct cnor_node_list cnor_node_list_t;

/* -- token plumbing (token.c) ---------------------------------------------- */

/** @brief Record @p code at the current token. @return Always ::lh_null. */
cnor_node_t *
cnor_parser_fail(cnor_parser_t *self, cnor_error_t code);

/** @brief Move to the next token; a lexer failure becomes the parser's. */
lh_bool_t
cnor_parser_advance(cnor_parser_t *self);

/** @brief Whether the current token is of @p kind. */
lh_bool_t
cnor_parser_at(const cnor_parser_t *self, cnor_token_kind_t kind);

/** @brief Whether the current token is the punctuator @p punct. */
lh_bool_t
cnor_parser_at_punct(const cnor_parser_t *self, cnor_punct_t punct);

/** @brief Whether the current token is the keyword @p keyword. */
lh_bool_t
cnor_parser_at_keyword(const cnor_parser_t *self, cnor_keyword_t keyword);

/** @brief Consume @p punct if it is current. */
lh_bool_t
cnor_parser_accept_punct(cnor_parser_t *self, cnor_punct_t punct);

/** @brief Consume @p punct or fail with ::cnor_error_unexpected_token. */
lh_bool_t
cnor_parser_expect_punct(cnor_parser_t *self, cnor_punct_t punct);

/** @brief Consume a token of @p kind or fail with @p code. */
lh_bool_t
cnor_parser_expect(cnor_parser_t *self, cnor_token_kind_t kind, cnor_error_t code);

/** @brief Text of the current token inside the source. */
lh_str_view_t
cnor_parser_text(const cnor_parser_t *self);

/** @brief A zeroed node of @p kind at the current line. */
cnor_node_t *
cnor_parser_node(cnor_parser_t *self, cnor_node_kind_t kind);

/** @brief Copy the current token's text into @p node's name. */
lh_bool_t
cnor_parser_take_name(cnor_parser_t *self, cnor_node_t *node);

/** @brief Append @p node to @p list. */
void
cnor_node_list_append(cnor_node_list_t *list, cnor_node_t *node);

/* -- literals (literal.c) -------------------------------------------------- */

/** @brief Parse the current int, real or string token into a literal node. */
cnor_node_t *
cnor_parser_literal(cnor_parser_t *self);

/* -- expressions (expr.c) -------------------------------------------------- */

/** @brief Parse a full expression. */
cnor_node_t *
cnor_parser_expr(cnor_parser_t *self);

/** @brief Parse a unary expression (used for match patterns). */
cnor_node_t *
cnor_parser_unary(cnor_parser_t *self);

/* -- statements (stmt.c) --------------------------------------------------- */

/** @brief Parse one statement, compound or simple. */
cnor_node_t *
cnor_parser_statement(cnor_parser_t *self);

/** @brief Parse a simple statement (no block) without its terminator. */
cnor_node_t *
cnor_parser_simple_statement(cnor_parser_t *self);

/**
 * @brief Close a simple statement: an optional `;` (and `,` when
 *        @p allow_comma), then a newline, a dedent or the end of the source.
 */
lh_bool_t
cnor_parser_end_simple(cnor_parser_t *self, lh_bool_t allow_comma);

/** @brief Parse `NEWLINE INDENT statement+ DEDENT`; the `:` is already read. */
cnor_node_t *
cnor_parser_block(cnor_parser_t *self);

/** @brief Parse `name: type` (the type part) into @p out. */
lh_bool_t
cnor_parser_type(cnor_parser_t *self, cnor_type_t *out);

/* -- control flow (control.c) ---------------------------------------------- */

/** @brief `if cond:` block, optional `else if` / `else:`. */
cnor_node_t *
cnor_parser_if(cnor_parser_t *self);

/** @brief `while cond:` block. */
cnor_node_t *
cnor_parser_while(cnor_parser_t *self);

/** @brief `for name in from..to:` block. */
cnor_node_t *
cnor_parser_for(cnor_parser_t *self);

/** @brief `match subject:` NEWLINE INDENT arms DEDENT. */
cnor_node_t *
cnor_parser_match(cnor_parser_t *self);

/* -- declarations (fn.c) --------------------------------------------------- */

/** @brief `fn` or `native fn` declaration. */
cnor_node_t *
cnor_parser_fn(cnor_parser_t *self);

#endif /* CNOR_SRC_PARSER_INTERNAL_H */
