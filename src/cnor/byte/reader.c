#include <cnor/byte/reader.h>
#include <lh/bit/endian.h>
#include <lh/null.h>

/* Reserve @p size bytes; the address of the first, or ::lh_null. */
static const lh_byte_t *
cnor_byte_reader_take(cnor_byte_reader_t *self, lh_u32_t size)
{
    const lh_byte_t *at;

    if (!self->ok || self->size - self->pos < size)
    {
        self->ok = lh_bool_false;
        return lh_null;
    }
    at = self->data + self->pos;
    self->pos += size;
    return at;
}

void
cnor_byte_reader_init(cnor_byte_reader_t *self, const lh_byte_t *data, lh_u32_t size)
{
    self->data = data;
    self->size = data != lh_null ? size : 0U;
    self->pos = 0U;
    self->ok = lh_bool_true;
}

lh_u8_t
cnor_byte_reader_u8(cnor_byte_reader_t *self)
{
    const lh_byte_t *at = cnor_byte_reader_take(self, 1U);

    return at != lh_null ? at[0] : 0U;
}

lh_u16_t
cnor_byte_reader_u16(cnor_byte_reader_t *self)
{
    const lh_byte_t *at = cnor_byte_reader_take(self, 2U);

    return at != lh_null ? lh_bit_unpack_le16(at) : 0U;
}

lh_u32_t
cnor_byte_reader_u32(cnor_byte_reader_t *self)
{
    const lh_byte_t *at = cnor_byte_reader_take(self, 4U);

    return at != lh_null ? lh_bit_unpack_le32(at) : 0U;
}

lh_str_view_t
cnor_byte_reader_view(cnor_byte_reader_t *self, lh_u32_t size)
{
    lh_str_view_t view = lh_str_view_empty();
    const lh_byte_t *at = cnor_byte_reader_take(self, size);

    if (at != lh_null)
    {
        view.begin = at;
        view.size = size;
    }
    return view;
}

lh_bool_t
cnor_byte_reader_is_ok(const cnor_byte_reader_t *self)
{
    return self->ok;
}

lh_bool_t
cnor_byte_reader_at_end(const cnor_byte_reader_t *self)
{
    return (self->ok && self->pos == self->size) ? lh_bool_true : lh_bool_false;
}
