/* Shared by libFuzzer and the portable deterministic runner. No source compiler
 * or host natives are needed: arbitrary images cross the public load boundary. */
#include <cnor/vm.h>
#include <lh/runtime/allocator.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define HEAP_LIMIT (8U * 1024U * 1024U)
typedef union block_header {
    max_align_t alignment;
    size_t size;
} block_header;
static size_t live_bytes;

static lh_ptr bounded_alloc(lh_self_ptr ctx, lh_usize_t size)
{
    block_header *block;
    (void) ctx;
    if (size > HEAP_LIMIT - live_bytes) return lh_null;
    block = malloc(sizeof(*block) + size);
    if (!block) return lh_null;
    block->size = size;
    live_bytes += size;
    return block + 1;
}

static void bounded_free(lh_self_ptr ctx, lh_ptr ptr)
{
    block_header *block;
    (void) ctx;
    if (!ptr) return;
    block = (block_header *) ptr - 1;
    if (block->size > live_bytes) abort();
    live_bytes -= block->size;
    free(block);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_vm_t vm;
    lh_memory_allocator_t saved;
    unsigned pass;
    if (size > 1024U * 1024U) return 0;
    saved = *lh_runtime_allocator();
    lh_runtime_allocator_init(bounded_alloc, bounded_free);
    config.stack_slots = 256U;
    config.max_depth = 16U;
    config.str_pool_size = 1024U;
    config.max_steps = 512U;
    cnor_vm_init(&vm, &config);
    /* Reload on the same machine, including after a failed load/run. */
    for (pass = 0; pass < 2U; ++pass)
    {
        if (cnor_vm_load(&vm, data, (lh_u32_t) size))
        {
            (void) cnor_vm_run(&vm);
            cnor_vm_reset(&vm);
            (void) cnor_vm_run(&vm);
        }
    }
    cnor_vm_deinit(&vm);
    cnor_vm_deinit(&vm);
    if (live_bytes != 0U) abort();
    *lh_runtime_allocator() = saved;
    return 0;
}
