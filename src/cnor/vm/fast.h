/**
 * @file fast.h
 * @brief Inline value access and operand reads for the dispatch loop.
 *
 * lh's value accessors (`lh_value_get_kind`, `lh_value_make_sint`, ...) are
 * ordinary functions in another library, so every call costs a call and a
 * null check the loop does not need: its values always live in the machine's
 * own stack. These helpers touch the ::lh_value_fields members directly and are
 * meant for the hot path only; everything else keeps using the lh API.
 *
 * @note Candidate lh brick: header-inline variants of the value accessors.
 */

#ifndef CNOR_VM_FAST_H
#define CNOR_VM_FAST_H

#include <lh/bool.h>
#include <lh/byte.h>
#include <lh/compiler/attribute/force_inline.h>
#include <lh/compiler/type.h>
#include <lh/null.h>
#include <lh/numeric/fixed/types.h>
#include <lh/value.h>

/**
 * @def CNOR_VM_LIKELY(cond)
 * @brief Tell the compiler @p cond is usually true, so the common case stays on
 *        the straight path of the dispatch loop. Plain @p cond elsewhere.
 * @def CNOR_VM_UNLIKELY(cond)
 * @brief The opposite.
 * @def CNOR_VM_COLD
 * @brief Marks a function only failures and rare kinds reach.
 */
#if (LH_COMPILER_TYPE_IS_GCC_LIKE)
#    define CNOR_VM_LIKELY(cond) __builtin_expect(!!(cond), 1)
#    define CNOR_VM_UNLIKELY(cond) __builtin_expect(!!(cond), 0)
#    define CNOR_VM_COLD __attribute__((cold, noinline))
#else
#    define CNOR_VM_LIKELY(cond) (cond)
#    define CNOR_VM_UNLIKELY(cond) (cond)
#    define CNOR_VM_COLD
#endif

/** @brief What @p value holds. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_value_kind_t
cnor_vm_fast_kind(const lh_value_t *value)
{
    return value->kind;
}

/** @brief Whether both values are signed integers. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE int
cnor_vm_fast_both_sint(const lh_value_t *left, const lh_value_t *right)
{
    return (left->kind == lh_value_kind_sint) & (right->kind == lh_value_kind_sint);
}

/** @brief The signed integer @p value holds; its kind is known. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_sllong_t
cnor_vm_fast_sint(const lh_value_t *value)
{
    return value->data.i;
}

/** @brief Overwrite @p value with none. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_set_none(lh_value_t *value)
{
    value->kind = lh_value_kind_none;
    value->data.p = lh_null;
}

/** @brief Overwrite @p value with a boolean. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_set_bool(lh_value_t *value, int truth)
{
    value->kind = lh_value_kind_bool;
    value->data.b = truth ? lh_bool_true : lh_bool_false;
}

/** @brief Overwrite @p value with a signed integer. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_set_sint(lh_value_t *value, lh_sllong_t number)
{
    value->kind = lh_value_kind_sint;
    value->data.i = number;
}

/** @brief Overwrite @p value with an unsigned integer. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_set_uint(lh_value_t *value, lh_ullong_t number)
{
    value->kind = lh_value_kind_uint;
    value->data.u = number;
}

/**
 * @brief Truthiness of the common kinds without a call; anything else is
 *        decided by ::cnor_vm_value_is_truthy.
 * @return 1 or 0, or -1 for "ask the full function".
 */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE int
cnor_vm_fast_truthy(const lh_value_t *value)
{
    switch (value->kind)
    {
    case lh_value_kind_bool:
        return value->data.b ? 1 : 0;
    case lh_value_kind_sint:
        return value->data.i != 0 ? 1 : 0;
    case lh_value_kind_none:
        return 0;
    default:
        return -1;
    }
}

/** @brief Little-endian u16 operand at @p at. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_u32_t
cnor_vm_fast_u16(const lh_byte_t *at)
{
    return (lh_u32_t) at[0] | ((lh_u32_t) at[1] << 8);
}

/** @brief Little-endian u32 operand at @p at. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_u32_t
cnor_vm_fast_u32(const lh_byte_t *at)
{
    return (lh_u32_t) at[0] | ((lh_u32_t) at[1] << 8) | ((lh_u32_t) at[2] << 16)
           | ((lh_u32_t) at[3] << 24);
}

/** @brief Little-endian two's complement s32 operand at @p at, without
 *         implementation-defined conversions. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_sllong_t
cnor_vm_fast_s32(const lh_byte_t *at)
{
    const lh_u32_t bits = cnor_vm_fast_u32(at);

    return bits >= 0x80000000U ? -(lh_sllong_t) (0xFFFFFFFFU - bits) - 1 : (lh_sllong_t) bits;
}

#endif /* CNOR_VM_FAST_H */
