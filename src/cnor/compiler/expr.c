#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- literals -------------------------------------------------------------- */

#define CNOR_I32_MAX 0x7FFFFFFFU

static lh_bool_t
cnor_compile_int(cnor_compiler_t *self, cnor_int_literal_t value, lh_bool_t negate)
{
    if (negate && value <= CNOR_I32_MAX + 1U)
    {
        return cnor_emit_insn(self, cnor_op_push_i32, (lh_u32_t) (0U - value), 0U);
    }
    if (value <= CNOR_I32_MAX)
    {
        return cnor_emit_insn(self, cnor_op_push_i32, value, 0U);
    }
    return cnor_emit_insn(self, cnor_op_push_u32, value, 0U)
           && (!negate || cnor_emit_op(self, cnor_op_neg));
}

static lh_bool_t
cnor_compile_constant(cnor_compiler_t *self, const cnor_node_t *node, cnor_const_kind_t kind)
{
    cnor_const_index_t index;

    return cnor_constant_pool_intern(self, node, kind, &index)
           && cnor_emit_insn(self, cnor_op_push_const, index, 0U);
}

lh_bool_t
cnor_compile_literal(cnor_compiler_t *self, const cnor_node_t *node)
{
    switch (node->kind)
    {
    case cnor_node_int:
        return cnor_compile_int(self, node->data.int_.value, lh_bool_false);
    case cnor_node_real:
        return cnor_compile_constant(self, node, cnor_const_real);
    case cnor_node_string:
        return cnor_compile_constant(self, node, cnor_const_str);
    case cnor_node_true:
        return cnor_emit_op(self, cnor_op_push_true);
    case cnor_node_false:
        return cnor_emit_op(self, cnor_op_push_false);
    default:
        return cnor_emit_op(self, cnor_op_push_none);
    }
}

/* -- names ----------------------------------------------------------------- */

lh_bool_t
cnor_compile_ident(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_binding_t binding = cnor_scope_resolve(self, node->name);

    switch (binding.kind)
    {
    case cnor_binding_local:
        return cnor_emit_insn(self, cnor_op_load_local, binding.index, 0U);
    case cnor_binding_global:
        return cnor_emit_insn(self, cnor_op_load_global, binding.index, 0U);
    default:
        /* Functions and natives are not values: there is nothing to push. */
        return cnor_compile_fail_name(self, node, cnor_error_unknown_name);
    }
}

/* -- operators ------------------------------------------------------------- */

lh_bool_t
cnor_compile_unary(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *operand = node->data.unary.operand;

    if (node->data.unary.op == cnor_unary_neg && operand->kind == cnor_node_int)
    {
        /* `-5` is one push, and `-2147483648` fits where `2147483648` does not. */
        return cnor_compile_int(self, operand->data.int_.value, lh_bool_true);
    }
    if (!cnor_compile_expr(self, operand))
    {
        return lh_bool_false;
    }
    return cnor_emit_op(self, node->data.unary.op == cnor_unary_neg ? cnor_op_neg : cnor_op_not);
}

/* Opcode of an arithmetic or comparison operator; `and` / `or` are logic.c's. */
static const cnor_op_t m_binary_ops[cnor_binary_count] = {
    [cnor_binary_add] = cnor_op_add, [cnor_binary_sub] = cnor_op_sub,
    [cnor_binary_mul] = cnor_op_mul, [cnor_binary_div] = cnor_op_div,
    [cnor_binary_mod] = cnor_op_mod, [cnor_binary_eq] = cnor_op_eq,
    [cnor_binary_ne] = cnor_op_ne,   [cnor_binary_lt] = cnor_op_lt,
    [cnor_binary_le] = cnor_op_le,   [cnor_binary_gt] = cnor_op_gt,
    [cnor_binary_ge] = cnor_op_ge,
};

/* -- operand forms --------------------------------------------------------- */

lh_bool_t
cnor_compile_local_operand(cnor_compiler_t *self, const cnor_node_t *node, cnor_slot_t *slot)
{
    cnor_binding_t binding;

    if (node->kind != cnor_node_ident)
    {
        return lh_bool_false;
    }
    binding = cnor_scope_resolve(self, node->name);
    if (binding.kind != cnor_binding_local)
    {
        return lh_bool_false;
    }
    *slot = binding.index;
    return lh_bool_true;
}

lh_bool_t
cnor_compile_int_operand(const cnor_node_t *node, lh_u32_t *bits)
{
    if (node->kind == cnor_node_int && node->data.int_.value <= CNOR_I32_MAX)
    {
        *bits = node->data.int_.value;
        return lh_bool_true;
    }
    if (node->kind == cnor_node_unary && node->data.unary.op == cnor_unary_neg
        && node->data.unary.operand->kind == cnor_node_int
        && node->data.unary.operand->data.int_.value <= CNOR_I32_MAX + 1U)
    {
        /* Exactly what cnor_compile_int pushes for `-k`. */
        *bits = 0U - node->data.unary.operand->data.int_.value;
        return lh_bool_true;
    }
    return lh_bool_false;
}

/*
 * A binary operator whose operands are locals or `i32` literals takes them
 * straight from the frame or the code (`add_ll`, `add_li`, `add_i`, ...)
 * instead of pushing them first. Reading a local or a literal has no effect
 * of its own, so the order of evaluation is unchanged.
 */
static lh_bool_t
cnor_compile_binary_form(cnor_compiler_t *self, const cnor_node_t *node, cnor_op_t op)
{
    const cnor_node_t *left = node->data.binary.left;
    const cnor_node_t *right = node->data.binary.right;
    cnor_slot_t a;
    cnor_slot_t b;
    lh_u32_t k;

    if (cnor_compile_local_operand(self, left, &a))
    {
        if (cnor_compile_local_operand(self, right, &b))
        {
            return cnor_emit_insn(
                self, cnor_op_get_operand_form(op, cnor_operand_form_local_local), a, b);
        }
        if (cnor_compile_int_operand(right, &k))
        {
            return cnor_emit_insn(self, cnor_op_get_operand_form(op, cnor_operand_form_local_int),
                                  a, k);
        }
    }
    if (cnor_compile_int_operand(right, &k))
    {
        return cnor_compile_expr(self, left)
               && cnor_emit_insn(
                   self, cnor_op_get_operand_form(op, cnor_operand_form_stack_int), k, 0U);
    }
    return cnor_compile_expr(self, left) && cnor_compile_expr(self, right)
           && cnor_emit_op(self, op);
}

lh_bool_t
cnor_compile_binary(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_binary_op_t op = node->data.binary.op;

    if (op == cnor_binary_and || op == cnor_binary_or)
    {
        return cnor_compile_logic(self, node);
    }
    return cnor_compile_binary_form(self, node, m_binary_ops[op]);
}

lh_bool_t
cnor_compile_index(cnor_compiler_t *self, const cnor_node_t *node)
{
    return cnor_compile_expr(self, node->data.index.target)
           && cnor_compile_expr(self, node->data.index.index) && cnor_emit_op(self, cnor_op_index);
}
