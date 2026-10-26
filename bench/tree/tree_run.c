#include "tree_run.h"

#include <cnor/arena.h>
#include <cnor/parser.h>
#include <cnor/vm/fast.h>
#include <assert.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>
#include <stdio.h>
#include <string.h>

/* -- the program ------------------------------------------------------------- */

/* One function of the program. */
struct tree_function
{
    const cnor_node_t *node; /**< The `fn` node. */
    lh_u32_t frame_slots;    /**< How many slots its frame has. */
};

/* The program: the tree, the functions, and what a run needs. */
struct tree_program
{
    cnor_arena_t arena;
    cnor_node_t *top;                /**< The top-level nodes. */
    struct tree_function *functions; /**< The functions, in source order. */
    lh_u32_t function_count;
    lh_u32_t frame_slots; /**< The largest frame. */
    lh_value_t *frames;   /**< The frame memory. */
    lh_u32_t max_depth;   /**< How many frames a run may have. */
};

/* How many frames a run may have: the walk is a C recursion, so the C stack
 * is the limit too. */
#define TREE_MAX_FRAMES 256U

/* How many arguments a call may have: they are held in the C frame while the
 * callee's own frame is not there yet. */
#define TREE_MAX_ARGS 16U

/* -- the resolver ------------------------------------------------------------ */

/*
 * The names are resolved before the run, and the slot of every name is written
 * into the tree. The bytecode machine has the same thing, done by the
 * compiler; the tree-walker is measured with it, so the comparison is not
 * about looking a name up at run time.
 *
 * The slot travels in the name's size: the text of a name is not needed while
 * the program runs, and the tree belongs to the program.
 */

/* A name and the slot it has. */
struct tree_binding
{
    lh_str_view_t name;
    lh_u32_t slot;
};

/* One block of names. */
struct tree_scope
{
    lh_u32_t first;     /**< The first binding of the scope. */
    lh_u32_t count;     /**< How many bindings it has. */
    lh_u32_t next_slot; /**< The next free slot when the scope opened. */
};

/* A node whose name's size is to become a number. */
struct tree_patch
{
    cnor_node_t *node;
    lh_u32_t value;
};

struct tree_resolver
{
    struct tree_program *program;
    struct tree_binding *bindings; /**< Every name of the function. */
    lh_u32_t binding_count;
    lh_u32_t binding_capacity;
    struct tree_scope scopes[32];
    lh_u32_t scope_depth;
    lh_u32_t next_slot;   /**< The next free slot of the frame. */
    lh_u32_t frame_slots; /**< The frame's high water mark. */
    struct tree_patch *patches;
    lh_u32_t patch_count;
    lh_u32_t patch_capacity;
};

/* The name @p name has in the resolver's scopes, or its slot is unknown. */
static lh_bool_t
tree_resolver_find(const struct tree_resolver *self, lh_str_view_t name, lh_u32_t *slot)
{
    lh_u32_t scope;

    for (scope = self->scope_depth; scope > 0U; --scope)
    {
        const struct tree_scope *const block = &self->scopes[scope - 1U];
        lh_u32_t i;

        for (i = block->count; i > 0U; --i)
        {
            const struct tree_binding *const binding = &self->bindings[block->first + i - 1U];

            if (lh_str_view_equals(&binding->name, &name, lh_bool_false))
            {
                *slot = binding->slot;
                return lh_bool_true;
            }
        }
    }
    return lh_bool_false;
}

/* Write @p value into the name's size of @p node, when the run starts. */
static lh_bool_t
tree_resolver_patch(struct tree_resolver *self, cnor_node_t *node, lh_u32_t value)
{
    if (self->patch_count >= self->patch_capacity)
    {
        const lh_u32_t capacity = self->patch_capacity == 0U ? 64U : self->patch_capacity * 2U;
        struct tree_patch *const grown = (struct tree_patch *) lh_runtime_allocator_alloc(
            (lh_usize_t) capacity * sizeof(struct tree_patch));

        if (grown == lh_null)
        {
            return lh_bool_false;
        }
        if (self->patches != lh_null)
        {
            memcpy(grown, self->patches, (lh_usize_t) self->patch_count * sizeof(*grown));
            lh_runtime_allocator_free(self->patches);
        }
        self->patches = grown;
        self->patch_capacity = capacity;
    }
    self->patches[self->patch_count].node = node;
    self->patches[self->patch_count].value = value;
    self->patch_count++;
    return lh_bool_true;
}

