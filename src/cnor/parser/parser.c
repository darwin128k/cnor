#include <cnor/parser/internal.h>
#include <lh/null.h>
#include <lh/util/return.h>

void
cnor_parser_init(cnor_parser_t *self, lh_str_view_t src, cnor_arena_t *arena)
{
    const cnor_token_t eof = cnor_token_eof_initializer();

    lh_return_ifn(self);
    cnor_lexer_init(&self->lexer, src);
    self->token = eof;
    self->arena = arena;
    self->src = src;
    cnor_diag_init(&self->diag);
}

const cnor_diag_t *
cnor_parser_get_diag(const cnor_parser_t *self)
{
    lh_return_ifn(self, lh_null);
    return &self->diag;
}

/* Declarations are only allowed at the top level. */
static cnor_node_t *
cnor_parser_top_level(cnor_parser_t *self)
{
    if (cnor_parser_at_keyword(self, cnor_keyword_fn)
        || cnor_parser_at_keyword(self, cnor_keyword_native))
    {
        return cnor_parser_fn(self);
    }
    return cnor_parser_statement(self);
}

cnor_node_t *
cnor_parser_parse(cnor_parser_t *self)
{
    cnor_node_list_t program = {lh_null, lh_null};

    lh_return_ifn(self, lh_null);
    lh_return_ifn(self->arena, lh_null);
    cnor_parser_advance(self);
    while (cnor_diag_is_ok(&self->diag) && !cnor_parser_at(self, cnor_token_eof))
    {
        cnor_node_t *node = cnor_parser_top_level(self);

        if (node == lh_null)
        {
            cnor_parser_fail(self, cnor_error_unexpected_token);
            return lh_null;
        }
        cnor_node_list_append(&program, node);
    }
    if (!cnor_diag_is_ok(&self->diag))
    {
        return lh_null;
    }
    if (program.head == lh_null)
    {
        return cnor_parser_fail(self, cnor_error_empty_block);
    }
    return program.head;
}
