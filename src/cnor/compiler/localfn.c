#include <cnor/compiler/internal.h>
#include <lh/null.h>

/*
 * A function declared inside another one.
 *
 * Its code is a part of the code of the function that declared it: it is
 * emitted after that function's `ret`, and the call jumps into it. Because it
 * shares the frame, the parameters and the variables it closes over are slots
 * of that frame, and nothing is allocated when it is called.
 *
 * The result of a call waits in a slot of its own and the address to come
 * back to waits in another one, so the stack is the same before a call and
 * after it. The code of a local function therefore runs with an empty stack,
 * whatever the caller had on it: the caller puts what it had into slots of
 * its own before the call and reads them back after it.
 */

/* -- declaring -------------------------------------------------------------- */

/*
 * The declaration of a local function: its name, the slots it needs and the
 * labels of its code. The code itself is compiled at the end of the function
 * that holds it, when every call to it is known.
 */
lh_bool_t
cnor_compile_local_fn_decl(cnor_compiler_t *self, const cnor_node_t *node)
{
    struct cnor_local_fn *fn;
    lh_u16_t index;

    if (lh_array_get_size(&self->scope.local_fns) >= CNOR_COMPILER_MAX_LOCAL_FNS)
    {
        return cnor_compile_fail(self, node, cnor_error_too_many_locals);
    }
    fn = (struct cnor_local_fn *) cnor_arena_alloc(&self->arena, sizeof(struct cnor_local_fn));
    if (fn == lh_null)
    {
        return cnor_compile_fail(self, node, cnor_error_no_memory);
    }
    fn->name = node->name;
    fn->node = node;
    cnor_label_init(&fn->body_label);
    cnor_label_init(&fn->end_label);
    fn->param_count = node->data.fn.param_count;
    fn->returns = cnor_type_of_node(self, node, node->data.fn.returns);
    /* The parameters, the address to come back to, and the result. */
    if (!cnor_scope_take_slots(&self->scope, fn->param_count, &fn->param_base)
        || !cnor_scope_take_slots(&self->scope, 1U, &fn->ret_slot)
        || !cnor_scope_take_slots(&self->scope, 1U, &fn->result_slot))
    {
        return cnor_compile_fail(self, node, cnor_error_too_many_locals);
    }
    index = (lh_u16_t) lh_array_push_back(&self->scope.local_fns, &fn);
    fn->index = index;
    if (!cnor_scope_add_local_fn(&self->scope, fn->name, index))
    {
        return lh_bool_false;
    }
    /* The variables the function closes over are the ones that were in sight
     * where it was declared: they are kept, because its code runs later and
     * reads them. */
    {
        const lh_usize_t count = lh_array_get_size(&self->scope.locals);
        const struct cnor_local_decl *const locals =
            (const struct cnor_local_decl *) lh_array_get_begin(&self->scope.locals);
        lh_usize_t i;

        if (count > 0U)
        {
            fn->captured = (struct cnor_local_decl *)
                cnor_arena_alloc(&self->arena, count * sizeof(struct cnor_local_decl));
            if (fn->captured == lh_null)
            {
                return cnor_compile_fail(self, node, cnor_error_no_memory);
            }
            for (i = 0U; i < count; ++i)
            {
                fn->captured[i] = locals[i];
                /* The slots of a variable it closes over are not given out
                 * again: its code may run when they hold something else. */
                cnor_scope_keep_slots(&self->scope,
                                      (cnor_slot_t) (locals[i].slot + locals[i].slots));
            }
        }
        fn->captured_count = (lh_u16_t) count;
    }
    return lh_bool_true;
}

/* -- calling ---------------------------------------------------------------- */

/*
 * The call of a local function: the arguments go into the parameter slots,
 * the stack is emptied into slots of its own, the call jumps into the code of
 * the function, and the result is read from the slot it waits in.
 */
