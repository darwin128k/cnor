#include <cnor/vm/internal.h>

/* -- constants ------------------------------------------------------------- */

lh_bool_t
cnor_vm_op_push_none(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    (void) insn;
    cnor_vm_stack_push(&vm->stack, lh_value_make_none());
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_push_true(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    (void) insn;
    cnor_vm_stack_push(&vm->stack, lh_value_make_bool(lh_bool_true));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_push_false(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    (void) insn;
    cnor_vm_stack_push(&vm->stack, lh_value_make_bool(lh_bool_false));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_push_i32(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    /* The operand is the two's complement bit pattern of a signed 32-bit value. */
    const lh_u32_t bits = insn->operands[0];
    const lh_sllong_t value =
        bits >= 0x80000000U ? -(lh_sllong_t) (0xFFFFFFFFU - bits) - 1 : (lh_sllong_t) bits;

    cnor_vm_stack_push(&vm->stack, lh_value_make_sint(value));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_push_u32(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_vm_stack_push(&vm->stack, lh_value_make_uint(insn->operands[0]));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_push_const(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    /* The operand is an ordinal into the constant table, range-checked at load. */
    cnor_vm_stack_push(&vm->stack, vm->constants[insn->operands[0]]);
    return lh_bool_true;
}

/* -- variables ------------------------------------------------------------- */

lh_bool_t
cnor_vm_op_load_local(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_vm_stack_push(&vm->stack, *cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[0]));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_store_local(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    *cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[0]) = cnor_vm_stack_pop(&vm->stack);
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_load_global(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_vm_stack_push(&vm->stack, *cnor_vm_stack_at(&vm->stack, insn->operands[0]));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_store_global(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    *cnor_vm_stack_at(&vm->stack, insn->operands[0]) = cnor_vm_stack_pop(&vm->stack);
    return lh_bool_true;
}

/* -- shuffling ------------------------------------------------------------- */

lh_bool_t
cnor_vm_op_dup(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    (void) insn;
    cnor_vm_stack_push(&vm->stack, *cnor_vm_stack_peek(&vm->stack, 0U));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_swap(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *top = cnor_vm_stack_peek(&vm->stack, 0U);
    lh_value_t *below = cnor_vm_stack_peek(&vm->stack, 1U);
    const lh_value_t held = *top;

    (void) insn;
    *top = *below;
    *below = held;
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_pop(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    (void) insn;
    (void) cnor_vm_stack_pop(&vm->stack);
    return lh_bool_true;
}
