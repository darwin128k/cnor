#include <cnor/parser/internal.h>
#include <lh/null.h>

/* -- parameters ------------------------------------------------------------ */

static cnor_node_t *
cnor_parser_param(cnor_parser_t *self)
{
    cnor_node_t *param = cnor_parser_node(self, cnor_node_param);

    if (param == lh_null)
    {
        return lh_null;
    }
    if (!cnor_parser_at(self, cnor_token_ident))
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    if (!cnor_parser_take_name(self, param) || !cnor_parser_advance(self)
        || !cnor_parser_expect_punct(self, cnor_punct_colon)
        || !cnor_parser_type(self, &param->data.param.type, &param->type_name))
    {
        return lh_null;
    }
    return param;
}

static lh_bool_t
cnor_parser_params(cnor_parser_t *self, cnor_node_t *node)
{
    cnor_node_list_t params = {lh_null, lh_null};
    lh_u32_t count = 0U;

    if (!cnor_parser_expect_punct(self, cnor_punct_lparen))
    {
        return lh_bool_false;
    }
    while (!cnor_parser_at_punct(self, cnor_punct_rparen))
    {
        cnor_node_t *param = cnor_parser_param(self);

        if (param == lh_null)
        {
            return lh_bool_false;
        }
        if (++count > CNOR_ARGC_MAX)
        {
            cnor_parser_fail(self, cnor_error_too_many_args);
            return lh_bool_false;
        }
        cnor_node_list_append(&params, param);
        if (!cnor_parser_accept_punct(self, cnor_punct_comma))
        {
            break;
        }
    }
    node->data.fn.params = params.head;
    node->data.fn.param_count = (cnor_argc_t) count;
    return cnor_parser_expect_punct(self, cnor_punct_rparen);
}

/* -- declarations ---------------------------------------------------------- */

cnor_node_t *
cnor_parser_fn(cnor_parser_t *self)
{
    const lh_bool_t is_native = cnor_parser_at_keyword(self, cnor_keyword_native);
    cnor_node_t *node = cnor_parser_node(self, is_native ? cnor_node_native_fn : cnor_node_fn);

    if (node == lh_null)
    {
        return lh_null;
    }
    if (is_native)
    {
        cnor_parser_advance(self);
        if (!cnor_parser_at_keyword(self, cnor_keyword_fn))
        {
            return cnor_parser_fail(self, cnor_error_unexpected_token);
        }
    }
    cnor_parser_advance(self);
    if (!cnor_parser_at(self, cnor_token_ident))
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    if (!cnor_parser_take_name(self, node) || !cnor_parser_advance(self)
        || !cnor_parser_params(self, node))
    {
        return lh_null;
    }
    if (cnor_parser_accept_punct(self, cnor_punct_arrow)
        && !cnor_parser_type(self, &node->data.fn.returns, &node->type_name))
    {
        return lh_null;
    }
    if (is_native)
    {
        /* A native declaration has no body: it is a line of its own. */
        return cnor_parser_end_simple(self, lh_bool_false) ? node : lh_null;
    }
    if (!cnor_parser_expect_punct(self, cnor_punct_colon))
    {
        return lh_null;
    }
    node->data.fn.body = cnor_parser_block(self);
    return node->data.fn.body != lh_null ? node : lh_null;
}

/* Named types are top-level declarations. Field types are resolved after imports. */
cnor_node_t *
cnor_parser_named_type(cnor_parser_t *self)
{
    const lh_bool_t record = cnor_parser_at_keyword(self, cnor_keyword_struct);
    cnor_node_t *node = cnor_parser_node(self, record ? cnor_node_struct : cnor_node_enum);
    cnor_node_list_t fields = {lh_null, lh_null};
    lh_u32_t count = 0U;

    if (node == lh_null || !cnor_parser_advance(self)
        || !cnor_parser_at(self, cnor_token_ident)
        || !cnor_parser_take_name(self, node) || !cnor_parser_advance(self)
        || !cnor_parser_expect_punct(self, cnor_punct_colon)
        || !cnor_parser_expect(self, cnor_token_newline)
        || !cnor_parser_expect(self, cnor_token_indent))
    {
        return cnor_parser_fail(self, cnor_error_unexpected_token);
    }
    while (!cnor_parser_at(self, cnor_token_dedent))
    {
        cnor_node_t *field = cnor_parser_node(self, cnor_node_param);
        if (field == lh_null || !cnor_parser_at(self, cnor_token_ident)
            || !cnor_parser_take_name(self, field) || !cnor_parser_advance(self))
        {
            return cnor_parser_fail(self, cnor_error_unexpected_token);
        }
        if (record && (!cnor_parser_expect_punct(self, cnor_punct_colon)
            || !cnor_parser_type(self, &field->data.param.type, &field->type_name)))
        {
            return lh_null;
        }
        if (++count > CNOR_ARGC_MAX)
        {
            return cnor_parser_fail(self, cnor_error_too_many_args);
        }
        cnor_node_list_append(&fields, field);
        if (!cnor_parser_end_simple(self, lh_bool_true)) return lh_null;
    }
    if (count == 0U) return cnor_parser_fail(self, cnor_error_empty_block);
    if (!cnor_parser_advance(self)) return lh_null;
    node->data.fn.params = fields.head;
    node->data.fn.param_count = (cnor_argc_t) count;
    return node;
}