lh_bool_t
cnor_compile_call_local(cnor_compiler_t *self, const cnor_node_t *node, cnor_local_fn_t *fn)
{
    const cnor_node_t *arg;
    const cnor_node_t *param = fn->node->data.fn.params;
    const cnor_argc_t argc = node->data.call.argc;
    cnor_slot_t spill = 0U;
    lh_s32_t depth;
    lh_u32_t i;

    if (argc != fn->param_count)
    {
        return cnor_compile_fail_name(self, node, cnor_error_wrong_arg_count);
    }
    for (arg = node->data.call.args; arg != lh_null; arg = arg->next, param = param->next)
    {
        if (!cnor_compile_expr(self, arg))
        {
            return lh_bool_false;
        }
        if (!cnor_type_require(self, arg,
                               cnor_type_of_node(self, param, param->data.param.type), self->type))
        {
            return lh_bool_false;
        }
    }
    /* The arguments go into the parameter slots, the last one first: the last
     * argument is the one on top of the stack. */
    for (i = argc; i > 0U; --i)
    {
        if (!cnor_emit_insn(self, cnor_op_store_local, (lh_u32_t) (fn->param_base + i - 1U), 0U))
        {
            return lh_bool_false;
        }
    }
    /* The code of the function runs with an empty stack, so what the caller
     * had on it goes into slots of its own and comes back afterwards. */
    depth = self->emit.depth;
    if (depth > 0)
    {
        if (!cnor_scope_take_slots(&self->scope, (lh_u16_t) depth, &spill))
        {
            return cnor_compile_fail(self, node, cnor_error_too_many_locals);
        }
        for (i = (lh_u32_t) depth; i > 0U; --i)
        {
            if (!cnor_emit_insn(self, cnor_op_store_local, (lh_u32_t) (spill + i - 1U), 0U))
            {
                return lh_bool_false;
            }
        }
    }
    fn->called = lh_bool_true;
    if (self->local_fn != lh_null)
    {
        /* The call is inside another local function: they must not call each
         * other in a circle, so the call is remembered. */
        struct cnor_local_call_edge edge;

        edge.from = self->local_fn->index;
        edge.to = fn->index;
        lh_array_push_back(&self->local_calls, &edge);
    }
    if (!cnor_emit_jump_with(self, cnor_op_call_local, &fn->body_label, fn->ret_slot, 0U))
    {
        return lh_bool_false;
    }
    for (i = 0U; i < (lh_u32_t) depth; ++i)
    {
        if (!cnor_emit_insn(self, cnor_op_load_local, (lh_u32_t) (spill + i), 0U))
        {
            return lh_bool_false;
        }
    }
    if (!cnor_emit_insn(self, cnor_op_load_local, fn->result_slot, 0U))
    {
        return lh_bool_false;
    }
    self->type = fn->returns;
    self->emit.line = node->line;
    return lh_bool_true;
}

/* -- the body --------------------------------------------------------------- */

/*
 * The code of a local function: it runs in the frame of the function that
 * declared it, so the variables of that function are in sight, and the
 * parameters are bound to the slots they were given.
 */
static lh_bool_t
cnor_compile_local_fn_body(cnor_compiler_t *self, cnor_local_fn_t *fn)
{
    const cnor_node_t *param;
    const cnor_scope_mark_t mark = cnor_scope_enter_block(&self->scope);
    struct cnor_local_fn *const outer_fn = self->local_fn;
    const lh_u32_t outer_try = self->try_depth;
    const lh_u32_t outer_loop = self->loop_depth;
    const cnor_static_type_t outer_returns = self->returns;
    lh_u16_t i = 0U;
    lh_u16_t captured = 0U;
    lh_bool_t ok = lh_bool_true;

    self->local_fn = fn;
    self->try_depth = 0U;
    /* A loop of the function that declared it is not a loop of this one: a
     * `break` here has nowhere to go. */
    self->loop_depth = 0U;
    self->returns = fn->returns;

    /* The variables the function closes over are the ones that were in sight
     * where it was declared. */
    lh_array_clear(&self->scope.locals);
    for (captured = 0U; captured < fn->captured_count; ++captured)
    {
        lh_array_push_back(&self->scope.locals, &fn->captured[captured]);
    }
    /* Its own variables go above every slot the function has: the code of the
     * function runs while the frame is still in use, and its variables must
     * not take the place of the ones of the function it is in. */
    self->scope.next_slot = self->scope.slot_count;

    for (param = fn->node->data.fn.params; param != lh_null; param = param->next, ++i)
    {
        const cnor_static_type_t type = cnor_type_of_node(self, param, param->data.param.type);
        const lh_bool_t is_mutable =
            (cnor_type_base(type) == cnor_static_array || cnor_type_base(type) == cnor_static_dict
             || cnor_named_is_struct(self, type))
                ? lh_bool_true
                : lh_bool_false;

        if (!cnor_scope_bind_local(&self->scope, param->name, (cnor_slot_t) (fn->param_base + i),
                                   is_mutable, type))
        {
            ok = lh_bool_false;
        }
    }
    /* The code starts where the calls jump to. */
    if (ok && !cnor_emit_bind(self, &fn->body_label))
    {
        ok = lh_bool_false;
    }
    if (ok && !cnor_compile_block(self, fn->node->data.fn.body))
    {
        ok = lh_bool_false;
    }
    /* Falling off the end of the code gives none. */
    if (ok && !cnor_emit_op(self, cnor_op_push_none))
    {
        ok = lh_bool_false;
    }
    if (ok && !cnor_emit_insn(self, cnor_op_store_local, fn->result_slot, 0U))
    {
        ok = lh_bool_false;
    }
    /* Every return goes to the end of the code, where the machine goes back
     * to where the call was made. */
    if (ok && !cnor_emit_bind(self, &fn->end_label))
    {
        ok = lh_bool_false;
    }
    if (ok && !cnor_emit_insn(self, cnor_op_ret_local, fn->ret_slot, 0U))
    {
        ok = lh_bool_false;
    }
    cnor_scope_leave_block(&self->scope, mark);
    self->local_fn = outer_fn;
    self->try_depth = outer_try;
    self->loop_depth = outer_loop;
    self->returns = outer_returns;
    return ok;
}

