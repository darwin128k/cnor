#include <cnor/builtin.h>
#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>
#include <lh/null.h>
#include <lh/str/parse/sllong.h>
#include <lh/str/parse/ullong.h>
#include <string.h>

/* -- helpers --------------------------------------------------------------- */

/* The bytes of a text. */
typedef struct builtin_text
{
    const lh_char_t *bytes;
    lh_usize_t size;
} builtin_text_t;

/* The text @p value holds. */
static lh_bool_t
builtin_view(const lh_value_t *value, builtin_text_t *out)
{
    if (cnor_vm_fast_kind(value) != lh_value_kind_str)
    {
        return lh_bool_false;
    }
    out->bytes = cnor_vm_fast_str_data(value);
    out->size = cnor_vm_fast_str_size(value);
    return lh_bool_true;
}

/* The argument @p index of the call (0 is the first). */
static const lh_value_t *
builtin_arg(cnor_vm_t *vm, cnor_argc_t argc, cnor_argc_t index)
{
    return cnor_vm_stack_at(&vm->stack, vm->stack.sp - argc + index);
}

/* Replace the arguments with the result. */
static void
builtin_result(cnor_vm_t *vm, cnor_argc_t argc, lh_value_t value)
{
    vm->stack.sp -= argc;
    cnor_vm_stack_push(&vm->stack, value);
}

/* A string of @p size bytes at @p bytes, copied into the machine's pool. */
static cnor_error_t
builtin_text(cnor_vm_t *vm, const lh_char_t *bytes, lh_usize_t size, lh_value_t *out)
{
    lh_char_t *const copy = cnor_str_pool_take(&vm->str_pool, (lh_u32_t) size);

    if (copy == lh_null)
    {
        return cnor_error_str_pool_full;
    }
    if (size > 0U)
    {
        memcpy(copy, bytes, size);
    }
    cnor_vm_fast_set_str(out, copy, size);
    return cnor_error_none;
}

/* The index of the first occurrence of @p needle in @p text, or -1. */
static lh_sllong_t
builtin_find(builtin_text_t text, builtin_text_t needle)
{
    lh_usize_t at;

    if (needle.size == 0U)
    {
        return 0;
    }
    if (needle.size > text.size)
    {
        return -1;
    }
    for (at = 0U; at + needle.size <= text.size; ++at)
    {
        if (memcmp(text.bytes + at, needle.bytes, needle.size) == 0)
        {
            return (lh_sllong_t) at;
        }
    }
    return -1;
}

/* The number of places @p needle occurs in @p text. */
static lh_usize_t
builtin_count(builtin_text_t text, builtin_text_t needle)
{
    lh_usize_t count = 0U;
    lh_usize_t at = 0U;

    if (needle.size == 0U)
    {
        return 0U;
    }
    while (at + needle.size <= text.size)
    {
        if (memcmp(text.bytes + at, needle.bytes, needle.size) == 0)
        {
            ++count;
            at += needle.size;
        }
        else
        {
            ++at;
        }
    }
    return count;
}

/* Whether @p text starts with @p prefix, or ends with @p suffix. */
static int
builtin_starts(builtin_text_t text, builtin_text_t prefix)
{
    if (prefix.size > text.size)
    {
        return 0;
    }
    return memcmp(text.bytes, prefix.bytes, prefix.size) == 0 ? 1 : 0;
}

static int
builtin_ends(builtin_text_t text, builtin_text_t suffix)
{
    if (suffix.size > text.size)
    {
        return 0;
    }
    return memcmp(text.bytes + (text.size - suffix.size), suffix.bytes, suffix.size) == 0 ? 1 : 0;
}

/* Whether @p byte is a space or a tab. */
static int
builtin_is_space(lh_char_t byte)
{
    return (byte == ' ' || byte == '\t') ? 1 : 0;
}

