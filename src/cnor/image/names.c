#include <cnor/image/view.h>
#include <lh/bit/endian.h>

/* -- line table ------------------------------------------------------------ */

static const lh_byte_t *
cnor_image_line_entry(const cnor_image_view_t *self, cnor_count_t index)
{
    return self->data + self->header.sections[cnor_image_section_lines].offset + 4U + index * 8U;
}

cnor_line_t
cnor_image_view_line_at(const cnor_image_view_t *self, cnor_code_addr_t addr)
{
    /* Find the last entry whose address is <= addr. `low` and `high` bound a
     * half-open range, so nothing is ever decremented below zero. */
    cnor_count_t low = 0U;
    cnor_count_t high = self->line_count;

    if ((self->header.flags & CNOR_IMAGE_FLAG_LINES) == 0U || high == 0U)
    {
        return 0U;
    }
    while (high - low > 1U)
    {
        const cnor_count_t middle = low + (high - low) / 2U;

        if (lh_bit_unpack_le32(cnor_image_line_entry(self, middle)) <= addr)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    if (lh_bit_unpack_le32(cnor_image_line_entry(self, low)) > addr)
    {
        return 0U;
    }
    return lh_bit_unpack_le32(cnor_image_line_entry(self, low) + 4);
}

/* -- file name ------------------------------------------------------------- */

lh_str_view_t
cnor_image_view_file(const cnor_image_view_t *self)
{
    const cnor_image_range_t *range = &self->header.sections[cnor_image_section_file];
    lh_str_view_t view = lh_str_view_empty();

    if (range->size > 0U)
    {
        view.begin = self->data + range->offset;
        view.size = range->size;
    }
    return view;
}
