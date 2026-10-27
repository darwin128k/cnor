#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- arguments ------------------------------------------------------------- */

/* Compile the arguments; with @p params (a declaration's parameter list),
 * each must have its parameter's type. */
static lh_bool_t
cnor_compile_args(cnor_compiler_t *self, const cnor_node_t *node, const cnor_node_t *params)
{
    const cnor_node_t *arg;

    for (arg = node->data.call.args; arg != lh_null; arg = arg->next)
    {
        if (!cnor_compile_expr(self, arg))
        {
            return lh_bool_false;
        }
        if (params != lh_null)
        {
            if (!cnor_type_require(self, arg,
                                   cnor_type_of_node(self, params, params->data.param.type), self->type))
            {
                return lh_bool_false;
            }
            params = params->next;
        }
    }
    return lh_bool_true;
}

/* The type a declaration's `->` names; `any` without one. */
static cnor_static_type_t
cnor_compile_result_type(cnor_compiler_t *self, const cnor_node_t *declaration)
{
    return declaration != lh_null ? cnor_type_of_node(self, declaration, declaration->data.fn.returns)
                                  : cnor_static_any;
}

/* -- callees --------------------------------------------------------------- */

static lh_bool_t
cnor_compile_call_function(cnor_compiler_t *self, const cnor_node_t *node, lh_u16_t index)
{
    const struct cnor_function_decl *callee = cnor_scope_function_at(&self->scope, index);
    const cnor_node_t *declaration = callee->node;

    if (callee->image.param_count != node->data.call.argc)
    {
        return cnor_compile_fail_name(self, node, cnor_error_wrong_arg_count);
    }
    if (!cnor_compile_args(self, node, declaration->data.fn.params))
    {
        return lh_bool_false;
    }
    cnor_order_call(self, node, index);
    self->type = cnor_compile_result_type(self, declaration);
    return cnor_emit_insn(self, cnor_op_call, index, node->data.call.argc);
}

static lh_bool_t
cnor_compile_call_native(cnor_compiler_t *self, const cnor_node_t *node, lh_u16_t index)
{
    const struct cnor_native_decl *native = cnor_scope_native_at(&self->scope, index);
    const cnor_argc_t argc = node->data.call.argc;

    if (argc < native->arity_min || argc > native->arity_max)
    {
        return cnor_compile_fail_name(self, node, cnor_error_wrong_arg_count);
    }
    /* A `native fn` declaration states types; the host's table does not. */
    if (!cnor_compile_args(self, node,
                           native->node != lh_null ? native->node->data.fn.params : lh_null))
    {
        return lh_bool_false;
    }
    self->type = cnor_compile_result_type(self, native->node);
    return cnor_emit_insn(self, cnor_op_native, cnor_scope_import_native(self, index), argc);
}

/* A function of the language itself: its arguments are checked against the
 * builtin's signature. */
static lh_bool_t
cnor_compile_call_string_builtin(cnor_compiler_t *self, const cnor_node_t *node,
                                 cnor_builtin_t builtin)
{
    const cnor_static_type_t *const params = cnor_compiler_builtin_params(builtin);
    const lh_u8_t argc = cnor_builtin_argc(builtin);
    const cnor_node_t *arg;
    lh_u8_t index = 0U;

    if (node->data.call.argc != argc)
    {
        return cnor_compile_fail_name(self, node, cnor_error_wrong_arg_count);
    }
    for (arg = node->data.call.args; arg != lh_null; arg = arg->next, ++index)
    {
        if (!cnor_compile_expr(self, arg))
        {
            return lh_bool_false;
        }
        if (builtin == cnor_builtin_split_into && index == 2U
            && cnor_type_base(self->type) == cnor_static_array
            && cnor_type_element(self->type) == cnor_static_str)
        {
            continue;
        }
        if (!cnor_type_require(self, arg, params[index], self->type))
        {
            return lh_bool_false;
        }
    }
    self->type = cnor_compiler_builtin_result(builtin);
    return cnor_emit_insn(self, cnor_op_call_builtin, (lh_u32_t) builtin, argc);
}

/* `len(s)` and the string functions are built in unless the script or the host
 * names something of the same name. */
