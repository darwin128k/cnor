#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- loop context ---------------------------------------------------------- */

static lh_bool_t
cnor_loop_push(cnor_compiler_t *self, const cnor_node_t *node, cnor_label_t *break_label,
               cnor_label_t *continue_label)
{
    if (self->loop_depth >= CNOR_COMPILER_MAX_LOOPS)
    {
        return cnor_compile_fail(self, node, cnor_error_too_deep);
    }
    self->loops[self->loop_depth].break_label = break_label;
    self->loops[self->loop_depth].continue_label = continue_label;
    self->loop_depth++;
    return lh_bool_true;
}

static lh_bool_t
cnor_loop_body(cnor_compiler_t *self, const cnor_node_t *node, const cnor_node_t *body,
               cnor_label_t *break_label, cnor_label_t *continue_label)
{
    lh_bool_t ok;

    if (!cnor_loop_push(self, node, break_label, continue_label))
    {
        return lh_bool_false;
    }
    ok = cnor_compile_block(self, body);
    self->loop_depth--;
    return ok;
}

/* -- while ----------------------------------------------------------------- */

/*
 * The condition is tested at the bottom, so an iteration costs one conditional
 * jump instead of a conditional and an unconditional one:
 *
 *     jmp check
 *   body:
 *     body                      (continue -> check, break -> end)
 *   check:
 *     cond; jmp_if_true body
 *   end:
 */
lh_bool_t
cnor_compile_while(cnor_compiler_t *self, const cnor_node_t *node)
{
    cnor_label_t body;
    cnor_label_t check;
    cnor_label_t end;

    cnor_label_init(&body);
    cnor_label_init(&check);
    cnor_label_init(&end);
    if (!cnor_emit_jump(self, cnor_op_jmp, &check) || !cnor_emit_bind(self, &body)
        || !cnor_loop_body(self, node, node->data.while_.body, &end, &check)
        || !cnor_emit_bind(self, &check))
    {
        return lh_bool_false;
    }
    /* The condition is the loop header's, wherever its code ends up. */
    self->emit.line = node->line;
    return cnor_compile_branch(self, node->data.while_.cond, lh_bool_true, &body)
           && cnor_emit_bind(self, &end);
}

/* -- for ------------------------------------------------------------------- */

/* The variable and the evaluated end bound live in two slots of their own. */
static lh_bool_t
cnor_compile_for_slots(cnor_compiler_t *self, const cnor_node_t *node, cnor_slot_t *var,
                       cnor_slot_t *bound)
{
    return cnor_compile_expr(self, node->data.for_.from)
           && cnor_compile_expr(self, node->data.for_.to)
           && cnor_scope_add_local(self, node, lh_str_view_empty(), lh_bool_false, bound)
           && cnor_emit_insn(self, cnor_op_store_local, *bound, 0U)
           && cnor_scope_add_local(self, node, node->name, lh_bool_false, var)
           && cnor_emit_insn(self, cnor_op_store_local, *var, 0U);
}

/*
 * Tested once on entry, then at the bottom by `for_loop`, which adds one to
 * the variable, compares it with the bound and jumps back in one instruction:
 *
 *     from -> var; to -> bound
 *     lt_ll var, bound; jmp_if_false end
 *   body:
 *     body                      (continue -> step, break -> end)
 *   step:
 *     for_loop body, var, bound
 *   end:
 */
static lh_bool_t
cnor_compile_for_loop(cnor_compiler_t *self, const cnor_node_t *node, cnor_slot_t var,
                      cnor_slot_t bound)
{
    cnor_label_t body;
    cnor_label_t step;
    cnor_label_t end;

    cnor_label_init(&body);
    cnor_label_init(&step);
    cnor_label_init(&end);
    self->emit.line = node->line;
    if (!cnor_emit_insn(self, cnor_op_lt_ll, var, bound)
        || !cnor_emit_jump(self, cnor_op_jmp_if_false, &end) || !cnor_emit_bind(self, &body)
        || !cnor_loop_body(self, node, node->data.for_.body, &end, &step))
    {
        return lh_bool_false;
    }
    self->emit.line = node->line;
    return cnor_emit_bind(self, &step)
           && cnor_emit_jump_with(self, cnor_op_for_loop, &body, var, bound)
           && cnor_emit_bind(self, &end);
}

lh_bool_t
cnor_compile_for(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_scope_mark_t mark = cnor_scope_enter_block(&self->scope);
    cnor_slot_t var = 0U;
    cnor_slot_t bound = 0U;
    lh_bool_t ok;

    ok = cnor_compile_for_slots(self, node, &var, &bound)
         && cnor_compile_for_loop(self, node, var, bound);
    cnor_scope_leave_block(&self->scope, mark);
    return ok;
}

/* -- break and continue ---------------------------------------------------- */

lh_bool_t
cnor_compile_break(cnor_compiler_t *self, const cnor_node_t *node)
{
    if (self->loop_depth == 0U)
    {
        return cnor_compile_fail(self, node, cnor_error_outside_loop);
    }
    return cnor_emit_jump(self, cnor_op_jmp, self->loops[self->loop_depth - 1U].break_label);
}

lh_bool_t
cnor_compile_continue(cnor_compiler_t *self, const cnor_node_t *node)
{
    if (self->loop_depth == 0U)
    {
        return cnor_compile_fail(self, node, cnor_error_outside_loop);
    }
    return cnor_emit_jump(self, cnor_op_jmp, self->loops[self->loop_depth - 1U].continue_label);
}
