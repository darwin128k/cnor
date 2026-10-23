#include <cnor/vm/internal.h>

/* -- values ---------------------------------------------------------------- */

lh_bool_t
cnor_vm_compare_values(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_op_t op,
                       const lh_value_t *left, const lh_value_t *right, lh_value_t *out)
{
    if (op == cnor_op_eq || op == cnor_op_ne)
    {
        const lh_bool_t equal = cnor_vm_value_equals(left, right);

        *out = lh_value_make_bool(op == cnor_op_eq ? equal : !equal);
        return lh_bool_true;
    }
    if (!cnor_vm_value_is_number(left) || !cnor_vm_value_is_number(right))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    *out = lh_value_make_bool(cnor_vm_value_order_holds(left, right, op));
    return lh_bool_true;
}

/* -- handlers -------------------------------------------------------------- */

/* Compare the two values on top and replace them with the result. */
static lh_bool_t
cnor_vm_compare_top(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t right = cnor_vm_stack_pop(&vm->stack);
    const lh_value_t left = cnor_vm_stack_pop(&vm->stack);
    lh_value_t result;

    if (!cnor_vm_compare_values(vm, insn, insn->op, &left, &right, &result))
    {
        return lh_bool_false;
    }
    cnor_vm_stack_push(&vm->stack, result);
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_equality(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    return cnor_vm_compare_top(vm, insn);
}

lh_bool_t
cnor_vm_op_order(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    return cnor_vm_compare_top(vm, insn);
}

lh_bool_t
cnor_vm_op_not(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *top = cnor_vm_stack_peek(&vm->stack, 0U);

    (void) insn;
    *top = lh_value_make_bool(!cnor_vm_value_is_truthy(top));
    return lh_bool_true;
}
