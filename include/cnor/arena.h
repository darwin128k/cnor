/**
 * @file arena.h
 * @brief Chunked bump allocator (::cnor_arena_t) for the syntax tree.
 *
 * Nodes and their text are allocated here and released all at once by
 * ::cnor_arena_deinit, which is also the whole teardown of a parse that failed
 * halfway. Chunks come from the lh runtime allocator; the arena grows by adding
 * chunks, so nothing already handed out ever moves.
 *
 * @note Candidate lh brick: nothing here is specific to cnor.
 */

#ifndef CNOR_ARENA_H
#define CNOR_ARENA_H

#include <lh/bool.h>
#include <lh/compiler/extern/c.h>
#include <lh/ptr.h>
#include <lh/size.h>
#include <lh/str/view.h>

/**
 * @def CNOR_ARENA_DEFAULT_CHUNK
 * @brief Chunk size used when ::cnor_arena_init is given 0.
 */
#define CNOR_ARENA_DEFAULT_CHUNK 4096U

/**
 * @def CNOR_ARENA_ALIGN
 * @brief Alignment of every block handed out.
 */
#define CNOR_ARENA_ALIGN 8U

struct cnor_arena_chunk;

/**
 * @struct cnor_arena
 * @brief A list of chunks, newest first.
 */
struct cnor_arena
{
    struct cnor_arena_chunk *head; /**< Chunk being filled, or ::lh_null. */
    lh_usize_t chunk_size;         /**< Payload bytes of a regular chunk. */
    lh_usize_t total;              /**< Bytes handed out so far. */
};
/** @copybrief cnor_arena */
typedef struct cnor_arena cnor_arena_t;

LH_COMPILER_EXTERN_C_BEGIN

/**
 * @brief Prepare an empty arena. Nothing is allocated yet.
 * @param chunk_size Payload of each chunk; 0 picks ::CNOR_ARENA_DEFAULT_CHUNK.
 */
void
cnor_arena_init(cnor_arena_t *self, lh_usize_t chunk_size);

/** @brief Release every chunk. The arena is empty and reusable afterwards. */
void
cnor_arena_deinit(cnor_arena_t *self);

/**
 * @brief Take @p size zeroed bytes aligned to ::CNOR_ARENA_ALIGN.
 * @return The block, or ::lh_null when the allocator refused a chunk.
 */
lh_ptr
cnor_arena_alloc(cnor_arena_t *self, lh_usize_t size);

/**
 * @brief Copy @p text into the arena.
 * @param[out] out The copy; untouched on failure.
 * @return ::lh_bool_false when there is no memory.
 */
lh_bool_t
cnor_arena_copy_text(cnor_arena_t *self, lh_str_view_t text, lh_str_view_t *out);

/** @brief Bytes handed out since init. */
lh_usize_t
cnor_arena_get_total(const cnor_arena_t *self);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_ARENA_H */
