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
#include <cnor/types.h>
#include <cnor/vm/str_pool.h>
#include <lh/bit/endian.h>
#include <lh/compiler/type.h>
#include <lh/memory/std.h>
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

/**
 * @brief Copy @p from into @p to, field by field: the kind, the first word of
 *        the storage, and for a string its length, the only storage beyond
 *        8 bytes. A value is usually read back right after an instruction
 *        wrote it the same way (a kind, an 8-byte number); loads no wider than
 *        those stores are forwarded from them, while the 16-byte block loads
 *        of a struct copy wait for the stores to retire.
 */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_move(lh_value_t *to, const lh_value_t *from)
{
    const lh_value_kind_t kind = from->kind;

    to->kind = kind;
    to->data.u = from->data.u;
    if (kind == lh_value_kind_str || kind == CNOR_VALUE_KIND_ARRAY)
    {
        to->data.s.size = from->data.s.size;
    }
}

/** @brief Overwrite @p value with an unsigned integer. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_set_uint(lh_value_t *value, lh_ullong_t number)
{
    value->kind = lh_value_kind_uint;
    value->data.u = number;
}

/** @brief Whether both values are strings. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE int
cnor_vm_fast_both_str(const lh_value_t *left, const lh_value_t *right)
{
    return (left->kind == lh_value_kind_str) & (right->kind == lh_value_kind_str);
}

/** @brief First byte of the string @p value holds; its kind is known. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE const lh_char_t *
cnor_vm_fast_str_data(const lh_value_t *value)
{
    return (const lh_char_t *) value->data.s.begin;
}

/** @brief Length in bytes of the string @p value holds; its kind is known. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_usize_t
cnor_vm_fast_str_size(const lh_value_t *value)
{
    return value->data.s.size;
}

/**
 * @brief Overwrite @p value with the string of @p size bytes at @p data; the
 *        same value `lh_value_make_str` builds, without the 24-byte return.
 */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_set_str(lh_value_t *value, const lh_char_t *data, lh_usize_t size)
{
    value->kind = lh_value_kind_str;
    value->data.s.begin = data;
    value->data.s.size = size;
}

/** @brief The elements of the array @p value holds; its kind is known. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_value_t *
cnor_vm_fast_array_data(const lh_value_t *value)
{
    return (lh_value_t *) (void *) (const void *) value->data.s.begin;
}

/** @brief How many elements the array @p value holds; its kind is known. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_u32_t
cnor_vm_fast_array_count(const lh_value_t *value)
{
    return (lh_u32_t) value->data.s.size;
}

/** @brief Overwrite @p value with the array of @p count elements at @p data. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_set_array(lh_value_t *value, lh_value_t *data, lh_u32_t count)
{
    value->kind = CNOR_VALUE_KIND_ARRAY;
    value->data.s.begin = (const lh_char_t *) (const void *) data;
    value->data.s.size = (lh_usize_t) count;
}

/** @def CNOR_VM_FAST_LONG_TEXT @brief Beyond this many bytes a call into lh's
 *       block compare and copy beats the inline word loop. */
#define CNOR_VM_FAST_LONG_TEXT 64U

/** @brief The 8 bytes at @p at as one word, at any alignment. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_u64_t
cnor_vm_fast_word(const lh_char_t *at)
{
    return lh_bit_unpack_le64((const lh_byte_ptr) at);
}

/** @brief The 4 bytes at @p at as one word, at any alignment. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE lh_u32_t
cnor_vm_fast_half_word(const lh_char_t *at)
{
    return lh_bit_unpack_le32((const lh_byte_ptr) at);
}

/**
 * @brief Whether the @p size bytes at @p a and @p b are the same; @p size is
 *        at least 1. The last word is compared first (keys often share a
 *        prefix and differ at the end), then the rest a word at a time, the
 *        words overlapping rather than running a byte loop over a tail.
 */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE int