/* The decimal text of @p number, into @p to (at least 24 bytes). */
static lh_usize_t
builtin_format_int(lh_sllong_t number, lh_char_t *to)
{
    lh_char_t digits[24];
    lh_usize_t used = 0U;
    lh_u64_t rest;
    lh_usize_t at = 0U;

    if (number < 0)
    {
        to[at++] = '-';
        rest = (lh_u64_t) (-(number + 1)) + 1U;
    }
    else
    {
        rest = (lh_u64_t) number;
    }
    do
    {
        digits[used++] = (lh_char_t) ('0' + (rest % 10U));
        rest /= 10U;
    } while (rest != 0U);
    while (used > 0U)
    {
        to[at++] = digits[--used];
    }
    return at;
}

/* The text of @p value, into @p to (at least 64 bytes). */
static lh_usize_t
builtin_format_value(const lh_value_t *value, lh_char_t *to)
{
    switch (cnor_vm_fast_kind(value))
    {
    case lh_value_kind_none:
        memcpy(to, "none", 4U);
        return 4U;
    case lh_value_kind_bool:
        if (value->data.b)
        {
            memcpy(to, "true", 4U);
            return 4U;
        }
        memcpy(to, "false", 5U);
        return 5U;
    case lh_value_kind_sint:
        return builtin_format_int(value->data.i, to);
    case lh_value_kind_uint:
    {
        lh_char_t digits[24];
        lh_usize_t used = 0U;
        lh_u64_t rest = value->data.u;
        lh_usize_t at = 0U;

        do
        {
            digits[used++] = (lh_char_t) ('0' + (rest % 10U));
            rest /= 10U;
        } while (rest != 0U);
        while (used > 0U)
        {
            to[at++] = digits[--used];
        }
        return at;
    }
#if LH_REAL_ENABLED
    case lh_value_kind_real:
    {
        /* The whole part, then six decimals, without the trailing zeros. */
        const lh_real_t value_real = value->data.r;
        const lh_bool_t negative = value_real < (lh_real_t) 0;
        lh_real_t whole = negative ? -value_real : value_real;
        lh_sllong_t integral = (lh_sllong_t) whole;
        lh_real_t fraction = (whole - (lh_real_t) integral) * (lh_real_t) 1000000;
        lh_sllong_t scaled = (lh_sllong_t) (fraction + (lh_real_t) 0.5);
        lh_char_t digits[8];
        lh_usize_t used = 0U;
        lh_usize_t at = 0U;

        if (scaled >= 1000000)
        {
            scaled = 999999;
        }
        if (negative && (integral != 0 || scaled != 0))
        {
            to[at++] = '-';
        }
        at += builtin_format_int(integral, to + at);
        if (scaled == 0)
        {
            /* A whole number: the text keeps a decimal point, so it reads
             * back as the same number. */
            to[at++] = '.';
            to[at++] = '0';
            return at;
        }
        to[at++] = '.';
        do
        {
            digits[used++] = (lh_char_t) ('0' + (scaled % 10));
            scaled /= 10;
        } while (scaled != 0 && used < sizeof(digits));
        while (used > 0U)
        {
            to[at++] = digits[--used];
        }
        /* Without the trailing zeros. */
        while (at > 0U && to[at - 1U] == '0')
        {
            --at;
        }
        if (at > 0U && to[at - 1U] == '.')
        {
            to[at++] = '0';
        }
        return at;
    }
#endif
    default:
        return 0U;
    }
}

/* -- the functions --------------------------------------------------------- */

/* find(text, needle): the index of the first occurrence, or -1. */
static cnor_error_t
builtin_do_find(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;
    builtin_text_t needle;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text)
        || !builtin_view(builtin_arg(vm, argc, 1U), &needle))
    {
        return cnor_error_type;
    }
    builtin_result(vm, argc, lh_value_make_sint(builtin_find(text, needle)));
    return cnor_error_none;
}

/* contains(text, needle): whether the needle is in the text. */
static cnor_error_t
builtin_do_contains(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;
    builtin_text_t needle;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text)
        || !builtin_view(builtin_arg(vm, argc, 1U), &needle))
    {
        return cnor_error_type;
    }
    builtin_result(vm, argc, lh_value_make_bool(builtin_find(text, needle) >= 0));
    return cnor_error_none;
}

