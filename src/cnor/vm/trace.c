/** @file trace.c @brief Fixed-budget persistent traceback snapshots; no runtime allocation. */
#include <cnor/vm/internal.h>

/* Each handler and the current error own a root; each node owns its suffix. */
static void
cnor_vm_trace_release(cnor_vm_t *vm, cnor_count_t head)
{
    while (head != CNOR_VM_TRACE_NONE)
    {
        struct cnor_vm_trace_node *node = &vm->trace_nodes[head];
        if (--node->refs != 0U) break;
        head = node->next;
    }
}

void
cnor_vm_trace_clear(cnor_vm_t *vm)
{
    cnor_count_t i;
    if (vm->trace_nodes != lh_null && vm->trace_valid)
        for (i = 0U; i < vm->config.trace_capacity; ++i) vm->trace_nodes[i].refs = 0U;
    vm->trace_head = CNOR_VM_TRACE_NONE;
    vm->trace_at = 0U;
    vm->trace_valid = lh_bool_false;
    vm->trace_truncated = lh_bool_false;
}

/* Consume the temporary suffix reference, sharing an existing chain when possible. */
static cnor_count_t
cnor_vm_trace_prepend(cnor_vm_t *vm, cnor_code_addr_t at, cnor_count_t tail)
{
    cnor_count_t free_node = CNOR_VM_TRACE_NONE;
    cnor_count_t i;
    for (i = 0U; i < vm->config.trace_capacity; ++i)
    {
        struct cnor_vm_trace_node *node = &vm->trace_nodes[i];
        if (node->refs == 0U)
        {
            if (free_node == CNOR_VM_TRACE_NONE) free_node = i;
        }
        else if (node->at == at && node->next == tail)
        {
            ++node->refs;
            cnor_vm_trace_release(vm, tail);
            return i;
        }
    }
    if (free_node != CNOR_VM_TRACE_NONE)
    {
        struct cnor_vm_trace_node *node = &vm->trace_nodes[free_node];
        node->at = at;
        node->next = tail;
        node->refs = 1U;
    }
    else cnor_vm_trace_release(vm, tail);
    return free_node;
}

void
cnor_vm_trace_capture(cnor_vm_t *vm, cnor_code_addr_t at)
{
    cnor_count_t head = CNOR_VM_TRACE_NONE;
    cnor_call_depth_t first;
    cnor_call_depth_t i;
    cnor_vm_trace_release(vm, vm->trace_head);
    vm->trace_head = CNOR_VM_TRACE_NONE;
    vm->trace_valid = vm->trace_nodes != lh_null;
    vm->trace_at = at;
    vm->trace_truncated = lh_bool_false;
    if (!vm->trace_valid) return;
    first = vm->depth >= vm->config.trace_capacity ? vm->depth - vm->config.trace_capacity + 1U : 0U;
    vm->trace_truncated = first != 0U;
    for (i = first; i < vm->depth; ++i)
    {
        const cnor_code_addr_t next = vm->frames[i].return_addr;
        head = cnor_vm_trace_prepend(vm, next > 0U ? next - 1U : 0U, head);
        if (head == CNOR_VM_TRACE_NONE) break;
    }
    if (i == vm->depth) head = cnor_vm_trace_prepend(vm, at, head);
    if (head == CNOR_VM_TRACE_NONE) vm->trace_truncated = lh_bool_true;
    vm->trace_head = head;
}

void
cnor_vm_trace_release_try(cnor_vm_t *vm, cnor_vm_try_t *entry)
{
    cnor_vm_trace_release(vm, entry->trace_head);
    entry->trace_head = CNOR_VM_TRACE_NONE;
    entry->trace_truncated = lh_bool_false;
}

void
cnor_vm_trace_save_try(cnor_vm_t *vm, cnor_vm_try_t *entry)
{
    cnor_vm_trace_release_try(vm, entry);
    entry->trace_head = vm->trace_head;
    entry->trace_truncated = vm->trace_truncated;
    if (entry->trace_head != CNOR_VM_TRACE_NONE) ++vm->trace_nodes[entry->trace_head].refs;
}

void
cnor_vm_trace_restore_try(cnor_vm_t *vm, const cnor_vm_try_t *entry)
{
    cnor_count_t head = entry->trace_head;
    if (head != CNOR_VM_TRACE_NONE) ++vm->trace_nodes[head].refs;
    cnor_vm_trace_release(vm, vm->trace_head);
    vm->trace_head = head;
    vm->trace_at = entry->error_at;
    vm->trace_valid = vm->trace_nodes != lh_null;
    vm->trace_truncated = entry->trace_truncated;
}

void
cnor_vm_trace_finish(cnor_vm_t *vm, lh_bool_t ok)
{
    cnor_count_t i;
    for (i = 0U; i < vm->try_count; ++i) cnor_vm_trace_release_try(vm, &vm->tries[i]);
    if (ok) cnor_vm_trace_clear(vm);
}

cnor_count_t
cnor_vm_get_trace_count(const cnor_vm_t *vm)
{
    cnor_count_t count = 0U;
    cnor_count_t head = vm->trace_head;
    if (!vm->trace_valid || vm->image == lh_null) return 0U;
    if (head == CNOR_VM_TRACE_NONE) return 1U; /* Fault origin survives a full shared pool. */
    while (head != CNOR_VM_TRACE_NONE)
    {
        ++count;
        head = vm->trace_nodes[head].next;
    }
    return count;
}

lh_bool_t
cnor_vm_trace_is_truncated(const cnor_vm_t *vm)
{
    return vm->trace_valid && vm->trace_truncated;
}
