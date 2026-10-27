#include <cnor/compiler/internal.h>
#include <lh/null.h>

/*
 * Static types. Every expression gets one while it is compiled; what can be
 * known is checked, the rest is `any` and left to the machine:
 *
 * - `i32` and `u32` are one family. The machine mixes them by value (mixed
 *   arithmetic computes signed, an unsigned subtraction below zero gives a
 *   signed result), so either is accepted where the other is declared.
 * - `f32` is not an integer: an integer where `f32` is declared, or the other
 *   way round, is a mismatch, since the value keeps its kind and `/` would
 *   divide differently.
 * - `none` is accepted only where nothing is declared.
 * - An operator fails at compile time only when the machine would always fail
 *   on those operand types; with `any` involved it is left to run time.
 */

/* -- names ----------------------------------------------------------------- */

static const lh_char_t *const m_type_names[cnor_static_type_count] = {
    [cnor_static_any] = "any",   [cnor_static_none] = "none", [cnor_static_bool] = "bool",
    [cnor_static_i32] = "i32",   [cnor_static_u32] = "u32",   [cnor_static_f32] = "f32",
    [cnor_static_str] = "str",   [cnor_static_array] = "array",
    [cnor_static_dict] = "dict", [cnor_static_function] = "function",
};

static const lh_char_t *const m_binary_spellings[cnor_binary_count] = {
    [cnor_binary_add] = "+",  [cnor_binary_sub] = "-",  [cnor_binary_mul] = "*",
    [cnor_binary_div] = "/",  [cnor_binary_mod] = "%",  [cnor_binary_eq] = "==",
    [cnor_binary_ne] = "!=",  [cnor_binary_lt] = "<",   [cnor_binary_le] = "<=",
    [cnor_binary_gt] = ">",   [cnor_binary_ge] = ">=",  [cnor_binary_and] = "and",
    [cnor_binary_or] = "or",  [cnor_binary_band] = "&", [cnor_binary_bor] = "|",
    [cnor_binary_bxor] = "^", [cnor_binary_shl] = "<<", [cnor_binary_shr] = ">>",
};

const lh_char_t *
cnor_type_name(cnor_static_type_t type)
{
    return m_type_names[type];
}

cnor_static_type_t
cnor_type_of_annotation(cnor_type_t annotation)
{
    switch (annotation)
    {
    case cnor_type_i32:
        return cnor_static_i32;
    case cnor_type_u32:
        return cnor_static_u32;
    case cnor_type_f32:
        return cnor_static_f32;
    case cnor_type_bool:
        return cnor_static_bool;
    case cnor_type_str:
        return cnor_static_str;
    case cnor_type_array:
        return cnor_static_array;
    case cnor_type_dict:
        return cnor_static_dict;
    case cnor_type_function:
        return cnor_static_function;
    default:
        return cnor_static_any;
    }
}

/* -- details --------------------------------------------------------------- */

/**
 * @struct cnor_type_detail
 * @brief The text of a type error ("expected i32, found str", "str - i32"),
 *        cut to what a diagnostic keeps.
 */
struct cnor_type_detail
{
    lh_char_t text[CNOR_DIAG_NAME_CAPACITY];
    lh_usize_t size;
};
/** @copybrief cnor_type_detail */
typedef struct cnor_type_detail cnor_type_detail_t;

static void
cnor_type_detail_put(cnor_type_detail_t *self, const lh_char_t *text)
{
    for (; *text != '\0' && self->size < CNOR_DIAG_NAME_CAPACITY; ++text)
    {
        self->text[self->size++] = *text;
    }
}

static lh_bool_t
cnor_type_fail(cnor_compiler_t *self, const cnor_node_t *node, cnor_error_t code,
               const cnor_type_detail_t *detail)
{
    lh_str_view_t text;

    lh_str_init_by_size(&text, detail->text, detail->size);
    return cnor_compile_fail_text(self, node->line, code, text);
}

/* -- families -------------------------------------------------------------- */

static lh_bool_t
cnor_type_is_integer(cnor_static_type_t type)
{
    return (type == cnor_static_i32 || type == cnor_static_u32) ? lh_bool_true : lh_bool_false;
}

