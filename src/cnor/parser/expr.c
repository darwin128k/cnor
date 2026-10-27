#include <cnor/parser/internal.h>
#include <lh/null.h>

/* -- operator table -------------------------------------------------------- */

/**
 * One binary operator: the token that spells it, the node operator, and how
 * tightly it binds (higher binds tighter). A new operator is one line here.
 */
struct cnor_parser_binary_rule
{
    cnor_token_kind_t kind; /* cnor_token_punct or cnor_token_keyword */
    lh_u32_t spelling;      /* the cnor_punct_t or cnor_keyword_t */
    cnor_binary_op_t op;
    lh_u32_t precedence;
};

static const struct cnor_parser_binary_rule m_binary_rules[] = {
    {cnor_token_keyword, cnor_keyword_or, cnor_binary_or, 1U},
    {cnor_token_keyword, cnor_keyword_and, cnor_binary_and, 2U},
    {cnor_token_punct, cnor_punct_eq, cnor_binary_eq, 3U},
    {cnor_token_punct, cnor_punct_ne, cnor_binary_ne, 3U},
    {cnor_token_punct, cnor_punct_lt, cnor_binary_lt, 3U},
    {cnor_token_punct, cnor_punct_le, cnor_binary_le, 3U},
    {cnor_token_punct, cnor_punct_gt, cnor_binary_gt, 3U},
    {cnor_token_punct, cnor_punct_ge, cnor_binary_ge, 3U},
    {cnor_token_punct, cnor_punct_pipe, cnor_binary_bor, 4U},
    {cnor_token_punct, cnor_punct_caret, cnor_binary_bxor, 5U},
    {cnor_token_punct, cnor_punct_amp, cnor_binary_band, 6U},
    {cnor_token_punct, cnor_punct_shift_left, cnor_binary_shl, 7U},
    {cnor_token_punct, cnor_punct_shift_right, cnor_binary_shr, 7U},
    {cnor_token_punct, cnor_punct_plus, cnor_binary_add, 8U},
    {cnor_token_punct, cnor_punct_minus, cnor_binary_sub, 8U},
    {cnor_token_punct, cnor_punct_star, cnor_binary_mul, 9U},
    {cnor_token_punct, cnor_punct_slash, cnor_binary_div, 9U},
    {cnor_token_punct, cnor_punct_percent, cnor_binary_mod, 9U},
};

#define CNOR_PARSER_BINARY_RULE_COUNT (sizeof(m_binary_rules) / sizeof(m_binary_rules[0]))

static const struct cnor_parser_binary_rule *
cnor_parser_binary_rule_of(const cnor_token_t *token)
{
    lh_uint_t i;

    for (i = 0U; i < CNOR_PARSER_BINARY_RULE_COUNT; ++i)
    {
        const struct cnor_parser_binary_rule *rule = &m_binary_rules[i];
        const lh_u32_t spelling = token->kind == cnor_token_punct ? (lh_u32_t) token->punct
                                                                  : (lh_u32_t) token->keyword;

        if (rule->kind == token->kind && rule->spelling == spelling)
        {
            return rule;
        }
    }
    return lh_null;
}

/* -- primaries ------------------------------------------------------------- */

static cnor_node_t *
cnor_parser_word_literal(cnor_parser_t *self, cnor_node_kind_t kind)
{
    cnor_node_t *node = cnor_parser_node(self, kind);

    if (node != lh_null)
    {
        cnor_parser_advance(self);
    }
    return node;
}

static cnor_node_t *
cnor_parser_ident(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_ident);

    if (node == lh_null || !cnor_parser_take_name(self, node))
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    return node;
}

static cnor_node_t *
cnor_parser_parenthesized(cnor_parser_t *self)
{
    cnor_node_t *node;

    cnor_parser_advance(self);
    node = cnor_parser_expr(self);
    if (node == lh_null || !cnor_parser_expect_punct(self, cnor_punct_rparen))
    {
        return lh_null;
    }
    return node;
}

