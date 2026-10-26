#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- try and catch ----------------------------------------------------------- */

/*
 * `try:` block `catch [name]:` block.
 *
 * The code is laid out like this:
 *
 *     try_push catch        ; the catch block, and where the error goes
 *       <the try block>
 *     try_pop               ; the try ended without an error
 *     jmp end
 *   catch:
 *     load_local error      ; the error, as a dictionary
 *     store_local name      ; the name it is bound to
 *     <the catch block>
 *   end:
 *
 * The catch block is laid out after the try block, so the `jmp` skips it when
 * the try ended without an error; the catch block itself falls through to the
 * end. The error's dictionary lives in slots the compiler reserves here, so
 * catching an error allocates nothing.
 */

/* How many pairs the error's dictionary holds: the message, the line, the
 * code and the file. */
#define CNOR_TRY_ERROR_PAIRS 4U

lh_bool_t
cnor_compile_try(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_scope_mark_t mark = cnor_scope_enter_block(&self->scope);
    cnor_label_t catch_label;
    cnor_label_t end_label;
    cnor_slot_t error_slot = 0U;
    cnor_slot_t name_slot = 0U;
    lh_bool_t has_name = lh_bool_false;
    lh_bool_t ok;

    cnor_label_init(&catch_label);
    cnor_label_init(&end_label);
    self->emit.line = node->line;
    /* The error's dictionary: its slots are here, so the machine can fill it
     * without allocating. */
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
    /* The catch block, and the slot the error goes into. */
    if (!cnor_emit_jump_with(self, cnor_op_try_push, &catch_label, error_slot, 0U))
    {
        cnor_scope_leave_block(&self->scope, mark);
        return lh_bool_false;
    }
    /* The try block: a handler is open, so a `break` or a `continue` out of
     * it has to close it. */
    ++self->try_depth;
    ok = cnor_compile_block(self, node->data.try_.body);
    --self->try_depth;
    if (!ok || !cnor_emit_op(self, cnor_op_try_pop) || !cnor_emit_jump(self, cnor_op_jmp, &end_label))
    {
        cnor_scope_leave_block(&self->scope, mark);
        return lh_bool_false;
    }
    /* The catch block: the handler is gone, and the error is the dictionary
     * the machine filled in. */
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
    ok = cnor_compile_block(self, node->data.try_.catch_body);
    cnor_scope_leave_block(&self->scope, mark);
    if (!ok || !cnor_emit_bind(self, &end_label))
    {
        return lh_bool_false;
    }
    return lh_bool_true;
}