/* replace(text, from, to): every occurrence of `from` replaced by `to`. */
static cnor_error_t
builtin_do_replace(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;
    builtin_text_t from;
    builtin_text_t to;
    lh_value_t result;
    lh_usize_t hits;
    lh_usize_t size;
    lh_usize_t at = 0U;
    lh_usize_t written = 0U;
    lh_char_t *bytes;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text)
        || !builtin_view(builtin_arg(vm, argc, 1U), &from)
        || !builtin_view(builtin_arg(vm, argc, 2U), &to))
    {
        return cnor_error_type;
    }
    if (from.size == 0U)
    {
        /* Nothing to replace: the text itself. */
        if (builtin_text(vm, text.bytes, text.size, &result) != cnor_error_none)
    {
        return cnor_error_str_pool_full;
    }
        builtin_result(vm, argc, result);
        return cnor_error_none;
    }
    hits = builtin_count(text, from);
    size = text.size + hits * to.size - hits * from.size;
    bytes = cnor_str_pool_take(&vm->str_pool, (lh_u32_t) size);
    if (bytes == lh_null)
    {
        return cnor_error_str_pool_full;
    }
    while (at < text.size)
    {
        if (at + from.size <= text.size && memcmp(text.bytes + at, from.bytes, from.size) == 0)
        {
            if (to.size > 0U)
            {
                memcpy(bytes + written, to.bytes, to.size);
                written += to.size;
            }
            at += from.size;
        }
        else
        {
            bytes[written++] = text.bytes[at++];
        }
    }
    cnor_vm_fast_set_str(&result, bytes, written);
    builtin_result(vm, argc, result);
    return cnor_error_none;
}

/* trim(text): without the spaces and tabs around it. */
static cnor_error_t
builtin_do_trim(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;
    lh_value_t result;
    lh_usize_t from = 0U;
    lh_usize_t to;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text))
    {
        return cnor_error_type;
    }
    to = text.size;
    while (from < to && builtin_is_space(text.bytes[from]))
    {
        ++from;
    }
    while (to > from && builtin_is_space(text.bytes[to - 1U]))
    {
        --to;
    }
    if (builtin_text(vm, text.bytes + from, to - from, &result) != cnor_error_none)
    {
        return cnor_error_str_pool_full;
    }
    builtin_result(vm, argc, result);
    return cnor_error_none;
}

/* upper(text) and lower(text): the case of the ASCII letters. */
static cnor_error_t
builtin_do_case(cnor_vm_t *vm, cnor_argc_t argc, lh_bool_t upper)
{
    builtin_text_t text;
    lh_value_t result;
    lh_char_t *bytes;
    lh_usize_t at;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text))
    {
        return cnor_error_type;
    }
    bytes = cnor_str_pool_take(&vm->str_pool, (lh_u32_t) text.size);
    if (bytes == lh_null)
    {
        return cnor_error_str_pool_full;
    }
    for (at = 0U; at < text.size; ++at)
    {
        const lh_char_t byte = text.bytes[at];

        if (upper && byte >= 'a' && byte <= 'z')
        {
            bytes[at] = (lh_char_t) (byte - ('a' - 'A'));
        }
        else if (!upper && byte >= 'A' && byte <= 'Z')
        {
            bytes[at] = (lh_char_t) (byte + ('a' - 'A'));
        }
        else
        {
            bytes[at] = byte;
        }
    }
    cnor_vm_fast_set_str(&result, bytes, text.size);
    builtin_result(vm, argc, result);
    return cnor_error_none;
}

/* starts_with(text, prefix) and ends_with(text, suffix). */
static cnor_error_t
builtin_do_affix(cnor_vm_t *vm, cnor_argc_t argc, lh_bool_t starts)
{
    builtin_text_t text;
    builtin_text_t affix;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text)
        || !builtin_view(builtin_arg(vm, argc, 1U), &affix))
    {
        return cnor_error_type;
    }
    builtin_result(vm, argc,
                   lh_value_make_bool(starts ? builtin_starts(text, affix)
                                             : builtin_ends(text, affix)));
    return cnor_error_none;
}