cnor_vm_fast_bytes_equal(const lh_char_t *a, const lh_char_t *b, lh_usize_t size)
{
    lh_usize_t i;

    if (size >= 8U)
    {
        if (cnor_vm_fast_word(a + size - 8U) != cnor_vm_fast_word(b + size - 8U))
        {
            return 0;
        }
        if (size > CNOR_VM_FAST_LONG_TEXT)
        {
            return lh_memory_std_compare((lh_ptr) a, (lh_ptr) b, size - 8U) == lh_null ? 1 : 0;
        }
        for (i = 0U; i + 8U < size; i += 8U)
        {
            if (cnor_vm_fast_word(a + i) != cnor_vm_fast_word(b + i))
            {
                return 0;
            }
        }
        return 1;
    }
    if (size >= 4U)
    {
        return cnor_vm_fast_half_word(a + size - 4U) == cnor_vm_fast_half_word(b + size - 4U)
               && cnor_vm_fast_half_word(a) == cnor_vm_fast_half_word(b);
    }
    /* 1 to 3 bytes: first, middle and last cover them all. */
    return a[size - 1U] == b[size - 1U] && a[0] == b[0] && a[size / 2U] == b[size / 2U];
}

/**
 * @brief Whether two strings hold the same bytes; the rule of
 *        `lh_value_equals` for two strings (case-sensitive, length first),
 *        without its call chain. The same view, or two empty ones, is equal
 *        without reading a byte.
 */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE int
cnor_vm_fast_str_equals(const lh_value_t *left, const lh_value_t *right)
{
    const lh_usize_t size = cnor_vm_fast_str_size(left);
    const lh_char_t *const a = cnor_vm_fast_str_data(left);
    const lh_char_t *const b = cnor_vm_fast_str_data(right);

    if (size != cnor_vm_fast_str_size(right))
    {
        return 0;
    }
    if (a == b || size == 0U)
    {
        return 1;
    }
    return cnor_vm_fast_bytes_equal(a, b, size);
}

/** @brief Copy @p size bytes from @p from to @p to; the ranges do not overlap. */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE void
cnor_vm_fast_copy(lh_char_t *to, const lh_char_t *from, lh_usize_t size)
{
    lh_usize_t i;

    if (size > CNOR_VM_FAST_LONG_TEXT)
    {
        (void) lh_memory_std_copy(to, (lh_ptr) from, size);
        return;
    }
    if (size >= 8U)
    {
        /* Whole words, the last one overlapping what is already copied. */
        for (i = 0U; i + 8U < size; i += 8U)
        {
            lh_bit_pack_le64(cnor_vm_fast_word(from + i), (lh_byte_ptr) (to + i));
        }
        lh_bit_pack_le64(cnor_vm_fast_word(from + size - 8U), (lh_byte_ptr) (to + size - 8U));
        return;
    }
    for (i = 0U; i < size; ++i)
    {
        to[i] = from[i];
    }
}

/**
 * @brief `left + right` for two strings into @p out (which may be either
 *        operand): the bytes are copied into @p pool, except that two empty
 *        strings give @p left itself.
 * @return 0, with nothing changed, when the pool cannot hold the result.
 */
LH_COMPILER_ATTRIBUTE_FORCE_INLINE int
cnor_vm_fast_join(cnor_str_pool_t *pool, lh_value_t *out, const lh_value_t *left,
                  const lh_value_t *right)
{
    const lh_usize_t a_size = cnor_vm_fast_str_size(left);
    const lh_usize_t b_size = cnor_vm_fast_str_size(right);
    const lh_ullong_t total = (lh_ullong_t) a_size + (lh_ullong_t) b_size;
    const lh_char_t *const a = cnor_vm_fast_str_data(left);
    const lh_char_t *const b = cnor_vm_fast_str_data(right);
    lh_char_t *bytes;

    if (total == 0U)
    {
        *out = *left;
        return 1;
    }
    /* A pool without a block has no room either, and no pool holds 4 GiB. */
    if ((lh_ullong_t) (pool->size - pool->used) < total)
    {
        return 0;
    }
    bytes = pool->data + pool->used;
    pool->used += (lh_u32_t) total;
    cnor_vm_fast_copy(bytes, a, a_size);
    cnor_vm_fast_copy(bytes + a_size, b, b_size);
    cnor_vm_fast_set_str(out, bytes, (lh_usize_t) total);
    return 1;
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

    /* Flipping the sign bit maps -2^31 .. 2^31 - 1 onto 0 .. 2^32 - 1 in
     * order, and one subtraction maps it back: two additions after the load,
     * where a test for the sign took a compare and a conditional move. */
    return (lh_sllong_t) (bits ^ 0x80000000U) - (lh_sllong_t) 0x80000000U;
}

#endif /* CNOR_VM_FAST_H */
