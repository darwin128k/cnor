#include <cnor/diag.h>
#include <lh/memory.h>
#include <lh/util/return.h>

void
cnor_diag_init(cnor_diag_t *self)
{
    lh_return_ifn(self);
    self->code = cnor_error_none;
    self->line = 0U;
    self->offset = 0U;
    self->name_size = 0U;
    self->file_size = 0U;
}

lh_bool_t
cnor_diag_set(cnor_diag_t *self, cnor_error_t code, cnor_line_t line, lh_u32_t offset)
{
    lh_return_ifn(self, lh_bool_false);
    if (self->code == cnor_error_none)
    {
        self->code = code;
        self->line = line;
        self->offset = offset;
    }
    return lh_bool_false;
}

/* Keep a copy: the tree or image the name points into rarely outlives the call. */
static void
cnor_diag_copy_name(cnor_diag_t *self, lh_str_view_t name)
{
    const lh_usize_t size = lh_str_view_get_size(&name);
    const lh_usize_t kept = size < CNOR_DIAG_NAME_CAPACITY ? size : CNOR_DIAG_NAME_CAPACITY;

    if (kept > 0U)
    {
        lh_memory_copy(self->name, CNOR_DIAG_NAME_CAPACITY, lh_str_view_get_data(&name), kept);
    }
    self->name_size = (lh_u8_t) kept;
}

lh_bool_t
cnor_diag_set_name(cnor_diag_t *self, cnor_error_t code, cnor_line_t line, lh_str_view_t name)
{
    lh_return_ifn(self, lh_bool_false);
    if (self->code == cnor_error_none)
    {
        self->code = code;
        self->line = line;
        self->offset = 0U;
        cnor_diag_copy_name(self, name);
    }
    return lh_bool_false;
}

/* UTF-8 continuation bytes are 10xxxxxx; a kept end must not start inside a
 * character. */
static lh_bool_t
cnor_diag_is_continuation(lh_char_t c)
{
    return ((lh_u8_t) c & 0xC0U) == 0x80U ? lh_bool_true : lh_bool_false;
}

void
cnor_diag_set_file(cnor_diag_t *self, lh_str_view_t file)
{
    const lh_char_t *data = lh_str_view_get_data(&file);
    lh_usize_t size = lh_str_view_get_size(&file);

    lh_return_ifn(self);
    if (self->code == cnor_error_none || self->file_size != 0U || size == 0U)
    {
        return;
    }
    while (size > CNOR_DIAG_FILE_CAPACITY
           || (size < lh_str_view_get_size(&file) && cnor_diag_is_continuation(*data)))
    {
        ++data;
        --size;
    }
    lh_memory_copy(self->file, CNOR_DIAG_FILE_CAPACITY, data, size);
    self->file_size = (lh_u8_t) size;
}

lh_bool_t
cnor_diag_is_ok(const cnor_diag_t *self)
{
    lh_return_ifn(self, lh_bool_true);
    return self->code == cnor_error_none ? lh_bool_true : lh_bool_false;
}

cnor_error_t
cnor_diag_get_code(const cnor_diag_t *self)
{
    lh_return_ifn(self, cnor_error_none);
    return self->code;
}

cnor_line_t
cnor_diag_get_line(const cnor_diag_t *self)
{
    lh_return_ifn(self, 0U);
    return self->line;
}

lh_u32_t
cnor_diag_get_offset(const cnor_diag_t *self)
{
    lh_return_ifn(self, 0U);
    return self->offset;
}

lh_str_view_t
cnor_diag_get_name(const cnor_diag_t *self)
{
    lh_str_view_t name;

    lh_return_ifn(self, lh_str_view_empty());
    if (self->name_size == 0U)
    {
        return lh_str_view_empty();
    }
    lh_str_init_by_size(&name, self->name, self->name_size);
    return name;
}

lh_str_view_t
cnor_diag_get_file(const cnor_diag_t *self)
{
    lh_str_view_t file;

    lh_return_ifn(self, lh_str_view_empty());
    if (self->file_size == 0U)
    {
        return lh_str_view_empty();
    }
    lh_str_init_by_size(&file, self->file, self->file_size);
    return file;
}