/* substr(text, start, length): `length` bytes from `start`. */
static cnor_error_t
builtin_do_substr(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;
    lh_value_t result;
    const lh_value_t *const start_value = builtin_arg(vm, argc, 1U);
    const lh_value_t *const length_value = builtin_arg(vm, argc, 2U);
    lh_sllong_t start;
    lh_sllong_t length;
    lh_usize_t size;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text)
        || cnor_vm_fast_kind(start_value) != lh_value_kind_sint
        || cnor_vm_fast_kind(length_value) != lh_value_kind_sint)
    {
        return cnor_error_type;
    }
    start = start_value->data.i;
    length = length_value->data.i;
    if (start < 0 || (lh_u64_t) start > (lh_u64_t) text.size)
    {
        /* Outside the text: the same failure as indexing a string. */
        return cnor_error_index;
    }
    size = (lh_usize_t) length;
    if (length < 0 || size > text.size - (lh_usize_t) start)
    {
        size = text.size - (lh_usize_t) start;
    }
    if (builtin_text(vm, text.bytes + start, size, &result) != cnor_error_none)
    {
        return cnor_error_str_pool_full;
    }
    builtin_result(vm, argc, result);
    return cnor_error_none;
}

/* ord(text): the code of the first byte, or -1. */
static cnor_error_t
builtin_do_ord(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text))
    {
        return cnor_error_type;
    }
    builtin_result(vm, argc,
                   lh_value_make_sint(text.size == 0U ? -1 : (lh_sllong_t) (lh_uchar_t) text.bytes[0]));
    return cnor_error_none;
}

/* char(code): the one-byte string of that code. */
static cnor_error_t
builtin_do_char(cnor_vm_t *vm, cnor_argc_t argc)
{
    const lh_value_t *const code_value = builtin_arg(vm, argc, 0U);
    lh_value_t result;
    lh_char_t byte;

    if (cnor_vm_fast_kind(code_value) != lh_value_kind_sint)
    {
        return cnor_error_type;
    }
    if (code_value->data.i < 0 || code_value->data.i > 255)
    {
        return cnor_error_type;
    }
    byte = (lh_char_t) code_value->data.i;
    if (builtin_text(vm, &byte, 1U, &result) != cnor_error_none)
    {
        return cnor_error_str_pool_full;
    }
    builtin_result(vm, argc, result);
    return cnor_error_none;
}

/* str(value): the value as text. */
static cnor_error_t
builtin_do_str(cnor_vm_t *vm, cnor_argc_t argc)
{
    const lh_value_t *const value = builtin_arg(vm, argc, 0U);
    lh_value_t result;
    lh_char_t text[64];
    lh_usize_t size;

    if (cnor_vm_fast_kind(value) == lh_value_kind_str)
    {
        if (builtin_text(vm, cnor_vm_fast_str_data(value), cnor_vm_fast_str_size(value),
                          &result) != cnor_error_none)
    {
        return cnor_error_str_pool_full;
    }
        builtin_result(vm, argc, result);
        return cnor_error_none;
    }
    size = builtin_format_value(value, text);
    if (size == 0U)
    {
        return cnor_error_type;
    }
    if (builtin_text(vm, text, size, &result) != cnor_error_none)
    {
        return cnor_error_str_pool_full;
    }
    builtin_result(vm, argc, result);
    return cnor_error_none;
}

/* parse_int(text): the number in the text, or 0. */
static cnor_error_t
builtin_do_parse_int(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;
    lh_sllong_t number = 0;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text))
    {
        return cnor_error_type;
    }
    (void) lh_str_ptr_parse_sllong(text.bytes, text.size, -9223372036854775807LL - 1LL,
                                   9223372036854775807LL, &number);
    builtin_result(vm, argc, lh_value_make_sint(number));
    return cnor_error_none;
}

/*
 * split_into(text, separator, out): the pieces of the text, written into the
 * array, and how many there are.
 *
 * A collection cannot be made while the program runs, so the array is made by
 * the program and given here. The array has to have a place for every piece;
 * if it does not, the run fails.
 */