/* Remember @p name in the current scope, in the next free slot; the slot goes
 * into @p node's name. */
static lh_bool_t
tree_resolver_declare(struct tree_resolver *self, cnor_node_t *node, lh_str_view_t name)
{
    struct tree_scope *const block = &self->scopes[self->scope_depth - 1U];
    struct tree_binding *binding;

    if (self->binding_count >= self->binding_capacity)
    {
        const lh_u32_t capacity = self->binding_capacity == 0U ? 32U : self->binding_capacity * 2U;
        struct tree_binding *const grown = (struct tree_binding *) lh_runtime_allocator_alloc(
            (lh_usize_t) capacity * sizeof(struct tree_binding));

        if (grown == lh_null)
        {
            return lh_bool_false;
        }
        if (self->bindings != lh_null)
        {
            memcpy(grown, self->bindings, (lh_usize_t) self->binding_count * sizeof(*grown));
            lh_runtime_allocator_free(self->bindings);
        }
        self->bindings = grown;
        self->binding_capacity = capacity;
    }
    binding = &self->bindings[self->binding_count++];
    binding->name = name;
    binding->slot = self->next_slot++;
    block->count++;
    if (self->next_slot > self->frame_slots)
    {
        self->frame_slots = self->next_slot;
    }
    return tree_resolver_patch(self, node, binding->slot);
}

/* Open a block of names. */
static void
tree_resolver_push_scope(struct tree_resolver *self)
{
    struct tree_scope *const block = &self->scopes[self->scope_depth++];

    block->first = self->binding_count;
    block->count = 0U;
    block->next_slot = self->next_slot;
}

/* Close a block of names; its slots are free again. */
static void
tree_resolver_pop_scope(struct tree_resolver *self)
{
    const struct tree_scope *const block = &self->scopes[--self->scope_depth];

    self->binding_count = block->first;
    self->next_slot = block->next_slot;
}

static lh_bool_t tree_resolver_expr(struct tree_resolver *self, const cnor_node_t *node);
static lh_bool_t tree_resolver_exprs(struct tree_resolver *self, const cnor_node_t *node);

/* The index of the function @p name, or the number of functions when there is
 * none. */
static lh_u32_t
tree_resolver_function(const struct tree_resolver *self, lh_str_view_t name)
{
    lh_u32_t i;

    for (i = 0U; i < self->program->function_count; ++i)
    {
        const cnor_node_t *const node = self->program->functions[i].node;

        if (lh_str_view_equals(&node->name, &name, lh_bool_false))
        {
            return i;
        }
    }
    return self->program->function_count;
}

/* The name @p node is an identifier: its slot goes into the tree. */
static lh_bool_t
tree_resolver_name(struct tree_resolver *self, cnor_node_t *node)
{
    lh_u32_t slot = 0U;

    if (!tree_resolver_find(self, node->name, &slot))
    {
        return lh_bool_false;
    }
    return tree_resolver_patch(self, node, slot);
}

/* The expression @p node: the names in it are resolved. Only a list of
 * expressions is chained; a node that is not in a list has no next. */
static lh_bool_t
tree_resolver_expr(struct tree_resolver *self, const cnor_node_t *node)
{
    if (node == lh_null)
    {
        return lh_bool_true;
    }
    switch (node->kind)
    {
    case cnor_node_ident:
        return tree_resolver_name(self, (cnor_node_t *) node);
    case cnor_node_call:
        if (!tree_resolver_patch(self, (cnor_node_t *) node,
                                 tree_resolver_function(self, node->name)))
        {
            return lh_bool_false;
        }
        return tree_resolver_exprs(self, node->data.call.args);
    case cnor_node_binary:
        return tree_resolver_expr(self, node->data.binary.left)
               && tree_resolver_expr(self, node->data.binary.right);
    case cnor_node_unary:
        return tree_resolver_expr(self, node->data.unary.operand);
    default:
        return lh_bool_true;
    }
}

