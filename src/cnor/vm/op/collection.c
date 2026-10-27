#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>

/* -- collections ------------------------------------------------------------- */

/*
 * A collection whose kind is not known when the code is written: an element
 * of an array, or a key of a dictionary. The machine tells them apart, and the
 * compiler picks the cheaper instruction when it knows the kind.
 */

/* The element of the array or the key of the dictionary in fp[slot], at the
 * index on the stack. The index takes the element's place. */
lh_bool_t
cnor_vm_op_collection_get_l(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_value_t *const collection = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_value_t *const index = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    lh_sllong_t at;

    if (cnor_vm_fast_kind(index) != lh_value_kind_sint
        && cnor_vm_fast_kind(index) != lh_value_kind_uint)
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    at = cnor_vm_value_to_sint(index);
    if (cnor_vm_fast_kind(collection) == CNOR_VALUE_KIND_ARRAY)
    {
        if (at < 0 || (lh_ullong_t) at >= cnor_vm_fast_array_count(collection))
        {
            return cnor_vm_fail(vm, insn, cnor_error_index);
        }
        cnor_vm_fast_move(index, &cnor_vm_fast_array_data(collection)[at]);
        return lh_bool_true;
    }
    if (cnor_vm_fast_kind(collection) == CNOR_VALUE_KIND_DICT)
    {
        if (at < 0 || (lh_ullong_t) at >= cnor_vm_fast_dict_count(collection))
        {
            return cnor_vm_fail(vm, insn, cnor_error_index);
        }
        cnor_vm_fast_move(index, &cnor_vm_fast_dict_data(collection)[at * 2]);
        return lh_bool_true;
    }
    /* Neither an array nor a dictionary. */
    return cnor_vm_fail(vm, insn, cnor_error_type);
}