static lh_bool_t
cnor_type_is_number(cnor_static_type_t type)
{
    return (cnor_type_is_integer(type) || type == cnor_static_f32) ? lh_bool_true : lh_bool_false;
}

/* `any` or a whole number: what the bitwise operators may get. */
static lh_bool_t
cnor_type_may_be_integer(cnor_static_type_t type)
{
    return (type == cnor_static_any || cnor_type_is_integer(type)) ? lh_bool_true : lh_bool_false;
}

/* `any` or a number: what arithmetic and ordering may get. */
static lh_bool_t
cnor_type_may_be_number(cnor_static_type_t type)
{
    return (type == cnor_static_any || cnor_type_is_number(type)) ? lh_bool_true : lh_bool_false;
}

lh_bool_t
cnor_type_accepts(cnor_static_type_t wanted, cnor_static_type_t found)
{
    return (wanted == cnor_static_any || found == cnor_static_any || wanted == found
            || (cnor_type_is_integer(wanted) && cnor_type_is_integer(found)))
               ? lh_bool_true
               : lh_bool_false;
}

lh_bool_t
cnor_type_require(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t wanted,
                  cnor_static_type_t found)
{
    cnor_type_detail_t detail;

    if (cnor_type_accepts(wanted, found))
    {
        return lh_bool_true;
    }
    detail.size = 0U;
    cnor_type_detail_put(&detail, "expected ");
    cnor_type_detail_put(&detail, cnor_type_name(wanted));
    cnor_type_detail_put(&detail, ", found ");
    cnor_type_detail_put(&detail, cnor_type_name(found));
    return cnor_type_fail(self, node, cnor_error_type_mismatch, &detail);
}

/* The same, for a wanted type that has no name of its own ("array or dict"). */
lh_bool_t
cnor_type_require_text(cnor_compiler_t *self, const cnor_node_t *node, const lh_char_t *wanted,
                       cnor_static_type_t found)
{
    cnor_type_detail_t detail;

    detail.size = 0U;
    cnor_type_detail_put(&detail, "expected ");
    cnor_type_detail_put(&detail, wanted);
    cnor_type_detail_put(&detail, ", found ");
    cnor_type_detail_put(&detail, cnor_type_name(found));
    return cnor_type_fail(self, node, cnor_error_type_mismatch, &detail);
}

/* An unannotated variable holds what its initializer gives; one that starts
 * as `none` (or without an initializer) may hold anything later. */
cnor_static_type_t
cnor_type_of_variable(cnor_type_t annotation, cnor_static_type_t init)
{
    if (annotation != cnor_type_none)
    {
        return cnor_type_of_annotation(annotation);
    }
    return init == cnor_static_none ? cnor_static_any : init;
}

cnor_static_type_t
cnor_type_of_literal(const cnor_node_t *node)
{
    switch (node->kind)
    {
    case cnor_node_int:
        return node->data.int_.value <= 0x7FFFFFFFU ? cnor_static_i32 : cnor_static_u32;
    case cnor_node_real:
        return cnor_static_f32;
    case cnor_node_string:
        return cnor_static_str;
    case cnor_node_true:
    case cnor_node_false:
        return cnor_static_bool;
    case cnor_node_none:
        return cnor_static_none;
    case cnor_node_unary:
        /* `-k` as a pattern: negation makes any integer signed. */
        switch (cnor_type_of_literal(node->data.unary.operand))
        {
        case cnor_static_f32:
            return cnor_static_f32;
        case cnor_static_i32:
        case cnor_static_u32:
            return cnor_static_i32;
        default:
            return cnor_static_any;
        }
    default:
        return cnor_static_any;
    }
}

lh_bool_t
cnor_type_comparable(cnor_static_type_t a, cnor_static_type_t b)
{
    return (a == cnor_static_any || b == cnor_static_any || a == cnor_static_none
            || b == cnor_static_none || a == b
            || (cnor_type_is_number(a) && cnor_type_is_number(b)))
               ? lh_bool_true
               : lh_bool_false;
}

/* -- operators ------------------------------------------------------------- */

