#include <cnor/compiler/internal.h>

void
cnor_constant_pool_init(cnor_constant_pool_t *self)
{
    lh_array_init(&self->entries, sizeof(struct cnor_constant));
    cnor_byte_writer_init(&self->records);
}

void
cnor_constant_pool_deinit(cnor_constant_pool_t *self)
{
    lh_array_deinit(&self->entries);
    cnor_byte_writer_deinit(&self->records);
}

cnor_count_t
cnor_constant_pool_count(const cnor_constant_pool_t *self)
{
    return (cnor_count_t) lh_array_get_size(&self->entries);
}

static lh_u16_t
cnor_constant_pool_find(const cnor_constant_pool_t *self, cnor_const_kind_t kind,
                        lh_str_view_t bytes)
{
    const cnor_count_t count = cnor_constant_pool_count(self);
    cnor_count_t i;

    for (i = 0U; i < count; ++i)
    {
        const struct cnor_constant *entry =
            (const struct cnor_constant *) lh_array_get_ptr(&self->entries, i);

        if (entry->kind == kind && lh_str_view_equals(&entry->bytes, &bytes, lh_bool_false))
        {
            return (lh_u16_t) i;
        }
    }
    return CNOR_INDEX_NONE;
}

lh_bool_t
cnor_constant_pool_intern(cnor_compiler_t *self, const cnor_node_t *node, cnor_const_kind_t kind,
                          cnor_const_index_t *index)
{
    cnor_constant_pool_t *pool = &self->constants;
    struct cnor_constant entry;
    lh_u16_t found = cnor_constant_pool_find(pool, kind, node->name);

    if (found != CNOR_INDEX_NONE)
    {
        *index = found;
        return lh_bool_true;
    }
    if (cnor_constant_pool_count(pool) > CNOR_INDEX_MAX || node->name.size > 0xFFFFU)
    {
        return cnor_compile_fail(self, node, cnor_error_too_many_constants);
    }
    entry.kind = kind;
    entry.bytes = node->name;
    *index = (cnor_const_index_t) lh_array_push_back(&pool->entries, &entry);
    cnor_byte_writer_put_u8(&pool->records, (lh_u8_t) kind);
    cnor_byte_writer_put_u16(&pool->records, (lh_u16_t) node->name.size);
    cnor_byte_writer_put_bytes(&pool->records, (const lh_byte_t *) node->name.begin,
                               (lh_u32_t) node->name.size);
    return lh_bool_true;
}