/* The list of expressions @p node: each one is resolved. */
static lh_bool_t
tree_resolver_exprs(struct tree_resolver *self, const cnor_node_t *node)
{
    for (; node != lh_null; node = node->next)
    {
        if (!tree_resolver_expr(self, node))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* The statements @p body: the names in them are resolved. */
static lh_bool_t
tree_resolver_block(struct tree_resolver *self, const cnor_node_t *body)
{
    const cnor_node_t *node;

    tree_resolver_push_scope(self);
    for (node = body; node != lh_null; node = node->next)
    {
        switch (node->kind)
        {
        case cnor_node_let:
            if (node->data.let.init != lh_null
                && !tree_resolver_expr(self, node->data.let.init))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            if (!tree_resolver_declare(self, (cnor_node_t *) node, node->name))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            break;
        case cnor_node_assign:
            if (!tree_resolver_expr(self, node->data.assign.value)
                || !tree_resolver_name(self, (cnor_node_t *) node->data.assign.target))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            break;
        case cnor_node_expr_stmt:
            if (!tree_resolver_expr(self, node->data.expr_stmt.expr))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            break;
        case cnor_node_if:
            if (!tree_resolver_expr(self, node->data.if_.cond)
                || !tree_resolver_block(self, node->data.if_.then_body)
                || !tree_resolver_block(self, node->data.if_.else_body))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            break;
        case cnor_node_while:
            if (!tree_resolver_expr(self, node->data.while_.cond)
                || !tree_resolver_block(self, node->data.while_.body))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            break;
        case cnor_node_for:
            if (!tree_resolver_expr(self, node->data.for_.from)
                || (node->data.for_.to != lh_null
                    && !tree_resolver_expr(self, node->data.for_.to)))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            tree_resolver_push_scope(self);
            if (!tree_resolver_declare(self, (cnor_node_t *) node, node->name)
                || !tree_resolver_block(self, node->data.for_.body))
            {
                tree_resolver_pop_scope(self);
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            tree_resolver_pop_scope(self);
            break;
        case cnor_node_return:
            if (node->data.return_.value != lh_null
                && !tree_resolver_expr(self, node->data.return_.value))
            {
                tree_resolver_pop_scope(self);
                return lh_bool_false;
            }
            break;
        default:
            break;
        }
    }
    tree_resolver_pop_scope(self);
    return lh_bool_true;
}

/* The function @p node, the @p index-th one: its frame, its names, and the
 * calls in it. */
static lh_bool_t
tree_resolver_function_body(struct tree_resolver *self, const cnor_node_t *node, lh_u32_t index)
{
    const cnor_node_t *param;

    self->binding_count = 0U;
    self->next_slot = 0U;
    self->frame_slots = 0U;
    self->scope_depth = 0U;
    tree_resolver_push_scope(self);
    for (param = node->data.fn.params; param != lh_null; param = param->next)
    {
        if (!tree_resolver_declare(self, (cnor_node_t *) param, param->name))
        {
            return lh_bool_false;
        }
    }
    if (!tree_resolver_block(self, node->data.fn.body))
    {
        return lh_bool_false;
    }
    tree_resolver_pop_scope(self);
    self->program->functions[index].frame_slots = self->frame_slots;
    if (self->frame_slots > self->program->frame_slots)
    {
        self->program->frame_slots = self->frame_slots;
    }
    return lh_bool_true;
}

/* -- the walk ---------------------------------------------------------------- */

/* The state of one run. */
struct tree_run
{
    const struct tree_program *program;
    lh_value_t *slots;   /**< The current frame's slots. */
    lh_u32_t depth;      /**< The frames in use. */
    lh_u32_t frame_slots;/**< The current frame's size. */
    lh_u64_t nodes;      /**< The tree nodes visited. */
    lh_bool_t returning; /**< A `return` is on the way out. */
    lh_value_t result;   /**< What the `return` carries. */
};

/* The slot @p slot of the current frame. The bounds are checked in a debug
 * build: the measurement runs the release build, where the check is gone. */
static lh_value_t *
tree_slot(struct tree_run *run, lh_u32_t slot)
{
    assert(slot < run->frame_slots);
    return &run->slots[slot];
}

static lh_bool_t tree_eval(struct tree_run *run, const cnor_node_t *node, lh_value_t *out);
static lh_bool_t tree_exec(struct tree_run *run, const cnor_node_t *node);
static lh_bool_t tree_exec_block(struct tree_run *run, const cnor_node_t *body);

/* The arithmetic and the comparison: the rules of the machine, on the same
 * values. */
static lh_bool_t
tree_binary(struct tree_run *run, const cnor_node_t *node, lh_value_t *out)
{
    lh_value_t left;
    lh_value_t right;

    if (!tree_eval(run, node->data.binary.left, &left)
        || !tree_eval(run, node->data.binary.right, &right))
    {
        return lh_bool_false;
    }
    if (cnor_vm_fast_kind(&left) != lh_value_kind_sint
        || cnor_vm_fast_kind(&right) != lh_value_kind_sint)
    {
        return lh_bool_false;
    }
    {
        const lh_sllong_t a = cnor_vm_fast_sint(&left);
        const lh_sllong_t b = cnor_vm_fast_sint(&right);

        switch (node->data.binary.op)
        {
        case cnor_binary_add:
            cnor_vm_fast_set_sint(out, a + b);
            break;
        case cnor_binary_sub:
            cnor_vm_fast_set_sint(out, a - b);
            break;
        case cnor_binary_mul:
            cnor_vm_fast_set_sint(out, a * b);
            break;
        case cnor_binary_div:
            if (b == 0)
            {
                return lh_bool_false;
            }
            cnor_vm_fast_set_sint(out, a / b);
            break;
        case cnor_binary_mod:
            if (b == 0)
            {
                return lh_bool_false;
            }
            cnor_vm_fast_set_sint(out, a % b);
            break;
        case cnor_binary_eq:
            cnor_vm_fast_set_bool(out, a == b);
            break;
        case cnor_binary_ne:
            cnor_vm_fast_set_bool(out, a != b);
            break;
        case cnor_binary_lt:
            cnor_vm_fast_set_bool(out, a < b);
            break;
        case cnor_binary_le:
            cnor_vm_fast_set_bool(out, a <= b);
            break;
        case cnor_binary_gt:
            cnor_vm_fast_set_bool(out, a > b);
            break;
        case cnor_binary_ge:
            cnor_vm_fast_set_bool(out, a >= b);
            break;
        default:
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}

/* The call @p node: the function's index is in the name's size. */
static lh_bool_t
tree_call(struct tree_run *run, const cnor_node_t *node, lh_value_t *out)
{
    const lh_u32_t index = (lh_u32_t) node->name.size;
    const struct tree_function *function;
    const cnor_node_t *param;
    const cnor_node_t *arg;
    lh_value_t args[TREE_MAX_ARGS];
    lh_u32_t argc = 0U;
    lh_value_t *slots;
    lh_value_t *const caller_slots = run->slots;
    const lh_u32_t caller_frame_slots = run->frame_slots;

    if (index >= run->program->function_count)
    {
        return lh_bool_false;
    }
    function = &run->program->functions[index];
    for (arg = node->data.call.args; arg != lh_null; arg = arg->next)
    {
        if (argc >= TREE_MAX_ARGS)
        {
            return lh_bool_false;
        }
        if (!tree_eval(run, arg, &args[argc++]))
        {
            return lh_bool_false;
        }
    }
    if (run->depth >= run->program->max_depth)
    {
        return lh_bool_false;
    }
    /* The arguments go into the callee's frame; its own locals follow. */
    slots = run->program->frames + (lh_usize_t) (run->depth + 1U) * run->program->frame_slots;
    argc = 0U;
    for (param = function->node->data.fn.params; param != lh_null; param = param->next)
    {
        if (argc < TREE_MAX_ARGS)
        {
            slots[argc] = args[argc];
        }
        ++argc;
    }
    run->depth++;
    run->slots = slots;
    run->frame_slots = function->frame_slots;
    {
        const cnor_node_t *statement;

        for (statement = function->node->data.fn.body; statement != lh_null;
             statement = statement->next)
        {
            if (!tree_exec(run, statement))
            {
                run->depth--;
                return lh_bool_false;
            }
            if (run->returning)
            {
                break;
            }
        }
    }
    run->depth--;
    run->slots = caller_slots;
    run->frame_slots = caller_frame_slots;
    if (run->returning)
    {
        *out = run->result;
        run->returning = lh_bool_false;
        return lh_bool_true;
    }
    cnor_vm_fast_set_none(out);
    return lh_bool_true;
}

/* The expression @p node: its value in @p out. */
static lh_bool_t
tree_eval(struct tree_run *run, const cnor_node_t *node, lh_value_t *out)
{
    ++run->nodes;
    switch (node->kind)
    {
    case cnor_node_int:
        cnor_vm_fast_set_sint(out, (lh_sllong_t) node->data.int_.value);
        return lh_bool_true;
    case cnor_node_true:
        cnor_vm_fast_set_bool(out, 1);
        return lh_bool_true;
    case cnor_node_false:
        cnor_vm_fast_set_bool(out, 0);
        return lh_bool_true;
    case cnor_node_none:
        cnor_vm_fast_set_none(out);
        return lh_bool_true;
    case cnor_node_ident:
    {
        lh_value_t *const slot = tree_slot(run, (lh_u32_t) node->name.size);

        if (slot == lh_null)
        {
            return lh_bool_false;
        }
        *out = *slot;
        return lh_bool_true;
    }
    case cnor_node_binary:
        return tree_binary(run, node, out);
    case cnor_node_call:
        return tree_call(run, node, out);
    case cnor_node_unary:
    {
        lh_value_t operand;

        if (!tree_eval(run, node->data.unary.operand, &operand))
        {
            return lh_bool_false;
        }
        if (cnor_vm_fast_kind(&operand) != lh_value_kind_sint)
        {
            return lh_bool_false;
        }
        switch (node->data.unary.op)
        {
        case cnor_unary_neg:
            cnor_vm_fast_set_sint(out, -cnor_vm_fast_sint(&operand));
            break;
        case cnor_unary_not:
            cnor_vm_fast_set_bool(out, cnor_vm_fast_sint(&operand) == 0 ? 1 : 0);
            break;
        default:
            return lh_bool_false;
        }
        return lh_bool_true;
    }
    default:
        return lh_bool_false;
    }
}

/* The statement @p node. */
static lh_bool_t
tree_exec(struct tree_run *run, const cnor_node_t *node)
{
    ++run->nodes;
    switch (node->kind)
    {
    case cnor_node_let:
    {
        lh_value_t value;

        if (node->data.let.init != lh_null)
        {
            if (!tree_eval(run, node->data.let.init, &value))
            {
                return lh_bool_false;
            }
        }
        else
        {
            cnor_vm_fast_set_none(&value);
        }
        {
            lh_value_t *const slot = tree_slot(run, (lh_u32_t) node->name.size);

            if (slot == lh_null)
            {
                return lh_bool_false;
            }
            *slot = value;
        }
        return lh_bool_true;
    }
    case cnor_node_assign:
    {
        lh_value_t value;

        if (!tree_eval(run, node->data.assign.value, &value))
        {
            return lh_bool_false;
        }
        {
            lh_value_t *const slot = tree_slot(run, (lh_u32_t) node->data.assign.target->name.size);

            if (slot == lh_null)
            {
                return lh_bool_false;
            }
            *slot = value;
        }
        return lh_bool_true;
    }
    case cnor_node_expr_stmt:
    {
        lh_value_t value;

        return tree_eval(run, node->data.expr_stmt.expr, &value);
    }
    case cnor_node_if:
    {
        lh_value_t condition;

        if (!tree_eval(run, node->data.if_.cond, &condition))
        {
            return lh_bool_false;
        }
        if (cnor_vm_fast_truthy(&condition))
        {
            return tree_exec_block(run, node->data.if_.then_body);
        }
        return tree_exec_block(run, node->data.if_.else_body);
    }
    case cnor_node_while:
        for (;;)
        {
            lh_value_t condition;

            if (!tree_eval(run, node->data.while_.cond, &condition))
            {
                return lh_bool_false;
            }
            if (!cnor_vm_fast_truthy(&condition))
            {
                break;
            }
            if (!tree_exec_block(run, node->data.while_.body))
            {
                return lh_bool_false;
            }
        }
        return lh_bool_true;
    case cnor_node_for:
    {
        lh_value_t from;
        lh_value_t to;
        const lh_u32_t slot = (lh_u32_t) node->name.size;

        if (!tree_eval(run, node->data.for_.from, &from))
        {
            return lh_bool_false;
        }
        if (node->data.for_.to == lh_null)
        {
            return lh_bool_false;
        }
        if (!tree_eval(run, node->data.for_.to, &to))
        {
            return lh_bool_false;
        }
        for (cnor_vm_fast_set_sint(&from, cnor_vm_fast_sint(&from));
             cnor_vm_fast_sint(&from) < cnor_vm_fast_sint(&to);
             cnor_vm_fast_set_sint(&from, cnor_vm_fast_sint(&from) + 1))
        {
            {
                lh_value_t *const target = tree_slot(run, slot);

                if (target == lh_null)
                {
                    return lh_bool_false;
                }
                *target = from;
            }
            if (!tree_exec_block(run, node->data.for_.body))
            {
                return lh_bool_false;
            }
        }
        return lh_bool_true;
    }
    case cnor_node_return:
        if (node->data.return_.value != lh_null)
        {
            if (!tree_eval(run, node->data.return_.value, &run->result))
            {
                return lh_bool_false;
            }
        }
        else
        {
            cnor_vm_fast_set_none(&run->result);
        }
        run->returning = lh_bool_true;
        return lh_bool_true;
    default:
        return lh_bool_false;
    }
}

/* The statements @p body, in one block. */
static lh_bool_t
tree_exec_block(struct tree_run *run, const cnor_node_t *body)
{
    const cnor_node_t *node;

    for (node = body; node != lh_null; node = node->next)
    {
        if (!tree_exec(run, node))
        {
            return lh_bool_false;
        }
        if (run->returning)
        {
            break;
        }
    }
    return lh_bool_true;
}

/* -- the program ------------------------------------------------------------- */

struct tree_program *
tree_program_compile(const char *source, cnor_diag_t *diag)
{
    struct tree_program *const program =
        (struct tree_program *) lh_runtime_allocator_alloc(sizeof(struct tree_program));
    cnor_parser_t parser;
    struct tree_resolver resolver;
    cnor_node_t *node;
    lh_u32_t function_count = 0U;

    if (program == lh_null)
    {
        (void) cnor_diag_set(diag, cnor_error_no_memory, 0U, 0U);
        return lh_null;
    }
    memset(program, 0, sizeof(*program));
    cnor_arena_init(&program->arena, 0U);
    {
        lh_str_view_t text;

        lh_str_init_by_size(&text, source, strlen(source));
        cnor_parser_init(&parser, text, &program->arena);
    }
    program->top = cnor_parser_parse(&parser);
    if (program->top == lh_null)
    {
        const cnor_diag_t *const reason = cnor_parser_get_diag(&parser);

        if (reason != lh_null)
        {
            *diag = *reason;
        }
        cnor_arena_deinit(&program->arena);
        lh_runtime_allocator_free(program);
        return lh_null;
    }
    /* The functions, in source order. */
    for (node = program->top; node != lh_null; node = node->next)
    {
        if (node->kind == cnor_node_fn)
        {
            ++function_count;
        }
    }
    program->functions = (struct tree_function *) lh_runtime_allocator_alloc(
        (lh_usize_t) (function_count == 0U ? 1U : function_count) * sizeof(struct tree_function));
    if (program->functions == lh_null)
    {
        (void) cnor_diag_set(diag, cnor_error_no_memory, 0U, 0U);
        cnor_arena_deinit(&program->arena);
        lh_runtime_allocator_free(program);
        return lh_null;
    }
    program->function_count = 0U;
    for (node = program->top; node != lh_null; node = node->next)
    {
        if (node->kind == cnor_node_fn)
        {
            program->functions[program->function_count++].node = node;
        }
    }
    /* The names: resolved once, before the run. */
    memset(&resolver, 0, sizeof(resolver));
    resolver.program = program;
    /* The top-level code is a frame of its own, like the machine's globals. */
    resolver.binding_count = 0U;
    resolver.next_slot = 0U;
    resolver.frame_slots = 0U;
    resolver.scope_depth = 0U;
    if (!tree_resolver_block(&resolver, program->top))
    {
        (void) cnor_diag_set(diag, cnor_error_unknown_name, program->top->line, 0U);
        lh_runtime_allocator_free(resolver.bindings);
        lh_runtime_allocator_free(resolver.patches);
        lh_runtime_allocator_free(program->functions);
        cnor_arena_deinit(&program->arena);
        lh_runtime_allocator_free(program);
        return lh_null;
    }
    if (resolver.frame_slots > program->frame_slots)
    {
        program->frame_slots = resolver.frame_slots;
    }
    {
        lh_u32_t index = 0U;

        for (node = program->top; node != lh_null; node = node->next)
        {
            if (node->kind != cnor_node_fn)
            {
                continue;
            }
            if (!tree_resolver_function_body(&resolver, node, index))
            {
                (void) cnor_diag_set(diag, cnor_error_unknown_name, node->line, 0U);
                lh_runtime_allocator_free(resolver.bindings);
                lh_runtime_allocator_free(resolver.patches);
                lh_runtime_allocator_free(program->functions);
                cnor_arena_deinit(&program->arena);
                lh_runtime_allocator_free(program);
                return lh_null;
            }
            ++index;
        }
    }
    /* The slots and the functions go into the tree. */
    {
        lh_u32_t i;

        for (i = 0U; i < resolver.patch_count; ++i)
        {
            resolver.patches[i].node->name.size = (lh_usize_t) resolver.patches[i].value;
        }
    }
    lh_runtime_allocator_free(resolver.bindings);
    lh_runtime_allocator_free(resolver.patches);
    program->max_depth = TREE_MAX_FRAMES;
    program->frames = (lh_value_t *) lh_runtime_allocator_alloc(
        (lh_usize_t) program->max_depth * (program->frame_slots == 0U ? 1U : program->frame_slots)
        * sizeof(lh_value_t));
    if (program->frames == lh_null)
    {
        (void) cnor_diag_set(diag, cnor_error_no_memory, 0U, 0U);
        lh_runtime_allocator_free(program->functions);
        cnor_arena_deinit(&program->arena);
        lh_runtime_allocator_free(program);
        return lh_null;
    }
    return program;
}

void
tree_program_free(struct tree_program *program)
{
    if (program == lh_null)
    {
        return;
    }
    cnor_arena_deinit(&program->arena);
    lh_runtime_allocator_free(program->frames);
    lh_runtime_allocator_free(program->functions);
    lh_runtime_allocator_free(program);
}

lh_bool_t
tree_program_run(const struct tree_program *program, lh_value_t *out, lh_u64_t *nodes)
{
    struct tree_run run;
    const cnor_node_t *node;

    memset(&run, 0, sizeof(run));
    run.program = program;
    run.slots = program->frames;
    run.depth = 1U;
    run.result = lh_value_make_none();
    for (node = program->top; node != lh_null; node = node->next)
    {
        /* The functions are declarations: the top-level code runs the rest. */
        if (node->kind == cnor_node_fn)
        {
            continue;
        }
        if (!tree_exec(&run, node))
        {
            return lh_bool_false;
        }
        if (run.returning)
        {
            break;
        }
    }
    *out = run.result;
    if (nodes != lh_null)
    {
        *nodes = run.nodes;
    }
    return lh_bool_true;
}
