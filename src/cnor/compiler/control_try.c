#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- try, catch and finally --------------------------------------------------- */

/*
 * `try:` block [`catch [name]:` block] [`finally:` block].
 *
 * The code is laid out like this:
 *
 *     try_push catch        ; the catch block, and where the error goes
 *       <the try block>
 *     try_pop               ; the try ended without an error
 *     jmp finally
 *   catch:
 *     load_local error      ; the error, as a dictionary
 *     store_local name      ; the name it is bound to
 *     <the catch block>
 *     try_pop               ; the catch ended: the error is handled
 *   finally:
 *     <the finally block>
 *     finally_end           ; the handler is gone; an error goes on outward
 *   end:
 *
 * The catch block is laid out after the try block, so the `jmp` skips it when
 * the try ended without an error; the catch block itself falls through to the
 * finally block. The error's dictionary lives in slots the compiler reserves
 * here, so catching an error allocates nothing.
 *
 * The finally block runs when the `try` is left, however it is left: the code
 * after it went fine, an error happened, or the code asked to go elsewhere.
 * The machine knows where it is, so it can run it on the error path too.
 *
 * A `try` with no catch block has no dictionary: the machine writes one only
 * when a catch block takes the error, and the `try_push` says so with a zero
 * address.
 */

/* How many pairs the error's dictionary holds: the message, the line, the code,
 * the kind and the file. */
#define CNOR_TRY_ERROR_PAIRS 5U

lh_bool_t
cnor_compile_try(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_scope_mark_t mark = cnor_scope_enter_block(&self->scope);
    const cnor_node_t *const catch_body = node->data.try_.catch_body;
    const cnor_node_t *const finally_body = node->data.try_.finally_body;
    cnor_label_t catch_label;
    cnor_label_t finally_label;
    cnor_label_t end_label;
    cnor_slot_t error_slot = 0U;
    cnor_slot_t name_slot = 0U;
    cnor_slot_t value_slot = 0U;
    lh_bool_t has_name = lh_bool_false;
    lh_bool_t ok;

    cnor_label_init(&catch_label);
    cnor_label_init(&finally_label);
    cnor_label_init(&end_label);
    self->emit.line = node->line;
    if (catch_body != lh_null)
    {
        /* The error's dictionary: its slots are here, so the machine can fill
         * it without allocating. */
        if (!cnor_scope_add_dict_local(self, node, lh_str_view_empty(), lh_bool_false,
                                        CNOR_TRY_ERROR_PAIRS, &error_slot))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
        /* The name the error is bound to, in the catch block. */
        if (!lh_str_view_is_empty(&node->name))
        {
            has_name = lh_bool_true;
            if (!cnor_scope_add_local(self, node, node->name, lh_bool_false,
                                      cnor_static_dict, 0U, &name_slot))
            {
                cnor_scope_leave_block(&self->scope, mark);
                return lh_bool_false;
            }
        }
    }
    if (finally_body != lh_null)
    {
        /* Where a returned value waits while the finally block runs: the
         * machine returns it when the block is done. */
        if (!cnor_scope_add_local(self, node, lh_str_view_empty(), lh_bool_false,
                                  cnor_static_any, 0U, &value_slot))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
    }
    /* The catch block, and the slot the error goes into. */
    if (catch_body != lh_null)
    {
        if (!cnor_emit_jump_with(self, cnor_op_try_push, &catch_label, error_slot, 0U))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
    }
    else
    {
        /* No catch block: the machine is told so with a zero address. */
        if (!cnor_emit_insn(self, cnor_op_try_push, 0U, error_slot))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
    }
    /* The finally block, when there is one: the machine runs it when the
     * `try` is left. */
    if (finally_body != lh_null
        && !cnor_emit_jump(self, cnor_op_try_finally, &finally_label))
    {
        cnor_scope_leave_block(&self->scope, mark);
        return lh_bool_false;
    }
    /* The try block: a handler is open, so a `break` or a `continue` out of
     * it has to close it. */
    if (self->try_depth >= CNOR_COMPILER_MAX_TRIES)
    {
        cnor_scope_leave_block(&self->scope, mark);
        return cnor_compile_fail(self, node, cnor_error_too_many_tries);
    }
    {
        const lh_u32_t outer = self->try_depth;

        ++self->try_depth;
        self->tries[self->try_depth - 1U].finally_label =
            finally_body != lh_null ? &finally_label : lh_null;
        self->tries[self->try_depth - 1U].value_slot = value_slot;
        ok = cnor_compile_block(self, node->data.try_.body);
        /* A `break` or a `return` out of the block closed the handlers it
         * crossed; the block ends here, so the count is the one it began with. */
        self->try_depth = outer;
    }
    if (!ok || !cnor_emit_op(self, cnor_op_try_pop))
    {
        cnor_scope_leave_block(&self->scope, mark);
        return lh_bool_false;
    }
    /* The try ended: the finally block runs, if there is one. */
    if (!cnor_emit_jump(self, cnor_op_jmp, finally_body != lh_null ? &finally_label : &end_label))
    {
        cnor_scope_leave_block(&self->scope, mark);
        return lh_bool_false;
    }
    /* The catch block: the error is the dictionary the machine filled in. */
    if (catch_body != lh_null)
    {
        if (!cnor_emit_bind(self, &catch_label))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
        if (has_name)
        {
            if (!cnor_emit_insn(self, cnor_op_load_local, error_slot, 0U)
                || !cnor_emit_insn(self, cnor_op_store_local, name_slot, 0U))
            {
                cnor_scope_leave_block(&self->scope, mark);
                return lh_bool_false;
            }
        }
        ok = cnor_compile_block(self, catch_body);
        /* The catch ended: the error is handled. */
        if (!ok || !cnor_emit_op(self, cnor_op_try_pop))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
    }
    /* The finally block: it runs whatever happened before it. */
    if (finally_body != lh_null)
    {
        if (!cnor_emit_bind(self, &finally_label))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
        ok = cnor_compile_block(self, finally_body);
        if (!ok || !cnor_emit_op(self, cnor_op_finally_end))
        {
            cnor_scope_leave_block(&self->scope, mark);
            return lh_bool_false;
        }
    }
    cnor_scope_leave_block(&self->scope, mark);
    return cnor_emit_bind(self, &end_label);
}

