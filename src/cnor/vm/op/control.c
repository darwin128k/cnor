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
    const lh_value_t *args = cnor_vm_stack_at(&vm->stack, vm->stack.sp - argc);
    cnor_error_t error = cnor_error_none;
    lh_value_t result;

    if (!cnor_native_accepts(native, argc))
    {
        return cnor_vm_fail(vm, insn, cnor_error_bad_arity);
    }
    result = native->fn(args, argc, vm->config.ctx, &error);
    if (error != cnor_error_none)
    {
        return cnor_vm_fail(vm, insn, error);
    }
    vm->stack.sp -= argc;
    cnor_vm_stack_push(&vm->stack, result);
    return lh_bool_true;
}
