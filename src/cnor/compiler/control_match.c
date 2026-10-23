#include <cnor/compiler/internal.h>
#include <lh/null.h>

/*
 * The subject is evaluated once into a slot of its own. Each arm:
 *
 *     load subject; pattern; eq; jmp_if_false next     (not for `_`)
 *     body; jmp end
 *   next:
 *
 * No arm matching does nothing.
 */
static lh_bool_t
cnor_compile_arm(cnor_compiler_t *self, const cnor_node_t *arm, cnor_slot_t subject,
                 cnor_label_t *end)
{
    cnor_label_t next;

    cnor_label_init(&next);
    self->emit.line = arm->line;
    if (arm->data.arm.pattern != lh_null
        && (!cnor_emit_insn(self, cnor_op_load_local, subject, 0U)
            || !cnor_compile_expr(self, arm->data.arm.pattern) || !cnor_emit_op(self, cnor_op_eq)
            || !cnor_emit_jump(self, cnor_op_jmp_if_false, &next)))
    {
        return lh_bool_false;
    }
    return cnor_compile_block(self, arm->data.arm.body) && cnor_emit_jump(self, cnor_op_jmp, end)
           && cnor_emit_bind(self, &next);
}

lh_bool_t
cnor_compile_match(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_scope_mark_t mark = cnor_scope_enter_block(&self->scope);
    const cnor_node_t *arm;
    cnor_slot_t subject = 0U;
    cnor_label_t end;
    lh_bool_t ok;

    cnor_label_init(&end);
    ok = cnor_compile_expr(self, node->data.match.subject)
         && cnor_scope_add_local(self, node, lh_str_view_empty(), lh_bool_false, &subject)
         && cnor_emit_insn(self, cnor_op_store_local, subject, 0U);
    for (arm = node->data.match.arms; ok && arm != lh_null; arm = arm->next)
    {
        ok = cnor_compile_arm(self, arm, subject, &end);
    }
    ok = ok && cnor_emit_bind(self, &end);
    cnor_scope_leave_block(&self->scope, mark);
    return ok;
}
