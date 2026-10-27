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
        return cnor_compile_storable(self, value);
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

/* Fixed arrays use one header slot and one slot per element, globally or locally. */
static lh_bool_t
cnor_compile_let_array(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t type)
{
    const cnor_node_t *array = node->data.let.init;
    const cnor_node_t *value = array->data.array.value;
    const lh_u32_t count = array->data.array.count;
    const lh_bool_t global = cnor_compile_at_top_level(self);
    cnor_slot_t slot;
    lh_u32_t index;
    if (count >= CNOR_SLOT_MAX)
        return cnor_compile_fail(self, node, global ? cnor_error_too_many_globals : cnor_error_too_many_locals);
    /* Initializers see the outer binding, and run from left to right. */
    for (; value != lh_null; value = array->data.array.is_list ? value->next : lh_null)
    {
        if (!cnor_compile_storable(self, value)
            || !cnor_type_require(self, value, cnor_type_element(type), self->type)) return lh_bool_false;
    }
    if (global)
    {
        const cnor_binding_t binding = cnor_scope_resolve(self, node->name);
        slot = binding.index;
        cnor_scope_global_at(&self->scope, slot)->type = type;
        if (slot + count + 1U > self->globals_ready) self->globals_ready = slot + count + 1U;
    }
    else if (!cnor_scope_add_local(self, node, node->name, node->data.let.is_mutable,
                                  type, (lh_u16_t) count, &slot)) return lh_bool_false;
    if (array->data.array.is_list && !cnor_emit_op(self, cnor_op_push_none)) return lh_bool_false;
    if (!cnor_emit_insn(self, global ? cnor_op_array_new_global : cnor_op_array_new, slot, count))
        return lh_bool_false;
    if (array->data.array.is_list)
        for (index = count; index > 0U; --index)
            if (!cnor_emit_insn(self, global ? cnor_op_store_global : cnor_op_store_local,
                               slot + index, 0U)) return lh_bool_false;
    return lh_bool_true;
}

/* A top-level dictionary: its pairs live in the globals, so they outlive the
 * frame of the top-level code that makes them. */
static lh_bool_t
cnor_compile_let_global_dict(cnor_compiler_t *self, const cnor_node_t *node, lh_u32_t room,
                              cnor_static_type_t type)
{
    const cnor_binding_t binding = cnor_scope_resolve(self, node->name);
    const lh_u32_t slots = room * 2U + 3U;

    cnor_scope_global_at(&self->scope, binding.index)->type = type;
    if (binding.index + slots > self->globals_ready)
    {
        self->globals_ready = binding.index + slots;
    }
    return cnor_emit_insn(self, cnor_op_dict_new_global, binding.index, room);
}

/* `let mut name = dict(n)`: the name takes a slot for the dictionary, two
 * for its header and two per key and value. */
static lh_bool_t
cnor_compile_let_dict(cnor_compiler_t *self, const cnor_node_t *node, lh_u32_t room,
                       cnor_static_type_t type)
{
    cnor_slot_t slot;

    if (!cnor_scope_add_dict_local(self, node, node->name, node->data.let.is_mutable,
                                   (lh_u16_t) room, &slot))
    {
        return lh_bool_false;
    }
    ((struct cnor_local_decl *) lh_array_get_ptr(&self->scope.locals,
        lh_array_get_size(&self->scope.locals) - 1U))->type = type;
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

    if (node->data.let.init != lh_null && node->data.let.init->kind == cnor_node_call)
    {
        const struct cnor_named_type *named = cnor_named_find(self, node->data.let.init->name);
        if (named != lh_null && named->node->kind == cnor_node_struct)
            return cnor_compile_struct_let(self, node, named);
    }
    /* An array initializer: the name takes the slots of the elements. */
    if (node->data.let.init != lh_null && node->data.let.init->kind == cnor_node_array)
    {
        type = annotation == cnor_type_none ? cnor_static_array : cnor_type_of_node(self, node, annotation);
        if (!cnor_type_require(self, node, cnor_static_array, cnor_type_base(type))) return lh_bool_false;
        return cnor_compile_let_array(self, node, type);
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
            type = annotation == cnor_type_none ? cnor_static_dict : cnor_type_of_node(self, node, annotation);
            if (!cnor_type_require(self, node, cnor_static_dict, cnor_type_base(type))) return lh_bool_false;
            if (cnor_compile_at_top_level(self))
            {
                return cnor_compile_let_global_dict(self, node, room, type);
            }
            return cnor_compile_let_dict(self, node, room, type);
        }
    }
    /* The initializer is compiled before the name exists, so `let x = x + 1`
     * reads the outer `x` (and fails at the top level, where the outer `x`
     * would be this global before its `let`). */
    if (!cnor_compile_value_or_none(self, node->data.let.init)
        || (annotation != cnor_type_none
            && !cnor_type_require(self, node, cnor_type_of_node(self, node, annotation), self->type)))
    {
        return lh_bool_false;
    }
    type = annotation == cnor_type_none ? cnor_type_of_variable(annotation, self->type)
                                      : cnor_type_of_node(self, node, annotation);
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

