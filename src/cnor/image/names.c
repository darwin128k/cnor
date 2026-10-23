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

/* -- files ----------------------------------------------------------------- */

lh_str_view_t
cnor_image_view_file(const cnor_image_view_t *self, cnor_file_index_t index)
{
    lh_str_view_t name = lh_str_view_empty();
    cnor_byte_reader_t reader;
    cnor_count_t i;

    if (index >= self->file_count)
    {
        return lh_str_view_empty();
    }
    /* Checked at load; only a failure report walks the table. */
    cnor_image_view_records(self, cnor_image_section_files, &reader);
    for (i = 0U; i <= index; ++i)
    {
        (void) cnor_image_file_read(&reader, &name);
    }
    return name;
}

cnor_function_index_t
cnor_image_view_function_of(const cnor_image_view_t *self, cnor_code_addr_t addr)
{
    /* The last function whose entry is <= addr, as for the line table. */
    cnor_count_t low = 0U;
    cnor_count_t high = self->function_count;

    while (high - low > 1U)
    {
        const cnor_count_t middle = low + (high - low) / 2U;

        if (cnor_image_view_function_at(self, (cnor_function_index_t) middle).entry <= addr)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return (cnor_function_index_t) low;
}
