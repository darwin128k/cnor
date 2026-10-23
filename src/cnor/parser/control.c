#include <cnor/parser/internal.h>
#include <lh/null.h>

/* -- shared ---------------------------------------------------------------- */

/* Read `expr :` followed by a block into @p cond and @p body. */
static lh_bool_t
cnor_parser_header_and_block(cnor_parser_t *self, cnor_node_t **cond, cnor_node_t **body)
{
    *cond = cnor_parser_expr(self);
    if (*cond == lh_null || !cnor_parser_expect_punct(self, cnor_punct_colon))
    {
        return lh_bool_false;
    }
    *body = cnor_parser_block(self);
    return *body != lh_null ? lh_bool_true : lh_bool_false;
}

/* -- if -------------------------------------------------------------------- */

static lh_bool_t
cnor_parser_else(cnor_parser_t *self, cnor_node_t *node)
{
    cnor_parser_advance(self);
    if (cnor_parser_at_keyword(self, cnor_keyword_if))
    {
        /* `else if` is an `if` that is the whole else block. */
        node->data.if_.else_body = cnor_parser_if(self);
    }
    else
    {
        if (!cnor_parser_expect_punct(self, cnor_punct_colon))
        {
            return lh_bool_false;
        }
        node->data.if_.else_body = cnor_parser_block(self);
    }
    return node->data.if_.else_body != lh_null ? lh_bool_true : lh_bool_false;
}

cnor_node_t *
cnor_parser_if(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_if);

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    if (!cnor_parser_header_and_block(self, &node->data.if_.cond, &node->data.if_.then_body))
    {
        return lh_null;
    }
    if (cnor_parser_at_keyword(self, cnor_keyword_else) && !cnor_parser_else(self, node))
    {
        return lh_null;
    }
    return node;
}

/* -- loops ----------------------------------------------------------------- */

cnor_node_t *
cnor_parser_while(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_while);

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    if (!cnor_parser_header_and_block(self, &node->data.while_.cond, &node->data.while_.body))
    {
        return lh_null;
    }
    return node;
}

cnor_node_t *
cnor_parser_for(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_for);

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    if (!cnor_parser_at(self, cnor_token_ident))
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    if (!cnor_parser_take_name(self, node) || !cnor_parser_advance(self))
    {
        return lh_null;
    }
    if (!cnor_parser_at_keyword(self, cnor_keyword_in))
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    cnor_parser_advance(self);
    node->data.for_.from = cnor_parser_expr(self);
    if (node->data.for_.from == lh_null || !cnor_parser_expect_punct(self, cnor_punct_range))
    {
        return lh_null;
    }
    if (!cnor_parser_header_and_block(self, &node->data.for_.to, &node->data.for_.body))
    {
        return lh_null;
    }
    return node;
}

/* -- match ----------------------------------------------------------------- */

static lh_bool_t
cnor_parser_is_wildcard(const cnor_parser_t *self)
{
    const lh_str_view_t text = cnor_parser_text(self);

    return (cnor_parser_at(self, cnor_token_ident) && text.size == 1U
            && ((const lh_char_t *) text.begin)[0] == '_')
               ? lh_bool_true
               : lh_bool_false;
}

/* A pattern is a literal, optionally negated: it is compared with `==`. */
static lh_bool_t
cnor_parser_is_pattern(const cnor_node_t *node)
{
    if (node->kind == cnor_node_unary && node->data.unary.op == cnor_unary_neg)
    {
        node = node->data.unary.operand;
        return (node->kind == cnor_node_int || node->kind == cnor_node_real) ? lh_bool_true
                                                                           : lh_bool_false;
    }
    switch (node->kind)
    {
    case cnor_node_int:
    case cnor_node_real:
    case cnor_node_string:
    case cnor_node_true:
    case cnor_node_false:
    case cnor_node_none:
        return lh_bool_true;
    default:
        return lh_bool_false;
    }
}

static cnor_node_t *
cnor_parser_match_arm(cnor_parser_t *self)
{
    cnor_node_t *arm = cnor_parser_node(self, cnor_node_match_arm);

    if (arm == lh_null)
    {
        return lh_null;
    }
    if (cnor_parser_is_wildcard(self))
    {
        cnor_parser_advance(self);
    }
    else
    {
        arm->data.arm.pattern = cnor_parser_unary(self);
        if (arm->data.arm.pattern == lh_null)
        {
            return lh_null;
        }
        if (!cnor_parser_is_pattern(arm->data.arm.pattern))
        {
            return cnor_parser_fail(self, cnor_error_unexpected_token);
        }
    }
    if (!cnor_parser_expect_punct(self, cnor_punct_fat_arrow))
    {
        return lh_null;
    }
    arm->data.arm.body = cnor_parser_simple_statement(self);
    if (arm->data.arm.body == lh_null || !cnor_parser_end_simple(self, lh_bool_true))
    {
        return lh_null;
    }
    return arm;
}

cnor_node_t *
cnor_parser_match(cnor_parser_t *self)
{
    cnor_node_list_t arms = {lh_null, lh_null};
    cnor_node_t *node = cnor_parser_node(self, cnor_node_match);

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    node->data.match.subject = cnor_parser_expr(self);
    if (node->data.match.subject == lh_null || !cnor_parser_expect_punct(self, cnor_punct_colon)
        || !cnor_parser_expect(self, cnor_token_newline, cnor_error_unexpected_token)
        || !cnor_parser_expect(self, cnor_token_indent, cnor_error_empty_block))
    {
        return lh_null;
    }
    while (!cnor_parser_at(self, cnor_token_dedent))
    {
        cnor_node_t *arm;

        if (cnor_parser_at(self, cnor_token_eof))
        {
            return cnor_parser_fail(self, cnor_error_unexpected_token);
        }
        arm = cnor_parser_match_arm(self);
        if (arm == lh_null)
        {
            return lh_null;
        }
        cnor_node_list_append(&arms, arm);
    }
    cnor_parser_advance(self);
    node->data.match.arms = arms.head;
    return node;
}
