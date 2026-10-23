#include <cnor/compiler/internal.h>
#include <lh/bit/endian.h>

/* Ends a label's chain of pending jumps. */
#define CNOR_LABEL_END 0xFFFFFFFFU

/* -- state ----------------------------------------------------------------- */

void
cnor_emit_init(cnor_emit_t *self, lh_bool_t emit_lines)
{
    cnor_byte_writer_init(&self->code);
    cnor_byte_writer_init(&self->lines);
    self->line_count = 0U;
    self->last_line = 0U;
    self->line = 0U;
    self->emit_lines = emit_lines;
    cnor_emit_begin_function(self);
}

void
cnor_emit_deinit(cnor_emit_t *self)
{
    cnor_byte_writer_deinit(&self->code);
    cnor_byte_writer_deinit(&self->lines);
}

cnor_code_addr_t
cnor_emit_get_addr(const cnor_emit_t *self)
{
    return cnor_byte_writer_get_size(&self->code);
}

void
cnor_emit_begin_function(cnor_emit_t *self)
{
    self->depth = 0;
    self->max_depth = 0;
    self->reachable = lh_bool_true;
}

/* -- lines ----------------------------------------------------------------- */

static void
cnor_emit_record_line(cnor_emit_t *self)
{
    if (!self->emit_lines || self->line == 0U || self->line == self->last_line)
    {
        return;
    }
    cnor_byte_writer_put_u32(&self->lines, cnor_emit_get_addr(self));
    cnor_byte_writer_put_u32(&self->lines, self->line);
    self->line_count++;
    self->last_line = self->line;
}

/* -- stack depth ----------------------------------------------------------- */

/* Apply the stack effect of @p op from the op table. */
static lh_bool_t
cnor_emit_track(cnor_compiler_t *self, cnor_op_t op, lh_u32_t argc)
{
    const cnor_op_info_t *info = cnor_op_get_info(op);
    cnor_emit_t *emit = &self->emit;
    lh_s32_t pops = info->pops;

    if ((info->flags & CNOR_OP_FLAG_POPS_ARGC) != 0U)
    {
        pops += (lh_s32_t) argc;
    }
    if (pops > emit->depth)
    {
        /* Only a compiler bug gets here; refuse rather than emit bad code. */
        return cnor_diag_set(self->diag, cnor_error_bad_stack, emit->line, cnor_emit_get_addr(emit));
    }
    emit->depth += (lh_s32_t) info->pushes - pops;
    if (emit->depth > emit->max_depth)
    {
        emit->max_depth = emit->depth;
    }
    if ((info->flags & CNOR_OP_FLAG_TERMINATOR) != 0U)
    {
        emit->reachable = lh_bool_false;
    }
    return lh_bool_true;
}

/* -- instructions ---------------------------------------------------------- */

static void
cnor_emit_operand(cnor_emit_t *self, cnor_operand_kind_t kind, lh_u32_t value)
{
    switch (kind)
    {
    case cnor_operand_u8:
    case cnor_operand_slot:
        cnor_byte_writer_put_u8(&self->code, (lh_u8_t) value);
        break;
    case cnor_operand_u16:
        cnor_byte_writer_put_u16(&self->code, (lh_u16_t) value);
        break;
    case cnor_operand_u32:
    case cnor_operand_s32:
        cnor_byte_writer_put_u32(&self->code, value);
        break;
    default:
        break;
    }
}

lh_bool_t
cnor_emit_insn3(cnor_compiler_t *self, cnor_op_t op, lh_u32_t a, lh_u32_t b, lh_u32_t c)
{
    const cnor_op_info_t *info = cnor_op_get_info(op);

    cnor_emit_record_line(&self->emit);
    cnor_byte_writer_put_u8(&self->emit.code, (lh_u8_t) op);
    cnor_emit_operand(&self->emit, info->operands[0], a);
    cnor_emit_operand(&self->emit, info->operands[1], b);
    cnor_emit_operand(&self->emit, info->operands[2], c);
    return cnor_emit_track(self, op, b);
}

