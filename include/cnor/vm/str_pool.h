/**
 * @file str_pool.h
 * @brief Storage for strings a running program builds (::cnor_str_pool_t).
 *
 * A bump allocator over one block taken at load time. Nothing is freed one by
 * one: ::cnor_vm_reset rewinds the whole pool, which is why a string a native
 * receives stays valid until the next reset.
 *
 * @note Candidate lh brick: a byte arena with rewind.
 */

#ifndef CNOR_VM_STR_POOL_H
#define CNOR_VM_STR_POOL_H

#include <lh/bool.h>
#include <lh/char.h>
#include <lh/compiler/extern/c.h>
#include <lh/numeric/fixed/types.h>

/**
 * @struct cnor_str_pool
 * @brief One block and how much of it is taken.
 */
struct cnor_str_pool
{
    lh_char_t *data; /**< The block, or ::lh_null for a zero-size pool. */
    lh_u32_t size;   /**< Its capacity. */
    lh_u32_t used;   /**< Bytes taken since the last rewind. */
};
/** @copybrief cnor_str_pool */
typedef struct cnor_str_pool cnor_str_pool_t;

LH_COMPILER_EXTERN_C_BEGIN

/** @brief Empty pool. */
void
cnor_str_pool_init(cnor_str_pool_t *self);

/** @brief Allocate @p size bytes. @return ::lh_bool_false without memory. */
lh_bool_t
cnor_str_pool_reserve(cnor_str_pool_t *self, lh_u32_t size);

/** @brief Release the block. */
void
cnor_str_pool_deinit(cnor_str_pool_t *self);

/** @brief Forget every string. */
void
cnor_str_pool_rewind(cnor_str_pool_t *self);

/** @brief @p size bytes, or ::lh_null when the pool is full. */
lh_char_t *
cnor_str_pool_take(cnor_str_pool_t *self, lh_u32_t size);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_VM_STR_POOL_H */
