#include <cnor/byte/reader.h>
#include <cnor/image.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>
#include <lh/util/return.h>

static const lh_byte_t m_magic[4] = {'c', 'n', 'o', 'r'};

/* -- owning buffer --------------------------------------------------------- */

void
cnor_image_init(cnor_image_t *self)
{
    lh_return_ifn(self);
    self->data = lh_null;
    self->size = 0U;
}

void
cnor_image_deinit(cnor_image_t *self)
{
    lh_return_ifn(self);
    if (self->data != lh_null)
    {
        lh_runtime_allocator_free(self->data);
    }
    cnor_image_init(self);
}

/* -- header ---------------------------------------------------------------- */

static lh_bool_t
cnor_image_magic_matches(cnor_byte_reader_t *reader)
{
    lh_uint_t i;

    for (i = 0U; i < 4U; ++i)
    {
        if (cnor_byte_reader_u8(reader) != m_magic[i])
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

static lh_bool_t
cnor_image_range_is_valid(const cnor_image_range_t *range, lh_u32_t image_size)
{
    if (range->size == 0U)
    {
        /* Even an empty section forms data + offset in the view/reader. */
        return range->offset <= image_size ? lh_bool_true : lh_bool_false;
    }
    return (range->offset >= CNOR_IMAGE_HEADER_SIZE && range->offset % CNOR_IMAGE_ALIGN == 0U
            && range->offset <= image_size && image_size - range->offset >= range->size)
               ? lh_bool_true
               : lh_bool_false;
}

cnor_error_t
cnor_image_header_read(const lh_byte_t *data, lh_u32_t size, cnor_image_header_t *out)
{
    cnor_byte_reader_t reader;
    cnor_image_header_t header;
    lh_uint_t i;

    if (data == lh_null || size < CNOR_IMAGE_HEADER_SIZE)
    {
        return cnor_error_too_small;
    }
    cnor_byte_reader_init(&reader, data, size);
    if (!cnor_image_magic_matches(&reader))
    {
        return cnor_error_bad_magic;
    }
    header.version = cnor_byte_reader_u16(&reader);
    if (header.version != CNOR_IMAGE_VERSION)
    {
        return cnor_error_bad_version;
    }
    header.flags = cnor_byte_reader_u16(&reader);
    header.image_size = cnor_byte_reader_u32(&reader);
    header.stack_slots = cnor_byte_reader_u32(&reader);
    header.globals_count = cnor_byte_reader_u32(&reader);
    for (i = 0U; i < (lh_uint_t) cnor_image_section_count; ++i)
    {
        header.sections[i].offset = cnor_byte_reader_u32(&reader);
        header.sections[i].size = cnor_byte_reader_u32(&reader);
    }
    if (header.image_size != size || header.globals_count > CNOR_INDEX_MAX + 1U)
    {
        return cnor_error_bad_header;
    }
    for (i = 0U; i < (lh_uint_t) cnor_image_section_count; ++i)
    {
        if (!cnor_image_range_is_valid(&header.sections[i], size))
        {
            return cnor_error_bad_header;
        }
    }
    if (out != lh_null)
    {
        *out = header;
    }
    return cnor_error_none;
}
