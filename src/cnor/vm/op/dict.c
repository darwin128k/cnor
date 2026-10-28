#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>

/* -- dictionaries ------------------------------------------------------------ */

/*
 * A dictionary holds pairs of a key and a value. The pairs are slots, two per
 * pair, and the dictionary's value (where they are) is in the slot before
 * them; the header is in the slot before that:
 *
 *     fp[slot]     the dictionary: where the header is
 *     fp[slot + 1] the header: how many pairs there are
 *     fp[slot + 2] the room: how many pairs may be there
 *     fp[slot + 3] the pairs: a key and a value each
 *
 * The header is not in the value but where the dictionary was made, so a
 * dictionary that was copied elsewhere (into a parameter, say) still shares
 * the count with the one it came from: what a function writes into it, the
 * caller sees.
 *
 * A key is found by comparing the texts. The pairs live where the dictionary
 * was made, so a dictionary is a local and does not outlive its frame; the
 * values it holds may be anything.
 */

/* The value of the key @p key in the dictionary @p dictionary, or ::lh_null
 * when the dictionary has no such key. */
static lh_value_t *
cnor_vm_dict_find(const lh_value_t *dictionary, const lh_value_t *key)
{
    if (cnor_vm_fast_kind(key) != lh_value_kind_str)
    {
        return lh_null;
    }
    return cnor_vm_fast_dict_find(dictionary, key);
}

/* The same for the dispatch loop, where the dictionary and the key are known
 * to be one and a text. It is a call of its own on purpose: inlined, the walk
 * takes registers the whole loop needs (on a 32-bit host it slowed every
 * workload, the ones without a dictionary too). */
lh_value_t *
cnor_vm_dict_lookup(const lh_value_t *dictionary, const lh_value_t *key)
{
    return cnor_vm_fast_dict_find(dictionary, key);
}

/* Write @p value under @p key in the dictionary @p dictionary, adding the key
 * when it is not there yet. */
static lh_bool_t
cnor_vm_dict_store(cnor_vm_t *vm, const cnor_insn_t *insn, lh_value_t *dictionary,
                   lh_value_t *key, const lh_value_t *value)
{
    lh_value_t *pairs;
    lh_u32_t count;
    lh_value_t *found;

    if (cnor_vm_fast_kind(dictionary) != CNOR_VALUE_KIND_DICT
        || cnor_vm_fast_kind(key) != lh_value_kind_str)
    {
        /* A key is a text; anything else cannot be looked up. */
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    pairs = cnor_vm_fast_dict_data(dictionary);
    count = cnor_vm_fast_dict_count(dictionary);
    found = cnor_vm_dict_find(dictionary, key);
    if (found == lh_null)
    {
        if (count >= cnor_vm_fast_dict_room(dictionary))
        {
            /* The dictionary is full: the key cannot be added. */
            return cnor_vm_fail(vm, insn, cnor_error_dict_full);
        }
        cnor_vm_fast_move(&pairs[count * 2U], key);
        found = &pairs[count * 2U + 1U];
        cnor_vm_fast_dict_set_count(dictionary, count + 1U);
    }
    cnor_vm_fast_move(found, value);
    return lh_bool_true;
}

/* Make a dictionary with room for `room` pairs at fp[slot + 3], its header at
 * fp[slot + 1] and its value in fp[slot]. The dictionary starts empty. */
lh_bool_t
cnor_vm_op_dict_new(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_u16_t room = (lh_u16_t) insn->operands[1];
    lh_value_t *const header = cnor_vm_stack_at(&vm->stack, vm->fp + slot + 1U);
    lh_value_t *const dictionary = cnor_vm_stack_at(&vm->stack, vm->fp + slot);

    cnor_vm_fast_set_sint(&header[0], 0);
    cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) room);
    cnor_vm_fast_set_dict(dictionary, header);
    return lh_bool_true;
}

/* The same, in the globals: the pairs outlive the frame that made them. */
lh_bool_t
cnor_vm_op_dict_new_global(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_u16_t slot = (lh_u16_t) insn->operands[0];
    const lh_u16_t room = (lh_u16_t) insn->operands[1];
    lh_value_t *const header = &vm->stack.values[slot + 1U];
    lh_value_t *const dictionary = &vm->stack.values[slot];

    cnor_vm_fast_set_sint(&header[0], 0);
    cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) room);
    cnor_vm_fast_set_dict(dictionary, header);
    return lh_bool_true;
}

