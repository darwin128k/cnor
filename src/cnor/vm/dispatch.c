/*
 * The dispatch loop.
 *
 * The loop keeps the instruction pointer, the stack top and the frame base in
 * locals, reads operands straight from the code (the verifier proved at load
 * that every instruction decodes inside its function, every jump lands on an
 * instruction start, every index is in range and the operand stack stays
 * within its declared depth) and executes the common cases inline: constants,
 * locals, globals, signed-integer arithmetic and comparisons, jumps, calls,
 * returns, natives, and on strings indexing, `len`, equality and joins.
 *
 * Everything else (unsigned and real numbers, `halt`, a full string pool,
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
#include <lh/compiler/unreachable.h>

#ifndef CNOR_VM_COMPUTED_GOTO
#    if (LH_COMPILER_TYPE_IS_GCC_LIKE)
#        define CNOR_VM_COMPUTED_GOTO 1
#    else
#        define CNOR_VM_COMPUTED_GOTO 0
#    endif
#endif

_Static_assert(cnor_op_count == 122,"a new opcode needs a handler and a dispatch label below");

/* The three operand forms of binary operator @p name share one handler. */
#define CNOR_VM_FORM_HANDLERS(name)                                                                \
    [cnor_op_##name##_ll] = cnor_vm_op_operands, [cnor_op_##name##_li] = cnor_vm_op_operands,     \
    [cnor_op_##name##_i] = cnor_vm_op_operands

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
    [cnor_op_bnot] = cnor_vm_op_bnot,
    [cnor_op_band] = cnor_vm_op_arith,
    [cnor_op_bor] = cnor_vm_op_arith,
    [cnor_op_bxor] = cnor_vm_op_arith,
    [cnor_op_shl] = cnor_vm_op_arith,
    [cnor_op_shr] = cnor_vm_op_arith,
    [cnor_op_jmp] = cnor_vm_op_jmp,
    [cnor_op_jmp_if_false] = cnor_vm_op_jmp_if,
    [cnor_op_jmp_if_true] = cnor_vm_op_jmp_if,
    [cnor_op_call] = cnor_vm_op_call,
    [cnor_op_ret] = cnor_vm_op_ret,
    [cnor_op_call_local] = cnor_vm_op_call_local,
    [cnor_op_ret_local] = cnor_vm_op_ret_local,
    [cnor_op_native] = cnor_vm_op_native,
    [cnor_op_len] = cnor_vm_op_len,
    [cnor_op_index] = cnor_vm_op_index,
    [cnor_op_index_ll] = cnor_vm_op_index_operands,
    [cnor_op_index_li] = cnor_vm_op_index_operands,
    [cnor_op_index_i] = cnor_vm_op_index_operands,
    [cnor_op_array_new] = cnor_vm_op_array_new,
    [cnor_op_array_room] = cnor_vm_op_array_room,
    [cnor_op_array_new_global] = cnor_vm_op_array_new_global,
    [cnor_op_array_room_global] = cnor_vm_op_array_room_global,
    [cnor_op_array_get_l] = cnor_vm_op_array_get_l,
    [cnor_op_array_set_l] = cnor_vm_op_array_set_l,
    [cnor_op_array_get] = cnor_vm_op_array_get,
    [cnor_op_array_set] = cnor_vm_op_array_set,
    [cnor_op_dict_new] = cnor_vm_op_dict_new,
    [cnor_op_dict_new_global] = cnor_vm_op_dict_new_global,
    [cnor_op_dict_get_l] = cnor_vm_op_dict_get_l,
    [cnor_op_dict_get_lk] = cnor_vm_op_dict_get_lk,
    [cnor_op_dict_set_l] = cnor_vm_op_dict_set_l,
    [cnor_op_dict_get] = cnor_vm_op_dict_get,
    [cnor_op_dict_set] = cnor_vm_op_dict_set,
    [cnor_op_dict_key_l] = cnor_vm_op_dict_key_l,
    [cnor_op_collection_get_l] = cnor_vm_op_collection_get_l,
    [cnor_op_try_push] = cnor_vm_op_try_push,
    [cnor_op_try_pop] = cnor_vm_op_try_pop,
    [cnor_op_try_finally] = cnor_vm_op_try_finally,
    [cnor_op_finally_end] = cnor_vm_op_finally_end,
    [cnor_op_raise] = cnor_vm_op_raise,
    [cnor_op_try_exit] = cnor_vm_op_try_exit,
    [cnor_op_push_function] = cnor_vm_op_push_function,
    [cnor_op_call_value] = cnor_vm_op_call_value,
    [cnor_op_call_builtin] = cnor_vm_op_call_builtin,
    CNOR_VM_FORM_HANDLERS(add),
    CNOR_VM_FORM_HANDLERS(sub),
    CNOR_VM_FORM_HANDLERS(mul),
    CNOR_VM_FORM_HANDLERS(div),
    CNOR_VM_FORM_HANDLERS(mod),
    CNOR_VM_FORM_HANDLERS(eq),
    CNOR_VM_FORM_HANDLERS(ne),
    CNOR_VM_FORM_HANDLERS(lt),
    CNOR_VM_FORM_HANDLERS(le),
    CNOR_VM_FORM_HANDLERS(gt),
    CNOR_VM_FORM_HANDLERS(ge),
    CNOR_VM_FORM_HANDLERS(band),
    CNOR_VM_FORM_HANDLERS(bor),
    CNOR_VM_FORM_HANDLERS(bxor),
    CNOR_VM_FORM_HANDLERS(shl),
    CNOR_VM_FORM_HANDLERS(shr),
    [cnor_op_for_loop] = cnor_vm_op_for_loop,
    [cnor_op_switch] = cnor_vm_op_switch,
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
        vm->steps = limit - budget;                                                                \
        vm->depth = (cnor_call_depth_t) (frame_top - vm->frames);                                  \
    } while (0)

#define CNOR_VM_RESTORE()                                                                          \
    do                                                                                             \
    {                                                                                              \
        pc = code + vm->ip;                                                                        \
        sp = values + vm->stack.sp;                                                                \
        fp = values + vm->fp;                                                                      \
        frame_top = vm->frames + vm->depth;                                                        \
    } while (0)

/* The budget is checked before an instruction runs, so a stopped run reports
 * the instruction it did not execute. `budget` counts down the steps left, so
 * the count is `limit - budget` and reaching the limit is `budget` at 0. The
 * hot test is `budget` alone: whether a limit is set is only asked at 0, in
 * cnor_vm_budget_spent, so the test is one compare and branch, never a
 * computed truth value. With no limit (0) the count is `-budget`: at 0 it
 * wraps, as the count itself does. */
#define CNOR_VM_STEP()                                                                             \
    do                                                                                             \
    {                                                                                              \
        if (CNOR_VM_UNLIKELY(budget == 0U))                                                        \
        {                                                                                          \
            goto cnor_vm_budget_spent;                                                             \
        }                                                                                          \
        --budget;                                                                                  \
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

/* Hand the current instruction to the slow path and carry on after it. A
 * failure a `try` wants is caught: the machine unwinds to the catch block and
 * carries on there. */
#define CNOR_VM_SLOW()                                                                             \
    do                                                                                             \
    {                                                                                              \
        CNOR_VM_SAVE();                                                                            \
        if (!cnor_vm_slow(vm))                                                                     \
        {                                                                                          \
            if (!cnor_vm_catch(vm))                                                                \
            {                                                                                      \
                return lh_bool_false;                                                              \
            }                                                                                      \
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
 * Whether @p x fits in a signed 32-bit integer.
 */
#define CNOR_VM_FITS32(x) ((x) >= -2147483647LL - 1LL && (x) <= 2147483647LL)

/*
 * A 64-bit division is a call into the compiler's runtime on a 32-bit host
 * (`__divdi3`), a few dozen cycles and a call against one instruction. When
 * both operands fit in 32 bits, the division is exact in 32 bits: the value
 * keeps its 64-bit width, and nothing wraps that would not wrap at 64 bits.
 * The divisor of the `_li` and `_i` forms is an immediate, so the test of it
 * folds away and only the dividend is checked.
 */
#define CNOR_VM_DIV(a, b)                                                                            \
    ((CNOR_VM_FITS32(a) && CNOR_VM_FITS32(b)) ? (lh_sllong_t) ((lh_s32_t) (a) / (lh_s32_t) (b))      \
                                             : (a) / (b))
#define CNOR_VM_MOD(a, b)                                                                            \
    ((CNOR_VM_FITS32(a) && CNOR_VM_FITS32(b)) ? (lh_sllong_t) ((lh_s32_t) (a) % (lh_s32_t) (b))      \
                                             : (a) % (b))

/*
 * The remainder by a power of two is a mask, as long as the dividend is not
 * negative: the rule of `%` for integers is the C rule (truncated toward
 * zero), so a positive dividend and a power of two leave the low bits. The
 * divisor is an immediate, and the test is done on its 32 bits: a 64-bit test
 * would need two registers on a 32-bit host, where the dispatch loop has few
 * to spare.
 *
 * The test needs no sign test on the divisor: a negative divisor has more
 * than one bit set, so it fails the mask test (and 0 never reaches here: the
 * callers send it to the slow path, which fails the division).
 */
#define CNOR_VM_MOD_I(a, b32)                                                                        \
    (((((lh_u32_t) (b32) & ((lh_u32_t) (b32) - 1U)) == 0U) && ((a) >= 0))                            \
         ? ((a) & (lh_sllong_t) ((lh_u32_t) (b32) - 1U))                                            \
         : (CNOR_VM_FITS32(a) ? (lh_sllong_t) ((lh_s32_t) (a) % (lh_s32_t) (b32))                     \
                               : (a) % (lh_sllong_t) (b32)))

/*
 * Fusion: a few instructions look at the opcode after them and, when it is
 * the usual partner, execute it too without going back through dispatch. The
 * partner is still a separate instruction of the image: it is counted against
 * the budget (and not taken when the budget is at 0, so the stop lands where
 * it would without fusion; with no limit that is once in 2^32 steps, and the
 * partner then goes through dispatch and counts the same), and the verifier
 * proved it decodes right there, because an instruction that is not a
 * terminator is always followed by another one.
 */
#define CNOR_VM_CAN_FUSE() CNOR_VM_LIKELY(budget != 0U)

/*
 * Tails shared by binary operators on signed integers. `pc` is at the operator,
 * which is @p n bytes long; the result goes to sp[-1], the slot the operator
 * leaves its value in.
 *
 * A comparison followed by a conditional jump takes the jump without building
 * the boolean; arithmetic followed by `store_local` stores into the slot
 * directly.
 */
#define CNOR_VM_BRANCH_OR_SET(n, expr)                                                             \
    do                                                                                             \
    {                                                                                              \
        const int truth = (expr);                                                                  \
                                                                                                   \
        if ((pc[n] == cnor_op_jmp_if_false || pc[n] == cnor_op_jmp_if_true) && CNOR_VM_CAN_FUSE()) \
        {                                                                                          \
            const int taken = truth == (pc[n] == cnor_op_jmp_if_true);                             \
                                                                                                   \
            --budget;                                                                              \
            --sp;                                                                                  \
            pc += taken ? (n) + 5 + cnor_vm_fast_s32(pc + (n) + 1) : (n) + 5;                      \
            CNOR_VM_NEXT();                                                                        \
        }                                                                                          \
        cnor_vm_fast_set_bool(&sp[-1], truth);                                                     \
        pc += (n);                                                                                 \
        CNOR_VM_NEXT();                                                                            \
    } while (0)

#define CNOR_VM_STORE_OR_SET(n, expr)                                                              \
    do                                                                                             \
    {                                                                                              \
        const lh_sllong_t result = (expr);                                                         \
                                                                                                   \
        if (pc[n] == cnor_op_store_local && CNOR_VM_CAN_FUSE())                                    \
        {                                                                                          \
            --budget;                                                                              \
            --sp;                                                                                  \
            cnor_vm_fast_set_sint(&fp[pc[(n) + 1]], result);                                       \
            pc += (n) + 2;                                                                         \
            CNOR_VM_NEXT();                                                                        \
        }                                                                                          \
        cnor_vm_fast_set_sint(&sp[-1], result);                                                    \
        pc += (n);                                                                                 \
        CNOR_VM_NEXT();                                                                            \
    } while (0)

/* CNOR_VM_STORE_OR_SET for the stack forms, which also fuse with a following
 * store_global (`g = g + x`); the local forms rarely meet one, and the extra
 * test costs the tight local loops more than it would save. */
#define CNOR_VM_STORE_ANY_OR_SET(n, expr)                                                          \
    do                                                                                             \
    {                                                                                              \
        const lh_sllong_t result_any = (expr);                                                     \
                                                                                                   \
        if (pc[n] == cnor_op_store_global && CNOR_VM_CAN_FUSE())                                   \
        {                                                                                          \
            --budget;                                                                              \
            --sp;                                                                                  \
            cnor_vm_fast_set_sint(&values[cnor_vm_fast_u16(pc + (n) + 1)], result_any);            \
            pc += (n) + 3;                                                                         \
            CNOR_VM_NEXT();                                                                        \
        }                                                                                          \
        CNOR_VM_STORE_OR_SET(n, result_any);                                                       \
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
            tail(1U, expr);                                                                        \
        }                                                                                          \
        CNOR_VM_SLOW();                                                                            \
    } while (0)

/*
 * The tail of an instruction @p n bytes long that left a value of any kind in
 * sp[-1]: a `store_local` after it stores the value directly, as
 * CNOR_VM_STORE_OR_SET does for integers.
 */
#define CNOR_VM_STORE_OR_KEEP(n)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (pc[n] == cnor_op_store_local && CNOR_VM_CAN_FUSE())                                    \
        {                                                                                          \
            --budget;                                                                              \
            cnor_vm_fast_move(&fp[pc[(n) + 1]], --sp);                                             \
            pc += (n) + 2;                                                                         \
            CNOR_VM_NEXT();                                                                        \
        }                                                                                          \
        pc += (n);                                                                                 \
        CNOR_VM_NEXT();                                                                            \
    } while (0)

/* Equality by the full rule, with integers and strings compared in place. */
#define CNOR_VM_EQUALS(left, right)                                                                \
    (cnor_vm_fast_both_sint(left, right)  ? (left)->data.i == (right)->data.i                      \
     : cnor_vm_fast_both_str(left, right) ? cnor_vm_fast_str_equals(left, right)                   \
                                          : (int) cnor_vm_value_equals(left, right))

/*
 * Operand forms. `a` and `b` are the operands when both are signed integers
 * and @p ok holds for them (division and remainder leave 0 and -1 to the slow
 * path); otherwise the slow path runs the instruction.
 *
 * `_ll`: slots pc[1] and pc[2]; `_li`: slot pc[1] and the s32 at pc + 2. Both
 * push their result. `_i`: the top of the stack and the s32 at pc + 1; the
 * result replaces the top.
 */
#define CNOR_VM_FORM_LL(tail, ok, expr)                                                            \
    do                                                                                             \
    {                                                                                              \
        const lh_value_t *const left = &fp[pc[1]];                                                 \
        const lh_value_t *const right = &fp[pc[2]];                                                \
                                                                                                   \
        if (CNOR_VM_LIKELY(cnor_vm_fast_both_sint(left, right)))                                   \
        {                                                                                          \
            const lh_sllong_t a = cnor_vm_fast_sint(left);                                         \
            const lh_sllong_t b = cnor_vm_fast_sint(right);                                        \
                                                                                                   \
            if (ok)                                                                                \
            {                                                                                      \
                ++sp;                                                                              \
                tail(3U, expr);                                                                    \
            }                                                                                      \
        }                                                                                          \
        CNOR_VM_SLOW();                                                                            \
    } while (0)

#define CNOR_VM_FORM_LI(tail, ok, expr)                                                            \
    do                                                                                             \
    {                                                                                              \
        const lh_value_t *const left = &fp[pc[1]];                                                 \
                                                                                                   \
        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(left) == lh_value_kind_sint))                         \
        {                                                                                          \
            const lh_sllong_t a = cnor_vm_fast_sint(left);                                         \
            const lh_sllong_t b = cnor_vm_fast_s32(pc + 2);                                        \
                                                                                                   \
            if (ok)                                                                                \
            {                                                                                      \
                ++sp;                                                                              \
                tail(6U, expr);                                                                    \
            }                                                                                      \
        }                                                                                          \
        CNOR_VM_SLOW();                                                                            \
    } while (0)

