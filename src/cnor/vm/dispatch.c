/*
 * The dispatch loop.
 *
 * The loop keeps the instruction pointer, the stack top and the frame base in
 * locals, reads operands straight from the code (the verifier proved at load
 * that every instruction decodes inside its function, every jump lands on an
 * instruction start, every index is in range and the operand stack stays
 * within its declared depth) and executes the common cases inline: constants,
 * locals, globals, signed-integer arithmetic and comparisons, jumps, calls,
 * returns, natives, string indexing and equality.
 *
 * Everything else (unsigned and real numbers, string joins, `len`, `halt`,
 * failures) goes through the slow path: the locals are written back to the
 * machine, the instruction is decoded and its handler from the table below
 * runs, exactly as it would without the fast paths. A fast path therefore
 * never decides a rule of the language on its own; it only skips the work for
 * operands it knows.
 *
 * Some instructions also run the usual instruction after them (see "Fusion"
 * below); that one is still counted against the budget on its own.
 *
 * CNOR_VM_COMPUTED_GOTO selects how the next instruction is reached: 1 jumps
 * through a table of label addresses (a GCC/Clang extension, one indirect
 * branch per instruction), 0 uses a portable switch. It defaults to 1 where
 * the compiler supports it; define it to 0 (CMake: CNOR_VM_COMPUTED_GOTO=OFF)
 * to force the switch.
 */

#include <cnor/vm/fast.h>
#include <cnor/vm/internal.h>
#include <lh/compiler/type.h>

#ifndef CNOR_VM_COMPUTED_GOTO
#    if (LH_COMPILER_TYPE_IS_GCC_LIKE)
#        define CNOR_VM_COMPUTED_GOTO 1
#    else
#        define CNOR_VM_COMPUTED_GOTO 0
#    endif
#endif

_Static_assert(cnor_op_count == 35, "a new opcode needs a handler and a dispatch label below");

/* One handler per opcode, for the slow path; families share a handler and
 * look at `insn->op`. */
static const cnor_vm_handler_t m_handlers[cnor_op_count] = {
    [cnor_op_halt] = cnor_vm_op_halt,
    [cnor_op_push_none] = cnor_vm_op_push_none,
    [cnor_op_push_true] = cnor_vm_op_push_true,
    [cnor_op_push_false] = cnor_vm_op_push_false,
    [cnor_op_push_i32] = cnor_vm_op_push_i32,
    [cnor_op_push_u32] = cnor_vm_op_push_u32,
    [cnor_op_push_const] = cnor_vm_op_push_const,
    [cnor_op_load_local] = cnor_vm_op_load_local,
    [cnor_op_store_local] = cnor_vm_op_store_local,
    [cnor_op_load_global] = cnor_vm_op_load_global,
    [cnor_op_store_global] = cnor_vm_op_store_global,
    [cnor_op_dup] = cnor_vm_op_dup,
    [cnor_op_swap] = cnor_vm_op_swap,
    [cnor_op_pop] = cnor_vm_op_pop,
    [cnor_op_add] = cnor_vm_op_arith,
    [cnor_op_sub] = cnor_vm_op_arith,
    [cnor_op_mul] = cnor_vm_op_arith,
    [cnor_op_div] = cnor_vm_op_arith,
    [cnor_op_mod] = cnor_vm_op_arith,
    [cnor_op_neg] = cnor_vm_op_neg,
    [cnor_op_eq] = cnor_vm_op_equality,
    [cnor_op_ne] = cnor_vm_op_equality,
    [cnor_op_lt] = cnor_vm_op_order,
    [cnor_op_le] = cnor_vm_op_order,
    [cnor_op_gt] = cnor_vm_op_order,
    [cnor_op_ge] = cnor_vm_op_order,
    [cnor_op_not] = cnor_vm_op_not,
    [cnor_op_jmp] = cnor_vm_op_jmp,
    [cnor_op_jmp_if_false] = cnor_vm_op_jmp_if,
    [cnor_op_jmp_if_true] = cnor_vm_op_jmp_if,
    [cnor_op_call] = cnor_vm_op_call,
    [cnor_op_ret] = cnor_vm_op_ret,
    [cnor_op_native] = cnor_vm_op_native,
    [cnor_op_len] = cnor_vm_op_len,
    [cnor_op_index] = cnor_vm_op_index,
};

