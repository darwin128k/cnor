#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* `subject == pattern` on the stack; an `i32` pattern is one `eq_li`. */
static lh_bool_t
cnor_compile_arm_test(cnor_compiler_t *self, const cnor_node_t *pattern, cnor_slot_t subject)
{
    lh_u32_t k;

    if (cnor_compile_int_operand(pattern, &k))
    {
        return cnor_emit_insn(self, cnor_op_eq_li, subject, k);
    }
    return cnor_emit_insn(self, cnor_op_load_local, subject, 0U)
           && cnor_compile_expr(self, pattern) && cnor_emit_op(self, cnor_op_eq);
}

/*
 * The subject is evaluated once into a slot of its own. Each arm:
 *
 *     subject == pattern; jmp_if_false next     (not for `_`)
 *     body; jmp end                             (no jump after the last arm)
 *   next:
 *
 * No arm matching does nothing.
 */
static lh_bool_t
cnor_compile_arm(cnor_compiler_t *self, const cnor_node_t *arm, cnor_slot_t subject,
                 cnor_label_t *end)
{
    cnor_label_t next;

    cnor_label_init(&next);
    self->emit.line = arm->line;
    if (arm->data.arm.pattern != lh_null
        && (!cnor_compile_arm_test(self, arm->data.arm.pattern, subject)
            || !cnor_emit_jump(self, cnor_op_jmp_if_false, &next)))
    {
        return lh_bool_false;
    }
    /* The last arm ends where `end` is bound. */
    return cnor_compile_block(self, arm->data.arm.body)
           && (arm->next == lh_null || cnor_emit_jump(self, cnor_op_jmp, end))
           && cnor_emit_bind(self, &next);
}

/* -- jump table ------------------------------------------------------------ */

/** @def CNOR_MATCH_TABLE_MIN @brief Fewest integer arms worth a `switch`. */
#define CNOR_MATCH_TABLE_MIN 4U
/** @def CNOR_MATCH_TABLE_MAX @brief Most table entries: `switch` counts in a u8. */
#define CNOR_MATCH_TABLE_MAX 255U

/*
 * Whether the arms suit a `switch`: at least CNOR_MATCH_TABLE_MIN arms with
 * distinct `i32` patterns, then at most a final `_`, spanning no more than
 * twice as many values as there are arms. The lowest pattern goes through
 * @p first, the span through @p count.
 */
static lh_bool_t
cnor_match_is_dense(const cnor_node_t *arms, lh_s32_t *first, lh_u32_t *count)
{
    const cnor_node_t *arm;
    lh_s64_t low = 0;
    lh_s64_t high = 0;
    lh_u32_t n = 0U;

    for (arm = arms; arm != lh_null; arm = arm->next)
    {
        const cnor_node_t *other;
        lh_u32_t bits;
        lh_s64_t value;

        if (arm->data.arm.pattern == lh_null && arm->next == lh_null)
        {
            break;
        }
        if (arm->data.arm.pattern == lh_null
            || !cnor_compile_int_operand(arm->data.arm.pattern, &bits))
        {
            return lh_bool_false;
        }
        value = (lh_s32_t) bits;
        for (other = arms; other != arm; other = other->next)
        {
            lh_u32_t seen;

            (void) cnor_compile_int_operand(other->data.arm.pattern, &seen);
            if (seen == bits)
            {
                return lh_bool_false;
            }
        }
        low = n == 0U || value < low ? value : low;
        high = n == 0U || value > high ? value : high;
        ++n;
    }
    if (n < CNOR_MATCH_TABLE_MIN || high - low + 1 > (lh_s64_t) CNOR_MATCH_TABLE_MAX
        || high - low + 1 > 2 * (lh_s64_t) n)
    {
        return lh_bool_false;
    }
    *first = (lh_s32_t) low;
    *count = (lh_u32_t) (high - low + 1);
    return lh_bool_true;
}

/* The arm whose pattern is @p value, or the final `_` (lh_null without one). */
static const cnor_node_t *
cnor_match_arm_for(const cnor_node_t *arms, lh_s64_t value)
{
    const cnor_node_t *arm;

    for (arm = arms; arm != lh_null; arm = arm->next)
    {
        lh_u32_t bits;

        if (arm->data.arm.pattern == lh_null)
        {
            return arm;
        }
        (void) cnor_compile_int_operand(arm->data.arm.pattern, &bits);
        if ((lh_s64_t) (lh_s32_t) bits == value)
        {
            return arm;
        }
    }
    return lh_null;
}

