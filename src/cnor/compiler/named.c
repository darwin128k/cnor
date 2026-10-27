/**
 * @file named.c
 * @brief Nominal records and enumerations, erased to frame arrays and integers.
 * No runtime allocation or image-format change is needed for these types.
 */
#include <cnor/compiler/internal.h>
#include <cnor/parser.h>
#include <cnor/name.h>
#include <lh/null.h>

static lh_bool_t
cnor_named_same(lh_str_view_t a, lh_str_view_t b)
{
    return lh_str_view_equals(&a, &b, lh_bool_false);
}

const struct cnor_named_type *
cnor_named_of(cnor_compiler_t *self, cnor_static_type_t type)
{
    lh_usize_t index;
    if (type < cnor_static_type_count) return lh_null;
    index = (lh_usize_t) type - cnor_static_type_count;
    return index < lh_array_get_size(&self->named_types)
        ? (const struct cnor_named_type *) lh_array_get_ptr(&self->named_types, index) : lh_null;
}

lh_bool_t
cnor_named_is_struct(cnor_compiler_t *self, cnor_static_type_t type)
{
    const struct cnor_named_type *named = cnor_named_of(self, type);
    return named != lh_null && named->node->kind == cnor_node_struct;
}

static const struct cnor_named_type *
cnor_named_find_in(cnor_compiler_t *self, cnor_module_index_t module, lh_str_view_t name)
{
    lh_usize_t i;
    for (i = 0U; i < lh_array_get_size(&self->named_types); ++i)
    {
        const struct cnor_named_type *t = lh_array_get_ptr(&self->named_types, i);
        if (t->module == module && cnor_named_same(t->node->name, name)) return t;
    }
    return lh_null;
}

const struct cnor_named_type *
cnor_named_find(cnor_compiler_t *self, lh_str_view_t name)
{
    cnor_module_index_t qualified_module;
    lh_str_view_t qualified_name;
    const struct cnor_named_type *found;
    if (cnor_namespace_split(self, name, &qualified_module, &qualified_name))
        return qualified_module == CNOR_INDEX_NONE ? lh_null
            : cnor_named_find_in(self, qualified_module, qualified_name);
    found = cnor_named_find_in(self, self->module, name);
    const struct cnor_module *module = cnor_module_at(self, self->module);
    lh_usize_t i;
    if (found != lh_null) return found;
    for (i = 0U; i < lh_array_get_size(&module->imports); ++i)
    {
        const struct cnor_module_import *edge = lh_array_get_ptr(&module->imports, i);
        const struct cnor_named_type *t;
        if (edge->alias.size != 0U) continue;
        t = cnor_named_find_in(self, edge->module, name);
        if (t == lh_null) continue;
        if (found != lh_null && found != t)
        {
            cnor_compile_fail_text(self, self->emit.line, cnor_error_ambiguous_name, name);
            return lh_null;
        }
        found = t;
    }
    return found;
}

cnor_static_type_t
cnor_type_of_node(cnor_compiler_t *self, const cnor_node_t *node, cnor_type_t annotation)
{
    lh_usize_t i;
    if (annotation != cnor_type_named && node->element_type == cnor_type_none)
        return cnor_type_of_annotation(annotation);
    for (i = 0U; i < lh_array_get_size(&self->annotations); ++i)
    {
        const struct cnor_annotation *a = lh_array_get_ptr(&self->annotations, i);
        if (a->node == node) return a->type;
    }
    return cnor_static_any; /* Preparation rejects every unresolved annotation. */
}

static lh_bool_t
cnor_named_resolve_annotation(cnor_compiler_t *self, const cnor_node_t *node, cnor_type_t type)
{
    struct cnor_annotation annotation;
    const struct cnor_named_type *named;
    if (type != cnor_type_named && node->element_type == cnor_type_none) return lh_bool_true;
    self->emit.line = node->line;
    annotation.node = node;
    annotation.type = cnor_type_of_annotation(type);
    if (type == cnor_type_named)
    {
        named = cnor_named_find(self, node->type_name);
        if (named == lh_null)
            return cnor_compile_fail_text(self, node->line, cnor_error_unknown_type, node->type_name);
        annotation.type = named->type;
    }
    if (node->element_type != cnor_type_none)
    {
        cnor_static_type_t element = cnor_type_of_annotation(node->element_type);
        if (node->element_type == cnor_type_named)
        {
            named = cnor_named_find(self, node->element_type_name);
            if (named == lh_null)
                return cnor_compile_fail_text(self, node->line, cnor_error_unknown_type, node->element_type_name);
            element = named->type;
        }
        if (element == cnor_static_array || element == cnor_static_dict
            || cnor_named_is_struct(self, element))
            return cnor_compile_fail(self, node, cnor_error_type_mismatch);
        annotation.type = cnor_type_collection(annotation.type, element);
    }
    lh_array_push_back(&self->annotations, &annotation);
    return lh_bool_true;
}