/* -- the calls between them ------------------------------------------------- */

/*
 * The local functions of one function share the slots of their parameters: a
 * call inside a call would overwrite them. So they must not call each other in
 * a circle, which is what this walk looks for.
 */
static lh_bool_t
cnor_compile_local_fn_walk(cnor_compiler_t *self, lh_u16_t index, lh_u8_t *state)
{
    lh_usize_t i;

    if (state[index] == 2U)
    {
        return lh_bool_true;
    }
    if (state[index] == 1U)
    {
        return lh_bool_false;
    }
    state[index] = 1U;
    for (i = 0U; i < lh_array_get_size(&self->local_calls); ++i)
    {
        const struct cnor_local_call_edge *const edge =
            (const struct cnor_local_call_edge *) lh_array_get_ptr(&self->local_calls, i);

        if (edge->from == index && !cnor_compile_local_fn_walk(self, edge->to, state))
        {
            return lh_bool_false;
        }
    }
    state[index] = 2U;
    return lh_bool_true;
}

static lh_bool_t
cnor_compile_local_fn_cycles(cnor_compiler_t *self)
{
    lh_u8_t state[CNOR_COMPILER_MAX_LOCAL_FNS];
    const lh_usize_t count = lh_array_get_size(&self->scope.local_fns);
    lh_usize_t i;

    for (i = 0U; i < count; ++i)
    {
        state[i] = 0U;
    }
    for (i = 0U; i < count; ++i)
    {
        if (!cnor_compile_local_fn_walk(self, (lh_u16_t) i, state))
        {
            const cnor_local_fn_t *const fn = cnor_scope_local_fn_at(&self->scope, (lh_u16_t) i);

            return cnor_compile_fail_name(self, fn->node, cnor_error_recursive);
        }
    }
    return lh_bool_true;
}

/* -- the code of them all --------------------------------------------------- */

/*
 * The code of the local functions declared in the function being compiled: it
 * goes after the `ret` that ends the function, and the calls jump into it.
 */
lh_bool_t
cnor_compile_local_fn_bodies(cnor_compiler_t *self)
{
    lh_bool_t progress = lh_bool_true;

    /* A local function may declare one of its own, and one of them may call
     * another: the list grows while it is walked, and a function that is only
     * called by another one is found later. So the list is walked again while
     * it brings something new. */
    while (progress)
    {
        lh_usize_t i = 0U;

        progress = lh_bool_false;
        while (i < lh_array_get_size(&self->scope.local_fns))
        {
            cnor_local_fn_t *const fn = cnor_scope_local_fn_at(&self->scope, (lh_u16_t) i);

            i++;
            if (fn->compiled || !fn->called)
            {
                /* Its code is written already, or nothing calls it. */
                continue;
            }
            if (fn->node->data.fn.returns != cnor_type_none
                && !cnor_compile_list_returns(fn->node->data.fn.body))
            {
                return cnor_compile_fail_name(self, fn->node, cnor_error_missing_return);
            }
            self->emit.line = fn->node->line;
            if (!cnor_compile_local_fn_body(self, fn))
            {
                return lh_bool_false;
            }
            fn->compiled = lh_bool_true;
            progress = lh_bool_true;
        }
    }
    return cnor_compile_local_fn_cycles(self);
}
