#include <cnor/vm/internal.h>

lh_bool_t
cnor_vm_op_equality(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t right = cnor_vm_stack_pop(&vm->stack);
    const lh_value_t left = cnor_vm_stack_pop(&vm->stack);
    const lh_bool_t equal = cnor_vm_value_equals(&left, &right);

    cnor_vm_stack_push(&vm->stack,
                       lh_value_make_bool(insn->op == cnor_op_eq ? equal : !equal));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_order(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t right = cnor_vm_stack_pop(&vm->stack);
    const lh_value_t left = cnor_vm_stack_pop(&vm->stack);

    if (!cnor_vm_value_is_number(&left) || !cnor_vm_value_is_number(&right))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    cnor_vm_stack_push(&vm->stack,
                       lh_value_make_bool(cnor_vm_value_order_holds(&left, &right, insn->op)));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_not(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *top = cnor_vm_stack_peek(&vm->stack, 0U);

    (void) insn;
    *top = lh_value_make_bool(!cnor_vm_value_is_truthy(top));
    return lh_bool_true;
}