/* -- out of the loop ------------------------------------------------------- */

CNOR_VM_COLD static lh_bool_t
cnor_vm_fail_at(cnor_vm_t *vm, cnor_code_addr_t at, cnor_error_t code)
{
    cnor_insn_t insn;

    insn.at = at;
    return cnor_vm_fail(vm, &insn, code);
}

/* Execute the instruction at `vm->ip` through its handler. The machine's
 * fields are current; the handler leaves them current. */
CNOR_VM_COLD static lh_bool_t
cnor_vm_slow(cnor_vm_t *vm)
{
    cnor_insn_t insn;
    const cnor_error_t error = cnor_insn_decode(vm->code, vm->code_size, vm->ip, &insn);

    if (error != cnor_error_none)
    {
        return cnor_vm_fail_at(vm, vm->ip, error);
    }
    vm->ip = insn.next;
    return m_handlers[insn.op](vm, &insn);
}

/* -- the loop -------------------------------------------------------------- */

/* Write the locals back to the machine, and read them again. */
#define CNOR_VM_SAVE()                                                                             \
    do                                                                                             \
    {                                                                                              \
        vm->ip = (cnor_code_addr_t) (pc - code);                                                   \
        vm->stack.sp = (cnor_stack_slots_t) (sp - values);                                         \
        vm->fp = (cnor_stack_slots_t) (fp - values);                                               \
        vm->steps = steps;                                                                         \
    } while (0)

#define CNOR_VM_RESTORE()                                                                          \
    do                                                                                             \
    {                                                                                              \
        pc = code + vm->ip;                                                                        \
        sp = values + vm->stack.sp;                                                                \
        fp = values + vm->fp;                                                                      \
    } while (0)

/* The budget is checked before an instruction runs, so a stopped run reports
 * the instruction it did not execute. `steps` only ever grows by one, so
 * reaching the limit is equality; with no limit (0) the test is only true at
 * the start and after the counter wraps, and falls through. */
#define CNOR_VM_STEP()                                                                             \
    do                                                                                             \
    {                                                                                              \
        if (CNOR_VM_UNLIKELY(steps == limit) && limit != 0U)                                       \
        {                                                                                          \
            goto out_of_steps;                                                                     \
        }                                                                                          \
        ++steps;                                                                                   \
    } while (0)

#if CNOR_VM_COMPUTED_GOTO
#    define CNOR_VM_OP(name) cnor_vm_label_##name:
#    define CNOR_VM_NEXT()                                                                         \
        do                                                                                         \
        {                                                                                          \
            CNOR_VM_STEP();                                                                        \
            goto *m_labels[*pc];                                                                   \
        } while (0)
#else
#    define CNOR_VM_OP(name) case cnor_op_##name:
#    define CNOR_VM_NEXT() goto cnor_vm_next
#endif

/* Hand the current instruction to the slow path and carry on after it. */
#define CNOR_VM_SLOW()                                                                             \
    do                                                                                             \
    {                                                                                              \
        CNOR_VM_SAVE();                                                                            \
        if (!cnor_vm_slow(vm))                                                                     \
        {                                                                                          \
            return lh_bool_false;                                                                  \
        }                                                                                          \
        if (!vm->running)                                                                          \
        {                                                                                          \
            return lh_bool_true;                                                                   \
        }                                                                                          \
        CNOR_VM_RESTORE();                                                                         \
        CNOR_VM_NEXT();                                                                            \
    } while (0)

/* Wrapping signed arithmetic through unsigned, as op/arith.c does. */
#define CNOR_VM_WRAP(a, op, b) ((lh_sllong_t) ((lh_ullong_t) (a) op (lh_ullong_t) (b)))

