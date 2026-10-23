#include <cnor/vm/internal.h>
#include <lh/memory.h>

lh_bool_t
cnor_vm_text_join(cnor_vm_t *vm, const cnor_insn_t *insn, const lh_value_t *left,
                  const lh_value_t *right)
{
    const lh_str_view_t a = lh_value_get_data(left)->s;
    const lh_str_view_t b = lh_value_get_data(right)->s;
    const lh_usize_t a_size = lh_str_view_get_size(&a);
    const lh_usize_t b_size = lh_str_view_get_size(&b);
    lh_char_t *out;
    lh_str_view_t joined;

    if (a_size + b_size == 0U)
    {
        cnor_vm_stack_push(&vm->stack, *left);
        return lh_bool_true;
    }
    out = (a_size + b_size) > 0xFFFFFFFFU
              ? lh_null
              : cnor_str_pool_take(&vm->str_pool, (lh_u32_t) (a_size + b_size));
    if (out == lh_null)
    {
        return cnor_vm_fail(vm, insn, cnor_error_str_pool_full);
    }
    lh_memory_copy(out, a_size, lh_str_view_get_data(&a), a_size);
    lh_memory_copy(out + a_size, b_size, lh_str_view_get_data(&b), b_size);
    lh_str_init_by_size(&joined, out, a_size + b_size);
    cnor_vm_stack_push(&vm->stack, lh_value_make_str(joined));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_len(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *top = cnor_vm_stack_peek(&vm->stack, 0U);
    lh_str_view_t text;

    if (lh_value_get_kind(top) != lh_value_kind_str)
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    text = lh_value_get_data(top)->s;
    *top = lh_value_make_sint((lh_sllong_t) lh_str_view_get_size(&text));
    return lh_bool_true;
}

/* `text[i]` is a one-character string; it shares the text's storage. */
cnor_error_t
cnor_vm_text_index(lh_value_t *text, const lh_value_t *index)
{
    lh_str_view_t whole;
    lh_str_view_t single;
    lh_sllong_t at;

    if (lh_value_get_kind(text) != lh_value_kind_str
        || (lh_value_get_kind(index) != lh_value_kind_sint
            && lh_value_get_kind(index) != lh_value_kind_uint))
    {
        return cnor_error_type;
    }
    whole = lh_value_get_data(text)->s;
    at = cnor_vm_value_to_sint(index);
    if (at < 0 || (lh_ullong_t) at >= (lh_ullong_t) lh_str_view_get_size(&whole))
    {
        return cnor_error_index;
    }
    lh_str_init_by_size(&single, lh_str_view_get_data(&whole) + at, 1U);
    *text = lh_value_make_str(single);
    return cnor_error_none;
}

lh_bool_t
cnor_vm_op_index(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t index = cnor_vm_stack_pop(&vm->stack);
    const cnor_error_t error = cnor_vm_text_index(cnor_vm_stack_peek(&vm->stack, 0U), &index);

    return error == cnor_error_none ? lh_bool_true : cnor_vm_fail(vm, insn, error);
}