lh_bool_t
cnor_emit_insn(cnor_compiler_t *self, cnor_op_t op, lh_u32_t a, lh_u32_t b)
{
    return cnor_emit_insn3(self, op, a, b, 0U);
}

lh_bool_t
cnor_emit_op(cnor_compiler_t *self, cnor_op_t op)
{
    return cnor_emit_insn(self, op, 0U, 0U);
}

/* -- labels ---------------------------------------------------------------- */

void
cnor_label_init(cnor_label_t *label)
{
    label->addr = 0U;
    label->pending = CNOR_LABEL_END;
    label->depth = 0;
    label->bound = lh_bool_false;
    label->has_depth = lh_bool_false;
}

/* The label's depth must be the same from every edge that reaches it. */
static lh_bool_t
cnor_emit_meet(cnor_compiler_t *self, cnor_label_t *label, lh_s32_t depth)
{
    if (!label->has_depth)
    {
        label->depth = depth;
        label->has_depth = lh_bool_true;
        return lh_bool_true;
    }
    if (label->depth != depth)
    {
        return cnor_diag_set(self->diag, cnor_error_bad_stack, self->emit.line,
                             cnor_emit_get_addr(&self->emit));
    }
    return lh_bool_true;
}

/* Distance from the end of the jump whose operand is at @p operand_at (right
 * after its opcode) to @p target. */
static lh_u32_t
cnor_emit_distance(const lh_byte_t *code, lh_u32_t operand_at, cnor_code_addr_t target)
{
    const cnor_op_info_t *info = cnor_op_get_info((cnor_op_t) code[operand_at - 1U]);

    return (lh_u32_t) ((lh_s64_t) target - (lh_s64_t) (operand_at - 1U + info->size));
}

lh_bool_t
cnor_emit_jump_with(cnor_compiler_t *self, cnor_op_t op, cnor_label_t *label, lh_u32_t b,
                    lh_u32_t c)
{
    cnor_emit_t *emit = &self->emit;
    const cnor_code_addr_t operand_at = cnor_emit_get_addr(emit) + 1U;
    const lh_bool_t was_reachable = emit->reachable;
    lh_u32_t operand;

    if (label->bound)
    {
        /* Backward: the distance is known now. */
        operand = (lh_u32_t) ((lh_s64_t) label->addr
                              - (lh_s64_t) (operand_at - 1U + cnor_op_get_info(op)->size));
    }
    else
    {
        /* Forward: link this jump into the label's chain. */
        operand = label->pending;
        label->pending = operand_at;
    }
    if (!cnor_emit_insn3(self, op, operand, b, c))
    {
        return lh_bool_false;
    }
    return was_reachable ? cnor_emit_meet(self, label, emit->depth)
                         : lh_bool_true;
}

lh_bool_t
cnor_emit_jump(cnor_compiler_t *self, cnor_op_t op, cnor_label_t *label)
{
    return cnor_emit_jump_with(self, op, label, 0U, 0U);
}

lh_bool_t
cnor_emit_bind(cnor_compiler_t *self, cnor_label_t *label)
{
    cnor_emit_t *emit = &self->emit;
    lh_byte_t *code;

    label->addr = cnor_emit_get_addr(emit);
    label->bound = lh_bool_true;
    code = cnor_byte_writer_get_data(&emit->code);
    while (label->pending != CNOR_LABEL_END)
    {
        const lh_u32_t at = label->pending;

        label->pending = lh_bit_unpack_le32(code + at);
        lh_bit_pack_le32(cnor_emit_distance(code, at, label->addr), code + at);
    }
    if (emit->reachable && !cnor_emit_meet(self, label, emit->depth))
    {
        return lh_bool_false;
    }
    if (label->has_depth)
    {
        emit->depth = label->depth;
    }
    emit->reachable = lh_bool_true;
    return lh_bool_true;
}
