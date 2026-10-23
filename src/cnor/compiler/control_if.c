#include <cnor/compiler/internal.h>
#include <lh/null.h>

/*
 *     cond; jmp_if_false else
 *     then-block
 *     jmp end                  (only with an else block)
 *   else:
 *     else-block
 *   end:
 */
lh_bool_t
cnor_compile_if(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *else_body = node->data.if_.else_body;
    cnor_label_t otherwise;
    cnor_label_t end;

    cnor_label_init(&otherwise);
    cnor_label_init(&end);
    if (!cnor_compile_expr(self, node->data.if_.cond)
        || !cnor_emit_jump(self, cnor_op_jmp_if_false, &otherwise)
        || !cnor_compile_block(self, node->data.if_.then_body))
    {
        return lh_bool_false;
    }
    if (else_body == lh_null)
    {
        return cnor_emit_bind(self, &otherwise);
    }
    return cnor_emit_jump(self, cnor_op_jmp, &end) && cnor_emit_bind(self, &otherwise)
           && cnor_compile_block(self, else_body) && cnor_emit_bind(self, &end);
}