/* Resolve annotations without changing the caller-owned syntax tree. */
static lh_bool_t
cnor_named_resolve_list(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *arm;
    for (; node != lh_null; node = node->next)
    {
        switch (node->kind)
        {
        case cnor_node_fn:
        case cnor_node_native_fn:
            if (!cnor_named_resolve_annotation(self, node, node->data.fn.returns)
                || !cnor_named_resolve_list(self, node->data.fn.params)
                || !cnor_named_resolve_list(self, node->data.fn.body)) return lh_bool_false;
            break;
        case cnor_node_struct:
            if (!cnor_named_resolve_list(self, node->data.fn.params)) return lh_bool_false;
            break;
        case cnor_node_param:
            if (!cnor_named_resolve_annotation(self, node, node->data.param.type)) return lh_bool_false;
            break;
        case cnor_node_let:
            if (!cnor_named_resolve_annotation(self, node, node->data.let.type)) return lh_bool_false;
            break;
        case cnor_node_if:
            if (!cnor_named_resolve_list(self, node->data.if_.then_body)
                || !cnor_named_resolve_list(self, node->data.if_.else_body)) return lh_bool_false;
            break;
        case cnor_node_while:
            if (!cnor_named_resolve_list(self, node->data.while_.body)) return lh_bool_false;
            break;
        case cnor_node_for:
            if (!cnor_named_resolve_list(self, node->data.for_.body)) return lh_bool_false;
            break;
        case cnor_node_match:
            for (arm = node->data.match.arms; arm != lh_null; arm = arm->next)
                if (!cnor_named_resolve_list(self, arm->data.arm.body)) return lh_bool_false;
            break;
        case cnor_node_try:
            if (!cnor_named_resolve_list(self, node->data.try_.body)
                || !cnor_named_resolve_list(self, node->data.try_.catch_body)
                || !cnor_named_resolve_list(self, node->data.try_.finally_body)) return lh_bool_false;
            break;
        default: break;
        }
    }
    return lh_bool_true;
}

lh_bool_t
cnor_named_prepare(cnor_compiler_t *self)
{
    lh_usize_t i;
    for (i = 0U; i < lh_array_get_size(&self->modules); ++i)
    {
        const cnor_node_t *node;
        self->module = (cnor_module_index_t) i;
        for (node = cnor_module_at(self, self->module)->program; node != lh_null; node = node->next)
        {
            const cnor_node_t *other;
            const cnor_node_t *field;
            struct cnor_named_type named;
            if (node->kind != cnor_node_struct && node->kind != cnor_node_enum) continue;
            if (cnor_parser_type_of(node->name) != cnor_type_none)
                return cnor_compile_fail_name(self, node, cnor_error_duplicate_name);
            for (other = cnor_module_at(self, self->module)->program; other != lh_null; other = other->next)
                if (other != node && cnor_named_same(other->name, node->name)
                    && (other->kind == cnor_node_fn || other->kind == cnor_node_native_fn
                        || other->kind == cnor_node_let || other->kind == cnor_node_struct
                        || other->kind == cnor_node_enum))
                    return cnor_compile_fail_name(self, node, cnor_error_duplicate_name);
            for (field = node->data.fn.params; field != lh_null; field = field->next)
                for (other = field->next; other != lh_null; other = other->next)
                    if (cnor_named_same(field->name, other->name))
                        return cnor_compile_fail_name(self, other, cnor_error_duplicate_name);
            if (lh_array_get_size(&self->named_types) >= CNOR_INDEX_MAX)
                return cnor_compile_fail(self, node, cnor_error_too_many_constants);
            named.node = node;
            named.module = self->module;
            named.type = (cnor_static_type_t) (cnor_static_type_count + lh_array_get_size(&self->named_types));
            lh_array_push_back(&self->named_types, &named);
        }
    }
    for (i = 0U; i < lh_array_get_size(&self->modules); ++i)
    {
        self->module = (cnor_module_index_t) i;
        if (!cnor_named_resolve_list(self, cnor_module_at(self, self->module)->program)) return lh_bool_false;
    }
    /* Scalar fields keep records within the existing deterministic frame model. */
    for (i = 0U; i < lh_array_get_size(&self->named_types); ++i)
    {
        const struct cnor_named_type *named = lh_array_get_ptr(&self->named_types, i);
        const cnor_node_t *field;
        if (named->node->kind != cnor_node_struct) continue;
        self->module = named->module;
        for (field = named->node->data.fn.params; field != lh_null; field = field->next)
        {
            cnor_static_type_t type = cnor_type_of_node(self, field, field->data.param.type);
            if (cnor_type_base(type) == cnor_static_array || cnor_type_base(type) == cnor_static_dict
                || type == cnor_static_function || cnor_named_is_struct(self, type))
                return cnor_compile_fail_name(self, field, cnor_error_type_mismatch);
        }
    }
    return lh_bool_true;
}

