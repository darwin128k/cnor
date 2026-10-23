/**
 * @file stack.h
 * @brief The machine's value stack (::cnor_vm_stack_t): globals at the bottom,
 *        then one frame per active call (slots, then operands).
 *
 * Allocated once, when a program is loaded, and never grown. Bounds are proved
 * by the verifier per function and checked per call frame, so pushing and
 * popping do not test anything themselves.
 */

#ifndef CNOR_VM_STACK_H
#define CNOR_VM_STACK_H

#include <cnor/types.h>
#include <lh/bool.h>
#include <lh/compiler/extern/c.h>
#include <lh/value.h>

/**
 * @struct cnor_vm_stack
 * @brief A fixed array of values and how many are in use.
 */
struct cnor_vm_stack
{
    lh_value_t *values;            /**< Storage, runtime-allocator owned. */
    cnor_stack_slots_t capacity;   /**< Its length. */
    cnor_stack_slots_t sp;         /**< Index of the next free slot. */
};
/** @copybrief cnor_vm_stack */
typedef struct cnor_vm_stack cnor_vm_stack_t;

LH_COMPILER_EXTERN_C_BEGIN

/** @brief No storage yet. */
void
cnor_vm_stack_init(cnor_vm_stack_t *self);

/** @brief Allocate @p capacity slots. @return ::lh_bool_false without memory. */
lh_bool_t
cnor_vm_stack_reserve(cnor_vm_stack_t *self, cnor_stack_slots_t capacity);

/** @brief Release the storage. */
void
cnor_vm_stack_deinit(cnor_vm_stack_t *self);

/** @brief Whether @p count more slots fit. */
lh_bool_t
cnor_vm_stack_fits(const cnor_vm_stack_t *self, cnor_stack_slots_t count);

/** @brief Push @p value. The caller has proved there is room. */
void
cnor_vm_stack_push(cnor_vm_stack_t *self, lh_value_t value);

/** @brief Pop the top value. The caller has proved there is one. */
lh_value_t
cnor_vm_stack_pop(cnor_vm_stack_t *self);

/** @brief The value @p depth slots below the top (0 is the top). */
lh_value_t *
cnor_vm_stack_peek(cnor_vm_stack_t *self, cnor_stack_slots_t depth);

/** @brief The slot at absolute index @p index. */
lh_value_t *
cnor_vm_stack_at(cnor_vm_stack_t *self, cnor_stack_slots_t index);

/** @brief Push @p count none values. */
void
cnor_vm_stack_push_none(cnor_vm_stack_t *self, cnor_stack_slots_t count);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_VM_STACK_H */
