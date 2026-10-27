#include <cnor/parser/internal.h>
#include <lh/null.h>

/* -- types ----------------------------------------------------------------- */

static const lh_str_view_t m_type_names[cnor_type_count] = {
    lh_str_view_empty_initializer(),  lh_str_view_initializer_lit("i32"),
    lh_str_view_initializer_lit("u32"), lh_str_view_initializer_lit("f32"),
    lh_str_view_initializer_lit("bool"), lh_str_view_initializer_lit("str"),
    lh_str_view_initializer_lit("array"), lh_str_view_initializer_lit("dict"),
};

cnor_type_t
cnor_parser_type_of(lh_str_view_t name)
{
    lh_uint_t i;

    for (i = 1U; i < (lh_uint_t) cnor_type_count; ++i)
    {
        if (lh_str_view_equals(&name, &m_type_names[i], lh_bool_false))
        {
            return (cnor_type_t) i;
        }
    }
    return cnor_type_none;
}

lh_bool_t
cnor_parser_type(cnor_parser_t *self, cnor_type_t *out)
{
    if (!cnor_parser_at(self, cnor_token_ident))
    {
        cnor_parser_fail(self, cnor_error_unexpected_token);
        return lh_bool_false;
    }
    *out = cnor_parser_type_of(cnor_parser_text(self));
    if (*out == cnor_type_none)
    {
        cnor_parser_fail(self, cnor_error_unknown_type);
        return lh_bool_false;
    }
    return cnor_parser_advance(self);
}

/* -- simple statements ----------------------------------------------------- */

static cnor_node_t *
cnor_parser_let(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_let);

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    if (cnor_parser_at_keyword(self, cnor_keyword_mut))
    {
        node->data.let.is_mutable = lh_bool_true;
        cnor_parser_advance(self);
    }
    if (!cnor_parser_at(self, cnor_token_ident))
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    if (!cnor_parser_take_name(self, node) || !cnor_parser_advance(self))
    {
        return lh_null;
    }
    if (cnor_parser_accept_punct(self, cnor_punct_colon)
        && !cnor_parser_type(self, &node->data.let.type))
    {
        return lh_null;
    }
    if (cnor_parser_accept_punct(self, cnor_punct_assign))
    {
        node->data.let.init = cnor_parser_expr(self);
        if (node->data.let.init == lh_null)
        {
            return lh_null;
        }
    }
    return node;
}

static lh_bool_t
cnor_parser_at_statement_end(const cnor_parser_t *self)
{
    return (cnor_parser_at(self, cnor_token_newline) || cnor_parser_at(self, cnor_token_dedent)
            || cnor_parser_at(self, cnor_token_eof)
            || cnor_parser_at_punct(self, cnor_punct_semicolon)
            || cnor_parser_at_punct(self, cnor_punct_comma))
               ? lh_bool_true
               : lh_bool_false;
}

static cnor_node_t *
cnor_parser_return(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_return);

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    if (!cnor_parser_at_statement_end(self))
    {
        node->data.return_.value = cnor_parser_expr(self);
        if (node->data.return_.value == lh_null)
        {
            return lh_null;
        }
    }
    return node;
}

static cnor_node_t *
cnor_parser_jump(cnor_parser_t *self)
{
    const cnor_node_kind_t kind =
        cnor_parser_at_keyword(self, cnor_keyword_break) ? cnor_node_break : cnor_node_continue;
    cnor_node_t *node = cnor_parser_node(self, kind);

    if (node != lh_null)
    {
        cnor_parser_advance(self);
    }
    return node;
}

/* `raise`: the error being handled goes on outward. It takes no value: there
 * is only one error in flight at a time, and it is the one being handled. */
static cnor_node_t *
cnor_parser_raise(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_raise);

    if (node != lh_null)
    {
        cnor_parser_advance(self);
    }
    return node;
}

/* An expression, or an assignment when `=` follows it. */
static cnor_node_t *
cnor_parser_expr_or_assign(cnor_parser_t *self)
{
    cnor_node_t *value = cnor_parser_expr(self);
    cnor_node_t *node;

    if (value == lh_null)
    {
        return lh_null;
    }
    if (cnor_parser_at_punct(self, cnor_punct_assign))
    {
        node = cnor_parser_node(self, cnor_node_assign);
        if (node == lh_null)
        {
            return lh_null;
        }
        cnor_parser_advance(self);
        node->line = value->line;
        node->data.assign.target = value;
        node->data.assign.value = cnor_parser_expr(self);
        return node->data.assign.value == lh_null ? lh_null : node;
    }
    node = cnor_parser_node(self, cnor_node_expr_stmt);
    if (node != lh_null)
    {
        node->line = value->line;
        node->data.expr_stmt.expr = value;
    }
    return node;
}

/* -- import ---------------------------------------------------------------- */

cnor_node_t *
cnor_parser_import(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_import);
    cnor_node_t *path;

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    if (!cnor_parser_at(self, cnor_token_string))
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    path = cnor_parser_literal(self);
    if (path == lh_null)
    {
        return lh_null;
    }
    node->name = path->name;
    return cnor_parser_end_simple(self, lh_bool_false) ? node : lh_null;
}

