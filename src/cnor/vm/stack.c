#include <cnor/vm/stack.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>

void
cnor_vm_stack_init(cnor_vm_stack_t *self)
{
    self->values = lh_null;
    self->capacity = 0U;
    self->sp = 0U;
}

lh_bool_t
cnor_vm_stack_reserve(cnor_vm_stack_t *self, cnor_stack_slots_t capacity)
{
    self->values = (lh_value_t *) lh_runtime_allocator_alloc(capacity * sizeof(lh_value_t));
    self->capacity = self->values != lh_null ? capacity : 0U;
    self->sp = 0U;
    return self->values != lh_null ? lh_bool_true : lh_bool_false;
}

void
cnor_vm_stack_deinit(cnor_vm_stack_t *self)
{
    if (self->values != lh_null)
    {
        lh_runtime_allocator_free(self->values);
    }
    cnor_vm_stack_init(self);
}

lh_bool_t
cnor_vm_stack_fits(const cnor_vm_stack_t *self, cnor_stack_slots_t count)
{
    return self->capacity - self->sp >= count ? lh_bool_true : lh_bool_false;
}

void
cnor_vm_stack_push(cnor_vm_stack_t *self, lh_value_t value)
{
    self->values[self->sp++] = value;
}

lh_value_t
cnor_vm_stack_pop(cnor_vm_stack_t *self)
{
    return self->values[--self->sp];
}

lh_value_t *
cnor_vm_stack_peek(cnor_vm_stack_t *self, cnor_stack_slots_t depth)
{
    return &self->values[self->sp - 1U - depth];
}

lh_value_t *
cnor_vm_stack_at(cnor_vm_stack_t *self, cnor_stack_slots_t index)
{
    return &self->values[index];
}

void
cnor_vm_stack_push_none(cnor_vm_stack_t *self, cnor_stack_slots_t count)
{
    while (count-- > 0U)
    {
        cnor_vm_stack_push(self, lh_value_make_none());
    }
}