/* Both `[fill; count]` and an explicit list `[a, b, c]`. */
static cnor_node_t *
cnor_parser_array(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_array);
    cnor_node_list_t values = {lh_null, lh_null};
    if (node == lh_null || !cnor_parser_advance(self)) return lh_null;
    node->data.array.is_list = lh_bool_true;
    if (!cnor_parser_at_punct(self, cnor_punct_rbracket))
    {
        cnor_node_t *first = cnor_parser_expr(self);
        if (first == lh_null) return lh_null;
        if (cnor_parser_accept_punct(self, cnor_punct_semicolon))
        {
            cnor_node_t *count = cnor_parser_expr(self);
            if (count == lh_null) return lh_null;
            if (count->kind != cnor_node_int) return cnor_parser_fail(self, cnor_error_bad_number);
            node->data.array.value = first;
            node->data.array.count = count->data.int_.value;
            node->data.array.is_list = lh_bool_false;
        }
        else
        {
            cnor_node_list_append(&values, first);
            node->data.array.count = 1U;
            while (cnor_parser_accept_punct(self, cnor_punct_comma)
                   && !cnor_parser_at_punct(self, cnor_punct_rbracket))
            {
                cnor_node_t *value = cnor_parser_expr(self);
                if (value == lh_null) return lh_null;
                if (++node->data.array.count >= CNOR_SLOT_MAX)
                    return cnor_parser_fail(self, cnor_error_too_many_locals);
                cnor_node_list_append(&values, value);
            }
            node->data.array.value = values.head;
        }
    }
    if (!cnor_parser_expect_punct(self, cnor_punct_rbracket)) return lh_null;
    return node;
}

static cnor_node_t *
cnor_parser_primary(cnor_parser_t *self)
{
    if (cnor_parser_at(self, cnor_token_ident))
    {
        return cnor_parser_ident(self);
    }
    if (cnor_parser_at_punct(self, cnor_punct_lparen))
    {
        return cnor_parser_parenthesized(self);
    }
    if (cnor_parser_at_punct(self, cnor_punct_lbracket))
    {
        return cnor_parser_array(self);
    }
    if (cnor_parser_at_keyword(self, cnor_keyword_true))
    {
        return cnor_parser_word_literal(self, cnor_node_true);
    }
    if (cnor_parser_at_keyword(self, cnor_keyword_false))
    {
        return cnor_parser_word_literal(self, cnor_node_false);
    }
    if (cnor_parser_at_keyword(self, cnor_keyword_absent))
    {
        return cnor_parser_word_literal(self, cnor_node_none);
    }
    return cnor_parser_literal(self);
}

/* -- postfix --------------------------------------------------------------- */

/* `name(args)`. Only a name can be called: there are no function values. */
static cnor_node_t *
cnor_parser_call(cnor_parser_t *self, cnor_node_t *callee)
{
    cnor_node_list_t args = {lh_null, lh_null};
    cnor_node_t *call = cnor_parser_node(self, cnor_node_call);
    lh_u32_t argc = 0U;

    if (call == lh_null)
    {
        return lh_null;
    }
    if (callee->kind != cnor_node_ident)
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    call->line = callee->line;
    call->name = callee->name;
    cnor_parser_advance(self);
    while (!cnor_parser_at_punct(self, cnor_punct_rparen))
    {
        cnor_node_t *arg = cnor_parser_expr(self);

        if (arg == lh_null)
        {
            return lh_null;
        }
        if (++argc > CNOR_ARGC_MAX)
        {
            return cnor_parser_fail(self, cnor_error_too_many_args);
        }
        cnor_node_list_append(&args, arg);
        if (!cnor_parser_accept_punct(self, cnor_punct_comma))
        {
            break;
        }
    }
    if (!cnor_parser_expect_punct(self, cnor_punct_rparen))
    {
        return lh_null;
    }
    call->data.call.args = args.head;
    call->data.call.argc = (cnor_argc_t) argc;
    return call;
}

static cnor_node_t *
cnor_parser_subscript(cnor_parser_t *self, cnor_node_t *target)
{
    cnor_node_t *node = cnor_parser_node(self, cnor_node_index);

    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    node->data.index.target = target;
    node->data.index.index = cnor_parser_expr(self);
    if (node->data.index.index == lh_null || !cnor_parser_expect_punct(self, cnor_punct_rbracket))
    {
        return lh_null;
    }
    return node;
}

