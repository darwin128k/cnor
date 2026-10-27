#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- the rule table -------------------------------------------------------- */

/* Declarations were handled by the declaration pass, imports when the files
 * were loaded; as statements they emit nothing. */
static lh_bool_t
cnor_compile_nothing(cnor_compiler_t *self, const cnor_node_t *node)
{
    (void) self;
    (void) node;
    return lh_bool_true;
}

/**
 * How every node kind compiles. A construct is added by giving its kind an
 * entry here and its handler a module of its own; nothing else dispatches on
 * node kinds.
 */
static const cnor_compile_rule_t m_rules[cnor_node_kind_count] = {
    [cnor_node_fn] = {lh_null, cnor_compile_nothing},
    [cnor_node_native_fn] = {lh_null, cnor_compile_nothing},
    [cnor_node_import] = {lh_null, cnor_compile_nothing},
    [cnor_node_param] = {lh_null, lh_null},
    [cnor_node_let] = {lh_null, cnor_compile_let},
    [cnor_node_assign] = {lh_null, cnor_compile_assign},
    [cnor_node_expr_stmt] = {lh_null, cnor_compile_expr_stmt},
    [cnor_node_if] = {lh_null, cnor_compile_if},
    [cnor_node_while] = {lh_null, cnor_compile_while},
    [cnor_node_for] = {lh_null, cnor_compile_for},
    [cnor_node_match] = {lh_null, cnor_compile_match},
    [cnor_node_match_arm] = {lh_null, lh_null},
    [cnor_node_try] = {lh_null, cnor_compile_try},
    [cnor_node_raise] = {lh_null, cnor_compile_raise},
    [cnor_node_return] = {lh_null, cnor_compile_return},
    [cnor_node_break] = {lh_null, cnor_compile_break},
    [cnor_node_continue] = {lh_null, cnor_compile_continue},
    [cnor_node_ident] = {cnor_compile_ident, lh_null},
    [cnor_node_int] = {cnor_compile_literal, lh_null},
    [cnor_node_real] = {cnor_compile_literal, lh_null},
    [cnor_node_string] = {cnor_compile_literal, lh_null},
    [cnor_node_true] = {cnor_compile_literal, lh_null},
    [cnor_node_false] = {cnor_compile_literal, lh_null},
    [cnor_node_none] = {cnor_compile_literal, lh_null},
    [cnor_node_call] = {cnor_compile_call, lh_null},
    [cnor_node_binary] = {cnor_compile_binary, lh_null},
    [cnor_node_unary] = {cnor_compile_unary, lh_null},
    [cnor_node_index] = {cnor_compile_index, lh_null},
    [cnor_node_array] = {cnor_compile_array, lh_null},
};

const cnor_compile_rule_t *
cnor_compile_rule_of(cnor_node_kind_t kind)
{
    if ((lh_uint_t) kind >= (lh_uint_t) cnor_node_kind_count)
    {
        return lh_null;
    }
    return &m_rules[kind];
}

/* -- dispatch -------------------------------------------------------------- */

lh_bool_t
cnor_compile_expr(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_compile_rule_t *rule = cnor_compile_rule_of(node->kind);

    if (rule == lh_null || rule->expr == lh_null)
    {
        return cnor_compile_fail(self, node, cnor_error_unknown_construct);
    }
    self->emit.line = node->line;
    self->type = cnor_static_any;
    return rule->expr(self, node);
}

lh_bool_t
cnor_compile_stmt(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_compile_rule_t *rule = cnor_compile_rule_of(node->kind);

    if (rule == lh_null || rule->stmt == lh_null)
    {
        return cnor_compile_fail(self, node, cnor_error_unknown_construct);
    }
    self->emit.line = node->line;
    return rule->stmt(self, node);
}

lh_bool_t
cnor_compile_block(cnor_compiler_t *self, const cnor_node_t *body)
{
    const cnor_scope_mark_t mark = cnor_scope_enter_block(&self->scope);
    lh_bool_t ok = lh_bool_true;

    self->block_depth++;
    for (; ok && body != lh_null; body = body->next)
    {
        ok = cnor_compile_stmt(self, body);
    }
    self->block_depth--;
    cnor_scope_leave_block(&self->scope, mark);
    return ok;
}
