#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>

/* -- error handling ---------------------------------------------------------- */

/*
 * A `try` marks a catch block and a finally block: `try_push` puts them on
 * the stack of tries, and the machine unwinds to the catch block when an
 * error happens inside the `try`. The finally block runs when the `try` is
 * left, however it is left: the code after it went fine, an error happened, or
 * the code asked to go elsewhere.
 *
 * The tries are the machine's own: no `longjmp`, no unwinder tables, no
 * C++ runtime. The hot path does not know they exist: `try_push` and
 * `try_pop` are ordinary instructions, and an instruction inside the `try`
 * costs what it costs anywhere else.
 *
 * The error the catch block receives is a dictionary of five pairs:
 *
 *     "message"  the text of the failure
 *     "line"     the line it happened on
 *     "code"     the number of the failure (::cnor_error_t)
 *     "kind"     the name of the failure, which does not change with the order
 *     "file"     the file it happened in
 *
 * The dictionary lives in the slots the compiler reserved for it, so catching
 * an error allocates nothing.
 */

/* The keys of the error's dictionary. */
static const char m_key_message[] = "message";
static const char m_key_line[] = "line";
static const char m_key_code[] = "code";
static const char m_key_kind[] = "kind";
static const char m_key_file[] = "file";

/* How many pairs the error's dictionary holds. */
#define CNOR_ERROR_PAIRS 5U

/* The address of a block the instruction marks: zero when it is not there.
 * The compiler keeps the blocks away from the instruction itself, so a zero
 * offset means "there is no such block". */
static cnor_code_addr_t
cnor_vm_try_addr(const cnor_insn_t *insn, lh_uint_t index)
{
    const lh_s32_t rel = (lh_s32_t) insn->operands[index];

    if (rel == 0)
    {
        return 0U;
    }
    return (cnor_code_addr_t) ((lh_s64_t) insn->next + (lh_s64_t) rel);
}

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
    entry->catch_addr = cnor_vm_try_addr(insn, 0U);
    entry->finally_addr = 0U;
    entry->depth = vm->depth;
    entry->slot = (lh_u8_t) insn->operands[1];
    entry->flags = 0U;
    entry->error = cnor_error_none;
    entry->trace_head = CNOR_VM_TRACE_NONE;
    entry->trace_truncated = lh_bool_false;
    return lh_bool_true;
}

/* Mark the finally block of the `try` that is open. */
lh_bool_t
cnor_vm_op_try_finally(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    if (vm->try_count == 0U)
    {
        return lh_bool_true;
    }
    vm->tries[vm->try_count - 1U].finally_addr = cnor_vm_try_addr(insn, 0U);
    return lh_bool_true;
}

/* The `try` is over: the code that may fail is done, the catch block will not
 * take another error, and the error it took is handled. A `try` with a
 * finally block keeps its handler until the finally block ends. */