/* The value of a key of the dictionary in fp[slot]: the key is on the stack. A
 * key that is not there gives `none`, and the run goes on. */
lh_bool_t
cnor_vm_op_dict_get_l(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_value_t *const dictionary = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_value_t *const key = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    const lh_value_t *value;

    if (cnor_vm_fast_kind(dictionary) != CNOR_VALUE_KIND_DICT
        || cnor_vm_fast_kind(key) != lh_value_kind_str)
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    value = cnor_vm_dict_find(dictionary, key);
    if (value == lh_null)
    {
        *key = lh_value_make_none();
        return lh_bool_true;
    }
    cnor_vm_fast_move(key, value);
    return lh_bool_true;
}

/* The value of a key of the dictionary in fp[slot]: the key is constant k. A
 * key that is not there gives `none`, and the run goes on. */
lh_bool_t
cnor_vm_op_dict_get_lk(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_value_t *const dictionary = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    /* The operand is an ordinal into the constant table, range-checked at load. */
    const lh_value_t *const key = &vm->constants[insn->operands[1]];
    const lh_value_t *value;

    if (cnor_vm_fast_kind(dictionary) != CNOR_VALUE_KIND_DICT
        || cnor_vm_fast_kind(key) != lh_value_kind_str)
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    value = cnor_vm_dict_find(dictionary, key);
    cnor_vm_stack_push(&vm->stack, value == lh_null ? lh_value_make_none() : *value);
    return lh_bool_true;
}

/* Write the value of a key of the dictionary in fp[slot]: the key is on the
 * stack, the value above it. */
lh_bool_t
cnor_vm_op_dict_set_l(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    lh_value_t *const dictionary = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_value_t *const key = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 2U);
    const lh_value_t *const value = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);

    if (!cnor_vm_dict_store(vm, insn, dictionary, key, value))
    {
        vm->stack.sp -= 2U;
        return lh_bool_false;
    }
    vm->stack.sp -= 2U;
    return lh_bool_true;
}

/* The value of a key: the dictionary and the key are on the stack. */
lh_bool_t
cnor_vm_op_dict_get(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t *const dictionary = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 2U);
    lh_value_t *const key = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    const lh_value_t *value;

    if (cnor_vm_fast_kind(dictionary) != CNOR_VALUE_KIND_DICT
        || cnor_vm_fast_kind(key) != lh_value_kind_str)
    {
        vm->stack.sp -= 2U;
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    value = cnor_vm_dict_find(dictionary, key);
    vm->stack.sp -= 2U;
    if (value == lh_null)
    {
        cnor_vm_stack_push(&vm->stack, lh_value_make_none());
        return lh_bool_true;
    }
    cnor_vm_stack_push(&vm->stack, *value);
    return lh_bool_true;
}

/* Write the value of a key: the dictionary, the key and the value are on the
 * stack. */
lh_bool_t
cnor_vm_op_dict_set(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *const dictionary = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 3U);
    lh_value_t *const key = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 2U);
    const lh_value_t *const value = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);

    if (!cnor_vm_dict_store(vm, insn, dictionary, key, value))
    {
        vm->stack.sp -= 3U;
        return lh_bool_false;
    }
    vm->stack.sp -= 3U;
    return lh_bool_true;
}

/* The key at an index of the dictionary in fp[slot]: the index is on the
 * stack, and the key takes its place. */
lh_bool_t
cnor_vm_op_dict_key_l(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_slot_t slot = (cnor_slot_t) insn->operands[0];
    const lh_value_t *const dictionary = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_value_t *const index = cnor_vm_stack_at(&vm->stack, vm->stack.sp - 1U);
    lh_sllong_t at;

    if (cnor_vm_fast_kind(dictionary) != CNOR_VALUE_KIND_DICT
        || (cnor_vm_fast_kind(index) != lh_value_kind_sint
            && cnor_vm_fast_kind(index) != lh_value_kind_uint))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    at = cnor_vm_value_to_sint(index);
    if (at < 0 || (lh_ullong_t) at >= cnor_vm_fast_dict_count(dictionary))
    {
        return cnor_vm_fail(vm, insn, cnor_error_index);
    }
    cnor_vm_fast_move(index, &cnor_vm_fast_dict_data(dictionary)[at * 2]);
    return lh_bool_true;
}