static lh_bool_t
cnor_compile_collection_value(cnor_compiler_t *self, const cnor_node_t *value,
                               cnor_static_type_t collection)
{
    return cnor_compile_storable(self, value)
        && cnor_type_require(self, value, cnor_type_element(collection), self->type);
}

static lh_bool_t
cnor_compile_collection_op(cnor_compiler_t *self, const cnor_node_t *node,
                            cnor_static_type_t collection)
{
    static const cnor_binary_op_t binary_ops[cnor_assign_count] = {
        cnor_binary_add, cnor_binary_add, cnor_binary_sub, cnor_binary_mul, cnor_binary_div,
        cnor_binary_mod, cnor_binary_band, cnor_binary_bor, cnor_binary_bxor
    };
    static const cnor_op_t ops[cnor_assign_count] = {
        cnor_op_add, cnor_op_add, cnor_op_sub, cnor_op_mul, cnor_op_div,
        cnor_op_mod, cnor_op_band, cnor_op_bor, cnor_op_bxor
    };
    cnor_node_t binary = {0};
    cnor_static_type_t result;
    binary.line = node->line;
    binary.data.binary.op = binary_ops[node->data.assign.op];
    if (!cnor_compile_storable(self, node->data.assign.value)
        || !cnor_type_binary(self, &binary, cnor_type_element(collection), self->type, &result)
        || !cnor_type_require(self, node, cnor_type_element(collection), result)) return lh_bool_false;
    self->type = result;
    return cnor_emit_op(self, ops[node->data.assign.op]);
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
            || !cnor_type_require(self, target, binding->type, self->type)
            || !cnor_compile_expr(self, target->data.index.index)
            || !cnor_type_require(self, target, cnor_static_str, self->type)
            || !cnor_compile_collection_value(self, node->data.assign.value, binding->type))
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
        || !cnor_compile_collection_value(self, node->data.assign.value, binding->type))
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
    if (cnor_type_base(binding.type) == cnor_static_dict)
    {
        return cnor_compile_assign_dict(self, node, target, &binding);
    }
    if (binding.kind != cnor_binding_local || cnor_type_base(binding.type) != cnor_static_array)
    {
        if (cnor_type_base(binding.type) != cnor_static_array)
        {
            return cnor_compile_fail(self, target, cnor_error_not_assignable);
        }
        /* A global array: its value goes on the stack, then the index and the
         * value. */
        if (!cnor_compile_expr(self, base)
            || !cnor_type_require(self, target, binding.type, self->type)
            || !cnor_compile_expr(self, target->data.index.index)
            || !cnor_type_require(self, target, cnor_static_i32, self->type)
            || !cnor_compile_collection_value(self, node->data.assign.value, binding.type))
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
        || !cnor_compile_collection_value(self, node->data.assign.value, binding.type))
    {
        return lh_bool_false;
    }
    return cnor_emit_insn(self, cnor_op_array_set_l, binding.index, 0U);
}

/* -- assignment ------------------------------------------------------------ */

/* The opcode each compound assignment uses: what is done with the value on
 * the right before it is stored back. */
