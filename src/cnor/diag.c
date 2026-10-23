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
