#include <cnor/vm.h>
#include <lh/runtime/allocator.h>
#include "clock.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Track requested lh payload bytes, excluding malloc and tracking overhead. */
typedef union allocation_header {
    max_align_t alignment;
    size_t size;
} allocation_header;
static size_t live_bytes, peak_bytes, allocation_calls;

static lh_ptr tracked_alloc(lh_self_ptr self, lh_usize_t size)
{
    allocation_header *block;
    (void) self;
    if (size > SIZE_MAX - sizeof(*block)) return lh_null;
    block = malloc(sizeof(*block) + size);
    if (!block) return lh_null;
    block->size = size;
    live_bytes += size;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    ++allocation_calls;
    return block + 1;
}

static void tracked_free(lh_self_ptr self, lh_ptr ptr)
{
    allocation_header *block;
    (void) self;
    if (!ptr) return;
    block = (allocation_header *) ptr - 1;
    live_bytes -= block->size;
    free(block);
}

typedef struct host_state {
    int temperature;
    int relay;
    unsigned writes;
} host_state;

static void temperature(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx,
                        lh_value_t *result, cnor_error_t *error)
{
    (void) args; (void) argc; (void) error;
    *result = lh_value_make_sint(((host_state *) ctx)->temperature);
}

static void relay(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx,
                  lh_value_t *result, cnor_error_t *error)
{
    (void) args; (void) argc; (void) error;
    *result = lh_value_make_sint(((host_state *) ctx)->relay);
}

static void set_relay(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx,
                      lh_value_t *result, cnor_error_t *error)
{
    host_state *host = ctx;
    (void) argc;
    /* Native declarations are a compiler contract; validate host inputs too. */
    if (lh_value_get_kind(&args[0]) != lh_value_kind_sint
        || (lh_value_get_data(&args[0])->i != 0 && lh_value_get_data(&args[0])->i != 1))
    {
        *error = cnor_error_type_mismatch;
        return;
    }
    host->relay = (int) lh_value_get_data(&args[0])->i;
    ++host->writes;
    *result = lh_value_make_sint(host->relay);
}

static const cnor_native_t natives[] = {
    cnor_native_initializer("temperature", temperature, 0U, 0U),
    cnor_native_initializer("relay", relay, 0U, 0U),
    cnor_native_initializer("set_relay", set_relay, 1U, 1U)
};
static const cnor_native_table_t native_table = cnor_native_table_initializer(natives, 3U);

int main(int argc, char **argv)
{
    const unsigned events = 100000U;
    const int temperatures[] = {210, 180, 200, 230};
    const int expected[] = {0, 1, 1, 0};
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_vm_t vm;
    host_state host = {0, 0, 0};
    FILE *file;
    long length;
    unsigned char *image;
    unsigned i;
    size_t resident, calls_before;
    uint64_t started, elapsed, steps = 0;
    int status = 1;
    if (argc != 2) { fprintf(stderr, "usage: cnor_event_host thermostat.tbc\n"); return 2; }
    file = fopen(argv[1], "rb");
    if (!file) { perror(argv[1]); return 2; }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0
        || (uint64_t) length > UINT32_MAX || fseek(file, 0, SEEK_SET))
    { fclose(file); fprintf(stderr, "invalid image length\n"); return 2; }
    image = malloc((size_t) length);
    if (!image) { fclose(file); return 2; }
    if (fread(image, 1, (size_t) length, file) != (size_t) length)
    { fclose(file); free(image); return 2; }
    fclose(file);

    lh_runtime_allocator_init(tracked_alloc, tracked_free);
    config.natives = &native_table;
    config.ctx = &host;
    config.stack_slots = 64U;
    config.max_depth = 8U;
    config.str_pool_size = 256U;
    config.max_steps = 256U;
    cnor_vm_init(&vm, &config);
    if (!cnor_vm_load(&vm, image, (lh_u32_t) length))
    { free(image); goto done; }
    free(image); /* The VM owns its own copy now. */
    resident = live_bytes;
    calls_before = allocation_calls;
    started = cnor_bench_clock_ns();
    for (i = 0; i < events; ++i)
    {
        host.temperature = temperatures[i % 4U];
        cnor_vm_reset(&vm); /* Reclaim temporary strings; C retains relay state. */
        if (!cnor_vm_run(&vm)) goto done;
        steps += cnor_vm_get_steps(&vm);
        if (host.relay != expected[i % 4U] || host.writes != i + 1U)
        { fprintf(stderr, "event %u: unexpected relay state or write count\n", i); goto done; }
    }
    elapsed = cnor_bench_clock_ns() - started;
    if (allocation_calls != calls_before || live_bytes != resident)
    { fprintf(stderr, "event processing allocated memory\n"); goto done; }
    printf("events=%u image_bytes=%ld vm_object_bytes=%zu\n", events, length, sizeof(vm));
    printf("vm_heap_resident_bytes=%zu load_peak_heap_bytes=%zu event_allocations=%zu\n",
           resident, peak_bytes, allocation_calls - calls_before);
    printf("ns_per_event=%.1f mean_steps_per_event=%.1f relay=%d\n",
           (double) elapsed / events, (double) steps / events, host.relay);
    /* A failed event must not prevent the host from processing the next one. */
    vm.config.max_steps = 1U;
    cnor_vm_reset(&vm);
    if (cnor_vm_run(&vm)
        || cnor_diag_get_code(cnor_vm_get_diag(&vm)) != cnor_error_step_limit)
    { fprintf(stderr, "instruction budget did not stop the event\n"); goto done; }
    vm.config.max_steps = config.max_steps;
    host.temperature = 180;
    cnor_vm_reset(&vm);
    if (!cnor_vm_run(&vm) || host.relay != 1 || host.writes != events + 1U)
    { fprintf(stderr, "recovery after budget exhaustion failed\n"); goto done; }
    if (allocation_calls != calls_before || live_bytes != resident)
    { fprintf(stderr, "recovery allocated memory\n"); goto done; }
    puts("budget_stop_and_recovery=ok");
    status = 0;
done:
    if (status && cnor_diag_get_code(cnor_vm_get_diag(&vm)) != cnor_error_none)
        fprintf(stderr, "VM error: %s, line %u\n",
                        cnor_error_get_message(cnor_diag_get_code(cnor_vm_get_diag(&vm))),
                        (unsigned) cnor_diag_get_line(cnor_vm_get_diag(&vm)));
    cnor_vm_deinit(&vm);
    if (live_bytes) { fprintf(stderr, "unreleased VM bytes: %zu\n", live_bytes); status = 1; }
    return status;
}
