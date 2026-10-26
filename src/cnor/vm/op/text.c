#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>

lh_bool_t
cnor_vm_text_join(cnor_vm_t *vm, const cnor_insn_t *insn, const lh_value_t *left,
                  const lh_value_t *right, lh_value_t *out)
{
    return cnor_vm_fast_join(&vm->str_pool, out, left, right)
               ? lh_bool_true
               : cnor_vm_fail(vm, insn, cnor_error_str_pool_full);
}

lh_bool_t
cnor_vm_op_len(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *top = cnor_vm_stack_peek(&vm->stack, 0U);

    if (cnor_vm_fast_kind(top) == lh_value_kind_str)
    {
        cnor_vm_fast_set_sint(top, (lh_sllong_t) cnor_vm_fast_str_size(top));
        return lh_bool_true;
    }
    if (cnor_vm_fast_kind(top) == CNOR_VALUE_KIND_ARRAY)
    {
        cnor_vm_fast_set_sint(top, (lh_sllong_t) cnor_vm_fast_array_count(top));
        return lh_bool_true;
    }
    return cnor_vm_fail(vm, insn, cnor_error_type);
}

/* `text[i]` is a one-character string; it shares the text's storage. */
cnor_error_t
cnor_vm_text_index(lh_value_t *text, const lh_value_t *index)
{
    lh_sllong_t at;

    if (cnor_vm_fast_kind(text) != lh_value_kind_str
        || (cnor_vm_fast_kind(index) != lh_value_kind_sint
            && cnor_vm_fast_kind(index) != lh_value_kind_uint))
    {
        return cnor_error_type;
    }
    at = cnor_vm_value_to_sint(index);
    if (at < 0 || (lh_ullong_t) at >= (lh_ullong_t) cnor_vm_fast_str_size(text))
    {
        return cnor_error_index;
    }
    cnor_vm_fast_set_str(text, cnor_vm_fast_str_data(text) + at, 1U);
    return cnor_error_none;
}

lh_bool_t
cnor_vm_op_index(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t index = cnor_vm_stack_pop(&vm->stack);
    const cnor_error_t error = cnor_vm_text_index(cnor_vm_stack_peek(&vm->stack, 0U), &index);

    return error == cnor_error_none ? lh_bool_true : cnor_vm_fail(vm, insn, error);
}

/*
 * The index forms (`index_ll`, `index_li`, `index_i`): fetch the text and the
 * index where the form says, then apply the plain index's rule, so a form
 * computes and fails exactly as `load_local` / `push_i32` and the plain opcode
 * would.
 */
lh_bool_t
cnor_vm_op_index_operands(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t text;
    lh_value_t index;
    cnor_error_t error;

    switch (insn->op)
    {
    case cnor_op_index_ll:
        text = *cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[0]);
        index = *cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[1]);
        break;
    case cnor_op_index_li:
        text = *cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[0]);
        index = lh_value_make_sint((lh_sllong_t) (lh_s32_t) insn->operands[1]);
        break;
    default:
        /* `index_i`: the text is on the stack, and the result takes its place. */
        text = cnor_vm_stack_pop(&vm->stack);
        index = lh_value_make_sint((lh_sllong_t) (lh_s32_t) insn->operands[0]);
        break;
    }
    error = cnor_vm_text_index(&text, &index);
    if (error != cnor_error_none)
    {
        return cnor_vm_fail(vm, insn, error);
    }
    cnor_vm_stack_push(&vm->stack, text);
    return lh_bool_true;
}
