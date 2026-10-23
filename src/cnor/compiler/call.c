#include <cnor/compiler/internal.h>
#include <lh/null.h>

/* -- arguments ------------------------------------------------------------- */

static lh_bool_t
cnor_compile_args(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *arg;

    for (arg = node->data.call.args; arg != lh_null; arg = arg->next)
    {
        if (!cnor_compile_expr(self, arg))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* -- callees --------------------------------------------------------------- */

static lh_bool_t
cnor_compile_call_function(cnor_compiler_t *self, const cnor_node_t *node, lh_u16_t index)
{
    const struct cnor_function_decl *callee = cnor_scope_function_at(&self->scope, index);

    if (callee->image.param_count != node->data.call.argc)
    {
        return cnor_compile_fail_name(self, node, cnor_error_wrong_arg_count);
    }
    return cnor_compile_args(self, node)
           && cnor_emit_insn(self, cnor_op_call, index, node->data.call.argc);
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
    if (!cnor_compile_args(self, node))
    {
        return lh_bool_false;
    }
    return cnor_emit_insn(self, cnor_op_native, cnor_scope_import_native(self, index), argc);
}

/* `len(s)` is built in unless the script or the host names something `len`. */
static lh_bool_t
cnor_compile_call_builtin(cnor_compiler_t *self, const cnor_node_t *node)
{
    const lh_str_view_t len = lh_str_view_lit("len");

    if (!lh_str_view_equals(&node->name, &len, lh_bool_false))
    {
        return cnor_compile_fail_name(self, node, cnor_error_no_such_native);
    }
    if (node->data.call.argc != 1U)
    {
        return cnor_compile_fail_name(self, node, cnor_error_wrong_arg_count);
    }
    return cnor_compile_args(self, node) && cnor_emit_op(self, cnor_op_len);
}

lh_bool_t
cnor_compile_call(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_binding_t binding = cnor_scope_resolve(self, node->name);

    switch (binding.kind)
    {
    case cnor_binding_function:
        return cnor_compile_call_function(self, node, binding.index);
    case cnor_binding_native:
        return cnor_compile_call_native(self, node, binding.index);
    case cnor_binding_none:
        return cnor_compile_call_builtin(self, node);
    default:
        /* A variable: there are no function values to call. */
        return cnor_compile_fail_name(self, node, cnor_error_unknown_name);
    }
}
