#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- helpers --------------------------------------------------------------- */

/* Whether statements are being compiled directly at the top level of a file. */
static lh_bool_t
cnor_compile_at_top_level(const cnor_compiler_t *self)
{
    return (self->top_code && self->block_depth == 0U) ? lh_bool_true : lh_bool_false;
}

/* @p value, or `none` without one; its type in `self->type` either way. */
static lh_bool_t
cnor_compile_value_or_none(cnor_compiler_t *self, const cnor_node_t *value)
{
    if (value != lh_null)
    {
        return cnor_compile_expr(self, value);
    }
    self->type = cnor_static_none;
    return cnor_emit_op(self, cnor_op_push_none);
}

lh_bool_t
cnor_compile_store(cnor_compiler_t *self, const cnor_binding_t *binding)
{
    const cnor_op_t op =
        binding->kind == cnor_binding_local ? cnor_op_store_local : cnor_op_store_global;

    return cnor_emit_insn(self, op, binding->index, 0U);
}

/* -- let ------------------------------------------------------------------- */

/* A top-level `let`: the global was declared by the declaration pass, so
 * every function sees it; from here on it is initialized. */
static lh_bool_t
cnor_compile_let_global(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t type)
{
    const cnor_binding_t binding = cnor_scope_resolve(self, node->name);

    cnor_scope_global_at(&self->scope, binding.index)->type = type;
    if (binding.index + 1U > self->globals_ready)
    {
        self->globals_ready = binding.index + 1U;
    }
    return cnor_compile_store(self, &binding);
}

/* A top-level array: its elements live in the globals, so they outlive the
 * frame of the top-level code that makes them. */
static lh_bool_t
cnor_compile_let_global_array(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *const array = node->data.let.init;
    const lh_u32_t count = array->data.array.count;
    const cnor_binding_t binding = cnor_scope_resolve(self, node->name);

    cnor_scope_global_at(&self->scope, binding.index)->type = cnor_static_array;
    if (binding.index + count + 1U > self->globals_ready)
    {
        self->globals_ready = binding.index + count + 1U;
    }
    /* The value every element holds. */
    if (!cnor_compile_expr(self, array->data.array.value))
    {
        return lh_bool_false;
    }
    return cnor_emit_insn(self, cnor_op_array_new_global, binding.index, count);
}

/* `let mut name = [value; count]`: the name takes a slot for the array and a
 * slot per element, and every element holds the value. */
static lh_bool_t
cnor_compile_let_array(cnor_compiler_t *self, const cnor_node_t *node, const cnor_node_t *array)
{
    const lh_u32_t count = array->data.array.count;
    cnor_slot_t slot;

    if (cnor_compile_at_top_level(self))
    {
        /* A global array would outlive the frame its elements live in. */
        return cnor_compile_fail(self, node, cnor_error_not_a_declaration);
    }
    if (!cnor_scope_add_local(self, node, node->name, node->data.let.is_mutable,
                              cnor_static_array, (lh_u16_t) count, &slot))
    {
        return lh_bool_false;
    }
    /* The value every element holds. */
    if (!cnor_compile_expr(self, array->data.array.value))
    {
        return lh_bool_false;
    }
    /* `array_new` fills the elements and writes the array's value. */
    return cnor_emit_insn(self, cnor_op_array_new, slot, count);
}

lh_bool_t
cnor_compile_let(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_type_t annotation = node->data.let.type;
    cnor_static_type_t type;
    cnor_binding_t binding;
    cnor_slot_t slot;

    /* An array initializer: the name takes the slots of the elements. */
    if (node->data.let.init != lh_null && node->data.let.init->kind == cnor_node_array)
    {
        if (annotation != cnor_type_none
            && !cnor_type_require(self, node, cnor_type_of_annotation(annotation),
                                  cnor_static_array))
        {
            return lh_bool_false;
        }
        if (cnor_compile_at_top_level(self))
        {
            return cnor_compile_let_global_array(self, node);
        }
        return cnor_compile_let_array(self, node, node->data.let.init);
    }
    /* The initializer is compiled before the name exists, so `let x = x + 1`
     * reads the outer `x` (and fails at the top level, where the outer `x`
     * would be this global before its `let`). */
    if (!cnor_compile_value_or_none(self, node->data.let.init)
        || (annotation != cnor_type_none
            && !cnor_type_require(self, node, cnor_type_of_annotation(annotation), self->type)))
    {
        return lh_bool_false;
    }
    type = cnor_type_of_variable(annotation, self->type);
    if (cnor_compile_at_top_level(self))
    {
        return cnor_compile_let_global(self, node, type);
    }
    if (!cnor_scope_add_local(self, node, node->name, node->data.let.is_mutable, type, 0U, &slot))
    {
        return lh_bool_false;
    }
    binding.kind = cnor_binding_local;
    binding.index = slot;
    return cnor_compile_store(self, &binding);
}