/* An enum member is a compile-time constant, numbered from zero. */
lh_bool_t
cnor_named_pattern(cnor_compiler_t *self, const cnor_node_t *node,
                    cnor_static_type_t *type, lh_u32_t *value)
{
    const struct cnor_named_type *named;
    const cnor_node_t *field;
    cnor_node_t normalized;
    const cnor_node_t *base = node->data.index.target;
    if (cnor_namespace_node(self, base, &normalized)) base = &normalized;
    if (base->kind != cnor_node_ident) return cnor_compile_fail(self, node, cnor_error_type_mismatch);
    if (cnor_scope_resolve(self, base->name).kind != cnor_binding_none)
        return cnor_compile_fail(self, node, cnor_error_type_mismatch);
    named = cnor_named_find(self, base->name);
    if (named == lh_null || named->node->kind != cnor_node_enum)
        return cnor_compile_fail_name(self, base, cnor_error_unknown_name);
    *value = 0U;
    for (field = named->node->data.fn.params; field != lh_null; field = field->next, ++*value)
    {
        if (!cnor_named_same(field->name, node->name)) continue;
        *type = named->type;
        return lh_bool_true;
    }
    return cnor_compile_fail_name(self, node, cnor_error_unknown_name);
}

static const cnor_node_t *
cnor_named_record_field(cnor_compiler_t *self, const cnor_node_t *member,
              cnor_static_type_t type, lh_u32_t *index)
{
    const struct cnor_named_type *named = cnor_named_of(self, type);
    const cnor_node_t *field;
    if (named == lh_null || named->node->kind != cnor_node_struct)
    {
        cnor_compile_fail(self, member, cnor_error_type_mismatch);
        return lh_null;
    }
    *index = 0U;
    for (field = named->node->data.fn.params; field != lh_null; field = field->next, ++*index)
        if (cnor_named_same(field->name, member->name)) return field;
    cnor_compile_fail_name(self, member, cnor_error_unknown_name);
    return lh_null;
}

lh_bool_t
cnor_compile_member(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *base = node->data.index.target;
    const cnor_node_t *field;
    lh_u32_t index;
    cnor_node_t qualified;
    cnor_node_t normalized;
    if (cnor_namespace_node(self, node, &qualified)) return cnor_compile_ident(self, &qualified);
    if (cnor_namespace_node(self, base, &normalized)) base = &normalized;
    if (base->kind == cnor_node_ident
        && cnor_scope_resolve(self, base->name).kind == cnor_binding_none)
    {
        cnor_static_type_t type;
        if (!cnor_named_pattern(self, node, &type, &index)) return lh_bool_false;
        self->type = type;
        return cnor_emit_insn(self, cnor_op_push_i32, index, 0U);
    }
    if (!cnor_compile_expr(self, base)) return lh_bool_false;
    field = cnor_named_record_field(self, node, self->type, &index);
    if (field == lh_null) return lh_bool_false;
    self->type = cnor_type_of_node(self, field, field->data.param.type);
    return cnor_emit_insn(self, cnor_op_push_i32, index, 0U)
        && cnor_emit_op(self, cnor_op_array_get);
}