/* An `import` inside a block. */
static cnor_node_t *
cnor_parser_misplaced_import(cnor_parser_t *self)
{
    return cnor_parser_fail(self, cnor_error_misplaced_import);
}

/* -- statement table ------------------------------------------------------- */

/**
 * Statements found by their leading keyword. `compound` ones end with their own
 * block and need no terminator. A new statement is one entry here.
 */
struct cnor_parser_stmt_rule
{
    cnor_keyword_t keyword;
    cnor_parser_rule_fn parse;
    lh_bool_t compound;
};

static const struct cnor_parser_stmt_rule m_stmt_rules[] = {
    {cnor_keyword_let, cnor_parser_let, lh_bool_false},
    {cnor_keyword_return, cnor_parser_return, lh_bool_false},
    {cnor_keyword_break, cnor_parser_jump, lh_bool_false},
    {cnor_keyword_continue, cnor_parser_jump, lh_bool_false},
    {cnor_keyword_raise, cnor_parser_raise, lh_bool_false},
    {cnor_keyword_if, cnor_parser_if, lh_bool_true},
    {cnor_keyword_while, cnor_parser_while, lh_bool_true},
    {cnor_keyword_for, cnor_parser_for, lh_bool_true},
    {cnor_keyword_match, cnor_parser_match, lh_bool_true},
    {cnor_keyword_try, cnor_parser_try, lh_bool_true},
    {cnor_keyword_import, cnor_parser_misplaced_import, lh_bool_false},
};

#define CNOR_PARSER_STMT_RULE_COUNT (sizeof(m_stmt_rules) / sizeof(m_stmt_rules[0]))

/* The rule of the statement at the current token; most statements (an
 * assignment, a call) start with a name and need no look at the table. */
static const struct cnor_parser_stmt_rule *
cnor_parser_stmt_rule_of(const cnor_parser_t *self)
{
    lh_uint_t i;

    if (self->token.kind != cnor_token_keyword)
    {
        return lh_null;
    }
    for (i = 0U; i < CNOR_PARSER_STMT_RULE_COUNT; ++i)
    {
        if (self->token.keyword == m_stmt_rules[i].keyword)
        {
            return &m_stmt_rules[i];
        }
    }
    return lh_null;
}

lh_bool_t
cnor_parser_end_simple(cnor_parser_t *self, lh_bool_t allow_comma)
{
    if (allow_comma)
    {
        cnor_parser_accept_punct(self, cnor_punct_comma);
    }
    if (cnor_parser_accept_punct(self, cnor_punct_semicolon)
        && !cnor_parser_at(self, cnor_token_newline) && !cnor_parser_at(self, cnor_token_dedent)
        && !cnor_parser_at(self, cnor_token_eof))
    {
        /* `a = 1; b = 2`: the next statement follows on the same line. */
        return cnor_diag_is_ok(&self->diag);
    }
    if (cnor_parser_at(self, cnor_token_newline))
    {
        return cnor_parser_advance(self);
    }
    if (cnor_parser_at(self, cnor_token_dedent) || cnor_parser_at(self, cnor_token_eof))
    {
        return cnor_diag_is_ok(&self->diag);
    }
    cnor_parser_fail(self, cnor_error_unexpected_token);
    return lh_bool_false;
}

/* A simple statement whose rule (::lh_null for none) is already looked up. */
static cnor_node_t *
cnor_parser_simple_by_rule(cnor_parser_t *self, const struct cnor_parser_stmt_rule *rule)
{
    if (rule != lh_null && !rule->compound)
    {
        return rule->parse(self);
    }
    if (rule != lh_null)
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    return cnor_parser_expr_or_assign(self);
}

cnor_node_t *
cnor_parser_simple_statement(cnor_parser_t *self)
{
    return cnor_parser_simple_by_rule(self, cnor_parser_stmt_rule_of(self));
}

cnor_node_t *
cnor_parser_statement(cnor_parser_t *self)
{
    const struct cnor_parser_stmt_rule *rule = cnor_parser_stmt_rule_of(self);
    cnor_node_t *node;

    if (rule != lh_null && rule->compound)
    {
        return rule->parse(self);
    }
    node = cnor_parser_simple_by_rule(self, rule);
    if (node == lh_null || !cnor_parser_end_simple(self, lh_bool_false))
    {
        return lh_null;
    }
    return node;
}

/* -- blocks ---------------------------------------------------------------- */

cnor_node_t *
cnor_parser_block(cnor_parser_t *self)
{
    cnor_node_list_t body = {lh_null, lh_null};

    if (!cnor_parser_expect(self, cnor_token_newline, cnor_error_unexpected_token)
        || !cnor_parser_expect(self, cnor_token_indent, cnor_error_empty_block))
    {
        return lh_null;
    }
    while (!cnor_parser_at(self, cnor_token_dedent))
    {
        cnor_node_t *statement;

        if (cnor_parser_at(self, cnor_token_eof))
        {
            return cnor_parser_fail(self, cnor_error_unexpected_token);
        }
        statement = cnor_parser_statement(self);
        if (statement == lh_null)
        {
            return lh_null;
        }
        cnor_node_list_append(&body, statement);
    }
    cnor_parser_advance(self);
    return body.head;
}
