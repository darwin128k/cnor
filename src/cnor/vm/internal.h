/**
 * @file internal.h
 * @brief Pieces of the interpreter shared between its translation units.
 *
 * ## Adding an instruction
 *
 * 1. Add it to ::cnor_op_t and its row to the op info table (src/cnor/op.c):
 *    operands and stack effect, which the compiler and the verifier both read.
 * 2. Write a ::cnor_vm_handler_t in the op/ file of its family and declare it
 *    below.
 * 3. Point its slot of the handler table (src/cnor/vm/dispatch.c) at it.
 * 4. Give it a label in the dispatch loop (`m_labels` and a `CNOR_VM_OP`
 *    block): inline code for its common case, or just `CNOR_VM_SLOW()`.
 */

#ifndef CNOR_VM_INTERNAL_H
#define CNOR_VM_INTERNAL_H

#include <cnor/op.h>
#include <cnor/vm.h>

/**
 * @typedef cnor_vm_handler_t
 * @brief Executes one decoded instruction. `vm->ip` already points past it.
 * @return ::lh_bool_false after ::cnor_vm_fail.
 */
typedef lh_bool_t (*cnor_vm_handler_t)(cnor_vm_t *vm, const cnor_insn_t *insn);

/* -- vm.c ------------------------------------------------------------------ */

/** @brief Record @p code at @p insn and stop. @return ::lh_bool_false. */
lh_bool_t
cnor_vm_fail(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_error_t code);

/* -- load.c ---------------------------------------------------------------- */

/** @brief Everything ::cnor_vm_load does after the image is copied. */
lh_bool_t
cnor_vm_load_image(cnor_vm_t *vm, lh_u32_t size);

/* -- dispatch.c ------------------------------------------------------------ */

/** @brief Execute from `vm->ip` until the entry function returns or halts. */
lh_bool_t
cnor_vm_dispatch(cnor_vm_t *vm);

/* -- value.c --------------------------------------------------------------- */

/** @brief none, false, 0 and "" are false; everything else is true. */
lh_bool_t
cnor_vm_value_is_truthy(const lh_value_t *value);

/** @brief Whether @p value is a signed, unsigned or real number. */
lh_bool_t
cnor_vm_value_is_number(const lh_value_t *value);

/** @brief A signed or unsigned number as a signed one. */
lh_sllong_t
cnor_vm_value_to_sint(const lh_value_t *value);

/** @brief -1, 0 or 1 for two integers, signed or unsigned, in any mix. */
int
cnor_vm_value_compare(const lh_value_t *left, const lh_value_t *right);

/**
 * @brief Whether ordering @p op (`lt`, `le`, `gt`, `ge`) holds between two
 *        numbers. A real on either side compares as reals; NaN is unordered.
 */
lh_bool_t
cnor_vm_value_order_holds(const lh_value_t *left, const lh_value_t *right, cnor_op_t op);

/** @brief Numbers by value across kinds (NaN equals nothing), everything else
 *         by kind and content. */
lh_bool_t
cnor_vm_value_equals(const lh_value_t *left, const lh_value_t *right);

#if LH_REAL_ENABLED
/** @brief Any number as a real. */
lh_real_t
cnor_vm_value_to_real(const lh_value_t *value);
#endif

/* -- op/ ------------------------------------------------------------------- */

lh_bool_t cnor_vm_op_halt(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_push_none(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_push_true(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_push_false(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_push_i32(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_push_u32(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_push_const(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_load_local(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_store_local(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_load_global(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_store_global(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_dup(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_swap(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_pop(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_arith(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_neg(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_equality(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_order(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_not(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_jmp(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_jmp_if(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_call(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_ret(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_native(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_len(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_index(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_index_operands(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_array_new(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_array_new_global(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_array_get_l(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_array_set_l(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_array_get(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_array_set(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_call_builtin(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_operands(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_for_loop(cnor_vm_t *vm, const cnor_insn_t *insn);
lh_bool_t cnor_vm_op_switch(cnor_vm_t *vm, const cnor_insn_t *insn);

/**
 * @brief `add` `sub` `mul` `div` `mod` (@p op) on two values, into @p out;
 *        failures are reported at @p insn.
 */
lh_bool_t
cnor_vm_arith_values(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_op_t op,
                     const lh_value_t *left, const lh_value_t *right, lh_value_t *out);

/**
 * @brief `eq` `ne` `lt` `le` `gt` `ge` (@p op) on two values, into @p out as a
 *        boolean; failures are reported at @p insn.
 */
lh_bool_t
cnor_vm_compare_values(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_op_t op,
                       const lh_value_t *left, const lh_value_t *right, lh_value_t *out);

/** @brief Join two strings into the pool (`add` on two strings), into @p out. */
lh_bool_t
cnor_vm_text_join(cnor_vm_t *vm, const cnor_insn_t *insn, const lh_value_t *left,
                  const lh_value_t *right, lh_value_t *out);

/**
 * @brief Replace @p text with its character at @p index (`text[index]`).
 * @return ::cnor_error_type or ::cnor_error_index with @p text untouched.
 */
cnor_error_t
cnor_vm_text_index(lh_value_t *text, const lh_value_t *index);

#endif /* CNOR_VM_INTERNAL_H */