/* -- raise ------------------------------------------------------------------- */

/* The error being handled goes on outward, as if it had just happened. */
lh_bool_t
cnor_compile_raise(cnor_compiler_t *self, const cnor_node_t *node)
{
    self->emit.line = node->line;
    return cnor_emit_op(self, cnor_op_raise);
}

/* -- leaving a try ----------------------------------------------------------- */

/*
 * Leave the `try` blocks that are open, running the finally blocks of the
 * ones the exit crosses.
 *
 * The finally blocks run from the inside out. The machine jumps to the first
 * one and comes back to a trampoline that runs the next; the trampolines are
 * laid out where the exit is, after the jump that leaves the loop or the
 * function. The handlers of the blocks without a finally block are closed as
 * the machine goes.
 */
lh_bool_t
cnor_compile_exit_tries(cnor_compiler_t *self, lh_u32_t first, lh_u8_t kind,
                         cnor_label_t *target, cnor_slot_t slot)
{
    lh_u32_t i;
    cnor_label_t trampoline;

    cnor_label_init(&trampoline);
    /* The `try` blocks the exit crosses, from the inside out. */
    for (i = self->try_depth; i > first; --i)
    {
        const struct cnor_try *const open = &self->tries[i - 1U];

        if (open->finally_label == lh_null)
        {
            /* No finally block: the handler is closed here. */
            if (!cnor_emit_op(self, cnor_op_try_pop))
            {
                return lh_bool_false;
            }
            continue;
        }
        /* The finally block runs; the machine comes back to the trampoline,
         * which runs the next one. */
        if (!cnor_emit_jump_with(self, cnor_op_try_exit, &trampoline, 0U, 0U))
        {
            return lh_bool_false;
        }
        if (!cnor_emit_bind(self, &trampoline))
        {
            return lh_bool_false;
        }
        cnor_label_init(&trampoline);
    }
    self->try_depth = first;
    /* The exit itself: a jump, or the value that waits in the slot. */
    if (kind == (lh_u8_t) CNOR_EXIT_JUMP)
    {
        return cnor_emit_jump(self, cnor_op_jmp, target);
    }
    return cnor_emit_insn(self, cnor_op_load_local, slot, 0U)
           && cnor_emit_op(self, cnor_op_ret);
}