/*
 * Fusion: a few instructions look at the opcode after them and, when it is
 * the usual partner, execute it too without going back through dispatch. The
 * partner is still a separate instruction of the image: it is counted against
 * the budget (and not taken when the budget is spent, so the stop lands where
 * it would without fusion), and the verifier proved it decodes right there,
 * because an instruction that is not a terminator is always followed by
 * another one.
 */
#define CNOR_VM_CAN_FUSE() CNOR_VM_LIKELY(steps != limit || limit == 0U)

/*
 * Tails shared by binary operators on signed integers. `pc` is at the operator,
 * whose operands are gone except for the slot that takes the result, sp[-1].
 *
 * A comparison followed by a conditional jump takes the jump without building
 * the boolean; arithmetic followed by `store_local` stores into the slot
 * directly.
 */
#define CNOR_VM_BRANCH_OR_SET(expr)                                                                \
    do                                                                                             \
    {                                                                                              \
        const int truth = (expr);                                                                  \
                                                                                                   \
        if ((pc[1] == cnor_op_jmp_if_false || pc[1] == cnor_op_jmp_if_true) && CNOR_VM_CAN_FUSE()) \
        {                                                                                          \
            const int taken = truth == (pc[1] == cnor_op_jmp_if_true);                             \
                                                                                                   \
            ++steps;                                                                               \
            --sp;                                                                                  \
            pc += taken ? 6 + cnor_vm_fast_s32(pc + 2) : 6;                                        \
            CNOR_VM_NEXT();                                                                        \
        }                                                                                          \
        cnor_vm_fast_set_bool(&sp[-1], truth);                                                     \
        pc += 1;                                                                                   \
        CNOR_VM_NEXT();                                                                            \
    } while (0)

#define CNOR_VM_STORE_OR_SET(expr)                                                                 \
    do                                                                                             \
    {                                                                                              \
        const lh_sllong_t result = (expr);                                                         \
                                                                                                   \
        if (pc[1] == cnor_op_store_local && CNOR_VM_CAN_FUSE())                                    \
        {                                                                                          \
            ++steps;                                                                               \
            --sp;                                                                                  \
            cnor_vm_fast_set_sint(&fp[pc[2]], result);                                             \
            pc += 3;                                                                               \
            CNOR_VM_NEXT();                                                                        \
        }                                                                                          \
        cnor_vm_fast_set_sint(&sp[-1], result);                                                    \
        pc += 1;                                                                                   \
        CNOR_VM_NEXT();                                                                            \
    } while (0)

/* Two signed integers on top: pop one and finish with @p tail. */
#define CNOR_VM_SINT_PAIR(tail, expr)                                                              \
    do                                                                                             \
    {                                                                                              \
        if (CNOR_VM_LIKELY(cnor_vm_fast_both_sint(&sp[-2], &sp[-1])))                              \
        {                                                                                          \
            const lh_sllong_t a = cnor_vm_fast_sint(&sp[-2]);                                      \
            const lh_sllong_t b = cnor_vm_fast_sint(&sp[-1]);                                      \
                                                                                                   \
            --sp;                                                                                  \
            tail(expr);                                                                            \
        }                                                                                          \
        CNOR_VM_SLOW();                                                                            \
    } while (0)