/* Arithmetic on two numbers: real with a real, unsigned with two unsigned. */
static cnor_static_type_t
cnor_type_of_arithmetic(cnor_static_type_t left, cnor_static_type_t right)
{
    if (left == cnor_static_any || right == cnor_static_any)
    {
        return cnor_static_any;
    }
    if (left == cnor_static_f32 || right == cnor_static_f32)
    {
        return cnor_static_f32;
    }
    return (left == cnor_static_u32 && right == cnor_static_u32) ? cnor_static_u32
                                                                 : cnor_static_i32;
}

/* `and` / `or` give whichever operand decided. */
cnor_static_type_t
cnor_type_choice(cnor_static_type_t left, cnor_static_type_t right)
{
    if (left == right)
    {
        return left;
    }
    return (cnor_type_is_integer(left) && cnor_type_is_integer(right)) ? cnor_static_i32
                                                                       : cnor_static_any;
}

/* `+` also joins two strings. */
static lh_bool_t
cnor_type_of_add(cnor_static_type_t left, cnor_static_type_t right, cnor_static_type_t *out)
{
    if ((left == cnor_static_str || left == cnor_static_any)
        && (right == cnor_static_str || right == cnor_static_any)
        && (left == cnor_static_str || right == cnor_static_str))
    {
        *out = cnor_static_str;
        return lh_bool_true;
    }
    if (cnor_type_may_be_number(left) && cnor_type_may_be_number(right))
    {
        *out = cnor_type_of_arithmetic(left, right);
        return lh_bool_true;
    }
    return lh_bool_false;
}

/* What `left op right` gives, or ::lh_bool_false when it always fails. */
static lh_bool_t
cnor_type_of_binary(cnor_binary_op_t op, cnor_static_type_t left, cnor_static_type_t right,
                    cnor_static_type_t *out)
{
    switch (op)
    {
    case cnor_binary_add:
        return cnor_type_of_add(left, right, out);
    case cnor_binary_sub:
    case cnor_binary_mul:
    case cnor_binary_div:
    case cnor_binary_mod:
        *out = cnor_type_of_arithmetic(left, right);
        return (cnor_type_may_be_number(left) && cnor_type_may_be_number(right)) ? lh_bool_true
                                                                                  : lh_bool_false;
    case cnor_binary_band:
    case cnor_binary_bor:
    case cnor_binary_bxor:
    case cnor_binary_shl:
    case cnor_binary_shr:
        /* The bitwise operators work on whole numbers. */
        *out = cnor_static_i32;
        return (cnor_type_may_be_integer(left) && cnor_type_may_be_integer(right))
                   ? lh_bool_true
                   : lh_bool_false;
    case cnor_binary_eq:
    case cnor_binary_ne:
        *out = cnor_static_bool;        return cnor_type_comparable(left, right);
    case cnor_binary_lt:
    case cnor_binary_le:
    case cnor_binary_gt:
    case cnor_binary_ge:
        *out = cnor_static_bool;
        return (cnor_type_may_be_number(left) && cnor_type_may_be_number(right)) ? lh_bool_true
                                                                                  : lh_bool_false;
    default:
        *out = cnor_type_choice(left, right);
        return lh_bool_true;
    }
}

lh_bool_t
cnor_type_binary(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t left,
                 cnor_static_type_t right, cnor_static_type_t *out)
{
    cnor_type_detail_t detail;

    if (cnor_type_of_binary(node->data.binary.op, left, right, out))
    {
        return lh_bool_true;
    }
    detail.size = 0U;
    cnor_type_detail_put(&detail, cnor_type_name(left));
    cnor_type_detail_put(&detail, " ");
    cnor_type_detail_put(&detail, m_binary_spellings[node->data.binary.op]);
    cnor_type_detail_put(&detail, " ");
    cnor_type_detail_put(&detail, cnor_type_name(right));
    return cnor_type_fail(self, node, cnor_error_operand_type, &detail);
}

lh_bool_t
cnor_type_negate(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t operand,
                 cnor_static_type_t *out)
{
    cnor_type_detail_t detail;

    if (cnor_type_may_be_number(operand))
    {
        /* Negating an unsigned value gives a signed one. */
        *out = cnor_type_is_integer(operand) ? cnor_static_i32 : operand;
        return lh_bool_true;
    }
    detail.size = 0U;
    cnor_type_detail_put(&detail, "-");
    cnor_type_detail_put(&detail, cnor_type_name(operand));
    return cnor_type_fail(self, node, cnor_error_operand_type, &detail);
}
