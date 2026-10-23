#include <cnor/vm/internal.h>

/* -- operands -------------------------------------------------------------- */

/* The signed 32-bit value whose two's complement bits are @p bits. */
static lh_value_t
cnor_vm_operand_int(lh_u32_t bits)
{
    return lh_value_make_sint(bits >= 0x80000000U ? -(lh_sllong_t) (0xFFFFFFFFU - bits) - 1
                                                  : (lh_sllong_t) bits);
}

static lh_value_t
cnor_vm_operand_local(cnor_vm_t *vm, lh_u32_t slot)
{
    return *cnor_vm_stack_at(&vm->stack, vm->fp + slot);
}

/* -- handler --------------------------------------------------------------- */

/*
 * Every operand form (`add_ll`, `lt_li`, `mod_i`, ...): fetch the operands
 * where the form says, then apply the plain operator's rule to them, so a form
 * computes and fails exactly as `load_local` / `push_i32` and the plain
 * opcode would.
 */
lh_bool_t
cnor_vm_op_operands(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_operand_form_t form;
    const cnor_op_t op = cnor_op_get_binary_base(insn->op, &form);
    lh_value_t left;
    lh_value_t right;
    lh_value_t result;
    lh_bool_t ok;

    switch (form)
    {
    case cnor_operand_form_local_local:
        left = cnor_vm_operand_local(vm, insn->operands[0]);
        right = cnor_vm_operand_local(vm, insn->operands[1]);
        break;
    case cnor_operand_form_local_int:
        left = cnor_vm_operand_local(vm, insn->operands[0]);
        right = cnor_vm_operand_int(insn->operands[1]);
        break;
    default:
        left = cnor_vm_stack_pop(&vm->stack);
        right = cnor_vm_operand_int(insn->operands[0]);
        break;
    }
    ok = (op >= cnor_op_eq && op <= cnor_op_ge)
             ? cnor_vm_compare_values(vm, insn, op, &left, &right, &result)
             : cnor_vm_arith_values(vm, insn, op, &left, &right, &result);
    if (!ok)
    {
        return lh_bool_false;
    }
    cnor_vm_stack_push(&vm->stack, result);
    return lh_bool_true;
}
