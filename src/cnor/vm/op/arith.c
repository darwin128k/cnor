#include <cnor/vm/internal.h>

/* -- reals ----------------------------------------------------------------- */

#if LH_REAL_ENABLED
/* Remainder truncated toward zero, like C's fmod, without libm. A NaN operand
 * or an infinite dividend gives NaN and an infinite divisor gives the dividend,
 * as fmod does; quotients too large for an integer leave no meaningful
 * remainder at this precision and give 0. The quotient is range-checked before
 * the conversion, which is undefined for NaN and out-of-range values. */
static lh_real_t
cnor_vm_real_mod(lh_real_t a, lh_real_t b)
{
    const lh_real_t q = a / b;
    const lh_real_t limit = (lh_real_t) 9.0e15;
    lh_sllong_t whole;

    if (!(q < limit && q > -limit))
    {
        return (a - a) + (b - b);
    }
    whole = (lh_sllong_t) q;
    return whole == 0 ? a : a - b * (lh_real_t) whole;
}

static lh_bool_t
cnor_vm_arith_real(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_op_t op, lh_real_t a, lh_real_t b,
                   lh_value_t *out)
{
    lh_real_t value;

    if ((op == cnor_op_div || op == cnor_op_mod) && b == (lh_real_t) 0)
    {
        return cnor_vm_fail(vm, insn, cnor_error_div_by_zero);
    }
    switch (op)
    {
    case cnor_op_add:
        value = a + b;
        break;
    case cnor_op_sub:
        value = a - b;
        break;
    case cnor_op_mul:
        value = a * b;
        break;
    case cnor_op_div:
        value = a / b;
        break;
    default:
        value = cnor_vm_real_mod(a, b);
        break;
    }
    *out = lh_value_make_real(value);
    return lh_bool_true;
}
#endif

/* -- integers -------------------------------------------------------------- */

/* Two unsigned operands stay unsigned, except a subtraction that goes below zero. */
static lh_value_t
cnor_vm_arith_uint(cnor_op_t op, lh_ullong_t a, lh_ullong_t b)
{
    lh_ullong_t value;

    switch (op)
    {
    case cnor_op_add:
        value = a + b;
        break;
    case cnor_op_sub:
        if (a < b)
        {
            /* Negated through unsigned: b - a may not fit a signed value. */
            return lh_value_make_sint((lh_sllong_t) (0U - (b - a)));
        }
        value = a - b;
        break;
    case cnor_op_mul:
        value = a * b;
        break;
    case cnor_op_div:
        value = a / b;
        break;
    default:
        value = a % b;
        break;
    }
    return lh_value_make_uint(value);
}

/* Signed arithmetic wraps through unsigned so overflow is defined. */
static lh_value_t
cnor_vm_arith_sint(cnor_op_t op, lh_sllong_t a, lh_sllong_t b)
{
    const lh_ullong_t ua = (lh_ullong_t) a;
    const lh_ullong_t ub = (lh_ullong_t) b;
    lh_sllong_t value;

    switch (op)
    {
    case cnor_op_add:
        value = (lh_sllong_t) (ua + ub);
        break;
    case cnor_op_sub:
        value = (lh_sllong_t) (ua - ub);
        break;
    case cnor_op_mul:
        value = (lh_sllong_t) (ua * ub);
        break;
    case cnor_op_div:
        value = b == -1 ? (lh_sllong_t) (0U - ua) : a / b;
        break;
    default:
        value = b == -1 ? 0 : a % b;
        break;
    }
    return lh_value_make_sint(value);
}

static lh_bool_t
cnor_vm_arith_numbers(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_op_t op,
                      const lh_value_t *left, const lh_value_t *right, lh_value_t *out)
{
#if LH_REAL_ENABLED
    if (lh_value_get_kind(left) == lh_value_kind_real
        || lh_value_get_kind(right) == lh_value_kind_real)
    {
        return cnor_vm_arith_real(vm, insn, op, cnor_vm_value_to_real(left),
                                  cnor_vm_value_to_real(right), out);
    }
#endif
    if ((op == cnor_op_div || op == cnor_op_mod) && cnor_vm_value_to_sint(right) == 0)
    {
        return cnor_vm_fail(vm, insn, cnor_error_div_by_zero);
    }
    if (lh_value_get_kind(left) == lh_value_kind_uint
        && lh_value_get_kind(right) == lh_value_kind_uint)
    {
        *out = cnor_vm_arith_uint(op, lh_value_get_data(left)->u, lh_value_get_data(right)->u);
        return lh_bool_true;
    }
    /* Mixed kinds compute signed: a u32 operand always fits. */
    *out = cnor_vm_arith_sint(op, cnor_vm_value_to_sint(left), cnor_vm_value_to_sint(right));
    return lh_bool_true;
}

lh_bool_t
cnor_vm_arith_values(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_op_t op,
                     const lh_value_t *left, const lh_value_t *right, lh_value_t *out)
{
    if (op == cnor_op_add && lh_value_get_kind(left) == lh_value_kind_str
        && lh_value_get_kind(right) == lh_value_kind_str)
    {
        return cnor_vm_text_join(vm, insn, left, right, out);
    }
    if (!cnor_vm_value_is_number(left) || !cnor_vm_value_is_number(right))
    {
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
    return cnor_vm_arith_numbers(vm, insn, op, left, right, out);
}

/* -- handlers -------------------------------------------------------------- */

lh_bool_t
cnor_vm_op_arith(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t right = cnor_vm_stack_pop(&vm->stack);
    const lh_value_t left = cnor_vm_stack_pop(&vm->stack);
    lh_value_t result;

    if (!cnor_vm_arith_values(vm, insn, insn->op, &left, &right, &result))
    {
        return lh_bool_false;
    }
    cnor_vm_stack_push(&vm->stack, result);
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_neg(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *top = cnor_vm_stack_peek(&vm->stack, 0U);

    switch (lh_value_get_kind(top))
    {
    case lh_value_kind_sint:
        *top = lh_value_make_sint((lh_sllong_t) (0U - (lh_ullong_t) lh_value_get_data(top)->i));
        return lh_bool_true;
    case lh_value_kind_uint:
        *top = lh_value_make_sint((lh_sllong_t) (0U - lh_value_get_data(top)->u));
        return lh_bool_true;
#if LH_REAL_ENABLED
    case lh_value_kind_real:
        *top = lh_value_make_real(-lh_value_get_data(top)->r);
        return lh_bool_true;
#endif
    default:
        return cnor_vm_fail(vm, insn, cnor_error_type);
    }
}
