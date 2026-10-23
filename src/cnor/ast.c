#include <cnor/ast.h>
#include <lh/null.h>
#include <lh/util/return.h>

cnor_node_kind_t
cnor_node_get_kind(const cnor_node_t *self)
{
    lh_return_ifn(self, cnor_node_kind_count);
    return self->kind;
}

cnor_line_t
cnor_node_get_line(const cnor_node_t *self)
{
    lh_return_ifn(self, 0U);
    return self->line;
}

lh_str_view_t
cnor_node_get_name(const cnor_node_t *self)
{
    lh_return_ifn(self, lh_str_view_empty());
    return self->name;
}

const cnor_node_t *
cnor_node_get_next(const cnor_node_t *self)
{
    lh_return_ifn(self, lh_null);
    return self->next;
}

lh_u32_t
cnor_node_list_length(const cnor_node_t *head)
{
    lh_u32_t length = 0U;

    for (; head != lh_null; head = head->next)
    {
        length++;
    }
    return length;
}

lh_bool_t
cnor_node_kind_is_expr(cnor_node_kind_t kind)
{
    return (kind >= cnor_node_ident && kind < cnor_node_kind_count) ? lh_bool_true : lh_bool_false;
}