static const cnor_op_t m_assign_ops[cnor_assign_count] = {
    [cnor_assign_set] = cnor_op_add, /* Not used: `=` stores the value as it is. */
    [cnor_assign_add] = cnor_op_add,
    [cnor_assign_sub] = cnor_op_sub,
    [cnor_assign_mul] = cnor_op_mul,
    [cnor_assign_div] = cnor_op_div,
    [cnor_assign_mod] = cnor_op_mod,
    [cnor_assign_band] = cnor_op_band,
    [cnor_assign_bor] = cnor_op_bor,
    [cnor_assign_bxor] = cnor_op_bxor,
};

/*
 * `x += value`: the value of `x`, the operation, and the value stored back.
 * The value of `x` is read once, and the operation is the one the sign names.
 */
static lh_bool_t
cnor_compile_assign_name_op(cnor_compiler_t *self, const cnor_node_t *node,
                             const cnor_binding_t *binding)
{
    static const cnor_binary_op_t binary_ops[cnor_assign_count] = {
        cnor_binary_add, cnor_binary_add, cnor_binary_sub, cnor_binary_mul, cnor_binary_div,
        cnor_binary_mod, cnor_binary_band, cnor_binary_bor, cnor_binary_bxor
    };
    cnor_node_t binary = {0};
    cnor_static_type_t result;
    binary.line = node->line;
    binary.data.binary.op = binary_ops[node->data.assign.op];
    if (!cnor_compile_expr(self, node->data.assign.target)
        || !cnor_compile_storable(self, node->data.assign.value)
        || !cnor_type_binary(self, &binary, binding->type, self->type, &result)
        || !cnor_emit_op(self, m_assign_ops[node->data.assign.op]))
    {
        return lh_bool_false;
    }
    /* The result is what is stored, so it must have the type of the target. */
    self->type = result;
    return cnor_type_require(self, node, binding->type, result)
           && cnor_compile_store(self, binding);
}

/*
 * `a[i] += value`: the index, the value that is there, the operation, and the
 * value stored back. The index of a local array or dictionary is computed once;
 * a global one is computed twice, as the machine has no way to keep it.
 */
