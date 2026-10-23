#include <cnor/byte/writer.h>
#include <lh/bit/endian.h>
#include <lh/memory.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>

void
cnor_byte_writer_init(cnor_byte_writer_t *self)
{
    self->data = lh_null;
    self->size = 0U;
    self->capacity = 0U;
}

void
cnor_byte_writer_deinit(cnor_byte_writer_t *self)
{
    if (self->data != lh_null)
    {
        lh_runtime_allocator_free(self->data);
    }
    cnor_byte_writer_init(self);
}

lh_u32_t
cnor_byte_writer_get_size(const cnor_byte_writer_t *self)
{
    return self->size;
}

lh_byte_t *
cnor_byte_writer_get_data(const cnor_byte_writer_t *self)
{
    return self->size > 0U ? self->data : lh_null;
}

/* Make room for @p extra more bytes, doubling the block. @return ::lh_bool_false
 * when it cannot grow; the bytes written so far stay. */
static lh_bool_t
cnor_byte_writer_grow(cnor_byte_writer_t *self, lh_u32_t extra)
{
    const lh_u32_t needed = self->size + extra;
    lh_u32_t capacity = self->capacity > 0U ? self->capacity : CNOR_BYTE_WRITER_MIN_CAPACITY;
    lh_byte_t *data;

    if (needed < self->size)
    {
        return lh_bool_false;
    }
    while (capacity < needed)
    {
        capacity = capacity <= 0x7FFFFFFFU ? capacity * 2U : needed;
    }
    data = self->data == lh_null
               ? (lh_byte_t *) lh_runtime_allocator_alloc(capacity)
               : (lh_byte_t *) lh_runtime_allocator_realloc(self->data, self->capacity, capacity);
    if (data == lh_null)
    {
        return lh_bool_false;
    }
    self->data = data;
    self->capacity = capacity;
    return lh_bool_true;
}

void
cnor_byte_writer_reserve(cnor_byte_writer_t *self, lh_u32_t capacity)
{
    if (capacity > self->capacity)
    {
        (void) cnor_byte_writer_grow(self, capacity - self->size);
    }
}

/* Where @p extra more bytes go, or ::lh_null when there is no room for them. */
static lh_byte_t *
cnor_byte_writer_room(cnor_byte_writer_t *self, lh_u32_t extra)
{
    lh_byte_t *at;

    if (self->capacity - self->size < extra && !cnor_byte_writer_grow(self, extra))
    {
        return lh_null;
    }
    at = self->data + self->size;
    self->size += extra;
    return at;
}

void
cnor_byte_writer_put_bytes(cnor_byte_writer_t *self, const lh_byte_t *data, lh_u32_t size)
{
    lh_byte_t *at;

    if (size == 0U)
    {
        return;
    }
    at = cnor_byte_writer_room(self, size);
    if (at != lh_null)
    {
        lh_memory_copy(at, size, data, size);
    }
}

void
cnor_byte_writer_put_u8(cnor_byte_writer_t *self, lh_u8_t value)
{
    lh_byte_t *at = cnor_byte_writer_room(self, 1U);

    if (at != lh_null)
    {
        *at = value;
    }
}

void
cnor_byte_writer_put_u16(cnor_byte_writer_t *self, lh_u16_t value)
{
    lh_byte_t *at = cnor_byte_writer_room(self, 2U);

    if (at != lh_null)
    {
        lh_bit_pack_le16(value, at);
    }
}

void
cnor_byte_writer_put_u32(cnor_byte_writer_t *self, lh_u32_t value)
{
    lh_byte_t *at = cnor_byte_writer_room(self, 4U);

    if (at != lh_null)
    {
        lh_bit_pack_le32(value, at);
    }
}

void
cnor_byte_writer_align(cnor_byte_writer_t *self, lh_u32_t alignment)
{
    while (self->size % alignment != 0U)
    {
        cnor_byte_writer_put_u8(self, 0U);
    }
}

void
cnor_byte_writer_patch_u32(cnor_byte_writer_t *self, lh_u32_t at, lh_u32_t value)
{
    if (at + 4U <= self->size)
    {
        lh_bit_pack_le32(value, self->data + at);
    }
}

lh_byte_t *
cnor_byte_writer_release(cnor_byte_writer_t *self, lh_u32_t *size)
{
    lh_byte_t *block = self->data;

    *size = self->size;
    if (self->size == 0U)
    {
        return lh_null;
    }
    if (self->size < self->capacity)
    {
        lh_byte_t *cut =
            (lh_byte_t *) lh_runtime_allocator_realloc(self->data, self->capacity, self->size);

        /* A block that cannot shrink is handed over as it is. */
        block = cut != lh_null ? cut : self->data;
    }
    cnor_byte_writer_init(self);
    return block;
}
