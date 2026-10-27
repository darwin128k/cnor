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

/* -- dictionaries ----------------------------------------------------------- */

/*
 * `dict(n)` is a declaration, like an array literal: it names the room the
 * dictionary has, and the room is a whole number the compiler knows, since
 * the dictionary takes its slots.
 */

cnor_dict_form_t
cnor_compiler_dict_form(const cnor_node_t *init, lh_u32_t *room)
{
    const lh_str_view_t dict = lh_str_view_lit("dict");
    const cnor_node_t *arg;

    if (init == lh_null || init->kind != cnor_node_call
        || !lh_str_view_equals(&init->name, &dict, lh_bool_false))
    {
        return cnor_dict_not_a_dict;
    }
    if (init->data.call.argc != 1U)
    {
        return cnor_dict_malformed;
    }
    arg = init->data.call.args;
    if (arg->kind != cnor_node_int)
    {
        return cnor_dict_malformed;
    }
    *room = arg->data.int_.value;
    return cnor_dict_declared;
}

/* Whether the script names a function or native of its own `dict`: such a
 * name wins, as it does for `len`. */
lh_bool_t
cnor_compiler_names_its_own_dict(cnor_compiler_t *self)
{
    const lh_str_view_t dict = lh_str_view_lit("dict");
    const cnor_binding_t binding = cnor_scope_resolve(self, dict);

    return (binding.kind == cnor_binding_function || binding.kind == cnor_binding_native
            || binding.kind == cnor_binding_ambiguous)
               ? lh_bool_true
               : lh_bool_false;
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

/* A top-level dictionary: its pairs live in the globals, so they outlive the
 * frame of the top-level code that makes them. */
static lh_bool_t
cnor_compile_let_global_dict(cnor_compiler_t *self, const cnor_node_t *node, lh_u32_t room)
{
    const cnor_binding_t binding = cnor_scope_resolve(self, node->name);
    const lh_u32_t slots = room * 2U + 3U;

    cnor_scope_global_at(&self->scope, binding.index)->type = cnor_static_dict;
    if (binding.index + slots > self->globals_ready)
    {
        self->globals_ready = binding.index + slots;
    }
    return cnor_emit_insn(self, cnor_op_dict_new_global, binding.index, room);
}

/* `let mut name = dict(n)`: the name takes a slot for the dictionary, two
 * for its header and two per key and value. */
static lh_bool_t
cnor_compile_let_dict(cnor_compiler_t *self, const cnor_node_t *node, lh_u32_t room)
{
    cnor_slot_t slot;

    if (!cnor_scope_add_dict_local(self, node, node->name, node->data.let.is_mutable,
                                   (lh_u16_t) room, &slot))
    {
        return lh_bool_false;
    }
    /* `dict_new` writes the room and the dictionary's value. */
    return cnor_emit_insn(self, cnor_op_dict_new, slot, room);
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
    /* A dictionary initializer: `dict(n)` names the room the dictionary has.
     * A name of the script's own wins, as it does for `len`. */
    {
        lh_u32_t room = 0U;
        const cnor_dict_form_t form = cnor_compiler_dict_form(node->data.let.init, &room);

        if (form != cnor_dict_not_a_dict && !cnor_compiler_names_its_own_dict(self))
        {
            if (form == cnor_dict_malformed || room > CNOR_DICT_ROOM_MAX)
            {
                return cnor_compile_fail(self, node, cnor_error_dict_room);
            }
            if (annotation != cnor_type_none
                && !cnor_type_require(self, node, cnor_type_of_annotation(annotation),
                                      cnor_static_dict))
            {
                return lh_bool_false;
            }
            if (cnor_compile_at_top_level(self))
            {
                return cnor_compile_let_global_dict(self, node, room);
            }
            return cnor_compile_let_dict(self, node, room);
        }
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

/* `settings["key"] = value`: a pair of the dictionary in @p binding. */
static lh_bool_t
cnor_compile_assign_dict(cnor_compiler_t *self, const cnor_node_t *node,
                          const cnor_node_t *target, const cnor_binding_t *binding)
{
    const cnor_node_t *const base = target->data.index.target;

    if (binding->kind != cnor_binding_local)
    {
        /* A global dictionary: its value goes on the stack, then the key and
         * the value. */
        if (!cnor_compile_expr(self, base)
            || !cnor_type_require(self, target, cnor_static_dict, self->type)
            || !cnor_compile_expr(self, target->data.index.index)
            || !cnor_type_require(self, target, cnor_static_str, self->type)
            || !cnor_compile_expr(self, node->data.assign.value))
        {
            return lh_bool_false;
        }
        return cnor_emit_op(self, cnor_op_dict_set);
    }
    if (!binding->is_mutable)
    {
        return cnor_compile_fail_name(self, base, cnor_error_immutable);
    }
    /* The key, then the value; `dict_set_l` takes both. */
    if (!cnor_compile_expr(self, target->data.index.index)
        || !cnor_type_require(self, target, cnor_static_str, self->type)
        || !cnor_compile_expr(self, node->data.assign.value))
    {
        return lh_bool_false;
    }
    return cnor_emit_insn(self, cnor_op_dict_set_l, binding->index, 0U);
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
    if (binding.type == cnor_static_dict)
    {
        return cnor_compile_assign_dict(self, node, target, &binding);
    }
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
    lh_u32_t i;
    cnor_slot_t slot = 0U;
    lh_bool_t has_finally = lh_bool_false;

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
    if (self->type == cnor_static_dict)
    {
        /* The pairs of a dictionary live in a frame or in the globals: a
         * returned dictionary would point at a frame that is gone. */
        return cnor_compile_fail(self, node, cnor_error_dict_returned);
    }
    /* The innermost `try` with a finally block: the value waits in its slot
     * while the finally blocks run. */
    for (i = self->try_depth; i > 0U; --i)
    {
        if (self->tries[i - 1U].finally_label != lh_null)
        {
            slot = self->tries[i - 1U].value_slot;
            has_finally = lh_bool_true;
            break;
        }
    }
    if (!has_finally)
    {
        return cnor_emit_op(self, cnor_op_ret);
    }
    /* The value waits in a slot: the finally blocks run with the stack as it
     * was, and the machine returns the value when they are done. */
    if (!cnor_emit_insn(self, cnor_op_store_local, slot, 0U)
        || !cnor_compile_exit_tries(self, 0U, (lh_u8_t) CNOR_EXIT_RETURN, lh_null, slot))
    {
        return lh_bool_false;
    }
    return lh_bool_true;
}