static lh_bool_t
cnor_compile_assign_index_op(cnor_compiler_t *self, const cnor_node_t *node,
                              const cnor_node_t *target)
{
    const cnor_node_t *const base = target->data.index.target;
    cnor_binding_t binding;

    if (base->kind != cnor_node_ident)
    {
        return cnor_compile_fail(self, target, cnor_error_not_assignable);
    }
    binding = cnor_scope_resolve(self, base->name);
    if (cnor_type_base(binding.type) != cnor_static_array && cnor_type_base(binding.type) != cnor_static_dict)
    {
        return cnor_compile_fail(self, target, cnor_error_not_assignable);
    }
    if (binding.kind == cnor_binding_local)
    {
        if (!binding.is_mutable)
        {
            return cnor_compile_fail_name(self, base, cnor_error_immutable);
        }
        /* The index, then the index again: the machine reads the element and
         * keeps the index for the write. */
        if (!cnor_compile_expr(self, target->data.index.index)
            || !cnor_type_require(self, target,
                                  cnor_type_base(binding.type) == cnor_static_dict ? cnor_static_str
                                                                    : cnor_static_i32,
                                  self->type)
            || !cnor_emit_op(self, cnor_op_dup))
        {
            return lh_bool_false;
        }
        if (cnor_type_base(binding.type) == cnor_static_dict)
        {
            if (!cnor_emit_insn(self, cnor_op_dict_get_l, binding.index, 0U)
                || !cnor_compile_collection_op(self, node, binding.type))
            {
                return lh_bool_false;
            }
            return cnor_emit_insn(self, cnor_op_dict_set_l, binding.index, 0U);
        }
        if (!cnor_emit_insn(self, cnor_op_array_get_l, binding.index, 0U)
            || !cnor_compile_collection_op(self, node, binding.type))
        {
            return lh_bool_false;
        }
        return cnor_emit_insn(self, cnor_op_array_set_l, binding.index, 0U);
    }
    /* A global array or dictionary: its value goes on the stack, and the index
     * is computed once for the read and once for the write. The machine wants
     * the array or the dictionary and the index under the value, and the stack
     * has no room to move it, so the value waits in a slot. */
    {
        const cnor_static_type_t index_type =
            cnor_type_base(binding.type) == cnor_static_dict ? cnor_static_str : cnor_static_i32;
        cnor_slot_t slot;

        if (!cnor_scope_add_local(self, node, lh_str_view_empty(), lh_bool_false,
                                  cnor_static_any, 0U, &slot))
        {
            return lh_bool_false;
        }
        /* The value that is there. */
        if (!cnor_compile_expr(self, base)
            || !cnor_type_require(self, target, binding.type, self->type)
            || !cnor_compile_expr(self, target->data.index.index)
            || !cnor_type_require(self, target, index_type, self->type))
        {
            return lh_bool_false;
        }
        if (cnor_type_base(binding.type) == cnor_static_dict)
        {
            if (!cnor_emit_op(self, cnor_op_dict_get))
            {
                return lh_bool_false;
            }
        }
        else if (!cnor_emit_op(self, cnor_op_array_get))
        {
            return lh_bool_false;
        }
        /* The value on the right, and the operation: the result waits in the
         * slot. */
        if (!cnor_compile_collection_op(self, node, binding.type)
            || !cnor_emit_insn(self, cnor_op_store_local, slot, 0U))
        {
            return lh_bool_false;
        }
        /* The array or dictionary, the index, and the result. */
        if (!cnor_compile_expr(self, base)
            || !cnor_type_require(self, target, binding.type, self->type)
            || !cnor_compile_expr(self, target->data.index.index)
            || !cnor_type_require(self, target, index_type, self->type)
            || !cnor_emit_insn(self, cnor_op_load_local, slot, 0U))
        {
            return lh_bool_false;
        }
        if (cnor_type_base(binding.type) == cnor_static_dict)
        {
            return cnor_emit_op(self, cnor_op_dict_set);
        }
        return cnor_emit_op(self, cnor_op_array_set);
    }
}

lh_bool_t
cnor_compile_assign(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *target = node->data.assign.target;
    cnor_binding_t binding;

    cnor_node_t rewritten_target;
    cnor_node_t rewritten_base;
    cnor_node_t rewritten_assignment;
    lh_bool_t qualified = cnor_namespace_node(self, target, &rewritten_target);
    if (!qualified && (target->kind == cnor_node_index || target->kind == cnor_node_member)
        && cnor_namespace_node(self, target->data.index.target, &rewritten_base))
    {
        rewritten_target = *target;
        rewritten_target.data.index.target = &rewritten_base;
        qualified = lh_bool_true;
    }
    if (qualified)
    {
        rewritten_assignment = *node;
        rewritten_assignment.data.assign.target = &rewritten_target;
        return cnor_compile_assign(self, &rewritten_assignment);
    }

    if (target->kind == cnor_node_index)
    {
        if (node->data.assign.op != cnor_assign_set)
        {
            return cnor_compile_assign_index_op(self, node, target);
        }
        return cnor_compile_assign_index(self, node, target);
    }
    if (target->kind == cnor_node_member) return cnor_compile_member_assign(self, node);
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
    if (cnor_named_is_struct(self, binding.type))
        return cnor_compile_fail(self, node, cnor_error_struct_lifetime);
    if (node->data.assign.op != cnor_assign_set)
    {
        return cnor_compile_assign_name_op(self, node, &binding);
    }
    return cnor_compile_storable(self, node->data.assign.value)
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
    if (cnor_named_is_struct(self, self->type))
        return cnor_compile_fail(self, node, cnor_error_struct_lifetime);
    if (cnor_type_base(self->type) == cnor_static_array)
    {
        /* The elements of an array live in a frame or in the globals: a
         * returned array would point at a frame that is gone. */
        return cnor_compile_fail(self, node, cnor_error_array_returned);
    }
    if (cnor_type_base(self->type) == cnor_static_dict)
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
