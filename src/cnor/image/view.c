#include <cnor/image/view.h>
#include <lh/bit/endian.h>

/* -- records --------------------------------------------------------------- */

lh_bool_t
cnor_image_const_read(cnor_byte_reader_t *reader, cnor_const_kind_t *kind, lh_str_view_t *bytes)
{
    const lh_u8_t raw_kind = cnor_byte_reader_u8(reader);
    const lh_u16_t length = cnor_byte_reader_u16(reader);

    *bytes = cnor_byte_reader_view(reader, length);
    *kind = (cnor_const_kind_t) raw_kind;
    return (cnor_byte_reader_is_ok(reader) && raw_kind < (lh_u8_t) cnor_const_kind_count)
               ? lh_bool_true
               : lh_bool_false;
}

lh_bool_t
cnor_image_native_read(cnor_byte_reader_t *reader, lh_str_view_t *name)
{
    const lh_u16_t length = cnor_byte_reader_u16(reader);

    *name = cnor_byte_reader_view(reader, length);
    return (cnor_byte_reader_is_ok(reader) && length > 0U) ? lh_bool_true : lh_bool_false;
}

lh_bool_t
cnor_image_file_read(cnor_byte_reader_t *reader, lh_str_view_t *name)
{
    const lh_u16_t length = cnor_byte_reader_u16(reader);

    *name = cnor_byte_reader_view(reader, length);
    return cnor_byte_reader_is_ok(reader);
}

static const lh_byte_t *
cnor_image_view_section(const cnor_image_view_t *self, cnor_image_section_t section)
{
    return self->data + self->header.sections[section].offset;
}

void
cnor_image_view_records(const cnor_image_view_t *self, cnor_image_section_t section,
                        cnor_byte_reader_t *out)
{
    const cnor_image_range_t *range = &self->header.sections[section];

    cnor_byte_reader_init(out, self->data + range->offset, range->size);
    if (range->size > 0U)
    {
        (void) cnor_byte_reader_u32(out);
    }
}

cnor_image_function_t
cnor_image_view_function_at(const cnor_image_view_t *self, cnor_function_index_t index)
{
    const lh_byte_t *at = cnor_image_view_section(self, cnor_image_section_functions) + 4U
                          + (lh_u32_t) index * CNOR_IMAGE_FUNCTION_SIZE;
    cnor_image_function_t function;

    function.entry = lh_bit_unpack_le32(at);
    function.size = lh_bit_unpack_le32(at + 4);
    function.param_count = at[8];
    function.slot_count = at[9];
    function.max_stack = lh_bit_unpack_le16(at + 10);
    function.file = lh_bit_unpack_le16(at + 12);
    return function;
}

/* -- section checks -------------------------------------------------------- */

/* Count of a table section; 0 for an absent one, which needs no count word. */
static lh_bool_t
cnor_image_view_count(const cnor_image_view_t *self, cnor_image_section_t section,
                      cnor_count_t *count)
{
    cnor_byte_reader_t reader;

    cnor_byte_reader_init(&reader, cnor_image_view_section(self, section),
                          self->header.sections[section].size);
    *count = self->header.sections[section].size > 0U ? cnor_byte_reader_u32(&reader) : 0U;
    return cnor_byte_reader_is_ok(&reader);
}

static lh_bool_t
cnor_image_view_check_fixed(const cnor_image_view_t *self, cnor_image_section_t section,
                            cnor_count_t count, lh_u32_t record_size)
{
    const lh_u32_t size = self->header.sections[section].size;

    if (size == 0U)
    {
        return count == 0U ? lh_bool_true : lh_bool_false;
    }
    return ((size - 4U) / record_size == count && (size - 4U) % record_size == 0U)
               ? lh_bool_true
               : lh_bool_false;
}

static lh_bool_t
cnor_image_view_check_consts(const cnor_image_view_t *self)
{
    cnor_byte_reader_t reader;
    cnor_count_t i;

    cnor_image_view_records(self, cnor_image_section_consts, &reader);
    for (i = 0U; i < self->const_count; ++i)
    {
        cnor_const_kind_t kind;
        lh_str_view_t bytes;

        if (!cnor_image_const_read(&reader, &kind, &bytes))
        {
            return lh_bool_false;
        }
    }
    return self->const_count == 0U || cnor_byte_reader_at_end(&reader);
}

/** @brief Reads one record of a table of names. */
typedef lh_bool_t (*cnor_image_name_read_fn_t)(cnor_byte_reader_t *reader, lh_str_view_t *name);

/* A table of names, read with @p read. */
static lh_bool_t
cnor_image_view_check_names(const cnor_image_view_t *self, cnor_image_section_t section,
                            cnor_count_t count, cnor_image_name_read_fn_t read)
{
    cnor_byte_reader_t reader;
    cnor_count_t i;

    cnor_image_view_records(self, section, &reader);
    for (i = 0U; i < count; ++i)
    {
        lh_str_view_t name;

        if (!read(&reader, &name))
        {
            return lh_bool_false;
        }
    }
    return count == 0U || cnor_byte_reader_at_end(&reader);
}

static lh_bool_t
cnor_image_view_check_counts(cnor_image_view_t *self)
{
    return (cnor_image_view_count(self, cnor_image_section_consts, &self->const_count)
            && cnor_image_view_count(self, cnor_image_section_functions, &self->function_count)
            && cnor_image_view_count(self, cnor_image_section_natives, &self->native_count)
            && cnor_image_view_count(self, cnor_image_section_lines, &self->line_count)
            && cnor_image_view_count(self, cnor_image_section_files, &self->file_count)
            && self->const_count <= CNOR_INDEX_MAX + 1U
            && self->function_count >= 1U && self->function_count <= CNOR_INDEX_MAX + 1U
            && self->native_count <= CNOR_INDEX_MAX + 1U
            && self->file_count <= CNOR_INDEX_MAX + 1U)
               ? lh_bool_true
               : lh_bool_false;
}

cnor_error_t
cnor_image_view_open(cnor_image_view_t *self, const lh_byte_t *data, lh_u32_t size)
{
    const cnor_error_t error = cnor_image_header_read(data, size, &self->header);

    if (error != cnor_error_none)
    {
        return error;
    }
    self->data = data;
    self->code = cnor_image_view_section(self, cnor_image_section_code);
    self->code_size = self->header.sections[cnor_image_section_code].size;
    if (self->code_size == 0U || !cnor_image_view_check_counts(self))
    {
        return cnor_error_bad_section;
    }
    if (!cnor_image_view_check_fixed(self, cnor_image_section_functions, self->function_count,
                                     CNOR_IMAGE_FUNCTION_SIZE)
        || !cnor_image_view_check_fixed(self, cnor_image_section_lines, self->line_count, 8U)
        || !cnor_image_view_check_consts(self)
        || !cnor_image_view_check_names(self, cnor_image_section_natives, self->native_count,
                                        cnor_image_native_read)
        || !cnor_image_view_check_names(self, cnor_image_section_files, self->file_count,
                                        cnor_image_file_read))
    {
        return cnor_error_bad_section;
    }
    return cnor_error_none;
}
