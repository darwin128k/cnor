#include <cnor/compiler/internal.h>

/*
 * `a and b` / `a or b`, short-circuit, yielding the deciding operand:
 *
 *     a; dup; jmp_if_false end   (jmp_if_true for `or`)
 *     pop; b
 *   end:
 *
 * Both paths reach `end` with exactly one value on the stack.
 */
lh_bool_t
cnor_compile_logic(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_op_t skip =
        node->data.binary.op == cnor_binary_and ? cnor_op_jmp_if_false : cnor_op_jmp_if_true;
    cnor_label_t end;

    cnor_label_init(&end);
    return cnor_compile_expr(self, node->data.binary.left) && cnor_emit_op(self, cnor_op_dup)
           && cnor_emit_jump(self, skip, &end) && cnor_emit_op(self, cnor_op_pop)
           && cnor_compile_expr(self, node->data.binary.right) && cnor_emit_bind(self, &end);
}

/*
 * A condition where only its truth matters (`if`, `while`): jump to @p target
 * when it is @p when, fall through otherwise. `and`, `or` and `not` become
 * jumps instead of values, so `a < b and c` tests each comparison with its
 * own conditional jump and never builds the boolean:
 *
 *     a and b, jump when false:  a; jmp_if_false target; b; jmp_if_false target
 *     a or b, jump when false:   a; jmp_if_true skip; b; jmp_if_false target
 *                                skip:
 *
 * and their mirror images for "jump when true". The operands are evaluated in
 * the same order and as far as `and` / `or` would evaluate them, and the truth
 * of `a and b` is the truth of a then b, so only the instructions change.
 */
lh_bool_t
cnor_compile_branch(cnor_compiler_t *self, const cnor_node_t *node, lh_bool_t when,
                    cnor_label_t *target)
{
    self->emit.line = node->line;
    if (node->kind == cnor_node_unary && node->data.unary.op == cnor_unary_not)
    {
        return cnor_compile_branch(self, node->data.unary.operand, !when, target);
    }
    if (node->kind == cnor_node_binary
        && (node->data.binary.op == cnor_binary_and || node->data.binary.op == cnor_binary_or))
    {
        /* `and` jumping when false, `or` jumping when true: both operands may
         * jump straight to the target. Otherwise the left one decides only
         * when it skips the right one. */
        const lh_bool_t left_decides = (node->data.binary.op == cnor_binary_and) != when;
        cnor_label_t skip;

        if (left_decides)
        {
            return cnor_compile_branch(self, node->data.binary.left, when, target)
                   && cnor_compile_branch(self, node->data.binary.right, when, target);
        }
        cnor_label_init(&skip);
        return cnor_compile_branch(self, node->data.binary.left, !when, &skip)
               && cnor_compile_branch(self, node->data.binary.right, when, target)
               && cnor_emit_bind(self, &skip);
    }
    return cnor_compile_expr(self, node)
           && cnor_emit_jump(self, when ? cnor_op_jmp_if_true : cnor_op_jmp_if_false, target);
}
