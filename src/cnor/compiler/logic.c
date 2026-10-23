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
