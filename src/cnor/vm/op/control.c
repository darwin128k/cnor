#include <cnor/vm/internal.h>

/* -- jumps ----------------------------------------------------------------- */

/* Targets were proved to be instruction starts inside the function at load. */
lh_bool_t
cnor_vm_op_jmp(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    vm->ip = (cnor_code_addr_t) cnor_insn_get_jump_target(insn);
    return lh_bool_true;
}

lh_bool_t
cnor_vm_op_jmp_if(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t cond = cnor_vm_stack_pop(&vm->stack);
    const lh_bool_t wanted = insn->op == cnor_op_jmp_if_true ? lh_bool_true : lh_bool_false;

    if (cnor_vm_value_is_truthy(&cond) == wanted)
    {
        vm->ip = (cnor_code_addr_t) cnor_insn_get_jump_target(insn);
    }
    return lh_bool_true;
}

/*
 * One step of a `for` loop: the variable becomes `var + 1` by the rule of
 * `add`, then the loop jumps back when `var < bound` holds by the rule of
 * `lt`, exactly as the instructions it replaces did.
 */
lh_bool_t
cnor_vm_op_for_loop(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    lh_value_t *var = cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[1]);
    const lh_value_t *bound = cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[2]);
    const lh_value_t one = lh_value_make_sint(1);
    lh_value_t next;
    lh_value_t below;

    if (!cnor_vm_arith_values(vm, insn, cnor_op_add, var, &one, &next))
    {
        return lh_bool_false;
    }
    *var = next;
    if (!cnor_vm_compare_values(vm, insn, cnor_op_lt, var, bound, &below))
    {
        return lh_bool_false;
    }
    if (lh_value_get_data(&below)->b)
    {
        vm->ip = (cnor_code_addr_t) cnor_insn_get_jump_target(insn);
    }
    return lh_bool_true;
}

/*
 * Pick a `switch` table entry: the first k + i that the subject equals by the
 * rule of `==`, as the chain of `eq_li` tests it replaces would. The
 * verifier proved the table is all `jmp`s and the code after it exists.
 */
lh_bool_t
cnor_vm_op_switch(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t *subject = cnor_vm_stack_at(&vm->stack, vm->fp + insn->operands[0]);
    const lh_sllong_t first = (lh_sllong_t) (lh_s32_t) insn->operands[1];
    const lh_u32_t count = insn->operands[2];
    lh_u32_t i;

    for (i = 0U; i < count; ++i)
    {
        const lh_value_t pattern = lh_value_make_sint(first + (lh_sllong_t) i);
        lh_value_t equal;

        if (!cnor_vm_compare_values(vm, insn, cnor_op_eq, subject, &pattern, &equal))
        {
            return lh_bool_false;
        }
        if (lh_value_get_data(&equal)->b)
        {
            break;
        }
    }
    vm->ip = insn->next + i * CNOR_OP_SWITCH_ENTRY_SIZE;
    return lh_bool_true;
}

/* -- script calls ---------------------------------------------------------- */

/*
 * The arguments already on the stack become the callee's first slots; the rest
 * of its slots are reserved as none. The verifier proved the callee never uses
 * more than `max_stack` operands, so checking room once per call is enough.
 */
lh_bool_t
cnor_vm_op_call(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_function_index_t index = (cnor_function_index_t) insn->operands[0];
    const cnor_image_function_t *callee = &vm->functions[index];
    const cnor_stack_slots_t reserve =
        (cnor_stack_slots_t) (callee->slot_count - callee->param_count);
    cnor_vm_frame_t *frame;

    if (vm->depth >= vm->config.max_depth)
    {
        return cnor_vm_fail(vm, insn, cnor_error_call_depth);
    }
    if (!cnor_vm_stack_fits(&vm->stack, reserve + callee->max_stack))
    {
        return cnor_vm_fail(vm, insn, cnor_error_stack_overflow);
    }
    frame = &vm->frames[vm->depth++];
    frame->return_addr = vm->ip;
    frame->fp = vm->fp;
    frame->function = vm->function;
    vm->fp = vm->stack.sp - callee->param_count;
    vm->function = index;
    vm->ip = callee->entry;
    cnor_vm_stack_push_none(&vm->stack, reserve);
    return lh_bool_true;
}

/* Drops the whole frame, arguments included, and leaves the value in its place. */
lh_bool_t
cnor_vm_op_ret(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const lh_value_t value = cnor_vm_stack_pop(&vm->stack);
    const cnor_vm_frame_t *frame;

    (void) insn;
    vm->stack.sp = vm->fp;
    if (vm->depth == 0U)
    {
        vm->result = value;
        vm->running = lh_bool_false;
        return lh_bool_true;
    }
    frame = &vm->frames[--vm->depth];
    vm->ip = frame->return_addr;
    vm->fp = frame->fp;
    vm->function = frame->function;
    cnor_vm_stack_push(&vm->stack, value);
    return lh_bool_true;
}

/* Stops the program; whatever is above the frame's slots is the result. */
lh_bool_t
cnor_vm_op_halt(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_stack_slots_t base = vm->fp + vm->functions[vm->function].slot_count;

    (void) insn;
    vm->result = vm->stack.sp > base ? *cnor_vm_stack_peek(&vm->stack, 0U) : lh_value_make_none();
    vm->running = lh_bool_false;
    return lh_bool_true;
}

/* -- natives --------------------------------------------------------------- */

lh_bool_t
cnor_vm_op_native(cnor_vm_t *vm, const cnor_insn_t *insn)
{
    const cnor_native_t *native = vm->natives[insn->operands[0]];
    const cnor_argc_t argc = (cnor_argc_t) insn->operands[1];
    lh_value_t *const args = cnor_vm_stack_at(&vm->stack, vm->stack.sp - argc);
    cnor_error_t error = cnor_error_none;

    if (!cnor_native_accepts(native, argc))
    {
        return cnor_vm_fail(vm, insn, cnor_error_bad_arity);
    }
    /* The result goes into the slot of the arguments: the native writes it
     * there, and no value is copied. */
    native->fn(args, argc, vm->config.ctx, args, &error);
    if (error != cnor_error_none)
    {
        return cnor_vm_fail(vm, insn, error);
    }
    vm->stack.sp -= argc;
    cnor_vm_stack_push(&vm->stack, *args);
    return lh_bool_true;
}
