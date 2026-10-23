#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- helpers --------------------------------------------------------------- */

/* Whether statements are being compiled directly at the top level of the file. */
static lh_bool_t
cnor_compile_at_top_level(const cnor_compiler_t *self)
{
    return (self->function == 0U && self->block_depth == 0U) ? lh_bool_true : lh_bool_false;
}

static lh_bool_t
cnor_compile_value_or_none(cnor_compiler_t *self, const cnor_node_t *value)
{
    return value != lh_null ? cnor_compile_expr(self, value) : cnor_emit_op(self, cnor_op_push_none);
}

lh_bool_t
cnor_compile_store(cnor_compiler_t *self, const cnor_binding_t *binding)
{
    const cnor_op_t op =
        binding->kind == cnor_binding_local ? cnor_op_store_local : cnor_op_store_global;

    return cnor_emit_insn(self, op, binding->index, 0U);
}

/* -- let ------------------------------------------------------------------- */

lh_bool_t
cnor_compile_let(cnor_compiler_t *self, const cnor_node_t *node)
{
    cnor_binding_t binding;

    /* The initializer is compiled before the name exists, so `let x = x + 1`
     * reads the outer `x`. */
    if (!cnor_compile_value_or_none(self, node->data.let.init))
    {
        return lh_bool_false;
    }
    if (cnor_compile_at_top_level(self))
    {
        /* Declared by the declaration pass, so every function sees it. */
        binding = cnor_scope_resolve(self, node->name);
    }
    else
    {
        cnor_slot_t slot;

        if (!cnor_scope_add_local(self, node, node->name, node->data.let.is_mutable, &slot))
        {
            return lh_bool_false;
        }
        binding.kind = cnor_binding_local;
        binding.index = slot;
    }
    return cnor_compile_store(self, &binding);
}

/* -- assignment ------------------------------------------------------------ */

lh_bool_t
cnor_compile_assign(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *target = node->data.assign.target;
    cnor_binding_t binding;

    if (target->kind != cnor_node_ident)
    {
        return cnor_compile_fail(self, target, cnor_error_not_assignable);
    }
    binding = cnor_scope_resolve(self, target->name);
    if (binding.kind == cnor_binding_none)
    {
        return cnor_compile_fail_name(self, target, cnor_error_unknown_name);
    }
    if (binding.kind != cnor_binding_local && binding.kind != cnor_binding_global)
    {
        return cnor_compile_fail_name(self, target, cnor_error_not_assignable);
    }
    if (!binding.is_mutable)
    {
        return cnor_compile_fail_name(self, target, cnor_error_immutable);
    }
    return cnor_compile_expr(self, node->data.assign.value) && cnor_compile_store(self, &binding);
}

/* -- expression statements and return -------------------------------------- */

lh_bool_t
cnor_compile_expr_stmt(cnor_compiler_t *self, const cnor_node_t *node)
{
    if (!cnor_compile_expr(self, node->data.expr_stmt.expr))
    {
        return lh_bool_false;
    }
    if (cnor_compile_at_top_level(self))
    {
        /* The last top-level expression is the program's result. */
        return cnor_emit_insn(self, cnor_op_store_local, self->result_slot, 0U);
    }
    return cnor_emit_op(self, cnor_op_pop);
}

lh_bool_t
cnor_compile_return(cnor_compiler_t *self, const cnor_node_t *node)
{
    return cnor_compile_value_or_none(self, node->data.return_.value)
           && cnor_emit_op(self, cnor_op_ret);
}