#define CNOR_VM_FORM_I(tail, ok, expr)                                                             \
    do                                                                                             \
    {                                                                                              \
        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint))                      \
        {                                                                                          \
            const lh_sllong_t a = cnor_vm_fast_sint(&sp[-1]);                                      \
            const lh_sllong_t b = cnor_vm_fast_s32(pc + 1);                                        \
                                                                                                   \
            if (ok)                                                                                \
            {                                                                                      \
                tail(5U, expr);                                                                    \
            }                                                                                      \
        }                                                                                          \
        CNOR_VM_SLOW();                                                                            \
    } while (0)

/* The three forms of an arithmetic or ordering operator, as labels. */
#define CNOR_VM_FORMS(name, tail, ok, expr)                                                        \
    CNOR_VM_OP(name##_ll)                                                                          \
    {                                                                                              \
        CNOR_VM_FORM_LL(tail, ok, expr);                                                           \
    }                                                                                              \
    CNOR_VM_OP(name##_li)                                                                          \
    {                                                                                              \
        CNOR_VM_FORM_LI(tail, ok, expr);                                                           \
    }                                                                                              \
    CNOR_VM_OP(name##_i)                                                                           \
    {                                                                                              \
        CNOR_VM_FORM_I(tail, ok, expr);                                                            \
    }

/*
 * The three forms of `eq` (@p want 1) or `ne` (@p want 0). Equality never
 * fails, so other kinds are compared inline by the full rule as well.
 */
#define CNOR_VM_EQUALITY_FORMS(name, want)                                                         \
    CNOR_VM_OP(name##_ll)                                                                          \
    {                                                                                              \
        const lh_value_t *const left = &fp[pc[1]];                                                 \
        const lh_value_t *const right = &fp[pc[2]];                                                \
        const int equal = CNOR_VM_EQUALS(left, right);                                             \
                                                                                                   \
        ++sp;                                                                                      \
        CNOR_VM_BRANCH_OR_SET(3U, equal == (want));                                                \
    }                                                                                              \
    CNOR_VM_OP(name##_li)                                                                          \
    {                                                                                              \
        const lh_value_t *const left = &fp[pc[1]];                                                 \
        const lh_sllong_t k = cnor_vm_fast_s32(pc + 2);                                            \
        const int equal = cnor_vm_fast_kind(left) == lh_value_kind_sint                            \
                              ? left->data.i == k                                                  \
                              : cnor_vm_fast_equals_sint(left, k);                                 \
                                                                                                   \
        ++sp;                                                                                      \
        CNOR_VM_BRANCH_OR_SET(6U, equal == (want));                                                \
    }                                                                                              \
    CNOR_VM_OP(name##_i)                                                                           \
    {                                                                                              \
        const lh_sllong_t k = cnor_vm_fast_s32(pc + 1);                                            \
        const int equal = cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint                         \
                              ? sp[-1].data.i == k                                                 \
                              : cnor_vm_fast_equals_sint(&sp[-1], k);                              \
                                                                                                   \
        CNOR_VM_BRANCH_OR_SET(5U, equal == (want));                                                \
    }

/* Labels of the three forms of binary operator @p name. */
#define CNOR_VM_FORM_LABELS(name)                                                                  \
    [cnor_op_##name##_ll] = &&cnor_vm_label_##name##_ll,                                          \
    [cnor_op_##name##_li] = &&cnor_vm_label_##name##_li,                                          \
    [cnor_op_##name##_i] = &&cnor_vm_label_##name##_i

/* Whether @p value equals the signed integer @p k by the full rule. */
static int
cnor_vm_fast_equals_sint(const lh_value_t *value, lh_sllong_t k)
{
    lh_value_t other;

    cnor_vm_fast_set_sint(&other, k);
    return (int) cnor_vm_value_equals(value, &other);
}

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
        [cnor_op_bnot] = &&cnor_vm_label_bnot,
        [cnor_op_band] = &&cnor_vm_label_band,
        [cnor_op_bor] = &&cnor_vm_label_bor,
        [cnor_op_bxor] = &&cnor_vm_label_bxor,
        [cnor_op_shl] = &&cnor_vm_label_shl,
        [cnor_op_shr] = &&cnor_vm_label_shr,
        [cnor_op_jmp] = &&cnor_vm_label_jmp,
        [cnor_op_jmp_if_false] = &&cnor_vm_label_jmp_if_false,
        [cnor_op_jmp_if_true] = &&cnor_vm_label_jmp_if_true,
        [cnor_op_call] = &&cnor_vm_label_call,
        [cnor_op_ret] = &&cnor_vm_label_ret,
        [cnor_op_call_local] = &&cnor_vm_label_call_local,
        [cnor_op_ret_local] = &&cnor_vm_label_ret_local,
        [cnor_op_native] = &&cnor_vm_label_native,
        [cnor_op_len] = &&cnor_vm_label_len,
        [cnor_op_index] = &&cnor_vm_label_index,
        CNOR_VM_FORM_LABELS(add),
        CNOR_VM_FORM_LABELS(sub),
        CNOR_VM_FORM_LABELS(mul),
        CNOR_VM_FORM_LABELS(div),
        CNOR_VM_FORM_LABELS(mod),
        CNOR_VM_FORM_LABELS(eq),
        CNOR_VM_FORM_LABELS(ne),
        CNOR_VM_FORM_LABELS(lt),
        CNOR_VM_FORM_LABELS(le),
        CNOR_VM_FORM_LABELS(gt),
        CNOR_VM_FORM_LABELS(ge),
        CNOR_VM_FORM_LABELS(band),
        CNOR_VM_FORM_LABELS(bor),
        CNOR_VM_FORM_LABELS(bxor),
        CNOR_VM_FORM_LABELS(shl),
        CNOR_VM_FORM_LABELS(shr),
        [cnor_op_for_loop] = &&cnor_vm_label_for_loop,
        [cnor_op_switch] = &&cnor_vm_label_switch,
        [cnor_op_index_ll] = &&cnor_vm_label_index_ll,
        [cnor_op_index_li] = &&cnor_vm_label_index_li,
        [cnor_op_index_i] = &&cnor_vm_label_index_i,
        [cnor_op_array_new] = &&cnor_vm_label_array_new,
        [cnor_op_array_room] = &&cnor_vm_label_array_room,
        [cnor_op_array_new_global] = &&cnor_vm_label_array_new_global,
        [cnor_op_array_room_global] = &&cnor_vm_label_array_room_global,
        [cnor_op_array_get_l] = &&cnor_vm_label_array_get_l,
        [cnor_op_array_set_l] = &&cnor_vm_label_array_set_l,
        [cnor_op_array_get] = &&cnor_vm_label_array_get,
        [cnor_op_array_set] = &&cnor_vm_label_array_set,
        [cnor_op_dict_new] = &&cnor_vm_label_dict_new,
        [cnor_op_dict_new_global] = &&cnor_vm_label_dict_new_global,
        [cnor_op_dict_get_l] = &&cnor_vm_label_dict_get_l,
        [cnor_op_dict_get_lk] = &&cnor_vm_label_dict_get_lk,
        [cnor_op_dict_set_l] = &&cnor_vm_label_dict_set_l,
        [cnor_op_dict_get] = &&cnor_vm_label_dict_get,
        [cnor_op_dict_set] = &&cnor_vm_label_dict_set,
        [cnor_op_dict_key_l] = &&cnor_vm_label_dict_key_l,
        [cnor_op_collection_get_l] = &&cnor_vm_label_collection_get_l,
        [cnor_op_try_push] = &&cnor_vm_label_try_push,
        [cnor_op_try_pop] = &&cnor_vm_label_try_pop,
        [cnor_op_try_finally] = &&cnor_vm_label_try_finally,
        [cnor_op_finally_end] = &&cnor_vm_label_finally_end,
        [cnor_op_raise] = &&cnor_vm_label_raise,
        [cnor_op_try_exit] = &&cnor_vm_label_try_exit,
        [cnor_op_push_function] = &&cnor_vm_label_push_function,
        [cnor_op_call_value] = &&cnor_vm_label_call_value,
        [cnor_op_call_builtin] = &&cnor_vm_label_call_builtin,
    };
#endif
    const lh_byte_t *const code = vm->code;
    lh_value_t *const values = vm->stack.values;
    const lh_value_t *const constants = vm->constants;
    const cnor_step_count_t limit = vm->config.max_steps;
    /* Steps left: see CNOR_VM_STEP. */
    cnor_step_count_t budget = limit - vm->steps;
    const lh_byte_t *pc = code + vm->ip;
    lh_value_t *sp = values + vm->stack.sp;
    lh_value_t *fp = values + vm->fp;
    /* The call stack: the next free frame, kept here like `sp`. */
    cnor_vm_frame_t *frame_top = vm->frames + vm->depth;

    if (!vm->running)
    {
        return lh_bool_true;
    }
#if CNOR_VM_COMPUTED_GOTO
    CNOR_VM_NEXT();
#else
cnor_vm_next:
    CNOR_VM_STEP();
cnor_vm_switch:
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

            --budget;
            if (pc[5] == cnor_op_add)
            {
                pc += 5;
                CNOR_VM_STORE_OR_SET(1U, CNOR_VM_WRAP(a, +, value));
            }
            if (pc[5] == cnor_op_sub)
            {
                pc += 5;
                CNOR_VM_STORE_OR_SET(1U, CNOR_VM_WRAP(a, -, value));
            }
            pc += 5;
            CNOR_VM_BRANCH_OR_SET(1U, a < value);
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
        cnor_vm_fast_move(sp++, &fp[pc[1]]);
        if (pc[2] == cnor_op_load_local && CNOR_VM_CAN_FUSE())
        {
            --budget;
            cnor_vm_fast_move(sp++, &fp[pc[3]]);
            pc += 4;
            CNOR_VM_NEXT();
        }
        pc += 2;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(store_local)
    {
        cnor_vm_fast_move(&fp[pc[1]], --sp);
        pc += 2;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(load_global)
    {
        cnor_vm_fast_move(sp++, &values[cnor_vm_fast_u16(pc + 1)]);
        pc += 3;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(store_global)
    {
        cnor_vm_fast_move(&values[cnor_vm_fast_u16(pc + 1)], --sp);
        pc += 3;
        CNOR_VM_NEXT();
    }

    /* -- shuffling -------------------------------------------------------- */

    CNOR_VM_OP(dup)
    {
        cnor_vm_fast_move(&sp[0], &sp[-1]);
        ++sp;
        pc += 1;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(swap)
    {
        lh_value_t held;

        cnor_vm_fast_move(&held, &sp[-1]);
        cnor_vm_fast_move(&sp[-1], &sp[-2]);
        cnor_vm_fast_move(&sp[-2], &held);
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
        if (!cnor_vm_fast_both_sint(&sp[-2], &sp[-1]))
        {
            /* Two strings join here; the pool running out is the slow path's. */
            if (cnor_vm_fast_both_str(&sp[-2], &sp[-1])
                && cnor_vm_fast_join(&vm->str_pool, &sp[-2], &sp[-2], &sp[-1]))
            {
                --sp;
                CNOR_VM_STORE_OR_KEEP(1U);
            }
            CNOR_VM_SLOW();
        }
        CNOR_VM_SINT_PAIR(CNOR_VM_STORE_ANY_OR_SET, CNOR_VM_WRAP(a, +, b));
    }
    CNOR_VM_OP(sub)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_STORE_ANY_OR_SET, CNOR_VM_WRAP(a, -, b));
    }
    CNOR_VM_OP(mul)
    {
        CNOR_VM_SINT_PAIR(CNOR_VM_STORE_ANY_OR_SET, CNOR_VM_WRAP(a, *, b));
    }
    CNOR_VM_OP(div)
    {
        /* Zero fails and -1 may overflow: both are the slow path's. */
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint && sp[-1].data.i != 0
            && sp[-1].data.i != -1)
        {
            CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, CNOR_VM_DIV(a, b));
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(mod)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint && sp[-1].data.i != 0
            && sp[-1].data.i != -1)
        {
            CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, CNOR_VM_MOD(a, b));
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
    /* The bitwise operators: whole numbers, bit by bit. */
    CNOR_VM_OP(band)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint)
        {
            CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, a & b);
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(bor)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint)
        {
            CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, a | b);
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(bxor)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint)
        {
            CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, a ^ b);
        }
        CNOR_VM_SLOW();
    }
    /* A shift of more than the width of the number has no answer, so the
     * amount is checked before the shift. */
    CNOR_VM_OP(shl)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint)
        {
            const lh_sllong_t amount = cnor_vm_fast_sint(&sp[-1]);

            if (amount >= 0 && amount <= 63)
            {
                CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, a << amount);
            }
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(shr)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint)
        {
            const lh_sllong_t amount = cnor_vm_fast_sint(&sp[-1]);

            if (amount >= 0 && amount <= 63)
            {
                CNOR_VM_SINT_PAIR(CNOR_VM_STORE_OR_SET, a >> amount);
            }
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(bnot)
    {
        if (cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint)
        {
            cnor_vm_fast_set_sint(&sp[-1], ~sp[-1].data.i);
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
            const int equal = CNOR_VM_EQUALS(&sp[-2], &sp[-1]);

            --sp;
            CNOR_VM_BRANCH_OR_SET(1U, equal);
        }
        CNOR_VM_SINT_PAIR(CNOR_VM_BRANCH_OR_SET, a == b);
    }
    CNOR_VM_OP(ne)
    {
        /* Equality never fails: other kinds are compared by the full rule. */
        if (!cnor_vm_fast_both_sint(&sp[-2], &sp[-1]))
        {
            const int equal = CNOR_VM_EQUALS(&sp[-2], &sp[-1]);

            --sp;
            CNOR_VM_BRANCH_OR_SET(1U, !equal);
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
        cnor_stack_slots_t left = reserve;
        cnor_vm_frame_t *frame;

        /* One check per call for the frame and every slot the callee can use.
         * The limits are read here, not kept in locals: with the switch
         * dispatch, more locals across the loop cost more than these loads. */
        if (frame_top == vm->frames + vm->config.max_depth
            || (lh_usize_t) (values + vm->stack.capacity - sp)
                   < (lh_usize_t) reserve + callee->max_stack)
        {
            CNOR_VM_SLOW();
        }
        frame = frame_top++;
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
        const lh_value_t *const top = &sp[-1];
        const cnor_vm_frame_t *frame;

        if (frame_top == vm->frames)
        {
            vm->result = *top;
            vm->running = lh_bool_false;
            sp = fp;
            CNOR_VM_SAVE();
            return lh_bool_true;
        }
        /* The value takes the place of the frame's first slot. */
        cnor_vm_fast_move(fp, top);
        sp = fp + 1;
        frame = --frame_top;
        pc = code + frame->return_addr;
        fp = values + frame->fp;
        vm->function = frame->function;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(call_local)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(ret_local)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(native)
    {
        const cnor_native_t *native = vm->natives[cnor_vm_fast_u16(pc + 1)];
        const cnor_argc_t argc = (cnor_argc_t) pc[3];
        cnor_error_t error = cnor_error_none;

        if (argc < native->arity_min || argc > native->arity_max)
        {
            CNOR_VM_SLOW();
        }
        /* A native cannot reach the machine, so the locals stay valid. The
         * result goes into the slot of the arguments: the native writes it
         * there, and no value is copied. */
        native->fn(sp - argc, argc, vm->config.ctx, &sp[-argc], &error);
        if (error != cnor_error_none)
        {
            CNOR_VM_SAVE();
            return cnor_vm_fail_at(vm, vm->ip, error);
        }
        /* The result takes the place of the arguments, and is on the stack. */
        sp -= argc;
        ++sp;
        pc += 4;
        CNOR_VM_NEXT();
    }

    /* -- operand forms ---------------------------------------------------- */

    CNOR_VM_OP(add_ll)
    {
        const lh_value_t *const text_left = &fp[pc[1]];
        const lh_value_t *const text_right = &fp[pc[2]];

        /* Integers first, as `add` tests them: two strings join here; the
         * pool running out is the slow path's. */
        if (!cnor_vm_fast_both_sint(text_left, text_right))
        {
            if (cnor_vm_fast_both_str(text_left, text_right)
                && cnor_vm_fast_join(&vm->str_pool, sp, text_left, text_right))
            {
                ++sp;
                CNOR_VM_STORE_OR_KEEP(3U);
            }
            CNOR_VM_SLOW();
        }
        CNOR_VM_FORM_LL(CNOR_VM_STORE_OR_SET, 1, CNOR_VM_WRAP(a, +, b));
    }
    CNOR_VM_OP(add_li)
    {
        CNOR_VM_FORM_LI(CNOR_VM_STORE_OR_SET, 1, CNOR_VM_WRAP(a, +, b));
    }
    CNOR_VM_OP(add_i)
    {
        CNOR_VM_FORM_I(CNOR_VM_STORE_OR_SET, 1, CNOR_VM_WRAP(a, +, b));
    }
    CNOR_VM_FORMS(sub, CNOR_VM_STORE_OR_SET, 1, CNOR_VM_WRAP(a, -, b))
    CNOR_VM_FORMS(mul, CNOR_VM_STORE_OR_SET, 1, CNOR_VM_WRAP(a, *, b))
    CNOR_VM_FORMS(div, CNOR_VM_STORE_OR_SET, b != 0 && b != -1, CNOR_VM_DIV(a, b))
    /* The bitwise operators, whole numbers only. */
    CNOR_VM_FORMS(band, CNOR_VM_STORE_OR_SET, 1, a & b)
    CNOR_VM_FORMS(bor, CNOR_VM_STORE_OR_SET, 1, a | b)
    CNOR_VM_FORMS(bxor, CNOR_VM_STORE_OR_SET, 1, a ^ b)
    CNOR_VM_FORMS(shl, CNOR_VM_STORE_OR_SET, b >= 0 && b <= 63, a << b)
    CNOR_VM_FORMS(shr, CNOR_VM_STORE_OR_SET, b >= 0 && b <= 63, a >> b)
    /* The remainder by a power of two is a mask; only the forms with an
     * immediate divisor are worth the test, the other two pay for it on every
     * remainder. */
    CNOR_VM_OP(mod_ll)
    {
        CNOR_VM_FORM_LL(CNOR_VM_STORE_OR_SET, b != 0 && b != -1, CNOR_VM_MOD(a, b));
    }
    CNOR_VM_OP(mod_li)
    {
        const lh_value_t *const left = &fp[pc[1]];
        const lh_s32_t b32 = (lh_s32_t) cnor_vm_fast_s32(pc + 2);

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(left) == lh_value_kind_sint) && b32 != 0
            && b32 != -1)
        {
            const lh_sllong_t a = cnor_vm_fast_sint(left);

            ++sp;
            CNOR_VM_STORE_OR_SET(6U, CNOR_VM_MOD_I(a, b32));
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(mod_i)
    {
        const lh_s32_t b32 = (lh_s32_t) cnor_vm_fast_s32(pc + 1);

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint) && b32 != 0
            && b32 != -1)
        {
            const lh_sllong_t a = cnor_vm_fast_sint(&sp[-1]);

            CNOR_VM_STORE_OR_SET(5U, CNOR_VM_MOD_I(a, b32));
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_EQUALITY_FORMS(eq, 1)
    CNOR_VM_EQUALITY_FORMS(ne, 0)
    CNOR_VM_FORMS(lt, CNOR_VM_BRANCH_OR_SET, 1, a < b)
    CNOR_VM_FORMS(le, CNOR_VM_BRANCH_OR_SET, 1, a <= b)
    CNOR_VM_FORMS(gt, CNOR_VM_BRANCH_OR_SET, 1, a > b)
    CNOR_VM_FORMS(ge, CNOR_VM_BRANCH_OR_SET, 1, a >= b)

    /* -- loops ------------------------------------------------------------ */

    CNOR_VM_OP(for_loop)
    {
        /* Same steps as cnor_vm_op_for_loop: `var + 1`, then `var < bound`. */
        lh_value_t *const var = &fp[pc[5]];
        const lh_value_t *const bound = &fp[pc[6]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_both_sint(var, bound)))
        {
            const lh_sllong_t next = CNOR_VM_WRAP(var->data.i, +, 1);

            var->data.i = next;
            /* The jump back has its own dispatch: its target is always the
             * loop's first instruction, so it predicts on its own. */
            if (CNOR_VM_LIKELY(next < bound->data.i))
            {
                pc += 7 + cnor_vm_fast_s32(pc + 1);
                CNOR_VM_NEXT();
            }
            pc += 7;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }

    /* -- match ------------------------------------------------------------ */

    CNOR_VM_OP(switch)
    {
        /* A signed integer picks its entry by subtraction, and the entry's
         * `jmp` runs here too when the budget allows a fused step; anything
         * else compares like the slow path. */
        const lh_value_t *const subject = &fp[pc[1]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(subject) == lh_value_kind_sint))
        {
            const lh_ullong_t i = (lh_ullong_t) cnor_vm_fast_sint(subject)
                                  - (lh_ullong_t) (lh_sllong_t) cnor_vm_fast_s32(pc + 2);
            const lh_ullong_t count = pc[6];

            pc += 7;
            if (i >= count)
            {
                pc += count * CNOR_OP_SWITCH_ENTRY_SIZE;
                CNOR_VM_NEXT();
            }
            pc += i * CNOR_OP_SWITCH_ENTRY_SIZE;
            if (CNOR_VM_CAN_FUSE())
            {
                --budget;
                pc += CNOR_OP_SWITCH_ENTRY_SIZE + cnor_vm_fast_s32(pc + 1);
            }
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }

    /* -- the rest --------------------------------------------------------- */

    CNOR_VM_OP(halt)
    {
        CNOR_VM_SLOW();
    }
    /* An array that is a value on the stack: the slow path knows where it is. */
    CNOR_VM_OP(array_get)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(array_set)
    {
        CNOR_VM_SLOW();
    }
    /* A function of the language itself: it works on the machine's strings,
     * so it runs through the slow path. */
    CNOR_VM_OP(call_builtin)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(len)
    {
        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_str))
        {
            cnor_vm_fast_set_sint(&sp[-1], (lh_sllong_t) cnor_vm_fast_str_size(&sp[-1]));
            pc += 1;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(index)
    {
        /* A signed index inside the text; anything else (unsigned indexes
         * included, and every failure) is the slow path's. */
        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(&sp[-2]) == lh_value_kind_str
                           && cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint))
        {
            const lh_sllong_t at = cnor_vm_fast_sint(&sp[-1]);

            if (at >= 0 && (lh_ullong_t) at < (lh_ullong_t) cnor_vm_fast_str_size(&sp[-2]))
            {
                cnor_vm_fast_set_str(&sp[-2], cnor_vm_fast_str_data(&sp[-2]) + at, 1U);
                --sp;
                pc += 1;
                CNOR_VM_NEXT();
            }
        }
        CNOR_VM_SLOW();
    }
    /* The index forms: the text and the index come from the frame or the code,
     * so the text is not copied onto the stack first. */
    CNOR_VM_OP(index_ll)
    {
        const lh_value_t *const text = &fp[pc[1]];
        const lh_value_t *const index = &fp[pc[2]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(text) == lh_value_kind_str
                           && cnor_vm_fast_kind(index) == lh_value_kind_sint))
        {
            const lh_sllong_t at = cnor_vm_fast_sint(index);

            if (at >= 0 && (lh_ullong_t) at < (lh_ullong_t) cnor_vm_fast_str_size(text))
            {
                cnor_vm_fast_set_str(&sp[0], cnor_vm_fast_str_data(text) + at, 1U);
                ++sp;
                pc += 3;
                CNOR_VM_NEXT();
            }
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(index_li)
    {
        const lh_value_t *const text = &fp[pc[1]];
        const lh_s32_t at = (lh_s32_t) cnor_vm_fast_s32(pc + 2);

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(text) == lh_value_kind_str) && at >= 0
            && (lh_u32_t) at < (lh_u32_t) cnor_vm_fast_str_size(text))
        {
            cnor_vm_fast_set_str(&sp[0], cnor_vm_fast_str_data(text) + at, 1U);
            ++sp;
            pc += 6;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(index_i)
    {
        const lh_s32_t at = (lh_s32_t) cnor_vm_fast_s32(pc + 1);

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_str) && at >= 0
            && (lh_u32_t) at < (lh_u32_t) cnor_vm_fast_str_size(&sp[-1]))
        {
            cnor_vm_fast_set_str(&sp[-1], cnor_vm_fast_str_data(&sp[-1]) + at, 1U);
            pc += 5;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }
    /* The array forms: the elements are slots of the frame, the header that
     * counts them is in the slot before them, and the array's value is in the
     * slot before that. */
    CNOR_VM_OP(array_new)
    {
        const cnor_slot_t slot = (cnor_slot_t) pc[1];
        const lh_u16_t count = (lh_u16_t) cnor_vm_fast_u16(pc + 2);
        lh_value_t *const header = &fp[slot + 1U];
        lh_value_t *const elements = &fp[slot + 3U];
        lh_u16_t i;

        for (i = 0U; i < count; ++i)
        {
            cnor_vm_fast_move(&elements[i], &sp[-1]);
        }
        cnor_vm_fast_set_sint(&header[0], (lh_sllong_t) count);
        cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) count);
        cnor_vm_fast_set_array(&fp[slot], header);
        --sp;
        pc += 4;
        CNOR_VM_NEXT();
    }
    /* An empty array with room for more: the elements are `none` until
     * something is pushed into them. */
    CNOR_VM_OP(array_room)
    {
        const cnor_slot_t slot = (cnor_slot_t) pc[1];
        const lh_u16_t room = (lh_u16_t) cnor_vm_fast_u16(pc + 2);
        lh_value_t *const header = &fp[slot + 1U];

        cnor_vm_fast_set_sint(&header[0], 0);
        cnor_vm_fast_set_sint(&header[1], (lh_sllong_t) room);
        cnor_vm_fast_set_array(&fp[slot], header);
        pc += 4;
        CNOR_VM_NEXT();
    }
    /* The same, in the globals: they outlive the frame that made them. */
    CNOR_VM_OP(array_new_global)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(array_room_global)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(array_get_l)
    {
        const lh_value_t *const array = &fp[pc[1]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(array) == CNOR_VALUE_KIND_ARRAY
                           && cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint))
        {
            const lh_sllong_t at = cnor_vm_fast_sint(&sp[-1]);

            if (at >= 0 && (lh_ullong_t) at < cnor_vm_fast_array_count(array))
            {
                cnor_vm_fast_move(&sp[-1], &cnor_vm_fast_array_data(array)[at]);
                pc += 2;
                CNOR_VM_NEXT();
            }
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(array_set_l)
    {
        const lh_value_t *const array = &fp[pc[1]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(array) == CNOR_VALUE_KIND_ARRAY
                           && cnor_vm_fast_kind(&sp[-2]) == lh_value_kind_sint))
        {
            const lh_sllong_t at = cnor_vm_fast_sint(&sp[-2]);

            if (at >= 0 && (lh_ullong_t) at < cnor_vm_fast_array_count(array))
            {
                cnor_vm_fast_move(&cnor_vm_fast_array_data(array)[at], &sp[-1]);
                sp -= 2;
                pc += 2;
                CNOR_VM_NEXT();
            }
        }
        CNOR_VM_SLOW();
    }
    /* The dictionary forms: the pairs are slots of the frame, and the header
     * is in the slot before them. */
    CNOR_VM_OP(dict_new)
    {
        const cnor_slot_t slot = (cnor_slot_t) pc[1];
        const lh_u16_t room = (lh_u16_t) cnor_vm_fast_u16(pc + 2);

        cnor_vm_fast_set_sint(&fp[slot + 1U], 0);
        cnor_vm_fast_set_sint(&fp[slot + 2U], (lh_sllong_t) room);
        cnor_vm_fast_set_dict(&fp[slot], &fp[slot + 1U]);
        pc += 4;
        CNOR_VM_NEXT();
    }
    /* The same, in the globals: they outlive the frame that made them. */
    CNOR_VM_OP(dict_new_global)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(dict_get_l)
    {
        const lh_value_t *const dictionary = &fp[pc[1]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(dictionary) == CNOR_VALUE_KIND_DICT
                           && cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_str))
        {
            lh_value_t *const found = cnor_vm_dict_lookup(dictionary, &sp[-1]);

            if (found != lh_null)
            {
                cnor_vm_fast_move(&sp[-1], found);
            }
            else
            {
                /* No such key: `none`, and the run goes on. */
                cnor_vm_fast_set_none(&sp[-1]);
            }
            pc += 2;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(dict_get_lk)
    {
        const lh_value_t *const dictionary = &fp[pc[1]];
        const lh_value_t *const key = &constants[cnor_vm_fast_u16(pc + 2)];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(dictionary) == CNOR_VALUE_KIND_DICT
                           && cnor_vm_fast_kind(key) == lh_value_kind_str))
        {
            lh_value_t *const found = cnor_vm_dict_lookup(dictionary, key);

            if (found != lh_null)
            {
                cnor_vm_fast_move(sp++, found);
            }
            else
            {
                /* No such key: `none`, and the run goes on. */
                cnor_vm_fast_set_none(sp++);
            }
            pc += 4;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(dict_set_l)
    {
        lh_value_t *const dictionary = &fp[pc[1]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(dictionary) == CNOR_VALUE_KIND_DICT
                           && cnor_vm_fast_kind(&sp[-2]) == lh_value_kind_str))
        {
            lh_value_t *const found = cnor_vm_dict_lookup(dictionary, &sp[-2]);

            if (found != lh_null)
            {
                cnor_vm_fast_move(found, &sp[-1]);
                sp -= 2;
                pc += 2;
                CNOR_VM_NEXT();
            }
            if (cnor_vm_fast_dict_count(dictionary) < cnor_vm_fast_dict_room(dictionary))
            {
                lh_value_t *const pairs = cnor_vm_fast_dict_data(dictionary);
                const lh_u32_t count = cnor_vm_fast_dict_count(dictionary);

                cnor_vm_fast_move(&pairs[count * 2U], &sp[-2]);
                cnor_vm_fast_move(&pairs[count * 2U + 1U], &sp[-1]);
                cnor_vm_fast_dict_set_count(dictionary, count + 1U);
                sp -= 2;
                pc += 2;
                CNOR_VM_NEXT();
            }
            /* The dictionary is full: the slow path reports it. */
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(dict_key_l)
    {
        const lh_value_t *const dictionary = &fp[pc[1]];

        if (CNOR_VM_LIKELY(cnor_vm_fast_kind(dictionary) == CNOR_VALUE_KIND_DICT
                           && cnor_vm_fast_kind(&sp[-1]) == lh_value_kind_sint))
        {
            const lh_sllong_t at = cnor_vm_fast_sint(&sp[-1]);

            if (at >= 0 && (lh_ullong_t) at < cnor_vm_fast_dict_count(dictionary))
            {
                cnor_vm_fast_move(&sp[-1], &cnor_vm_fast_dict_data(dictionary)[at * 2]);
                pc += 2;
                CNOR_VM_NEXT();
            }
        }
        CNOR_VM_SLOW();
    }
    /* A collection whose kind is not known when the code is written: the
     * machine tells an array from a dictionary. */
    CNOR_VM_OP(collection_get_l)
    {
        CNOR_VM_SLOW();
    }
    /* The error handlers: the catch block, the finally block, and the end of
     * the `try`. */
    CNOR_VM_OP(try_push)
    {
        if (CNOR_VM_LIKELY(vm->try_count < (cnor_count_t) vm->config.max_depth))
        {
            cnor_vm_try_t *const entry = &vm->tries[vm->try_count];
            const lh_s32_t catch_rel = cnor_vm_fast_s32(pc + 1);
            const cnor_code_addr_t next = (cnor_code_addr_t) ((pc - code) + 6);

            /* The catch block, as the jump operand says: the next instruction
             * plus the distance. A distance of zero means the block is not
             * there. */
            entry->catch_addr = catch_rel != 0 ? (cnor_code_addr_t) (next + catch_rel) : 0U;
            entry->finally_addr = 0U;
            entry->depth = (cnor_call_depth_t) (frame_top - vm->frames);
            entry->slot = (lh_u8_t) pc[5];
            entry->flags = 0U;
            entry->error = cnor_error_none;
            entry->trace_head = CNOR_VM_TRACE_NONE;
            entry->trace_truncated = lh_bool_false;
            ++vm->try_count;
            pc += 6;
            CNOR_VM_NEXT();
        }
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(try_finally)
    {
        if (vm->try_count > 0U)
        {
            const lh_s32_t rel = cnor_vm_fast_s32(pc + 1);

            vm->tries[vm->try_count - 1U].finally_addr =
                rel != 0 ? (cnor_code_addr_t) ((pc - code) + 5 + rel) : 0U;
        }
        pc += 5;
        CNOR_VM_NEXT();
    }
    CNOR_VM_OP(try_pop)
    {
        if (vm->try_count > 0U)
        {
            cnor_vm_try_t *const entry = &vm->tries[vm->try_count - 1U];

            /* The code that may fail is done, and the error it took is handled.
             * A `try` with a finally block keeps its handler until the finally
             * block ends. */
            entry->flags |= (lh_u8_t) (CNOR_VM_TRY_CLOSED | CNOR_VM_TRY_CLEANED);
            entry->error = cnor_error_none;
            cnor_vm_trace_release_try(vm, entry);
            if (entry->finally_addr == 0U)
            {
                --vm->try_count;
            }
        }
        pc += 1;
        CNOR_VM_NEXT();
    }
    /* The end of a finally block, the error being raised again, and leaving a
     * `try` that has one: the slow path knows where they go. */
    CNOR_VM_OP(finally_end)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(raise)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(try_exit)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(push_function)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(call_value)
    {
        CNOR_VM_SLOW();
    }
    /* A dictionary that is a value on the stack: the slow path knows where it
     * is. */
    CNOR_VM_OP(dict_get)
    {
        CNOR_VM_SLOW();
    }
    CNOR_VM_OP(dict_set)
    {
        CNOR_VM_SLOW();
    }

#if !CNOR_VM_COMPUTED_GOTO
    default:
        /* The verifier proved every executed byte is an opcode, so the
         * switch needs no range check. */
        lh_compiler_unreachable();
    }
#endif

cnor_vm_budget_spent:
    /* With no limit, `budget` at 0 is the count wrapping, and the instruction
     * runs; otherwise it is the stop. */
    if (limit == 0U)
    {
        --budget;
#if CNOR_VM_COMPUTED_GOTO
        goto *m_labels[*pc];
#else
        goto cnor_vm_switch;
#endif
    }
    CNOR_VM_SAVE();
    return cnor_vm_fail_at(vm, vm->ip, cnor_error_step_limit);
}
