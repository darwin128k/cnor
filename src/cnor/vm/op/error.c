#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>

/* -- error handling ---------------------------------------------------------- */

/*
 * A `try` marks a catch block: `try_push` puts it on the stack of tries,
 * and the machine unwinds to it when an error happens inside the `try`.
 *
 * The tries are the machine's own: no `longjmp`, no unwinder tables, no
 * C++ runtime. The hot path does not know they exist: `try_push` and
 * `try_pop` are ordinary instructions, and an instruction inside the `try`
 * costs what it costs anywhere else.
 *
 * The error the catch block receives is a dictionary of four pairs:
 *
 *     "message"  the text of the failure
 *     "line"     the line it happened on
 *     "code"     the number of the failure (::cnor_error_t)
 *     "file"     the file it happened in
 *
 * The dictionary lives in the slots the compiler reserved for it, so catching
 * an error allocates nothing.
 */

/* The keys of the error's dictionary. */
static const char m_key_message[] = "message";
static const char m_key_line[] = "line";
static const char m_key_code[] = "code";
static const char m_key_file[] = "file";

/* How many pairs the error's dictionary holds. */
#define CNOR_ERROR_PAIRS 4U

/* Mark the catch block at @p insn's operand; the error's dictionary goes into
 * the slot the other operand names. */
lh_bool_t
cnor_vm_op_try_push(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_vm_try_t *entry;

    if (vm->try_count >= (cnor_count_t) vm->config.max_depth)
    {
        /* More `try` blocks than the machine has room for. */
        return cnor_vm_fail(vm, insn, cnor_error_too_many_tries);
    }
    entry = &vm->tries[vm->try_count++];
    entry->catch_addr = (cnor_code_addr_t) cnor_insn_get_jump_target(insn);
    entry->fp = vm->fp;
    entry->depth = vm->depth;
    entry->slot = (lh_u8_t) insn->operands[1];
    return lh_bool_true;
}

/* The `try` ended without an error. */
lh_bool_t
cnor_vm_op_try_pop(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    (void) insn;
    if (vm->try_count > 0U)
    {
        --vm->try_count;
    }
    return lh_bool_true;
}

/* The tries of the frame that is leaving are gone with it. */
void
cnor_vm_tries_leave(cnor_vm_t *vm)
{
    while (vm->try_count > 0U
           && vm->tries[vm->try_count - 1U].depth >= vm->depth)
    {
        --vm->try_count;
    }
}

/* Write the error's dictionary into the slots at @p slot: the value, the
 * header (the count and the room) and the pairs. */
static void
cnor_vm_error_dict(cnor_vm_t *vm, cnor_slot_t slot, const cnor_error_t code,
                   const cnor_code_addr_t at)
{
    lh_value_t *const cells = cnor_vm_stack_at(&vm->stack, vm->fp + slot);
    lh_value_t *const pairs = &cells[3];
    const lh_str_view_t file = cnor_vm_get_file_at(vm, at);
    const char *const message = cnor_error_get_message(code);

    cnor_vm_fast_set_sint(&cells[1], (lh_sllong_t) CNOR_ERROR_PAIRS);
    cnor_vm_fast_set_sint(&cells[2], (lh_sllong_t) CNOR_ERROR_PAIRS);
    cnor_vm_fast_set_str(&pairs[0], m_key_message, sizeof(m_key_message) - 1U);
    cnor_vm_fast_set_str(&pairs[2], m_key_line, sizeof(m_key_line) - 1U);
    cnor_vm_fast_set_str(&pairs[4], m_key_code, sizeof(m_key_code) - 1U);
    cnor_vm_fast_set_str(&pairs[6], m_key_file, sizeof(m_key_file) - 1U);
    cnor_vm_fast_set_str(&pairs[1], message, strlen(message));
    cnor_vm_fast_set_sint(&pairs[3], (lh_sllong_t) cnor_vm_get_line_at(vm, at));
    cnor_vm_fast_set_sint(&pairs[5], (lh_sllong_t) code);
    cnor_vm_fast_set_str(&pairs[7], lh_str_view_get_data(&file),
                         lh_str_view_get_size(&file));
    cnor_vm_fast_set_dict(&cells[0], &cells[1]);
}

/* Drop the frame on top of the call stack: the machine goes back to the
 * caller, as a `ret` would. */
static void
cnor_vm_unwind_frame(cnor_vm_t *vm)
{
    const cnor_vm_frame_t *const frame = &vm->frames[--vm->depth];

    vm->ip = frame->return_addr;
    vm->fp = frame->fp;
    vm->function = frame->function;
}

/**
 * @brief Catch the failure the machine has just met, if a `try` wants it.
 *
 * The tries are looked at from the top: the ones whose frame is gone are
 * dropped, the ones in a frame that is deeper than the failure's are left for
 * the frame that holds them. The first handler in the frame the failure
 * happened in takes it.
 *
 * @return Whether a handler took the failure.
 */
lh_bool_t
cnor_vm_catch(cnor_vm_t *vm)
{
    const cnor_error_t code = cnor_diag_get_code(&vm->diag);
    const cnor_code_addr_t at = (cnor_code_addr_t) cnor_diag_get_offset(&vm->diag);

    while (vm->try_count > 0U)
    {
        const cnor_vm_try_t *const entry = &vm->tries[vm->try_count - 1U];

        if (entry->depth > vm->depth)
        {
            /* The frame the `try` was made in is gone. */
            --vm->try_count;
            continue;
        }
        if (entry->depth < vm->depth)
        {
            /* The failure happened deeper: leave the frame it happened in. */
            cnor_vm_unwind_frame(vm);
            continue;
        }
        /* The `try` is in the frame the failure happened in: it takes it. */
        vm->try_count--;
        vm->stack.sp = vm->fp + vm->functions[vm->function].slot_count;
        cnor_vm_error_dict(vm, (cnor_slot_t) entry->slot, code, at);
        vm->ip = entry->catch_addr;
        vm->running = lh_bool_true;
        cnor_diag_init(&vm->diag);
        return lh_bool_true;
    }
    return lh_bool_false;
}