static cnor_node_t *
cnor_parser_postfix(cnor_parser_t *self)
{
    cnor_node_t *node = cnor_parser_primary(self);

    while (node != lh_null)
    {
        if (cnor_parser_at_punct(self, cnor_punct_lparen))
        {
            node = cnor_parser_call(self, node);
        }
        else if (cnor_parser_accept_punct(self, cnor_punct_dot))
        {
            cnor_node_t *member = cnor_parser_node(self, cnor_node_member);
            if (member == lh_null || !cnor_parser_at(self, cnor_token_ident)
                || !cnor_parser_take_name(self, member) || !cnor_parser_advance(self))
            {
                return cnor_parser_fail(self, cnor_error_unexpected_token);
            }
            member->data.index.target = node;
            node = member;
        }
        else if (cnor_parser_at_punct(self, cnor_punct_lbracket))
        {
            node = cnor_parser_subscript(self, node);
        }
        else
        {
            break;
        }
    }
    return node;
}

/* -- unary and binary ------------------------------------------------------ */

cnor_node_t *
cnor_parser_unary(cnor_parser_t *self)
{
    cnor_unary_op_t op;
    cnor_node_t *node;

    if (cnor_parser_at_punct(self, cnor_punct_minus))
    {
        op = cnor_unary_neg;
    }
    else if (cnor_parser_at_punct(self, cnor_punct_tilde))
    {
        op = cnor_unary_bnot;
    }
    else if (cnor_parser_at_keyword(self, cnor_keyword_not))
    {
        op = cnor_unary_not;
    }
    else
    {
        return cnor_parser_postfix(self);
    }
    node = cnor_parser_node(self, cnor_node_unary);
    if (node == lh_null)
    {
        return lh_null;
    }
    cnor_parser_advance(self);
    node->data.unary.op = op;
    node->data.unary.operand = cnor_parser_unary(self);
    return node->data.unary.operand == lh_null ? lh_null : node;
}

/* Precedence climbing: operators binding at least @p min_precedence. */
static cnor_node_t *
cnor_parser_binary(cnor_parser_t *self, lh_u32_t min_precedence)
{
    cnor_node_t *left = cnor_parser_unary(self);

    while (left != lh_null)
    {
        const struct cnor_parser_binary_rule *rule = cnor_parser_binary_rule_of(&self->token);
        cnor_node_t *node;

        if (rule == lh_null || rule->precedence < min_precedence)
        {
            break;
        }
        node = cnor_parser_node(self, cnor_node_binary);
        if (node == lh_null)
        {
            return lh_null;
        }
        cnor_parser_advance(self);
        node->line = left->line;
        node->data.binary.op = rule->op;
        node->data.binary.left = left;
        node->data.binary.right = cnor_parser_binary(self, rule->precedence + 1U);
        if (node->data.binary.right == lh_null)
        {
            return lh_null;
        }
        left = node;
    }
    return left;
}

cnor_node_t *
cnor_parser_expr(cnor_parser_t *self)
{
    cnor_node_t *value = cnor_parser_binary(self, 1U);

    if (value == lh_null || !cnor_parser_at_keyword(self, cnor_keyword_if))
    {
        return value;
    }
    /*
     * `value if condition else other`: the condition is an expression without
     * a ternary of its own, and the other value may have one.
     */
    {
        cnor_node_t *node = cnor_parser_node(self, cnor_node_ternary);
        cnor_node_t *cond;

        if (node == lh_null)
        {
            return lh_null;
        }
        cnor_parser_advance(self);
        node->line = value->line;
        node->data.ternary.then_value = value;
        cond = cnor_parser_binary(self, 1U);
        if (cond == lh_null)
        {
            return lh_null;
        }
        if (!cnor_parser_at_keyword(self, cnor_keyword_else))
        {
            return cnor_parser_fail(self, cnor_error_unexpected_token);
        }
        cnor_parser_advance(self);
        node->data.ternary.cond = cond;
        node->data.ternary.else_value = cnor_parser_expr(self);
        return node->data.ternary.else_value == lh_null ? lh_null : node;
    }
}