static cnor_error_t
builtin_do_split_into(cnor_vm_t *vm, cnor_argc_t argc)
{
    builtin_text_t text;
    builtin_text_t separator;
    const lh_value_t *const out = builtin_arg(vm, argc, 2U);
    lh_value_t *elements;
    lh_u32_t room;
    lh_u32_t written = 0U;
    lh_usize_t start = 0U;
    lh_usize_t at = 0U;

    if (!builtin_view(builtin_arg(vm, argc, 0U), &text)
        || !builtin_view(builtin_arg(vm, argc, 1U), &separator))
    {
        return cnor_error_type;
    }
    if (cnor_vm_fast_kind(out) != CNOR_VALUE_KIND_ARRAY)
    {
        return cnor_error_type;
    }
    if (separator.size == 0U)
    {
        return cnor_error_empty_separator;
    }
    elements = cnor_vm_fast_array_data(out);
    room = cnor_vm_fast_array_count(out);
    while (at + separator.size <= text.size)
    {
        lh_value_t piece;
        cnor_error_t error;

        if (memcmp(text.bytes + at, separator.bytes, separator.size) != 0)
        {
            ++at;
            continue;
        }
        /* The piece before the separator. */
        if (written >= room)
        {
            return cnor_error_array_too_small;
        }
        error = builtin_text(vm, text.bytes + start, at - start, &piece);
        if (error != cnor_error_none)
        {
            return error;
        }
        cnor_vm_fast_move(&elements[written], &piece);
        ++written;
        at += separator.size;
        start = at;
    }
    /* The piece after the last separator, even when it is empty. */
    if (written >= room)
    {
        return cnor_error_array_too_small;
    }
    {
        lh_value_t piece;
        const cnor_error_t error = builtin_text(vm, text.bytes + start, text.size - start, &piece);

        if (error != cnor_error_none)
        {
            return error;
        }
        cnor_vm_fast_move(&elements[written], &piece);
        ++written;
    }
    builtin_result(vm, argc, lh_value_make_sint((lh_sllong_t) written));
    return cnor_error_none;
}

/* -- the handler ------------------------------------------------------------ */

lh_bool_t
cnor_vm_op_call_builtin(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_builtin_t builtin = (cnor_builtin_t) insn->operands[0];
    const cnor_argc_t argc = (cnor_argc_t) insn->operands[1];
    cnor_error_t error = cnor_error_none;

    if ((lh_uint_t) builtin >= (lh_uint_t) cnor_builtin_count || argc != cnor_builtin_argc(builtin))
    {
        return cnor_vm_fail(vm, insn, cnor_error_bad_operand);
    }
    switch (builtin)
    {
    case cnor_builtin_find:
        error = builtin_do_find(vm, argc);
        break;
    case cnor_builtin_contains:
        error = builtin_do_contains(vm, argc);
        break;
    case cnor_builtin_replace:
        error = builtin_do_replace(vm, argc);
        break;
    case cnor_builtin_trim:
        error = builtin_do_trim(vm, argc);
        break;
    case cnor_builtin_upper:
        error = builtin_do_case(vm, argc, lh_bool_true);
        break;
    case cnor_builtin_lower:
        error = builtin_do_case(vm, argc, lh_bool_false);
        break;
    case cnor_builtin_starts_with:
        error = builtin_do_affix(vm, argc, lh_bool_true);
        break;
    case cnor_builtin_ends_with:
        error = builtin_do_affix(vm, argc, lh_bool_false);
        break;
    case cnor_builtin_substr:
        error = builtin_do_substr(vm, argc);
        break;
    case cnor_builtin_ord:
        error = builtin_do_ord(vm, argc);
        break;
    case cnor_builtin_char:
        error = builtin_do_char(vm, argc);
        break;
    case cnor_builtin_str:
        error = builtin_do_str(vm, argc);
        break;
    case cnor_builtin_parse_int:
        error = builtin_do_parse_int(vm, argc);
        break;
    case cnor_builtin_split_into:
        error = builtin_do_split_into(vm, argc);
        break;
    default:
        error = cnor_error_bad_operand;
        break;
    }
    if (error != cnor_error_none)
    {
        return cnor_vm_fail(vm, insn, error);
    }
    return lh_bool_true;
}
