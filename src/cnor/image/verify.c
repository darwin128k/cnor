#include <cnor/image/verify.h>
#include <cnor/op.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>

/* Depth of an address that is not an instruction start. */
#define CNOR_VERIFY_NOT_INSN (-2)
/* Depth of an instruction start no path has reached yet. */
#define CNOR_VERIFY_UNREACHED (-1)

/**
 * State for one function: its table entry and, per code byte of the function,
 * the stack depth on entry to the instruction starting there.
 */
struct cnor_verify
{
    const cnor_image_view_t *view;
    cnor_diag_t *diag;
    cnor_image_function_t function;
    lh_s32_t *depth;
    lh_bool_t changed;
};

static lh_bool_t
cnor_verify_fail(struct cnor_verify *self, cnor_error_t code, cnor_code_addr_t at)
{
    return cnor_diag_set(self->diag, code, cnor_image_view_line_at(self->view, at), at);
}

/* -- operands -------------------------------------------------------------- */

static lh_bool_t
cnor_verify_call(struct cnor_verify *self, const cnor_insn_t *insn)
{
    cnor_image_function_t callee;

    if (insn->operands[0] >= self->view->function_count)
    {
        return lh_bool_false;
    }
    callee = cnor_image_view_function_at(self->view, (cnor_function_index_t) insn->operands[0]);
    return insn->operands[1] == callee.param_count ? lh_bool_true : lh_bool_false;
}

/* A `switch` table is `count` (at least one) `jmp` instructions right after
 * it, inside the function; that they decode and where they jump is checked
 * with every other instruction. */
