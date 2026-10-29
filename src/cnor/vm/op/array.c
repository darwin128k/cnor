#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>

/* -- arrays ---------------------------------------------------------------- */

/*
 * An array holds elements that are slots of the frame. Its header is in the
 * slot before them, and the array's value (where the header is) in the slot
 * before that:
 *
 *     fp[slot]     the array: where the header is
 *     fp[slot + 1] the header: how many elements there are
 *     fp[slot + 2] the room: how many elements may be there
 *     fp[slot + 3] the elements
 *
 * The header is not in the value but where the array was made, so an array
 * that was copied elsewhere (into a parameter, say) still shares the count
 * with the one it came from: what a function pushes into it, the caller sees.
 *
 * The array forms (`array_new`, `array_get`, `array_set`): the elements are
 * slots of the frame, and the array's value (where they are and how many) is
 * in the slot before them. The fast path in the dispatch loop does the same
 * work; these handlers take the failures: an index outside the array, and a
 * value that is no array at all.
 */

/* Make an array of `count` elements at fp[slot + 3], its header at
 * fp[slot + 1] and its value in fp[slot]. The value every element holds is on
 * top of the stack. */
lh_bool_t
cnor_vm_op_array_new(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_u16_t count = (lh_u16_t) insn->operands[1];
    lh_value_t *const header = cnor_vm_stack_at(&vm->stack, vm->fp + slot + 1U);
    lh_value_t *const elements = cnor_vm_stack_at(&vm->stack, vm->fp + slot + 3U);
    lh_value_t *const fill = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    lh_value_t *const array = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_u16_t i;

    for (i = 0U; i < count; ++i)
    {
        cnor_vm_fast_move(&elements[i], fill);
    }
    cnor_vm_fast_set_sint(&header[0], (lh_sllong_t) count);
    cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) count);
    cnor_vm_fast_set_array(array, header);
    vm->stack.sp -= 1U;
    return lh_bool_true;
}

/* Make an empty array with room for `room` elements at fp[slot + 3], its
 * header at fp[slot + 1] and its value in fp[slot]. The elements are `none`
 * until something is pushed into them. */
lh_bool_t
cnor_vm_op_array_room(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_u16_t room = (lh_u16_t) insn->operands[1];
    lh_value_t *const header = cnor_vm_stack_at(&vm->stack, vm->fp + slot + 1U);
    lh_value_t *const array = cnor_vm_stack_at(&vm->stack, vm->fp + slot);

    cnor_vm_fast_set_sint(&header[0], 0);
    cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) room);
    cnor_vm_fast_set_array(array, header);
    return lh_bool_true;
}

/* Make an array of `count` elements in the globals: they live in the globals
 * area, so they outlive the frame that made them. The value every element
 * holds is on top of the stack. */
lh_bool_t
cnor_vm_op_array_new_global(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_u16_t slot = (lh_u16_t) insn->operands[0];
    const lh_u16_t count = (lh_u16_t) insn->operands[1];
    lh_value_t *const header = &vm->stack.values[slot + 1U];
    lh_value_t *const elements = &vm->stack.values[slot + 3U];
    lh_value_t *const array = &vm->stack.values[slot];
    const lh_value_t *const fill = cnor_vm_stack_peek(&vm->stack, 0U);
    lh_u16_t i;

    for (i = 0U; i < count; ++i)
    {
        cnor_vm_fast_move(&elements[i], fill);
    }
    cnor_vm_fast_set_sint(&header[0], (lh_sllong_t) count);
    cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) count);
    cnor_vm_fast_set_array(array, header);
    vm->stack.sp -= 1U;
    return lh_bool_true;
}

/* Make an empty array with room for `room` elements in the globals. */
lh_bool_t
cnor_vm_op_array_room_global(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_u16_t slot = (lh_u16_t) insn->operands[0];
    const lh_u16_t room = (lh_u16_t) insn->operands[1];
    lh_value_t *const header = &vm->stack.values[slot + 1U];
    lh_value_t *const array = &vm->stack.values[slot];

    cnor_vm_fast_set_sint(&header[0], 0);
    cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) room);
    cnor_vm_fast_set_array(array, header);
    return lh_bool_true;
}

/* An element of fp[slot], at the index on top of the stack. */
lh_bool_t
cnor_vm_op_array_get_l(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_value_t *const array = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_value_t *const index = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    lh_sllong_t at;

    if (cnor_vm_fast_kind(array) != CNOR_VALUE_KIND_ARRAY
        || (cnor_vm_fast_kind(index) != lh_value_kind_sint
            && cnor_vm_fast_kind(index) != lh_value_kind_uint))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    at = cnor_vm_value_to_sint(index);
    if (at < 0 || (lh_ullong_t) at >= cnor_vm_fast_array_count(array))
    {
        return cnor_vm_fail(vm, insn, cnor_error_index);
    }
    cnor_vm_fast_move(index, &cnor_vm_fast_array_data(array)[at]);
    return lh_bool_true;
}

/* Write an element of fp[slot]: the index and the value are on the stack. */
lh_bool_t
cnor_vm_op_array_set_l(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_value_t *const array = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_value_t *const index = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 2U);
    const lh_value_t *const value = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    lh_sllong_t at;

    if (cnor_vm_fast_kind(array) != CNOR_VALUE_KIND_ARRAY
        || (cnor_vm_fast_kind(index) != lh_value_kind_sint
            && cnor_vm_fast_kind(index) != lh_value_kind_uint))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    at = cnor_vm_value_to_sint(index);
    if (at < 0 || (lh_ullong_t) at >= cnor_vm_fast_array_count(array))
    {
        return cnor_vm_fail(vm, insn, cnor_error_index);
    }
    cnor_vm_fast_move(&cnor_vm_fast_array_data(array)[at], value);
    vm->stack.sp -= 2U;
    return lh_bool_true;
}

/* An element of the array on the stack: the array, then the index. The result
 * takes the index's place. */
lh_bool_t
cnor_vm_op_array_get(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t *const array = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 2U);
    lh_value_t *const index = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    lh_sllong_t at;

    if (cnor_vm_fast_kind(array) != CNOR_VALUE_KIND_ARRAY
        || (cnor_vm_fast_kind(index) != lh_value_kind_sint
            && cnor_vm_fast_kind(index) != lh_value_kind_uint))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    at = cnor_vm_value_to_sint(index);
    if (at < 0 || (lh_ullong_t) at >= cnor_vm_fast_array_count(array))
    {
        return cnor_vm_fail(vm, insn, cnor_error_index);
    }
    vm->stack.sp -= 2U;
    cnor_vm_stack_push(&vm->stack, cnor_vm_fast_array_data(array)[at]);
    return lh_bool_true;
}

/* Write an element of the array on the stack: the array, the index and the
 * value. */
lh_bool_t
cnor_vm_op_array_set(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t *const array = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 3U);
    const lh_value_t *const index = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 2U);
    const lh_value_t *const value = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    lh_sllong_t at;

    if (cnor_vm_fast_kind(array) != CNOR_VALUE_KIND_ARRAY
        || (cnor_vm_fast_kind(index) != lh_value_kind_sint
            && cnor_vm_fast_kind(index) != lh_value_kind_uint))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    at = cnor_vm_value_to_sint(index);
    if (at < 0 || (lh_ullong_t) at >= cnor_vm_fast_array_count(array))
    {
        return cnor_vm_fail(vm, insn, cnor_error_index);
    }
    cnor_vm_fast_move(&cnor_vm_fast_array_data(array)[at], value);
    vm->stack.sp -= 3U;
    return lh_bool_true;
}