/*
 * Dense integer arms dispatch through one `switch` and its table of jumps
 * instead of testing the arms in turn:
 *
 *     switch subject, first, count
 *     jmp arm(first) ... jmp arm(first + count - 1)
 *   other:                       (values without an arm, and non-matches)
 *     `_` body, if any
 *     jmp end
 *   arm(k):
 *     body; jmp end              (each arm, in source order; not the last)
 *   end:
 *
 * Values without an arm jump to `other`. The patterns are distinct, so the
 * arm a value reaches is the one the tests in turn would have found.
 */
static lh_bool_t
cnor_compile_match_table(cnor_compiler_t *self, const cnor_node_t *node, cnor_slot_t subject,
                         lh_s32_t first, lh_u32_t count, cnor_label_t *end)
{
    cnor_label_t labels[CNOR_MATCH_TABLE_MAX];
    cnor_label_t other;
    const cnor_node_t *arms = node->data.match.arms;
    const cnor_node_t *arm;
    lh_u32_t n = 0U;
    lh_u32_t i;

    cnor_label_init(&other);
    for (arm = arms; arm != lh_null && arm->data.arm.pattern != lh_null; arm = arm->next)
    {
        cnor_label_init(&labels[n++]);
    }
    if (!cnor_emit_insn3(self, cnor_op_switch, subject, (lh_u32_t) first, count))
    {
        return lh_bool_false;
    }
    for (i = 0U; i < count; ++i)
    {
        const cnor_node_t *target = cnor_match_arm_for(arms, (lh_s64_t) first + i);
        cnor_label_t *label = &other;
        lh_u32_t k = 0U;

        if (target != lh_null && target->data.arm.pattern != lh_null)
        {
            for (arm = arms; arm != target; arm = arm->next)
            {
                ++k;
            }
            label = &labels[k];
        }
        /* The switch reaches every entry, not only the first. */
        self->emit.reachable = lh_bool_true;
        if (!cnor_emit_jump(self, cnor_op_jmp, label))
        {
            return lh_bool_false;
        }
    }
    self->emit.reachable = lh_bool_true;
    if (!cnor_emit_bind(self, &other))
    {
        return lh_bool_false;
    }
    /* The `_` arm is the last one, when there is one. */
    for (arm = arms; arm != lh_null && arm->data.arm.pattern != lh_null; arm = arm->next)
    {
    }
    if (arm != lh_null)
    {
        self->emit.line = arm->line;
        if (!cnor_compile_block(self, arm->data.arm.body))
        {
            return lh_bool_false;
        }
    }
    if (!cnor_emit_jump(self, cnor_op_jmp, end))
    {
        return lh_bool_false;
    }
    for (arm = arms, i = 0U; i < n; arm = arm->next, ++i)
    {
        self->emit.line = arm->line;
        if (!cnor_emit_bind(self, &labels[i]) || !cnor_compile_block(self, arm->data.arm.body)
            || (i + 1U < n && !cnor_emit_jump(self, cnor_op_jmp, end)))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* -- match ----------------------------------------------------------------- */

lh_bool_t
cnor_compile_match(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_scope_mark_t mark = cnor_scope_enter_block(&self->scope);
    const cnor_node_t *arm;
    cnor_slot_t subject = 0U;
    cnor_label_t end;
    lh_s32_t first = 0;
    lh_u32_t count = 0U;
    lh_bool_t ok;

    cnor_label_init(&end);
    ok = cnor_compile_expr(self, node->data.match.subject)
         && cnor_scope_add_local(self, node, lh_str_view_empty(), lh_bool_false, &subject)
         && cnor_emit_insn(self, cnor_op_store_local, subject, 0U);
    if (ok && cnor_match_is_dense(node->data.match.arms, &first, &count))
    {
        self->emit.line = node->line;
        ok = cnor_compile_match_table(self, node, subject, first, count, &end);
    }
    else
    {
        for (arm = node->data.match.arms; ok && arm != lh_null; arm = arm->next)
        {
            ok = cnor_compile_arm(self, arm, subject, &end);
        }
    }
    ok = ok && cnor_emit_bind(self, &end);
    cnor_scope_leave_block(&self->scope, mark);
    return ok;
}
