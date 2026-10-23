#include <cnor/vm/str_pool.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>

void
cnor_str_pool_init(cnor_str_pool_t *self)
{
    self->data = lh_null;
    self->size = 0U;
    self->used = 0U;
}

lh_bool_t
cnor_str_pool_reserve(cnor_str_pool_t *self, lh_u32_t size)
{
    cnor_str_pool_init(self);
    if (size == 0U)
    {
        return lh_bool_true;
    }
    self->data = (lh_char_t *) lh_runtime_allocator_alloc(size);
    self->size = self->data != lh_null ? size : 0U;
    return self->data != lh_null ? lh_bool_true : lh_bool_false;
}

void
cnor_str_pool_deinit(cnor_str_pool_t *self)
{
    if (self->data != lh_null)
    {
        lh_runtime_allocator_free(self->data);
    }
    cnor_str_pool_init(self);
}

void
cnor_str_pool_rewind(cnor_str_pool_t *self)
{
    self->used = 0U;
}

lh_char_t *
cnor_str_pool_take(cnor_str_pool_t *self, lh_u32_t size)
{
    lh_char_t *at;

    if (self->size - self->used < size || self->data == lh_null)
    {
        return lh_null;
    }
    at = self->data + self->used;
    self->used += size;
    return at;
}