lh_bool_t
cnor_vm_op_try_pop(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_vm_try_t *entry;

    (void) insn;
    if (vm->try_count == 0U)
    {
        return lh_bool_true;
    }
    entry = &vm->tries[vm->try_count - 1U];
    entry->flags |= (lh_u8_t) (CNOR_VM_TRY_CLOSED | CNOR_VM_TRY_CLEANED);
    entry->error = cnor_error_none;
    cnor_vm_trace_release_try(vm, entry);
    if (entry->finally_addr == 0U)
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
        cnor_vm_trace_release_try(vm, &vm->tries[vm->try_count - 1U]);
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
    const char *const kind = cnor_error_get_name(code);

    cnor_vm_fast_set_sint(&cells[1], (lh_sllong_t) CNOR_ERROR_PAIRS);
    cnor_vm_fast_set_sint(&cells[2], (lh_sllong_t) CNOR_ERROR_PAIRS);
    cnor_vm_fast_set_str(&pairs[0], m_key_message, sizeof(m_key_message) - 1U);
    cnor_vm_fast_set_str(&pairs[2], m_key_line, sizeof(m_key_line) - 1U);
    cnor_vm_fast_set_str(&pairs[4], m_key_code, sizeof(m_key_code) - 1U);
    cnor_vm_fast_set_str(&pairs[6], m_key_kind, sizeof(m_key_kind) - 1U);
    cnor_vm_fast_set_str(&pairs[8], m_key_file, sizeof(m_key_file) - 1U);
    if (code == cnor_error_script && vm->script_error.size != 0U)
    {
        /* The script failed with its own text: that text is the message. */
        cnor_vm_fast_set_str(&pairs[1], (const lh_char_t *) vm->script_error.begin,
                             vm->script_error.size);
    }
    else
    {
        cnor_vm_fast_set_str(&pairs[1], message, strlen(message));
    }
    cnor_vm_fast_set_sint(&pairs[3], (lh_sllong_t) cnor_vm_get_line_at(vm, at));
    cnor_vm_fast_set_sint(&pairs[5], (lh_sllong_t) code);
    cnor_vm_fast_set_str(&pairs[7], kind, strlen(kind));
    cnor_vm_fast_set_str(&pairs[9], lh_str_view_get_data(&file),
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
 * @brief Give the error @p code that happened at @p at to a handler.
 *
 * The tries are looked at from the top: the ones whose frame is gone are
 * dropped, the ones in a frame that is deeper than the failure's are left for
 * the frame that holds them. The first handler in the frame the failure
 * happened in takes it.
 *
 * A handler with a catch block takes the error and the catch block runs. A
 * handler that has only a finally block runs the finally block first, and the
 * error goes on outward when the finally block ends.
 *
 * @return Whether a handler took the error.
 */
lh_bool_t
cnor_vm_unwind(cnor_vm_t *vm, cnor_error_t code, cnor_code_addr_t at)
{
    while (vm->try_count > 0U)
    {
        cnor_vm_try_t *const entry = &vm->tries[vm->try_count - 1U];

        if (entry->depth > vm->depth)
        {
            /* The frame the `try` was made in is gone. */
            cnor_vm_trace_release_try(vm, &vm->tries[vm->try_count - 1U]);
            --vm->try_count;
            continue;
        }
        if (entry->depth < vm->depth)
        {
            /* The failure happened deeper: leave the frame it happened in. */
            cnor_vm_unwind_frame(vm);
            continue;
        }
        /* The `try` is in the frame the failure happened in. */
        if (entry->catch_addr != 0U
            && (entry->flags & (lh_u8_t) CNOR_VM_TRY_CLOSED) == 0U)
        {
            /* The catch block takes it. */
            cnor_vm_trace_save_try(vm, entry);
            entry->error = code;
            entry->error_at = at;
            entry->flags |= (lh_u8_t) CNOR_VM_TRY_CLOSED;
            vm->stack.sp = vm->fp + vm->functions[vm->function].slot_count;
            cnor_vm_error_dict(vm, (cnor_slot_t) entry->slot, code, at);
            vm->ip = entry->catch_addr;
            vm->running = lh_bool_true;
            cnor_diag_init(&vm->diag);
            return lh_bool_true;
        }
        if (entry->finally_addr != 0U
            && (entry->flags & (lh_u8_t) CNOR_VM_TRY_CLEANED) == 0U)
        {
            /* The finally block runs first; the error waits for it. */
            cnor_vm_trace_save_try(vm, entry);
            entry->error = code;
            entry->error_at = at;
            entry->flags |= (lh_u8_t) (CNOR_VM_TRY_CLOSED | CNOR_VM_TRY_CLEANED);
            vm->stack.sp = vm->fp + vm->functions[vm->function].slot_count;
            vm->ip = entry->finally_addr;
            vm->running = lh_bool_true;
            cnor_diag_init(&vm->diag);
            return lh_bool_true;
        }
        /* Nothing here can take it: drop the handler and look further out. */
        cnor_vm_trace_release_try(vm, &vm->tries[vm->try_count - 1U]);
        --vm->try_count;
    }
    /* Nobody took the error: the run fails with it. */
    (void) cnor_diag_set(&vm->diag, code, cnor_vm_get_line_at(vm, at), at);
    cnor_diag_set_file(&vm->diag, cnor_vm_get_file_at(vm, at));
    return lh_bool_false;
}

/**
 * @brief Catch the failure the machine has just met, if a `try` wants it.
 *
 * @return Whether a handler took the failure.
 */
lh_bool_t
cnor_vm_catch(cnor_vm_t *vm)
{
    const cnor_error_t code = cnor_diag_get_code(&vm->diag);
    const cnor_code_addr_t at = (cnor_code_addr_t) cnor_diag_get_offset(&vm->diag);

    return cnor_vm_unwind(vm, code, at);
}

/* The end of a finally block: the handler is gone. An error it was holding
 * goes on outward, and the jump it was asked to do happens now. */
lh_bool_t
cnor_vm_op_finally_end(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_vm_try_t *entry;
    cnor_error_t error;
    cnor_code_addr_t error_at;
    cnor_code_addr_t action_addr;
    lh_u8_t flags;

    (void) insn;
    if (vm->try_count == 0U)
    {
        return lh_bool_true;
    }
    entry = &vm->tries[vm->try_count - 1U];
    error = entry->error;
    error_at = entry->error_at;
    action_addr = entry->action_addr;
    flags = entry->flags;
    if (error != cnor_error_none) cnor_vm_trace_restore_try(vm, entry);
    cnor_vm_trace_release_try(vm, &vm->tries[vm->try_count - 1U]);
    --vm->try_count;

    if (error != cnor_error_none)
    {
        /* The finally block did not handle the error: it goes on outward. */
        return cnor_vm_unwind(vm, error, error_at);
    }
    if ((flags & (lh_u8_t) CNOR_VM_TRY_ACTION) != 0U)
    {
        /* The machine was asked to go somewhere when the block ended. */
        vm->ip = action_addr;
    }
    return lh_bool_true;
}

/* The error in flight: the innermost handler of this frame that holds one. */
static const cnor_vm_try_t *
cnor_vm_error_in_flight(const cnor_vm_t *vm)
{
    cnor_count_t i = vm->try_count;

    while (i > 0U)
    {
        const cnor_vm_try_t *const entry = &vm->tries[i - 1U];

        --i;
        if (entry->depth < vm->depth)
        {
            /* The handlers below are in outer frames: the error is not ours. */
            break;
        }
        if (entry->depth == vm->depth && entry->error != cnor_error_none)
        {
            return entry;
        }
    }
    return lh_null;
}

/* The error being handled goes on outward, as if it had just happened. */
lh_bool_t
cnor_vm_op_raise(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_vm_try_t *const entry = cnor_vm_error_in_flight(vm);

    if (entry == lh_null)
    {
        /* There is no error to raise. */
        return cnor_vm_fail(vm, insn, cnor_error_nothing_to_raise);
    }
    cnor_vm_trace_restore_try(vm, entry);
    return cnor_vm_unwind(vm, entry->error, entry->error_at);
}

/* Leave a `try` that has a finally block: the finally block runs, and the
 * machine goes where the instruction says afterwards. */
lh_bool_t
cnor_vm_op_try_exit(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    cnor_vm_try_t *entry;
    const cnor_code_addr_t addr = (cnor_code_addr_t) cnor_insn_get_jump_target(insn);

    /* The handler whose finally block has to run: the innermost one that has a
     * finally block and has not run it yet. The ones above it whose finally
     * block has run are left, and their blocks do not run again. */
    while (vm->try_count > 0U)
    {
        cnor_vm_try_t *const top = &vm->tries[vm->try_count - 1U];

        if ((top->flags & (lh_u8_t) CNOR_VM_TRY_CLEANED) == 0U)
        {
            if (top->finally_addr != 0U)
            {
                break;
            }
            /* No finally block: the handler is closed here. */
            top->flags |= (lh_u8_t) (CNOR_VM_TRY_CLOSED | CNOR_VM_TRY_CLEANED);
        }
        top->error = cnor_error_none;
        cnor_vm_trace_release_try(vm, &vm->tries[vm->try_count - 1U]);
        --vm->try_count;
    }
    if (vm->try_count == 0U)
    {
        /* There is no finally block to run: the machine goes straight on. */
        vm->ip = addr;
        return lh_bool_true;
    }
    entry = &vm->tries[vm->try_count - 1U];
    entry->flags |= (lh_u8_t) (CNOR_VM_TRY_CLOSED | CNOR_VM_TRY_CLEANED | CNOR_VM_TRY_ACTION);
    entry->error = cnor_error_none;
    cnor_vm_trace_release_try(vm, entry);
    entry->action_addr = addr;
    vm->ip = entry->finally_addr;
    return lh_bool_true;
}