/* `scores[i] = value`: an element of the array in a local. */
static lh_bool_t
cnor_compile_assign_index(cnor_compiler_t *self, const cnor_node_t *node,
                           const cnor_node_t *target)
{
    const cnor_node_t *const base = target->data.index.target;
    cnor_binding_t binding;

    if (base->kind != cnor_node_ident)
    {
        return cnor_compile_fail(self, target, cnor_error_not_assignable);
    }
    binding = cnor_scope_resolve(self, base->name);
    if (binding.kind != cnor_binding_local || binding.type != cnor_static_array)
    {
        if (binding.type != cnor_static_array)
        {
            return cnor_compile_fail(self, target, cnor_error_not_assignable);
        }
        /* A global array: its value goes on the stack, then the index and the
         * value. */
        if (!cnor_compile_expr(self, base)
            || !cnor_type_require(self, target, cnor_static_array, self->type)
            || !cnor_compile_expr(self, target->data.index.index)
            || !cnor_type_require(self, target, cnor_static_i32, self->type)
            || !cnor_compile_expr(self, node->data.assign.value))
        {
            return lh_bool_false;
        }
        return cnor_emit_op(self, cnor_op_array_set);
    }
    if (!binding.is_mutable)
    {
        return cnor_compile_fail_name(self, base, cnor_error_immutable);
    }
    /* The index, then the value; `array_set_l` takes both. */
    if (!cnor_compile_expr(self, target->data.index.index)
        || !cnor_type_require(self, target, cnor_static_i32, self->type)
        || !cnor_compile_expr(self, node->data.assign.value))
    {
        return lh_bool_false;
    }
    return cnor_emit_insn(self, cnor_op_array_set_l, binding.index, 0U);
}

/* -- assignment ------------------------------------------------------------ */

lh_bool_t
cnor_compile_assign(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *target = node->data.assign.target;
    cnor_binding_t binding;

    if (target->kind == cnor_node_index)
    {
        return cnor_compile_assign_index(self, node, target);
    }
    if (target->kind != cnor_node_ident)
    {
        return cnor_compile_fail(self, target, cnor_error_not_assignable);
    }
    binding = cnor_scope_resolve(self, target->name);
    if (binding.kind == cnor_binding_none || binding.kind == cnor_binding_ambiguous)
    {
        return cnor_compile_fail_binding(self, target, &binding);
    }
    if (binding.kind != cnor_binding_local && binding.kind != cnor_binding_global)
    {
        return cnor_compile_fail_name(self, target, cnor_error_not_assignable);
    }
    if (!binding.is_mutable)
    {
        return cnor_compile_fail_name(self, target, cnor_error_immutable);
    }
    if (binding.kind == cnor_binding_global && !cnor_order_use_global(self, target, binding.index))
    {
        return lh_bool_false;
    }
    return cnor_compile_expr(self, node->data.assign.value)
           && cnor_type_require(self, node, binding.type, self->type)
           && cnor_compile_store(self, &binding);
}

/* -- expression statements and return -------------------------------------- */

lh_bool_t
cnor_compile_expr_stmt(cnor_compiler_t *self, const cnor_node_t *node)
{
    if (!cnor_compile_expr(self, node->data.expr_stmt.expr))
    {
        return lh_bool_false;
    }
    if (cnor_compile_at_top_level(self) && self->function == 0U)
    {
        /* The last top-level expression of the main file is the program's result. */
        return cnor_emit_insn(self, cnor_op_store_local, self->result_slot, 0U);
    }
    return cnor_emit_op(self, cnor_op_pop);
}

/* In a function with a return type, the value must have it (`none` too). */
lh_bool_t
cnor_compile_return(cnor_compiler_t *self, const cnor_node_t *node)
{
    if (!cnor_compile_value_or_none(self, node->data.return_.value)
        || !cnor_type_require(self, node, self->returns, self->type))
    {
        return lh_bool_false;
    }
    if (self->type == cnor_static_array)
    {
        /* The elements of an array live in a frame or in the globals: a
         * returned array would point at a frame that is gone. */
        return cnor_compile_fail(self, node, cnor_error_array_returned);
    }
    return cnor_emit_op(self, cnor_op_ret);
}
