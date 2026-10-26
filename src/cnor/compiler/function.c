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
    {
        const cnor_node_t *const init = node->data.let.init;
        lh_u32_t room = 0U;
        lh_u16_t count;

        if (init != lh_null && init->kind == cnor_node_array)
        {
            /* An array takes a slot for itself and a slot per element. */
            count = (lh_u16_t) (init->data.array.count + 1U);
        }
        else if (cnor_compiler_dict_form(init, &room) == cnor_dict_declared
                 && room <= CNOR_DICT_ROOM_MAX)
        {
            /* A dictionary takes a slot for itself, two for its header and
             * two per pair. */
            count = (lh_u16_t) (room * 2U + 3U);
        }
        else
        {
            count = 1U;
        }
        return cnor_scope_add_global(self, node, count);
    }
    default:
        return lh_bool_true;
    }
}

/* One file: the function holding its top-level code (the main file's is
 * function 0, added first), then its functions, natives and globals. */
static lh_bool_t
cnor_compile_declare_module(cnor_compiler_t *self, cnor_module_index_t index)
{
    struct cnor_module *module = cnor_module_at(self, index);
    const cnor_node_t *node;

    self->module = index;
    if (index != 0U)
    {
        module->function = (cnor_function_index_t) cnor_scope_function_count(&self->scope);
        if (!cnor_scope_add_module_code(self, index))
        {
            return lh_bool_false;
        }
    }
    module->first_global = cnor_scope_global_count(&self->scope);
    for (node = module->program; node != lh_null; node = node->next)
    {
        if (!cnor_compile_declaration(self, node))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

lh_bool_t
cnor_compile_declare(cnor_compiler_t *self)
{
    const lh_usize_t count = lh_array_get_size(&self->order);
    lh_usize_t i;

    /* Function 0 is the entry point: the main file's top-level code. */
    if (!cnor_scope_add_module_code(self, 0U))
    {
        return lh_bool_false;
    }
    /* Imported files first, so globals are numbered in the order their
     * `let`s run; the main file, which `order` leaves out, last. */
    for (i = 0U; i < count; ++i)
    {
        if (!cnor_compile_declare_module(
                self, *(const cnor_module_index_t *) lh_array_get_ptr(&self->order, i)))
        {
            return lh_bool_false;
        }
    }
    return cnor_compile_declare_module(self, 0U);
}

/* -- returns --------------------------------------------------------------- */

static lh_bool_t
cnor_compile_list_returns(const cnor_node_t *body);

/* Whether @p body holds a `break` of the loop it is the body of. */
static lh_bool_t
cnor_compile_list_breaks(const cnor_node_t *body)
{
    const cnor_node_t *arm;

    for (; body != lh_null; body = body->next)
    {
        switch (body->kind)
        {
        case cnor_node_break:
            return lh_bool_true;
        case cnor_node_if:
            if (cnor_compile_list_breaks(body->data.if_.then_body)
                || cnor_compile_list_breaks(body->data.if_.else_body))
            {
                return lh_bool_true;
            }
            break;
        case cnor_node_match:
            for (arm = body->data.match.arms; arm != lh_null; arm = arm->next)
            {
                if (cnor_compile_list_breaks(arm->data.arm.body))
                {
                    return lh_bool_true;
                }
            }
            break;
        case cnor_node_try:
            if (cnor_compile_list_breaks(body->data.try_.body)
                || cnor_compile_list_breaks(body->data.try_.catch_body))
            {
                return lh_bool_true;
            }
            break;
        default:
            /* A `break` in an inner loop ends that loop. */
            break;
        }
    }
    return lh_bool_false;
}

/* A `match` returns when it has a `_` and every arm returns. */
static lh_bool_t
cnor_compile_match_returns(const cnor_node_t *node)
{
    const cnor_node_t *arm;
    lh_bool_t has_default = lh_bool_false;

    for (arm = node->data.match.arms; arm != lh_null; arm = arm->next)
    {
        if (!cnor_compile_list_returns(arm->data.arm.body))
        {
            return lh_bool_false;
        }
        has_default = has_default || arm->data.arm.pattern == lh_null;
    }
    return has_default;
}

/* Whether running @p node can never go on to the next statement. */
static lh_bool_t
cnor_compile_stmt_returns(const cnor_node_t *node)
{
    switch (node->kind)
    {
    case cnor_node_return:
        return lh_bool_true;
    case cnor_node_if:
        return (node->data.if_.else_body != lh_null
                && cnor_compile_list_returns(node->data.if_.then_body)
                && cnor_compile_list_returns(node->data.if_.else_body))
                   ? lh_bool_true
                   : lh_bool_false;
    case cnor_node_match:
        return cnor_compile_match_returns(node);
    case cnor_node_try:
        /* The try may fail and go to the catch: both must return. */
        return (cnor_compile_list_returns(node->data.try_.body)
                && cnor_compile_list_returns(node->data.try_.catch_body))
                   ? lh_bool_true
                   : lh_bool_false;
    case cnor_node_while:
        /* Only `while true` without a `break` never ends; any other loop may. */
        return (node->data.while_.cond->kind == cnor_node_true
                && !cnor_compile_list_breaks(node->data.while_.body))
                   ? lh_bool_true
                   : lh_bool_false;
    default:
        return lh_bool_false;
    }
}

/*
 * Whether running @p body can never reach its end. The rule is the one a
 * reader applies, not the emitter's reachability: some statement returns, an
 * `if` returns when both branches do, a `match` when it has a `_` and every
 * arm does. A `for` or a `while` may finish, unless it is `while true` with no
 * `break` of its own.
 */
static lh_bool_t
cnor_compile_list_returns(const cnor_node_t *body)
{
    for (; body != lh_null; body = body->next)
    {
        if (cnor_compile_stmt_returns(body))
        {
            return lh_bool_true;
        }
    }
    return lh_bool_false;
}

/* -- function frames ------------------------------------------------------- */

static void
cnor_compile_begin_function(cnor_compiler_t *self, cnor_function_index_t index)
{
    struct cnor_function_decl *function = cnor_scope_function_at(&self->scope, index);

    self->function = index;
    self->module = function->key.module;
    self->top_code = lh_bool_false;
    self->returns = cnor_static_any;
    self->block_depth = 0U;
    self->loop_depth = 0U;
    cnor_scope_begin_function(&self->scope);
    cnor_emit_begin_function(&self->emit);
    if (cnor_module_count(self) > 1U)
    {
        /* Functions are moved into index order when the image is written
         * (see compiler.c), so each needs a line record of its own. */
        self->emit.last_line = 0U;
    }
    function->image.entry = cnor_emit_get_addr(&self->emit);
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
        const cnor_static_type_t type = cnor_type_of_annotation(param->data.param.type);

        /* An array parameter is a reference to the elements, so it is mutable:
         * writing into it is what it is for. A dictionary parameter is a
         * reference to its pairs, and the same goes for it. The other
         * parameters hold a value, and writing to one changes nothing
         * outside. */
        if (!cnor_scope_add_local(self, param, param->name,
                                  (type == cnor_static_array || type == cnor_static_dict)
                                      ? lh_bool_true
                                      : lh_bool_false,
                                  type, 0U, &slot))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* -- top-level code -------------------------------------------------------- */

/* Start a file's top-level code: the globals of the files before it are ready. */
static void
cnor_compile_begin_top_code(cnor_compiler_t *self, cnor_function_index_t index)
{
    cnor_compile_begin_function(self, index);
    self->top_code = lh_bool_true;
    self->globals_ready = cnor_module_at(self, self->module)->first_global;
}

static lh_bool_t
cnor_compile_statements(cnor_compiler_t *self, const cnor_node_t *node)
{
    for (; node != lh_null; node = node->next)
    {
        if (!cnor_compile_stmt(self, node))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* The top-level code of every imported file, imported ones first, each once;
 * the lines are the main file's first `import`. */
static lh_bool_t
cnor_compile_module_calls(cnor_compiler_t *self)
{
    const lh_usize_t count = lh_array_get_size(&self->order);
    lh_usize_t i;

    self->emit.line = cnor_module_at(self, 0U)->program->line;
    for (i = 0U; i < count; ++i)
    {
        const cnor_module_index_t index =
            *(const cnor_module_index_t *) lh_array_get_ptr(&self->order, i);

        if (!cnor_emit_insn(self, cnor_op_call, cnor_module_at(self, index)->function, 0U)
            || !cnor_emit_op(self, cnor_op_pop))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* Function 0: the imported files' code, then the main file's top level. */
static lh_bool_t
cnor_compile_entry(cnor_compiler_t *self)
{
    const cnor_node_t *program = cnor_module_at(self, 0U)->program;

    cnor_compile_begin_top_code(self, 0U);
    if (!cnor_scope_add_local(self, program, lh_str_view_empty(), lh_bool_true, cnor_static_any, 0U,
                              &self->result_slot)
        || !cnor_compile_module_calls(self) || !cnor_compile_statements(self, program))
    {
        return lh_bool_false;
    }
    /* Falling off the end returns the last top-level expression's value. */
    return cnor_emit_insn(self, cnor_op_load_local, self->result_slot, 0U)
           && cnor_emit_op(self, cnor_op_ret) && cnor_compile_end_function(self, lh_null);
}

/* An imported file's top-level code, a function of its own run for its effects. */
static lh_bool_t
cnor_compile_module_code(cnor_compiler_t *self, cnor_function_index_t index)
{
    cnor_compile_begin_top_code(self, index);
    return cnor_compile_statements(self, cnor_module_at(self, self->module)->program)
           && cnor_emit_op(self, cnor_op_push_none) && cnor_emit_op(self, cnor_op_ret)
           && cnor_compile_end_function(self, lh_null);
}

/* -- bodies ---------------------------------------------------------------- */

static lh_bool_t
cnor_compile_function(cnor_compiler_t *self, cnor_function_index_t index)
{
    const cnor_node_t *node = cnor_scope_function_at(&self->scope, index)->node;

    cnor_compile_begin_function(self, index);
    self->returns = cnor_type_of_annotation(node->data.fn.returns);
    self->emit.line = node->line;
    if (!cnor_compile_params(self, node->data.fn.params)
        || !cnor_compile_block(self, node->data.fn.body))
    {
        return lh_bool_false;
    }
    /* Falling off the end of a function returns `none`, which a function with
     * a return type may not do. */
    if (node->data.fn.returns != cnor_type_none && !cnor_compile_list_returns(node->data.fn.body))
    {
        return cnor_compile_fail_name(self, node, cnor_error_missing_return);
    }
    return cnor_emit_op(self, cnor_op_push_none) && cnor_emit_op(self, cnor_op_ret)
           && cnor_compile_end_function(self, node);
}

static lh_bool_t
cnor_compile_body(cnor_compiler_t *self, cnor_function_index_t index)
{
    if (cnor_scope_function_at(&self->scope, index)->node != lh_null)
    {
        return cnor_compile_function(self, index);
    }
    return index == 0U ? cnor_compile_entry(self) : cnor_compile_module_code(self, index);
}

/*
 * Compile every function. A file's top-level code comes before its functions,
 * which see the types its `let`s give the globals, and imported files come
 * before the files importing them: so the main file's code (function 0) is
 * compiled after the imported files and before the main file's functions.
 * With a single file that is index order.
 */
lh_bool_t
cnor_compile_bodies(cnor_compiler_t *self)
{
    const cnor_count_t count = cnor_scope_function_count(&self->scope);
    cnor_count_t main_first = 1U;
    cnor_count_t i;

    while (main_first < count
           && cnor_scope_function_at(&self->scope, (cnor_function_index_t) main_first)->key.module
                  != 0U)
    {
        ++main_first;
    }
    for (i = 1U; i < main_first; ++i)
    {
        if (!cnor_compile_body(self, (cnor_function_index_t) i))
        {
            return lh_bool_false;
        }
    }
    if (!cnor_compile_body(self, 0U))
    {
        return lh_bool_false;
    }
    for (i = main_first; i < count; ++i)
    {
        if (!cnor_compile_body(self, (cnor_function_index_t) i))
        {
            return lh_bool_false;
        }
    }
    return cnor_order_check(self);
}
