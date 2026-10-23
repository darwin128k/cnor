#include <cnor/arena.h>
#include <lh/memory.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>
#include <lh/util/return.h>

struct cnor_arena_chunk
{
    struct cnor_arena_chunk *next;
    lh_usize_t size;
    lh_usize_t used;
};

/* The payload starts after the chunk header, rounded up to the alignment. */
#define CNOR_ARENA_HEADER                                                                          \
    ((sizeof(struct cnor_arena_chunk) + CNOR_ARENA_ALIGN - 1U) & ~(lh_usize_t) (CNOR_ARENA_ALIGN - 1U))

/* -- chunks ---------------------------------------------------------------- */

static lh_usize_t
cnor_arena_round_up(lh_usize_t size)
{
    return (size + CNOR_ARENA_ALIGN - 1U) & ~(lh_usize_t) (CNOR_ARENA_ALIGN - 1U);
}

static lh_byte_t *
cnor_arena_chunk_payload(struct cnor_arena_chunk *chunk)
{
    return (lh_byte_t *) chunk + CNOR_ARENA_HEADER;
}

static struct cnor_arena_chunk *
cnor_arena_add_chunk(cnor_arena_t *self, lh_usize_t at_least)
{
    const lh_usize_t size = at_least > self->chunk_size ? at_least : self->chunk_size;
    struct cnor_arena_chunk *chunk =
        (struct cnor_arena_chunk *) lh_runtime_allocator_alloc(CNOR_ARENA_HEADER + size);

    if (chunk == lh_null)
    {
        return lh_null;
    }
    chunk->next = self->head;
    chunk->size = size;
    chunk->used = 0U;
    self->head = chunk;
    return chunk;
}

/* -- public ---------------------------------------------------------------- */

void
cnor_arena_init(cnor_arena_t *self, lh_usize_t chunk_size)
{
    lh_return_ifn(self);
    self->head = lh_null;
    self->chunk_size = cnor_arena_round_up(chunk_size == 0U ? CNOR_ARENA_DEFAULT_CHUNK : chunk_size);
    self->total = 0U;
}

void
cnor_arena_deinit(cnor_arena_t *self)
{
    lh_return_ifn(self);
    while (self->head != lh_null)
    {
        struct cnor_arena_chunk *next = self->head->next;

        lh_runtime_allocator_free(self->head);
        self->head = next;
    }
    self->total = 0U;
}

lh_ptr
cnor_arena_alloc(cnor_arena_t *self, lh_usize_t size)
{
    const lh_usize_t rounded = cnor_arena_round_up(size == 0U ? 1U : size);
    struct cnor_arena_chunk *chunk;
    lh_byte_t *block;

    lh_return_ifn(self, lh_null);
    chunk = self->head;
    if (chunk == lh_null || chunk->size - chunk->used < rounded)
    {
        chunk = cnor_arena_add_chunk(self, rounded);
        lh_return_ifn(chunk, lh_null);
    }
    block = cnor_arena_chunk_payload(chunk) + chunk->used;
    chunk->used += rounded;
    self->total += rounded;
    lh_memory_set(block, rounded, 0U);
    return block;
}

lh_bool_t
cnor_arena_copy_text(cnor_arena_t *self, lh_str_view_t text, lh_str_view_t *out)
{
    lh_byte_t *copy;

    lh_return_ifn(out, lh_bool_false);
    copy = (lh_byte_t *) cnor_arena_alloc(self, text.size);
    lh_return_ifn(copy, lh_bool_false);
    if (text.size > 0U)
    {
        lh_memory_copy(copy, text.size, text.begin, text.size);
    }
    out->begin = copy;
    out->size = text.size;
    return lh_bool_true;
}

lh_usize_t
cnor_arena_get_total(const cnor_arena_t *self)
{
    lh_return_ifn(self, 0U);
    return self->total;
}
