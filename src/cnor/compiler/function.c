#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- declaration pass ------------------------------------------------------ */

lh_bool_t
cnor_compile_declaration(cnor_compiler_t *self, const cnor_node_t *node)
{
    switch (node->kind)
    {
    case cnor_node_fn:
        return cnor_scope_add_function(self, node);
    case cnor_node_native_fn:
        return cnor_scope_add_native(self, node);
    case cnor_node_let:
        return cnor_scope_add_global(self, node);
    default:
        return lh_bool_true;
    }
}

lh_bool_t
cnor_compile_declare(cnor_compiler_t *self, const cnor_node_t *program)
{
    /* Function 0 is the entry point: the top-level statements. */
    if (!cnor_scope_add_function(self, lh_null))
    {
        return lh_bool_false;
    }
    for (; program != lh_null; program = program->next)
    {
        if (!cnor_compile_declaration(self, program))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* -- function frames ------------------------------------------------------- */

static void
cnor_compile_begin_function(cnor_compiler_t *self, cnor_function_index_t index)
{
    self->function = index;
    self->block_depth = 0U;
    self->loop_depth = 0U;
    cnor_scope_begin_function(&self->scope);
    cnor_emit_begin_function(&self->emit);
    cnor_scope_function_at(&self->scope, index)->image.entry = cnor_emit_get_addr(&self->emit);
}

static lh_bool_t
cnor_compile_end_function(cnor_compiler_t *self, const cnor_node_t *node)
{
    cnor_image_function_t *image = &cnor_scope_function_at(&self->scope, self->function)->image;

    if (self->emit.max_depth > 0xFFFF)
    {
        return cnor_compile_fail(self, node, cnor_error_bad_stack);
    }
    image->size = cnor_emit_get_addr(&self->emit) - image->entry;
    image->slot_count = (lh_u8_t) self->scope.slot_count;
    image->max_stack = (lh_u16_t) self->emit.max_depth;
    return lh_bool_true;
}

static lh_bool_t
cnor_compile_params(cnor_compiler_t *self, const cnor_node_t *param)
{
    for (; param != lh_null; param = param->next)
    {
        cnor_slot_t slot;

        if (!cnor_scope_add_local(self, param, param->name, lh_bool_false, &slot))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* -- bodies ---------------------------------------------------------------- */

lh_bool_t
cnor_compile_entry(cnor_compiler_t *self, const cnor_node_t *program)
{
    cnor_compile_begin_function(self, 0U);
    if (!cnor_scope_add_local(self, program, lh_str_view_empty(), lh_bool_true,
                              &self->result_slot))
    {
        return lh_bool_false;
    }
    for (; program != lh_null; program = program->next)
    {
        if (!cnor_compile_stmt(self, program))
        {
            return lh_bool_false;
        }
    }
    /* Falling off the end returns the last top-level expression's value. */
    return cnor_emit_insn(self, cnor_op_load_local, self->result_slot, 0U)
           && cnor_emit_op(self, cnor_op_ret) && cnor_compile_end_function(self, lh_null);
}

static lh_bool_t
cnor_compile_function(cnor_compiler_t *self, cnor_function_index_t index)
{
    const cnor_node_t *node = cnor_scope_function_at(&self->scope, index)->node;

    cnor_compile_begin_function(self, index);
    self->emit.line = node->line;
    /* Falling off the end of a function returns `none`. */
    return cnor_compile_params(self, node->data.fn.params)
           && cnor_compile_block(self, node->data.fn.body)
           && cnor_emit_op(self, cnor_op_push_none) && cnor_emit_op(self, cnor_op_ret)
           && cnor_compile_end_function(self, node);
}

lh_bool_t
cnor_compile_functions(cnor_compiler_t *self)
{
    const cnor_count_t count = cnor_scope_function_count(&self->scope);
    cnor_count_t i;

    for (i = 1U; i < count; ++i)
    {
        if (!cnor_compile_function(self, (cnor_function_index_t) i))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}
