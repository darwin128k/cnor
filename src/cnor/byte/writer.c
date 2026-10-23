#include <cnor/byte/writer.h>
#include <lh/bit/endian.h>
#include <lh/memory.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>

void
cnor_byte_writer_init(cnor_byte_writer_t *self)
{
    lh_array_init(&self->bytes, 1U);
}

void
cnor_byte_writer_deinit(cnor_byte_writer_t *self)
{
    lh_array_deinit(&self->bytes);
}

lh_u32_t
cnor_byte_writer_get_size(const cnor_byte_writer_t *self)
{
    return (lh_u32_t) lh_array_get_size(&self->bytes);
}

lh_byte_t *
cnor_byte_writer_get_data(const cnor_byte_writer_t *self)
{
    if (lh_array_is_empty(&self->bytes))
    {
        return lh_null;
    }
    return (lh_byte_t *) lh_array_get_begin(&self->bytes);
}

void
cnor_byte_writer_put_bytes(cnor_byte_writer_t *self, const lh_byte_t *data, lh_u32_t size)
{
    if (size > 0U)
    {
        lh_array_push_back_of(&self->bytes, data, size);
    }
}

void
cnor_byte_writer_put_u8(cnor_byte_writer_t *self, lh_u8_t value)
{
    cnor_byte_writer_put_bytes(self, &value, 1U);
}

void
cnor_byte_writer_put_u16(cnor_byte_writer_t *self, lh_u16_t value)
{
    lh_byte_t packed[2];

    lh_bit_pack_le16(value, packed);
    cnor_byte_writer_put_bytes(self, packed, 2U);
}

void
cnor_byte_writer_put_u32(cnor_byte_writer_t *self, lh_u32_t value)
{
    lh_byte_t packed[4];

    lh_bit_pack_le32(value, packed);
    cnor_byte_writer_put_bytes(self, packed, 4U);
}

void
cnor_byte_writer_align(cnor_byte_writer_t *self, lh_u32_t alignment)
{
    while (cnor_byte_writer_get_size(self) % alignment != 0U)
    {
        cnor_byte_writer_put_u8(self, 0U);
    }
}

void
cnor_byte_writer_patch_u32(cnor_byte_writer_t *self, lh_u32_t at, lh_u32_t value)
{
    if (at + 4U <= cnor_byte_writer_get_size(self))
    {
        lh_bit_pack_le32(value, cnor_byte_writer_get_data(self) + at);
    }
}

lh_byte_t *
cnor_byte_writer_release(cnor_byte_writer_t *self, lh_u32_t *size)
{
    const lh_u32_t length = cnor_byte_writer_get_size(self);
    lh_byte_t *block;

    *size = 0U;
    if (length == 0U)
    {
        return lh_null;
    }
    block = (lh_byte_t *) lh_runtime_allocator_alloc(length);
    if (block != lh_null)
    {
        lh_memory_copy(block, length, cnor_byte_writer_get_data(self), length);
        *size = length;
    }
    lh_array_clear(&self->bytes);
    return block;
}
