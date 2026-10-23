#include <cnor/compiler/internal.h>
#include <lh/null.h>

/*
 * Globals are initialized by their `let`, which runs in source order with the
 * rest of a file's top-level code, the files a file imports first. Global
 * indices follow that order, so "every global below `globals_ready` has been
 * initialized" holds at each point of the top-level code.
 *
 * - Top-level code that names a global at or above `globals_ready` fails
 *   right away: its `let` comes later (or is the one being compiled).
 * - A function cannot know when it runs, so its code only records the highest
 *   global it names and the functions it calls. A call from top-level code is
 *   checked once every function is compiled: the callee, and everything it
 *   may call, must need no global above what is ready at the call. This is a
 *   conservative rule: a global named on a path that never runs counts too.
 */

/* -- lifetime -------------------------------------------------------------- */

void
cnor_order_init(cnor_compiler_t *self)
{
    lh_array_init(&self->calls, sizeof(struct cnor_call_edge));
    lh_array_init(&self->early_calls, sizeof(struct cnor_early_call));
    self->top_code = lh_bool_false;
    self->globals_ready = 0U;
}

void
cnor_order_deinit(cnor_compiler_t *self)
{
    lh_array_deinit(&self->calls);
    lh_array_deinit(&self->early_calls);
}

/* -- recording ------------------------------------------------------------- */

lh_bool_t
cnor_order_use_global(cnor_compiler_t *self, const cnor_node_t *node, lh_u32_t index)
{
    struct cnor_function_decl *function;

    if (self->top_code)
    {
        if (index >= self->globals_ready)
        {
            return cnor_compile_fail_text(self, node->line, cnor_error_not_initialized,
                                          cnor_scope_global_at(&self->scope, index)->key.name);
        }
        return lh_bool_true;
    }
    function = cnor_scope_function_at(&self->scope, self->function);
    if (index + 1U > function->globals_needed)
    {
        function->globals_needed = index + 1U;
    }
    return lh_bool_true;
}

void
cnor_order_call(cnor_compiler_t *self, const cnor_node_t *node, cnor_function_index_t callee)
{
    struct cnor_early_call call;
    struct cnor_call_edge edge;

    if (self->top_code)
    {
        call.callee = callee;
        call.globals_ready = self->globals_ready;
        call.line = node->line;
        call.module = self->module;
        lh_array_push_back(&self->early_calls, &call);
    }
    else
    {
        edge.caller = self->function;
        edge.callee = callee;
        lh_array_push_back(&self->calls, &edge);
    }
}

/* -- checking -------------------------------------------------------------- */

/* Pass what each callee needs on to its callers until nothing changes. */
static void
cnor_order_propagate(cnor_compiler_t *self)
{
    const lh_usize_t count = lh_array_get_size(&self->calls);
    const struct cnor_call_edge *edges =
        (const struct cnor_call_edge *) lh_array_get_begin(&self->calls);
    lh_bool_t changed = lh_bool_true;

    while (changed)
    {
        lh_usize_t i;

        changed = lh_bool_false;
        for (i = 0U; i < count; ++i)
        {
            struct cnor_function_decl *caller =
                cnor_scope_function_at(&self->scope, edges[i].caller);
            const lh_u32_t needed =
                cnor_scope_function_at(&self->scope, edges[i].callee)->globals_needed;

            if (needed > caller->globals_needed)
            {
                caller->globals_needed = needed;
                changed = lh_bool_true;
            }
        }
    }
}

lh_bool_t
cnor_order_check(cnor_compiler_t *self)
{
    const lh_usize_t count = lh_array_get_size(&self->early_calls);
    const struct cnor_early_call *calls =
        (const struct cnor_early_call *) lh_array_get_begin(&self->early_calls);
    lh_usize_t i;

    cnor_order_propagate(self);
    for (i = 0U; i < count; ++i)
    {
        const lh_u32_t needed =
            cnor_scope_function_at(&self->scope, calls[i].callee)->globals_needed;

        if (needed > calls[i].globals_ready)
        {
            const struct cnor_global_decl *global =
                cnor_scope_global_at(&self->scope, needed - 1U);

            self->module = calls[i].module;
            return cnor_compile_fail_text(self, calls[i].line, cnor_error_not_initialized,
                                          global->key.name);
        }
    }
    return lh_bool_true;
}