lh_bool_t
cnor_vm_dispatch(cnor_vm_t *vm)
{
#if CNOR_VM_COMPUTED_GOTO
    /* Indexed by opcode. The verifier proved every executed byte is one. */
    static const void *const m_labels[cnor_op_count] = {
        [cnor_op_halt] = &&cnor_vm_label_halt,
        [cnor_op_push_none] = &&cnor_vm_label_push_none,
        [cnor_op_push_true] = &&cnor_vm_label_push_true,
        [cnor_op_push_false] = &&cnor_vm_label_push_false,
        [cnor_op_push_i32] = &&cnor_vm_label_push_i32,
        [cnor_op_push_u32] = &&cnor_vm_label_push_u32,
        [cnor_op_push_const] = &&cnor_vm_label_push_const,
        [cnor_op_load_local] = &&cnor_vm_label_load_local,
        [cnor_op_store_local] = &&cnor_vm_label_store_local,
        [cnor_op_load_global] = &&cnor_vm_label_load_global,
        [cnor_op_store_global] = &&cnor_vm_label_store_global,
        [cnor_op_dup] = &&cnor_vm_label_dup,
        [cnor_op_swap] = &&cnor_vm_label_swap,
        [cnor_op_pop] = &&cnor_vm_label_pop,
        [cnor_op_add] = &&cnor_vm_label_add,
        [cnor_op_sub] = &&cnor_vm_label_sub,
        [cnor_op_mul] = &&cnor_vm_label_mul,
        [cnor_op_div] = &&cnor_vm_label_div,
        [cnor_op_mod] = &&cnor_vm_label_mod,
        [cnor_op_neg] = &&cnor_vm_label_neg,
        [cnor_op_eq] = &&cnor_vm_label_eq,
        [cnor_op_ne] = &&cnor_vm_label_ne,
        [cnor_op_lt] = &&cnor_vm_label_lt,
        [cnor_op_le] = &&cnor_vm_label_le,
        [cnor_op_gt] = &&cnor_vm_label_gt,
        [cnor_op_ge] = &&cnor_vm_label_ge,
        [cnor_op_not] = &&cnor_vm_label_not,
        [cnor_op_jmp] = &&cnor_vm_label_jmp,
        [cnor_op_jmp_if_false] = &&cnor_vm_label_jmp_if_false,
        [cnor_op_jmp_if_true] = &&cnor_vm_label_jmp_if_true,
        [cnor_op_call] = &&cnor_vm_label_call,
        [cnor_op_ret] = &&cnor_vm_label_ret,
        [cnor_op_native] = &&cnor_vm_label_native,
        [cnor_op_len] = &&cnor_vm_label_len,
        [cnor_op_index] = &&cnor_vm_label_index,
    };
#endif
    const lh_byte_t *const code = vm->code;
    lh_value_t *const values = vm->stack.values;
    const lh_value_t *const constants = vm->constants;
    const cnor_step_count_t limit = vm->config.max_steps;
    cnor_step_count_t steps = vm->steps;
    const lh_byte_t *pc = code + vm->ip;
    lh_value_t *sp = values + vm->stack.sp;
    lh_value_t *fp = values + vm->fp;

    if (!vm->running)
    {
        return lh_bool_true;
    }
#if CNOR_VM_COMPUTED_GOTO
    CNOR_VM_NEXT();
#else
cnor_vm_next:
    CNOR_VM_STEP();
    switch ((cnor_op_t) *pc)
    {
#endif

    /* -- constants -------------------------------------------------------- */

    CNOR_VM_OP(push_none)
    {
        cnor_vm_fast_set_none(sp++);
        pc += 1;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(push_true)
    {
        cnor_vm_fast_set_bool(sp++, 1);
        pc += 1;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(push_false)
    {
        cnor_vm_fast_set_bool(sp++, 0);
        pc += 1;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(push_i32)
    {
        const lh_sllong_t value = cnor_vm_fast_s32(pc + 1);

        /* `i = i + 1`, `n - 2`, `n < 2`: with a signed integer below, the
         * constant never reaches the stack. The partner is looked at first:
         * only a binary operator after the push proves there is a value
         * below it. */
        if ((pc[5] == cnor_op_add || pc[5] == cnor_op_sub || pc[5] == cnor_op_lt)
            && cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint && CNOR_VM_CAN_FUSE())
        {
            const lh_sllong_t a = sp[-1].data.i;

            ++steps;
            if (pc[5] == cnor_op_add)
            {
                pc += 5;
                CNOR_VM_STORE_OR_SET(CNOR_VM_WRAP(a, +, value));
            }
            if (pc[5] == cnor_op_sub)
            {
                pc += 5;
                CNOR_VM_STORE_OR_SET(CNOR_VM_WRAP(a, -, value));
            }
            pc += 5;
            CNOR_VM_BRANCH_OR_SET(a < value);
        }
        cnor_vm_fast_set_sint(sp++, value);
        pc += 5;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(push_u32)
    {
        cnor_vm_fast_set_uint(sp++, cnor_vm_fast_u32(pc + 1));
        pc += 5;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(push_const)
    {
        *sp++ = constants[cnor_vm_fast_u16(pc + 1)];
        pc += 3;
        CNOR_VM_NEXT();
    }

    /* -- variables -------------------------------------------------------- */

    CNOR_VM_OP(load_local)
    {
        *sp++ = fp[pc[1]];
        if (pc[2] == cnor_op_load_local && CNOR_VM_CAN_FUSE())
        {
            ++steps;
            *sp++ = fp[pc[3]];
            pc += 4;
            CNOR_VM_NEXT();
        }
        pc += 2;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(store_local)
    {
        fp[pc[1]] = *--sp;
        pc += 2;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(load_global)
    {
        *sp++ = values[cnor_vm_fast_u16(pc + 1)];
        pc += 3;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(store_global)
    {
        values[cnor_vm_fast_u16(pc + 1)] = *--sp;
        pc += 3;
        CNOR_VM_NEXT();
    }

    /* -- shuffling -------------------------------------------------------- */

    CNOR_VM_OP(dup)
    {
        sp[0] = sp[-1];
        ++sp;
        pc += 1;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(swap)
    {
        const lh_value_t held = sp[-1];

        sp[-1] = sp[-2];
        sp[-2] = held;
        pc += 1;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(pop)
    {
        --sp;
        pc += 1;
        CNOR_VM_NEXT();
    }

    /* -- arithmetic ------------------------------------------------------- */

    CNOR_VM_OP(add)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, CNOR_VM_WRAP(a, +, b));
    }
    CNOR_VM_OP(sub)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, CNOR_VM_WRAP(a, -, b));
    }
    CNOR_VM_OP(mul)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, CNOR_VM_WRAP(a, *, b));
    }
    CNOR_VM_OP(div)
    {
        /* Zero fails and -1 may overflow: both are the slow path's. */
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint && sp[-1].data.i != 0
            && sp[-1].data.i != -1)
        {
            CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, a / b);
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(mod)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint && sp[-1].data.i != 0
            && sp[-1].data.i != -1)
        {
            CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, a % b);
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(neg)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint)
        {
            cnor_vm_fast_set_sint(&sp[-1], CNOR_VM_WRAP(0, -, sp[-1].data.i));
            pc += 1;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }

    /* -- comparisons ------------------------------------------------------ */

    CNOR_VM_OP(eq)
    {
        /* Equality never fails: other kinds are compared by the full rule. */
        if (!cnor_vm_fast_both_sint(&sp[-2], &sp[-1]))
        {
            const lh_bool_t equal = cnor_vm_value_equals(&sp[-2], &sp[-1]);

            --sp;
            CNOR_VM_BRANCH_OR_SET(equal);
        }
        CNOR_VM_SINT_PAIR(CNOR_VM_BRANCH_OR_SET, a == b);
    }
    CNOR_VM_OP(ne)
    {
        /* Equality never fails: other kinds are compared by the full rule. */
        if (!cnor_vm_fast_both_sint(&sp[-2], &sp[-1]))
        {
            const lh_bool_t equal = cnor_vm_value_equals(&sp[-2], &sp[-1]);

            --sp;
            CNOR_VM_BRANCH_OR_SET(!equal);
        }
        CNOR_VM_SINT_PAIR(CNOR_VM_BRANCH_OR_SET, a != b);
    }
    CNOR_VM_OP(lt)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_BRANCH_OR_SET, a < b);
    }
    CNOR_VM_OP(le)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_BRANCH_OR_SET, a <= b);
    }
    CNOR_VM_OP(gt)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_BRANCH_OR_SET, a > b);
    }
    CNOR_VM_OP(ge)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_BRANCH_OR_SET, a >= b);
    }
    CNOR_VM_OP(not)
    {
        const int truth = cnor_vm_fast_truthy(&sp[-1]);

        cnor_vm_fast_set_bool(&sp[-1], truth >= 0 ? !truth : !cnor_vm_value_is_truthy(&sp[-1]));
        pc += 1;
        CNOR_VM_NEXT();
    }

    /* -- jumps ------------------------------------------------------------ */

    CNOR_VM_OP(jmp)
    {
        pc += 5 + cnor_vm_fast_s32(pc + 1);
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(jmp_if_false)
    {
        const int fast = cnor_vm_fast_truthy(&sp[-1]);
        const int truth = fast >= 0 ? fast : (int) cnor_vm_value_is_truthy(&sp[-1]);

        --sp;
        pc += truth ? 5 : 5 + cnor_vm_fast_s32(pc + 1);
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(jmp_if_true)
    {
        const int fast = cnor_vm_fast_truthy(&sp[-1]);
        const int truth = fast >= 0 ? fast : (int) cnor_vm_value_is_truthy(&sp[-1]);

        --sp;
        pc += truth ? 5 + cnor_vm_fast_s32(pc + 1) : 5;
        CNOR_VM_NEXT();
    }

    /* -- calls ------------------------------------------------------------ */

    CNOR_VM_OP(call)
    {
        /* Same steps as cnor_vm_op_call; the verifier proved argc equals the
         * callee's parameter count. */
        const cnor_function_index_t index = (cnor_function_index_t) cnor_vm_fast_u16(pc + 1);
        const cnor_image_function_t *callee = &vm->functions[index];
        const cnor_stack_slots_t reserve =
            (cnor_stack_slots_t) (callee->slot_count - callee->param_count);
        const cnor_stack_slots_t used = (cnor_stack_slots_t) (sp - values);
        cnor_stack_slots_t left = reserve;
        cnor_vm_frame_t *frame;

        if (vm->depth >= vm->config.max_depth
            || vm->stack.capacity - used < reserve + callee->max_stack)
        {
            CNOR_VM_SLOW();
        }
        frame = &vm->frames[vm->depth++];
        frame->return_addr = (cnor_code_addr_t) (pc + 4 - code);
        frame->fp = (cnor_stack_slots_t) (fp - values);
        frame->function = vm->function;
        fp = sp - callee->param_count;
        vm->function = index;
        pc = code + callee->entry;
        while (left-- > 0U)
        {
            cnor_vm_fast_set_none(sp++);
        }
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(ret)
    {
        const lh_value_t value = sp[-1];
        const cnor_vm_frame_t *frame;

        sp = fp;
        if (vm->depth == 0U)
        {
            vm->result = value;
            vm->running = lh_bool_false;
            CNOR_VM_SAVE();
            return lh_bool_true;
        }
        frame = &vm->frames[--vm->depth];
        pc = code + frame->return_addr;
        fp = values + frame->fp;
        vm->function = frame->function;
        *sp++ = value;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(native)
    {
        const cnor_native_t *native = vm->natives[cnor_vm_fast_u16(pc + 1)];
        const cnor_argc_t argc = (cnor_argc_t) pc[3];
        cnor_error_t error = cnor_error_none;
        lh_value_t result;

        if (argc < native->arity_min || argc > native->arity_max)
        {
            CNOR_VM_SLOW();
        }
        /* A native cannot reach the machine, so the locals stay valid. */
        result = native->fn(sp - argc, argc, vm->config.ctx, &error);
        if (error != cnor_error_none)
        {
            CNOR_VM_SAVE();
            return cnor_vm_fail_at(vm, vm->ip, error);
        }
        sp -= argc;
        *sp++ = result;
        pc += 4;
        CNOR_VM_NEXT();
    }

    /* -- the rest --------------------------------------------------------- */

    CNOR_VM_OP(halt)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(len)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(index)
    {
        /* On failure nothing changed; the slow path reports it. */
        if (cnor_vm_text_index(&sp[-2], &sp[-1]) == cnor_error_none)
        {
            --sp;
            pc += 1;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }

#if !CNOR_VM_COMPUTED_GOTO
    default:
        CNOR_VM_SLOW();
    }
#endif

out_of_steps:
    CNOR_VM_SAVE();
    return cnor_vm_fail_at(vm, vm->ip, cnor_error_step_limit);
}
