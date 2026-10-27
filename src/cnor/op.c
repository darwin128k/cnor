#include <cnor/error.h>
#include <cnor/op.h>
#include <lh/bit/endian.h>
#include <lh/null.h>

/* -- the table ------------------------------------------------------------- */

#define CNOR_OP_ROW3(name_, a_, b_, c_, size_, pops_, pushes_, flags_)                             \
    {                                                                                              \
        name_, {cnor_operand_##a_, cnor_operand_##b_, cnor_operand_##c_}, size_, pops_, pushes_,   \
            flags_                                                                                 \
    }
#define CNOR_OP_ROW(name_, a_, b_, size_, pops_, pushes_, flags_)                                  \
    CNOR_OP_ROW3(name_, a_, b_, none, size_, pops_, pushes_, flags_)

/* Indexed by ::cnor_op_t; the order must follow the enum. */
static const cnor_op_info_t m_ops[cnor_op_count] = {
    CNOR_OP_ROW("halt", none, none, 1U, 0U, 0U, CNOR_OP_FLAG_TERMINATOR),
    CNOR_OP_ROW("push_none", none, none, 1U, 0U, 1U, 0U),
    CNOR_OP_ROW("push_true", none, none, 1U, 0U, 1U, 0U),
    CNOR_OP_ROW("push_false", none, none, 1U, 0U, 1U, 0U),
    CNOR_OP_ROW("push_i32", s32, none, 5U, 0U, 1U, 0U),
    CNOR_OP_ROW("push_u32", u32, none, 5U, 0U, 1U, 0U),
    CNOR_OP_ROW("push_const", u16, none, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("load_local", slot, none, 2U, 0U, 1U, 0U),
    CNOR_OP_ROW("store_local", slot, none, 2U, 1U, 0U, 0U),
    CNOR_OP_ROW("load_global", u16, none, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("store_global", u16, none, 3U, 1U, 0U, 0U),
    CNOR_OP_ROW("dup", none, none, 1U, 1U, 2U, 0U),
    CNOR_OP_ROW("swap", none, none, 1U, 2U, 2U, 0U),
    CNOR_OP_ROW("pop", none, none, 1U, 1U, 0U, 0U),
    CNOR_OP_ROW("add", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("sub", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("mul", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("div", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("mod", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("neg", none, none, 1U, 1U, 1U, 0U),
    CNOR_OP_ROW("eq", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("ne", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("lt", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("le", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("gt", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("ge", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("band", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("bor", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("bxor", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("shl", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("shr", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("not", none, none, 1U, 1U, 1U, 0U),
    CNOR_OP_ROW("bnot", none, none, 1U, 1U, 1U, 0U),
    CNOR_OP_ROW("jmp", s32, none, 5U, 0U, 0U, CNOR_OP_FLAG_JUMP | CNOR_OP_FLAG_TERMINATOR),
    CNOR_OP_ROW("jmp_if_false", s32, none, 5U, 1U, 0U, CNOR_OP_FLAG_JUMP),
    CNOR_OP_ROW("jmp_if_true", s32, none, 5U, 1U, 0U, CNOR_OP_FLAG_JUMP),
    CNOR_OP_ROW("call", u16, u8, 4U, 0U, 1U, CNOR_OP_FLAG_POPS_ARGC),
    CNOR_OP_ROW("ret", none, none, 1U, 1U, 0U, CNOR_OP_FLAG_TERMINATOR),
    CNOR_OP_ROW("native", u16, u8, 4U, 0U, 1U, CNOR_OP_FLAG_POPS_ARGC),
    CNOR_OP_ROW("len", none, none, 1U, 1U, 1U, 0U),
    CNOR_OP_ROW("index", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("add_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("sub_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("mul_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("div_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("mod_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("eq_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("ne_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("lt_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("le_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("gt_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("ge_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("band_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("bor_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("bxor_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("shl_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("shr_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("add_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("sub_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("mul_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("div_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("mod_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("eq_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("ne_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("lt_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("le_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("gt_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("ge_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("band_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("bor_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("bxor_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("shl_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("shr_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("add_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("sub_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("mul_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("div_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("mod_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("eq_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("ne_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("lt_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("le_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("gt_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("ge_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("band_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("bor_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("bxor_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("shl_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("shr_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW3("for_loop", s32, slot, slot, 7U, 0U, 0U, CNOR_OP_FLAG_JUMP),
    CNOR_OP_ROW3("switch", slot, s32, u8, 7U, 0U, 0U, 0U),
    CNOR_OP_ROW("index_ll", slot, slot, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("index_li", slot, s32, 6U, 0U, 1U, 0U),
    CNOR_OP_ROW("index_i", s32, none, 5U, 1U, 1U, 0U),
    CNOR_OP_ROW("array_new", slot, u16, 4U, 1U, 0U, 0U),
    CNOR_OP_ROW("array_new_global", u16, u16, 5U, 1U, 0U, 0U),
    CNOR_OP_ROW("array_get_l", slot, none, 2U, 1U, 1U, 0U),
    CNOR_OP_ROW("array_set_l", slot, none, 2U, 2U, 0U, 0U),
    CNOR_OP_ROW("array_get", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("array_set", none, none, 1U, 3U, 0U, 0U),
    CNOR_OP_ROW("dict_new", slot, u16, 4U, 0U, 0U, 0U),
    CNOR_OP_ROW("dict_new_global", u16, u16, 5U, 0U, 0U, 0U),
    CNOR_OP_ROW("dict_get_l", slot, none, 2U, 1U, 1U, 0U),
    CNOR_OP_ROW("dict_set_l", slot, none, 2U, 2U, 0U, 0U),
    CNOR_OP_ROW("dict_get", none, none, 1U, 2U, 1U, 0U),
    CNOR_OP_ROW("dict_set", none, none, 1U, 3U, 0U, 0U),
    CNOR_OP_ROW("dict_key_l", slot, none, 2U, 1U, 1U, 0U),
    CNOR_OP_ROW("collection_get_l", slot, none, 2U, 1U, 1U, 0U),
    CNOR_OP_ROW3("try_push", s32, u8, none, 6U, 0U, 0U, CNOR_OP_FLAG_JUMP),
    CNOR_OP_ROW("try_pop", none, none, 1U, 0U, 0U, 0U),
    CNOR_OP_ROW("try_finally", s32, none, 5U, 0U, 0U, CNOR_OP_FLAG_JUMP),
    CNOR_OP_ROW("finally_end", none, none, 1U, 0U, 0U, 0U),
    CNOR_OP_ROW("raise", none, none, 1U, 0U, 0U, 0U),
    CNOR_OP_ROW("try_exit", s32, none, 5U, 0U, 0U, CNOR_OP_FLAG_JUMP),
    CNOR_OP_ROW("call_builtin", u8, u8, 3U, 0U, 1U, CNOR_OP_FLAG_POPS_ARGC),
    CNOR_OP_ROW("push_function", u16, none, 3U, 0U, 1U, 0U),
    CNOR_OP_ROW("call_value", u8, none, 2U, 1U, 1U, CNOR_OP_FLAG_POPS_ARGC0),
};

/* The binary operators that have operand forms, in the order of each family. */
static const cnor_op_t m_binary_ops[] = {
    cnor_op_add, cnor_op_sub, cnor_op_mul, cnor_op_div, cnor_op_mod, cnor_op_eq,
    cnor_op_ne,  cnor_op_lt,  cnor_op_le,  cnor_op_gt,  cnor_op_ge,
    cnor_op_band, cnor_op_bor, cnor_op_bxor, cnor_op_shl, cnor_op_shr,
};
#define CNOR_OP_BINARY_COUNT (sizeof(m_binary_ops) / sizeof(m_binary_ops[0]))

/* First opcode of each form's family, indexed by ::cnor_operand_form_t. */
static const cnor_op_t m_form_first[cnor_operand_form_count] = {
    [cnor_operand_form_stack] = cnor_op_count,
    [cnor_operand_form_local_local] = cnor_op_add_ll,
    [cnor_operand_form_local_int] = cnor_op_add_li,
    [cnor_operand_form_stack_int] = cnor_op_add_i,
};

_Static_assert(cnor_op_add_li - cnor_op_add_ll == 16 && cnor_op_add_i - cnor_op_add_li == 16
                   && cnor_op_for_loop - cnor_op_add_i == 16,
               "each operand-form family has one opcode per binary operator, in order");

/* -- queries --------------------------------------------------------------- */

lh_bool_t
cnor_op_is_valid(lh_u32_t byte)
{
    return byte < (lh_u32_t) cnor_op_count ? lh_bool_true : lh_bool_false;
}

const cnor_op_info_t *
cnor_op_get_info(cnor_op_t op)
{
    if (!cnor_op_is_valid((lh_u32_t) op))
    {
        return lh_null;
    }
    return &m_ops[op];
}

lh_u8_t
cnor_operand_get_size(cnor_operand_kind_t kind)
{
    switch (kind)
    {
    case cnor_operand_u8:
    case cnor_operand_slot:
        return 1U;
    case cnor_operand_u16:
        return 2U;
    case cnor_operand_u32:
    case cnor_operand_s32:
        return 4U;
    default:
        return 0U;
    }
}

lh_u32_t
cnor_insn_get_pops(const cnor_insn_t *insn)
{
    const cnor_op_info_t *info = cnor_op_get_info(insn->op);
    lh_u32_t pops = info->pops;

    if ((info->flags & CNOR_OP_FLAG_POPS_ARGC) != 0U)
    {
        pops += insn->operands[1];
    }
    if ((info->flags & CNOR_OP_FLAG_POPS_ARGC0) != 0U)
    {
        pops += insn->operands[0];
    }
    return pops;
}

lh_s64_t
cnor_insn_get_jump_target(const cnor_insn_t *insn)
{
    return (lh_s64_t) insn->next + (lh_s64_t) (lh_s32_t) insn->operands[0];
}

/* -- operand forms --------------------------------------------------------- */

cnor_op_t
cnor_op_get_operand_form(cnor_op_t op, cnor_operand_form_t form)
{
    lh_uint_t i;

    if ((lh_uint_t) form >= (lh_uint_t) cnor_operand_form_count)
    {
        return cnor_op_count;
    }
    for (i = 0U; i < CNOR_OP_BINARY_COUNT; ++i)
    {
        if (m_binary_ops[i] == op)
        {
            return form == cnor_operand_form_stack ? op
                                                   : (cnor_op_t) (m_form_first[form] + i);
        }
    }
    return cnor_op_count;
}

cnor_op_t
cnor_op_get_binary_base(cnor_op_t op, cnor_operand_form_t *form)
{
    lh_uint_t f;

    for (f = cnor_operand_form_local_local; f < cnor_operand_form_count; ++f)
    {
        const lh_uint_t first = (lh_uint_t) m_form_first[f];

        if ((lh_uint_t) op >= first && (lh_uint_t) op < first + CNOR_OP_BINARY_COUNT)
        {
            *form = (cnor_operand_form_t) f;
            return m_binary_ops[(lh_uint_t) op - first];
        }
    }
    *form = cnor_operand_form_stack;
    return op;
}

/* The index operation in each form; the plain opcode is the stack form. */
static const cnor_op_t m_index_forms[cnor_operand_form_count] = {
    [cnor_operand_form_stack] = cnor_op_index,
    [cnor_operand_form_local_local] = cnor_op_index_ll,
    [cnor_operand_form_local_int] = cnor_op_index_li,
    [cnor_operand_form_stack_int] = cnor_op_index_i,
};

cnor_op_t
cnor_op_get_index_form(cnor_operand_form_t form)
{
    if ((lh_uint_t) form >= (lh_uint_t) cnor_operand_form_count)
    {
        return cnor_op_count;
    }
    return m_index_forms[form];
}

/* -- decoding -------------------------------------------------------------- */

static lh_u32_t
cnor_operand_read(const lh_byte_t *at, cnor_operand_kind_t kind)
{
    switch (kind)
    {
    case cnor_operand_u8:
    case cnor_operand_slot:
        return at[0];
    case cnor_operand_u16:
        return lh_bit_unpack_le16(at);
    case cnor_operand_u32:
    case cnor_operand_s32:
        return lh_bit_unpack_le32(at);
    default:
        return 0U;
    }
}

cnor_error_t
cnor_insn_decode(const lh_byte_t *code, cnor_code_addr_t size, cnor_code_addr_t at,
                 cnor_insn_t *out)
{
    const cnor_op_info_t *info;
    cnor_code_addr_t cursor = at + 1U;
    lh_uint_t i;

    if (at >= size)
    {
        return cnor_error_code_truncated;
    }
    if (!cnor_op_is_valid(code[at]))
    {
        return cnor_error_bad_opcode;
    }
    out->op = (cnor_op_t) code[at];
    info = cnor_op_get_info(out->op);
    if (size - at < info->size)
    {
        return cnor_error_code_truncated;
    }
    out->at = at;
    out->next = at + info->size;
    for (i = 0U; i < CNOR_OP_MAX_OPERANDS; ++i)
    {
        out->operands[i] = cnor_operand_read(code + cursor, info->operands[i]);
        cursor += cnor_operand_get_size(info->operands[i]);
    }
    return cnor_error_none;
}