lh_bool_t
cnor_compile_member_assign(cnor_compiler_t *self, const cnor_node_t *node)
{
    const cnor_node_t *member = node->data.assign.target;
    const cnor_node_t *base = member->data.index.target;
    const cnor_node_t *field;
    cnor_binding_t binding;
    cnor_static_type_t wanted;
    lh_u32_t index;
    static const cnor_op_t ops[cnor_assign_count] = {
        cnor_op_add, cnor_op_add, cnor_op_sub, cnor_op_mul, cnor_op_div,
        cnor_op_mod, cnor_op_band, cnor_op_bor, cnor_op_bxor
    };
    if (base->kind != cnor_node_ident)
        return cnor_compile_fail(self, member, cnor_error_not_assignable);
    binding = cnor_scope_resolve(self, base->name);
    if (!binding.is_mutable) return cnor_compile_fail_name(self, base, cnor_error_immutable);
    field = cnor_named_record_field(self, member, binding.type, &index);
    if (field == lh_null) return lh_bool_false;
    wanted = cnor_type_of_node(self, field, field->data.param.type);
    if (!cnor_compile_expr(self, base)
        || !cnor_emit_insn(self, cnor_op_push_i32, index, 0U)) return lh_bool_false;
    if (node->data.assign.op != cnor_assign_set)
    {
        cnor_node_t binary = {0};
        cnor_static_type_t result;
        static const cnor_binary_op_t binary_ops[cnor_assign_count] = {
            cnor_binary_add, cnor_binary_add, cnor_binary_sub, cnor_binary_mul,
            cnor_binary_div, cnor_binary_mod, cnor_binary_band, cnor_binary_bor, cnor_binary_bxor
        };
        binary.line = node->line;
        binary.data.binary.op = binary_ops[node->data.assign.op];
        if (!cnor_compile_member(self, member)
            || !cnor_compile_expr(self, node->data.assign.value)
            || !cnor_type_binary(self, &binary, wanted, self->type, &result)
            || !cnor_type_require(self, node, wanted, result)
            || !cnor_emit_op(self, ops[node->data.assign.op])) return lh_bool_false;
    }
    else if (!cnor_compile_expr(self, node->data.assign.value)
        || !cnor_type_require(self, node, wanted, self->type)) return lh_bool_false;
    return cnor_emit_op(self, cnor_op_array_set);
}

lh_bool_t
cnor_compile_struct_let(cnor_compiler_t *self, const cnor_node_t *node,
                        const struct cnor_named_type *named)
{
    const cnor_node_t *init = node->data.let.init;
    const cnor_node_t *field;
    const cnor_node_t *arg;
    const lh_bool_t global = self->top_code && self->block_depth == 0U;
    cnor_slot_t slot;
    lh_u32_t index = 0U;
    if (init->data.call.argc != named->node->data.fn.param_count)
        return cnor_compile_fail_name(self, init, cnor_error_wrong_arg_count);
    if (node->data.let.type != cnor_type_none
        && !cnor_type_require(self, node, cnor_type_of_node(self, node, node->data.let.type), named->type))
        return lh_bool_false;
    /* Evaluate arguments before introducing a local name, as ordinary let does. */
    for (field = named->node->data.fn.params, arg = init->data.call.args;
         field != lh_null; field = field->next, arg = arg->next)
        if (!cnor_compile_expr(self, arg)
            || !cnor_type_require(self, arg, cnor_type_of_node(self, field, field->data.param.type), self->type))
            return lh_bool_false;
    if (global)
    {
        const cnor_binding_t binding = cnor_scope_resolve(self, node->name);
        slot = binding.index;
        cnor_scope_global_at(&self->scope, slot)->type = named->type;
        if (slot + named->node->data.fn.param_count + 1U > self->globals_ready)
            self->globals_ready = slot + named->node->data.fn.param_count + 1U;
    }
    else
    {
        if (!cnor_scope_add_local(self, node, node->name, node->data.let.is_mutable,
            cnor_static_array, named->node->data.fn.param_count, &slot)) return lh_bool_false;
        /* The allocator reserves the frame array; its binding keeps the nominal type. */
        {
            lh_usize_t last = lh_array_get_size(&self->scope.locals) - 1U;
            struct cnor_local_decl *local = lh_array_get_ptr(&self->scope.locals, last);
            local->type = named->type;
        }
    }
    if (!cnor_emit_op(self, cnor_op_push_none)
        || !cnor_emit_insn(self, global ? cnor_op_array_new_global : cnor_op_array_new,
                           slot, named->node->data.fn.param_count)) return lh_bool_false;
    /* Values remain on the operand stack in declaration order; pop them backwards. */
    for (index = named->node->data.fn.param_count; index > 0U; --index)
        if (!cnor_emit_insn(self, global ? cnor_op_store_global : cnor_op_store_local,
                           slot + index, 0U)) return lh_bool_false;
    return lh_bool_true;
}

/* Records own frame storage: only calls may borrow their reference. */
lh_bool_t
cnor_compile_storable(cnor_compiler_t *self, const cnor_node_t *node)
{
    if (!cnor_compile_expr(self, node)) return lh_bool_false;
    return !cnor_named_is_struct(self, self->type)
        || cnor_compile_fail(self, node, cnor_error_struct_lifetime);
}