static lh_bool_t
cnor_compile_call_builtin(cnor_compiler_t *self, const cnor_node_t *node)
{
    const lh_str_view_t len = lh_str_view_lit("len");
    const lh_str_view_t dict = lh_str_view_lit("dict");
    cnor_builtin_t builtin;

    if (lh_str_view_equals(&node->name, &dict, lh_bool_false))
    {
        /* `dict(n)` is a declaration: it makes a dictionary, and only a `let`
         * may make one. */
        return cnor_compile_fail_name(self, node, cnor_error_not_a_declaration);
    }
    if (!lh_str_view_equals(&node->name, &len, lh_bool_false))
    {
        if (!cnor_compiler_builtin_find(node->name, &builtin))
        {
            return cnor_compile_fail_name(self, node, cnor_error_no_such_native);
        }
        return cnor_compile_call_string_builtin(self, node, builtin);
    }
    if (node->data.call.argc != 1U)
    {
        return cnor_compile_fail_name(self, node, cnor_error_wrong_arg_count);
    }
    if (!cnor_compile_args(self, node, lh_null))
    {
        return lh_bool_false;
    }
    /* The length of a text, of an array or of a dictionary. */
    if (self->type != cnor_static_str && cnor_type_base(self->type) != cnor_static_array
        && cnor_type_base(self->type) != cnor_static_dict
        && !cnor_type_require(self, node->data.call.args, cnor_static_str, self->type))
    {
        return lh_bool_false;
    }
    self->type = cnor_static_i32;
    return cnor_emit_op(self, cnor_op_len);
}

/*
 * `f(args)`, where `f` holds a function: the function goes on the stack, and
 * the arguments follow it. The signature of the function is not known here,
 * so the arguments are not checked.
 */
static lh_bool_t
cnor_compile_call_value(cnor_compiler_t *self, const cnor_node_t *node,
                         const cnor_binding_t *binding)
{
    const cnor_node_t *arg;

    /* The arguments first, and the function over them: the machine takes the
     * function off the stack and the arguments stay as its parameters. */
    for (arg = node->data.call.args; arg != lh_null; arg = arg->next)
    {
        if (!cnor_compile_expr(self, arg))
        {
            return lh_bool_false;
        }
    }
    if (binding->kind == cnor_binding_local)
    {
        if (!cnor_emit_insn(self, cnor_op_load_local, binding->index, 0U))
        {
            return lh_bool_false;
        }
    }
    else if (binding->kind == cnor_binding_global)
    {
        if (!cnor_order_use_global(self, node, binding->index)
            || !cnor_emit_insn(self, cnor_op_load_global, binding->index, 0U))
        {
            return lh_bool_false;
        }
    }
    else
    {
        return cnor_compile_fail_binding(self, node, binding);
    }
    self->type = cnor_static_any;
    return cnor_emit_insn(self, cnor_op_call_value, node->data.call.argc, 0U);
}

lh_bool_t
cnor_compile_call(cnor_compiler_t *self, const cnor_node_t *node)
{
    cnor_module_index_t qualified_module;
    lh_str_view_t qualified_name;
    if (cnor_namespace_module(self, node->name) != CNOR_INDEX_NONE)
        return cnor_compile_fail_name(self, node, cnor_error_unknown_name);
    if (cnor_named_find(self, node->name) != lh_null)
        return cnor_compile_fail(self, node, cnor_error_not_a_declaration);
    const cnor_binding_t binding = cnor_scope_resolve(self, node->name);

    switch (binding.kind)
    {
    case cnor_binding_function:
        return cnor_compile_call_function(self, node, binding.index);
    case cnor_binding_native:
        return cnor_compile_call_native(self, node, binding.index);
    case cnor_binding_none:
        if (cnor_namespace_split(self, node->name, &qualified_module, &qualified_name))
            return cnor_compile_fail_name(self, node, cnor_error_unknown_name);
        return cnor_compile_call_builtin(self, node);
    default:
        if (binding.type == cnor_static_function)
        {
            /* A value that holds a function: called through the value. */
            return cnor_compile_call_value(self, node, &binding);
        }
        return cnor_compile_fail_binding(self, node, &binding);
    }
}
