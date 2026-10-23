#include <cnor/parser/internal.h>
#include <lh/null.h>

cnor_node_t *
cnor_parser_fail(cnor_parser_t *self, cnor_error_t code)
{
    cnor_diag_set(&self->diag, code, self->token.line, self->token.start);
    return lh_null;
}

lh_bool_t
cnor_parser_advance(cnor_parser_t *self)
{
    if (!cnor_diag_is_ok(&self->diag))
    {
        return lh_bool_false;
    }
    if (!cnor_lexer_next(&self->lexer, &self->token))
    {
        self->diag = *cnor_lexer_get_diag(&self->lexer);
        self->token.kind = cnor_token_eof;
        return lh_bool_false;
    }
    return lh_bool_true;
}

lh_bool_t
cnor_parser_at(const cnor_parser_t *self, cnor_token_kind_t kind)
{
    return self->token.kind == kind ? lh_bool_true : lh_bool_false;
}

lh_bool_t
cnor_parser_at_punct(const cnor_parser_t *self, cnor_punct_t punct)
{
    return (self->token.kind == cnor_token_punct && self->token.punct == punct) ? lh_bool_true
                                                                               : lh_bool_false;
}

lh_bool_t
cnor_parser_at_keyword(const cnor_parser_t *self, cnor_keyword_t keyword)
{
    return (self->token.kind == cnor_token_keyword && self->token.keyword == keyword)
               ? lh_bool_true
               : lh_bool_false;
}

lh_bool_t
cnor_parser_accept_punct(cnor_parser_t *self, cnor_punct_t punct)
{
    if (!cnor_parser_at_punct(self, punct))
    {
        return lh_bool_false;
    }
    return cnor_parser_advance(self);
}

lh_bool_t
cnor_parser_expect_punct(cnor_parser_t *self, cnor_punct_t punct)
{
    if (cnor_parser_accept_punct(self, punct))
    {
        return lh_bool_true;
    }
    cnor_parser_fail(self, cnor_error_unexpected_token);
    return lh_bool_false;
}

lh_bool_t
cnor_parser_expect(cnor_parser_t *self, cnor_token_kind_t kind, cnor_error_t code)
{
    if (!cnor_parser_at(self, kind))
    {
        cnor_parser_fail(self, code);
        return lh_bool_false;
    }
    return cnor_parser_advance(self);
}

lh_str_view_t
cnor_parser_text(const cnor_parser_t *self)
{
    return cnor_token_get_text(&self->token, self->src);
}

cnor_node_t *
cnor_parser_node(cnor_parser_t *self, cnor_node_kind_t kind)
{
    cnor_node_t *node = (cnor_node_t *) cnor_arena_alloc(self->arena, sizeof(cnor_node_t));

    if (node == lh_null)
    {
        return cnor_parser_fail(self, cnor_error_no_memory);
    }
    node->kind = kind;
    node->line = self->token.line;
    node->name = lh_str_view_empty();
    return node;
}

lh_bool_t
cnor_parser_take_name(cnor_parser_t *self, cnor_node_t *node)
{
    if (!cnor_arena_copy_text(self->arena, cnor_parser_text(self), &node->name))
    {
        cnor_parser_fail(self, cnor_error_no_memory);
        return lh_bool_false;
    }
    return lh_bool_true;
}

void
cnor_node_list_append(cnor_node_list_t *list, cnor_node_t *node)
{
    if (list->tail == lh_null)
    {
        list->head = node;
    }
    else
    {
        list->tail->next = node;
    }
    list->tail = node;
}
