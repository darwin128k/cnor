#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>

#if LH_REAL_ENABLED
lh_real_t
cnor_vm_value_to_real(const lh_value_t *value)
{
    switch (lh_value_get_kind(value))
    {
    case lh_value_kind_real:
        return lh_value_get_data(value)->r;
    case lh_value_kind_uint:
        return (lh_real_t) lh_value_get_data(value)->u;
    default:
        return (lh_real_t) lh_value_get_data(value)->i;
    }
}
#endif

lh_bool_t
cnor_vm_value_is_number(const lh_value_t *value)
{
    const lh_value_kind_t kind = lh_value_get_kind(value);

    return (kind == lh_value_kind_sint || kind == lh_value_kind_uint
            || kind == lh_value_kind_real)
               ? lh_bool_true
               : lh_bool_false;
}

lh_bool_t
cnor_vm_value_is_truthy(const lh_value_t *value)
{
    switch (lh_value_get_kind(value))
    {
    case lh_value_kind_bool:
        return lh_value_get_data(value)->b;
    case lh_value_kind_sint:
        return lh_value_get_data(value)->i != 0 ? lh_bool_true : lh_bool_false;
    case lh_value_kind_uint:
        return lh_value_get_data(value)->u != 0U ? lh_bool_true : lh_bool_false;
#if LH_REAL_ENABLED
    case lh_value_kind_real:
        return lh_value_get_data(value)->r != (lh_real_t) 0 ? lh_bool_true : lh_bool_false;
#endif
    case lh_value_kind_str:
    {
        const lh_str_view_t text = lh_value_get_data(value)->s;

        return lh_str_view_get_size(&text) != 0U ? lh_bool_true : lh_bool_false;
    }
    case lh_value_kind_ptr:
        return lh_value_get_data(value)->p != lh_null ? lh_bool_true : lh_bool_false;
    default:
        return lh_bool_false;
    }
}

lh_sllong_t
cnor_vm_value_to_sint(const lh_value_t *value)
{
    return lh_value_get_kind(value) == lh_value_kind_uint
               ? (lh_sllong_t) lh_value_get_data(value)->u
               : lh_value_get_data(value)->i;
}

/* A negative signed value is below every unsigned one; otherwise both fit an
 * unsigned comparison, which keeps unsigned values above 2^63 in order. */
int
cnor_vm_value_compare(const lh_value_t *left, const lh_value_t *right)
{
    const lh_value_kind_t left_kind = lh_value_get_kind(left);
    const lh_value_kind_t right_kind = lh_value_get_kind(right);
    lh_ullong_t a;
    lh_ullong_t b;

    if (left_kind == lh_value_kind_sint && right_kind == lh_value_kind_sint)
    {
        const lh_sllong_t x = lh_value_get_data(left)->i;
        const lh_sllong_t y = lh_value_get_data(right)->i;

        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (left_kind == lh_value_kind_sint && lh_value_get_data(left)->i < 0)
    {
        return -1;
    }
    if (right_kind == lh_value_kind_sint && lh_value_get_data(right)->i < 0)
    {
        return 1;
    }
    a = left_kind == lh_value_kind_uint ? lh_value_get_data(left)->u
                                        : (lh_ullong_t) lh_value_get_data(left)->i;
    b = right_kind == lh_value_kind_uint ? lh_value_get_data(right)->u
                                         : (lh_ullong_t) lh_value_get_data(right)->i;
    return a < b ? -1 : (a > b ? 1 : 0);
}

#if LH_REAL_ENABLED
static lh_bool_t
cnor_vm_value_has_real(const lh_value_t *left, const lh_value_t *right)
{
    return (lh_value_get_kind(left) == lh_value_kind_real
            || lh_value_get_kind(right) == lh_value_kind_real)
               ? lh_bool_true
               : lh_bool_false;
}
#endif

/* Reals compare with the C operators, so NaN is unordered: only `!=` holds. */
lh_bool_t
cnor_vm_value_order_holds(const lh_value_t *left, const lh_value_t *right, cnor_op_t op)
{
    int order;

#if LH_REAL_ENABLED
    if (cnor_vm_value_has_real(left, right))
    {
        const lh_real_t a = cnor_vm_value_to_real(left);
        const lh_real_t b = cnor_vm_value_to_real(right);

        switch (op)
        {
        case cnor_op_lt:
            return a < b ? lh_bool_true : lh_bool_false;
        case cnor_op_le:
            return a <= b ? lh_bool_true : lh_bool_false;
        case cnor_op_gt:
            return a > b ? lh_bool_true : lh_bool_false;
        default:
            return a >= b ? lh_bool_true : lh_bool_false;
        }
    }
#endif
    order = cnor_vm_value_compare(left, right);
    switch (op)
    {
    case cnor_op_lt:
        return order < 0 ? lh_bool_true : lh_bool_false;
    case cnor_op_le:
        return order <= 0 ? lh_bool_true : lh_bool_false;
    case cnor_op_gt:
        return order > 0 ? lh_bool_true : lh_bool_false;
    default:
        return order >= 0 ? lh_bool_true : lh_bool_false;
    }
}

lh_bool_t
cnor_vm_value_equals(const lh_value_t *left, const lh_value_t *right)
{
    if (cnor_vm_value_is_number(left) && cnor_vm_value_is_number(right))
    {
#if LH_REAL_ENABLED
        if (cnor_vm_value_has_real(left, right))
        {
            return cnor_vm_value_to_real(left) == cnor_vm_value_to_real(right) ? lh_bool_true
                                                                               : lh_bool_false;
        }
#endif
        return cnor_vm_value_compare(left, right) == 0 ? lh_bool_true : lh_bool_false;
    }
    if (cnor_vm_fast_both_str(left, right))
    {
        return cnor_vm_fast_str_equals(left, right) ? lh_bool_true : lh_bool_false;
    }
    return lh_value_equals(left, right);
}
