#include <cnor/image/writer.h>
#include <lh/null.h>

/* Header field offsets, in the order of the table in image.h. */
#define CNOR_IMAGE_AT_SIZE 8U
#define CNOR_IMAGE_AT_SECTIONS 20U

/* -- sections -------------------------------------------------------------- */

/* Start a section on the alignment and remember where it began. */
static lh_u32_t
cnor_image_begin_section(cnor_byte_writer_t *out)
{
    cnor_byte_writer_align(out, CNOR_IMAGE_ALIGN);
    return cnor_byte_writer_get_size(out);
}

static void
cnor_image_end_section(cnor_byte_writer_t *out, cnor_image_section_t section, lh_u32_t begin)
{
    const lh_u32_t size = cnor_byte_writer_get_size(out) - begin;
    const lh_u32_t at = CNOR_IMAGE_AT_SECTIONS + (lh_u32_t) section * 8U;

    cnor_byte_writer_patch_u32(out, at, size > 0U ? begin : 0U);
    cnor_byte_writer_patch_u32(out, at + 4U, size);
}

/* A table: count then records; nothing at all when empty. */
static void
cnor_image_put_table(cnor_byte_writer_t *out, cnor_image_section_t section, cnor_count_t count,
                     const cnor_byte_writer_t *records)
{
    const lh_u32_t begin = cnor_image_begin_section(out);

    if (count > 0U)
    {
        cnor_byte_writer_put_u32(out, count);
        cnor_byte_writer_put_bytes(out, cnor_byte_writer_get_data(records),
                                   cnor_byte_writer_get_size(records));
    }
    cnor_image_end_section(out, section, begin);
}

static void
cnor_image_put_functions(cnor_byte_writer_t *out, const cnor_image_parts_t *parts)
{
    const lh_u32_t begin = cnor_image_begin_section(out);
    cnor_count_t i;

    cnor_byte_writer_put_u32(out, parts->function_count);
    for (i = 0U; i < parts->function_count; ++i)
    {
        const cnor_image_function_t *function = &parts->functions[i];

        cnor_byte_writer_put_u32(out, function->entry);
        cnor_byte_writer_put_u32(out, function->size);
        cnor_byte_writer_put_u8(out, function->param_count);
        cnor_byte_writer_put_u8(out, function->slot_count);
        cnor_byte_writer_put_u16(out, function->max_stack);
        cnor_byte_writer_put_u16(out, function->file);
    }
    cnor_image_end_section(out, cnor_image_section_functions, begin);
}

/* A table of names: count, then a u16 length and the bytes of each. */
static void
cnor_image_put_names(cnor_byte_writer_t *out, cnor_image_section_t section,
                     const lh_str_view_t *names, cnor_count_t count)
{
    const lh_u32_t begin = cnor_image_begin_section(out);
    cnor_count_t i;

    if (count > 0U)
    {
        cnor_byte_writer_put_u32(out, count);
    }
    for (i = 0U; i < count; ++i)
    {
        const lh_str_view_t *name = &names[i];

        cnor_byte_writer_put_u16(out, (lh_u16_t) name->size);
        cnor_byte_writer_put_bytes(out, (const lh_byte_t *) name->begin, (lh_u32_t) name->size);
    }
    cnor_image_end_section(out, section, begin);
}

static void
cnor_image_put_raw(cnor_byte_writer_t *out, cnor_image_section_t section, const lh_byte_t *data,
                   lh_u32_t size)
{
    const lh_u32_t begin = cnor_image_begin_section(out);

    cnor_byte_writer_put_bytes(out, data, size);
    cnor_image_end_section(out, section, begin);
}

/* -- header ---------------------------------------------------------------- */

static void
cnor_image_put_header(cnor_byte_writer_t *out, const cnor_image_parts_t *parts, lh_u16_t flags)
{
    lh_uint_t i;

    cnor_byte_writer_put_bytes(out, (const lh_byte_t *) "cnor", 4U);
    cnor_byte_writer_put_u16(out, CNOR_IMAGE_VERSION);
    cnor_byte_writer_put_u16(out, flags);
    cnor_byte_writer_put_u32(out, 0U); /* image_size, patched at the end */
    cnor_byte_writer_put_u32(out, parts->stack_slots);
    cnor_byte_writer_put_u32(out, parts->globals_count);
    for (i = 0U; i < 2U * (lh_uint_t) cnor_image_section_count; ++i)
    {
        cnor_byte_writer_put_u32(out, 0U);
    }
}

lh_bool_t
cnor_image_write(const cnor_image_parts_t *parts, cnor_image_t *out)
{
    const lh_u16_t flags = parts->line_count > 0U ? CNOR_IMAGE_FLAG_LINES : 0U;
    cnor_byte_writer_t writer;

    cnor_byte_writer_init(&writer);
    cnor_image_put_header(&writer, parts, flags);
    cnor_image_put_raw(&writer, cnor_image_section_code, cnor_byte_writer_get_data(parts->code),
                       cnor_byte_writer_get_size(parts->code));
    cnor_image_put_table(&writer, cnor_image_section_consts, parts->const_count, parts->consts);
    cnor_image_put_functions(&writer, parts);
    cnor_image_put_names(&writer, cnor_image_section_natives, parts->natives, parts->native_count);
    if (parts->lines != lh_null)
    {
        cnor_image_put_table(&writer, cnor_image_section_lines, parts->line_count, parts->lines);
    }
    cnor_image_put_names(&writer, cnor_image_section_files, parts->files, parts->file_count);
    cnor_byte_writer_patch_u32(&writer, CNOR_IMAGE_AT_SIZE, cnor_byte_writer_get_size(&writer));
    out->data = cnor_byte_writer_release(&writer, &out->size);
    cnor_byte_writer_deinit(&writer);
    return out->data != lh_null ? lh_bool_true : lh_bool_false;
}
