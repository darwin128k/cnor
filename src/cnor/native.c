#include <cnor/native.h>
#include <lh/null.h>
#include <lh/util/return.h>

const cnor_native_t *
cnor_native_table_find(const cnor_native_table_t *self, lh_str_view_t name)
{
    lh_usize_t i;

    lh_return_ifn(self, lh_null);
    for (i = 0U; i < self->count; ++i)
    {
        if (lh_str_view_equals(&self->entries[i].name, &name, lh_bool_false))
        {
            return &self->entries[i];
        }
    }
    return lh_null;
}

lh_bool_t
cnor_native_accepts(const cnor_native_t *self, cnor_argc_t argc)
{
    lh_return_ifn(self, lh_bool_false);
    return (argc >= self->arity_min && argc <= self->arity_max) ? lh_bool_true : lh_bool_false;
}