static lh_bool_t
cnor_verify_switch(const struct cnor_verify *self, const cnor_insn_t *insn)
{
    const lh_u64_t count = insn->operands[2];
    const lh_u64_t end = (lh_u64_t) self->function.entry + self->function.size;
    lh_u64_t i;

    if (count == 0U || (lh_u64_t) insn->next + count * CNOR_OP_SWITCH_ENTRY_SIZE > end)
    {
        return lh_bool_false;
    }
    for (i = 0U; i < count; ++i)
    {
        if (self->view->code[insn->next + i * CNOR_OP_SWITCH_ENTRY_SIZE] != (lh_byte_t) cnor_op_jmp)
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* Every operand of kind `slot` names a slot of the function's frame. */
static lh_bool_t
cnor_verify_slots(const struct cnor_verify *self, const cnor_insn_t *insn)
{
    const cnor_op_info_t *info = cnor_op_get_info(insn->op);
    lh_uint_t i;

    for (i = 0U; i < CNOR_OP_MAX_OPERANDS; ++i)
    {
        if (info->operands[i] == cnor_operand_slot
            && insn->operands[i] >= self->function.slot_count)
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* An `array_new` writes its elements in the slots after the array's own, so
 * they must be inside the function's frame. */
static lh_bool_t
cnor_verify_array(const struct cnor_verify *self, const cnor_insn_t *insn)
{
    const lh_u64_t slot = insn->operands[0];
    const lh_u64_t count = insn->operands[1];

    return slot + 1U + count <= (lh_u64_t) self->function.slot_count ? lh_bool_true
                                                                    : lh_bool_false;
}

/* A `dict_new` writes its header and its pairs in the slots after the
 * dictionary's own, so they must be inside the function's frame. */
static lh_bool_t
cnor_verify_dict(const struct cnor_verify *self, const cnor_insn_t *insn)
{
    const lh_u64_t slot = insn->operands[0];
    const lh_u64_t room = insn->operands[1];

    return slot + 3U + room * 2U <= (lh_u64_t) self->function.slot_count ? lh_bool_true
                                                                         : lh_bool_false;
}

/* A global array or dictionary takes its slots in the globals, so they must be
 * inside them. */
static lh_bool_t
cnor_verify_global(const struct cnor_verify *self, const cnor_insn_t *insn)
{
    const lh_u64_t index = insn->operands[0];
    const lh_u64_t count = insn->operands[1];
    const lh_u64_t slots = insn->op == cnor_op_dict_new_global ? count * 2U + 3U
                                                               : count + 1U;

    return index + slots <= (lh_u64_t) self->view->header.globals_count ? lh_bool_true
                                                                        : lh_bool_false;
}

static lh_bool_t
cnor_verify_operands(struct cnor_verify *self, const cnor_insn_t *insn)
{
    const lh_u32_t operand = insn->operands[0];

    if (!cnor_verify_slots(self, insn))
    {
        return lh_bool_false;
    }
    switch (insn->op)
    {
    case cnor_op_push_const:
        return operand < self->view->const_count;
    case cnor_op_dict_get_lk:
        return insn->operands[1] < self->view->const_count;
    case cnor_op_load_global:
    case cnor_op_store_global:
        return operand < self->view->header.globals_count;
    case cnor_op_call:
        return cnor_verify_call(self, insn);
    case cnor_op_native:
        return operand < self->view->native_count;
    case cnor_op_switch:
        return cnor_verify_switch(self, insn);
    case cnor_op_array_new:
        return cnor_verify_array(self, insn);
    case cnor_op_dict_new:
        return cnor_verify_dict(self, insn);
    case cnor_op_array_new_global:
    case cnor_op_dict_new_global:
        return cnor_verify_global(self, insn);
    case cnor_op_try_push:
        /* A catch writes a dictionary: value, two header cells, five pairs. */
        return insn->operands[0] == 0U
               || insn->operands[1] + 13U <= self->function.slot_count;
    default:
        return lh_bool_true;
    }
}

/* -- structure ------------------------------------------------------------- */

static lh_s32_t *
cnor_verify_depth_at(struct cnor_verify *self, cnor_code_addr_t addr)
{
    return &self->depth[addr - self->function.entry];
}

static lh_bool_t
cnor_verify_in_function(const struct cnor_verify *self, lh_s64_t addr)
{
    return (addr >= (lh_s64_t) self->function.entry
            && addr < (lh_s64_t) self->function.entry + (lh_s64_t) self->function.size)
               ? lh_bool_true
               : lh_bool_false;
}

/* Decode every instruction once: operands, bounds, the final terminator. */
static lh_bool_t
cnor_verify_decode(struct cnor_verify *self)
{
    const cnor_code_addr_t end = self->function.entry + self->function.size;
    cnor_code_addr_t at = self->function.entry;
    cnor_insn_t insn;

    insn.op = cnor_op_halt;
    while (at < end)
    {
        const cnor_error_t error = cnor_insn_decode(self->view->code, end, at, &insn);

        if (error != cnor_error_none)
        {
            return cnor_verify_fail(self, error, at);
        }
        if (!cnor_verify_operands(self, &insn))
        {
            return cnor_verify_fail(self, cnor_error_bad_operand, at);
        }
        *cnor_verify_depth_at(self, at) = CNOR_VERIFY_UNREACHED;
        at = insn.next;
    }
    if ((cnor_op_get_info(insn.op)->flags & CNOR_OP_FLAG_TERMINATOR) == 0U)
    {
        return cnor_verify_fail(self, cnor_error_no_terminator, end - 1U);
    }
    return lh_bool_true;
}

/* -- stack depth ----------------------------------------------------------- */

static lh_bool_t
cnor_verify_reach(struct cnor_verify *self, lh_s64_t target, lh_s32_t depth, cnor_code_addr_t from)
{
    lh_s32_t *slot;

    if (!cnor_verify_in_function(self, target))
    {
        return cnor_verify_fail(self, cnor_error_bad_jump, from);
    }
    slot = cnor_verify_depth_at(self, (cnor_code_addr_t) target);
    if (*slot == CNOR_VERIFY_NOT_INSN)
    {
        return cnor_verify_fail(self, cnor_error_bad_jump, from);
    }
    if (*slot == CNOR_VERIFY_UNREACHED)
    {
        *slot = depth;
        self->changed = lh_bool_true;
        return lh_bool_true;
    }
    return *slot == depth ? lh_bool_true : cnor_verify_fail(self, cnor_error_bad_stack, from);
}

static lh_bool_t
cnor_verify_step(struct cnor_verify *self, const cnor_insn_t *insn, lh_s32_t depth)
{
    const cnor_op_info_t *info = cnor_op_get_info(insn->op);
    const lh_s32_t pops = (lh_s32_t) cnor_insn_get_pops(insn);
    const lh_s32_t after = depth - pops + (lh_s32_t) info->pushes;

    if (pops > depth || after > (lh_s32_t) self->function.max_stack)
    {
        return cnor_verify_fail(self, cnor_error_bad_stack, insn->at);
    }
    if ((info->flags & CNOR_OP_FLAG_JUMP) != 0U
        && !cnor_verify_reach(self, cnor_insn_get_jump_target(insn), after, insn->at))
    {
        return lh_bool_false;
    }
    if ((info->flags & CNOR_OP_FLAG_TERMINATOR) == 0U
        && !cnor_verify_reach(self, (lh_s64_t) insn->next, after, insn->at))
    {
        return lh_bool_false;
    }
    if (insn->op == cnor_op_switch)
    {
        /* Every later table entry, and the code after the table. */
        lh_u32_t i;

        for (i = 1U; i <= insn->operands[2]; ++i)
        {
            if (!cnor_verify_reach(self, (lh_s64_t) insn->next + i * CNOR_OP_SWITCH_ENTRY_SIZE,
                                   after, insn->at))
            {
                return lh_bool_false;
            }
        }
    }
    return lh_bool_true;
}

/* Propagate depths until nothing changes; each start is set at most once. */
static lh_bool_t
cnor_verify_flow(struct cnor_verify *self)
{
    const cnor_code_addr_t end = self->function.entry + self->function.size;

    *cnor_verify_depth_at(self, self->function.entry) = 0;
    do
    {
        cnor_code_addr_t at = self->function.entry;

        self->changed = lh_bool_false;
        while (at < end)
        {
            cnor_insn_t insn;
            const lh_s32_t depth = *cnor_verify_depth_at(self, at);

            cnor_insn_decode(self->view->code, end, at, &insn);
            if (depth >= 0 && !cnor_verify_step(self, &insn, depth))
            {
                return lh_bool_false;
            }
            at = insn.next;
        }
    } while (self->changed);
    return lh_bool_true;
}

/* -- functions ------------------------------------------------------------- */

static lh_bool_t
cnor_verify_frame(struct cnor_verify *self)
{
    const cnor_image_function_t *function = &self->function;
    const cnor_count_t files = self->view->file_count > 0U ? self->view->file_count : 1U;

    if (function->size == 0U || function->slot_count < function->param_count
        || (lh_u32_t) function->slot_count + function->max_stack > self->view->header.stack_slots
        || function->file >= files)
    {
        return cnor_verify_fail(self, cnor_error_bad_header, function->entry);
    }
    return lh_bool_true;
}

static lh_bool_t
cnor_verify_function(struct cnor_verify *self)
{
    lh_bool_t ok;
    lh_u32_t i;

    if (!cnor_verify_frame(self))
    {
        return lh_bool_false;
    }
    self->depth = (lh_s32_t *) lh_runtime_allocator_alloc(self->function.size * sizeof(lh_s32_t));
    if (self->depth == lh_null)
    {
        return cnor_verify_fail(self, cnor_error_no_memory, self->function.entry);
    }
    for (i = 0U; i < self->function.size; ++i)
    {
        self->depth[i] = CNOR_VERIFY_NOT_INSN;
    }
    ok = cnor_verify_decode(self) && cnor_verify_flow(self);
    lh_runtime_allocator_free(self->depth);
    self->depth = lh_null;
    return ok;
}

lh_bool_t
cnor_image_verify(const cnor_image_view_t *view, cnor_diag_t *diag)
{
    struct cnor_verify state;
    cnor_code_addr_t expected = 0U;
    cnor_count_t i;

    state.view = view;
    state.diag = diag;
    state.depth = lh_null;
    for (i = 0U; i < view->function_count; ++i)
    {
        state.function = cnor_image_view_function_at(view, (cnor_function_index_t) i);
        if (state.function.entry != expected || state.function.size > view->code_size - expected)
        {
            return cnor_verify_fail(&state, cnor_error_bad_header, state.function.entry);
        }
        if (!cnor_verify_function(&state))
        {
            return lh_bool_false;
        }
        expected += state.function.size;
    }
    if (expected != view->code_size)
    {
        return cnor_verify_fail(&state, cnor_error_bad_header, expected);
    }
    return lh_bool_true;
}
