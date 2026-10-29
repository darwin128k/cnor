/**
 * @file tree_bench.c
 * @brief The same programs, run by the bytecode machine and by a tree-walker.
 *
 * The question this answers: "how much slower is a tree-walker than the
 * bytecode machine?" — with a number, not an opinion.
 *
 * Both run the very same source text (the sources come from the benchmark's
 * workloads), in the same process, with the same clock and the same values.
 * The rounds are interleaved, so a busy machine hits both equally, and the
 * minimum is reported as well as the median: the minimum is the least
 * disturbed round, and the ratio of the two minima is the fairest number a
 * noisy machine gives.
 *
 * The tree-walker is measured at its best: its names are resolved once, before
 * the run, so the comparison is not about looking a name up — the one thing
 * the bytecode machine has and a tree-walker does not.
 *
 * Usage: cnor_tree_bench [--iterations N] [--warmup N]
 */

#include "../clock.h"
#include "../workloads.h"
#include "tree_run.h"

#include <cnor/compiler.h>
#include <cnor/error.h>
#include <cnor/vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TREE_BENCH_MAX_ITERATIONS 100U

/* -- options --------------------------------------------------------------- */

struct tree_bench_options
{
    unsigned iterations;
    unsigned warmup;
};

static int
tree_bench_parse_options(int argc, char **argv, struct tree_bench_options *out)
{
    int i;

    out->iterations = 7U;
    out->warmup = 2U;
    for (i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            out->iterations = (unsigned) strtoul(argv[++i], NULL, 10);
        }
        else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
        {
            out->warmup = (unsigned) strtoul(argv[++i], NULL, 10);
        }
        else
        {
            fprintf(stderr, "usage: %s [--iterations N] [--warmup N]\n", argv[0]);
            return 0;
        }
    }
    if (out->iterations == 0U || out->iterations > TREE_BENCH_MAX_ITERATIONS)
    {
        fprintf(stderr, "--iterations must be 1..%u\n", TREE_BENCH_MAX_ITERATIONS);
        return 0;
    }
    return 1;
}

/* -- samples --------------------------------------------------------------- */

static int
tree_bench_compare_u64(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *) a;
    const uint64_t y = *(const uint64_t *) b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

static void
tree_bench_summarise(uint64_t *samples, unsigned count, uint64_t *median, uint64_t *min)
{
    qsort(samples, count, sizeof(samples[0]), tree_bench_compare_u64);
    *min = samples[0];
    *median = count % 2U != 0U ? samples[count / 2U]
                                : (samples[count / 2U - 1U] + samples[count / 2U]) / 2U;
}

/* -- the two machines ------------------------------------------------------- */

static int
tree_bench_result_is(const lh_value_t *value, int64_t expected)
{
    switch (lh_value_get_kind(value))
    {
    case lh_value_kind_sint:
        return lh_value_get_data(value)->i == expected;
    case lh_value_kind_uint:
        return expected >= 0 && lh_value_get_data(value)->u == (lh_ullong_t) expected;
    default:
        return 0;
    }
}

/* The bytecode machine: the image is compiled and loaded once. */
struct tree_bench_vm
{
    cnor_vm_t vm;
    cnor_image_t image;
};

static int
tree_bench_vm_open(struct tree_bench_vm *machine, const char *name, const char *source)
{
    cnor_compiler_config_t config = cnor_compiler_config_initializer();
    cnor_vm_config_t vm_config = cnor_vm_config_initializer();
    cnor_diag_t diag;
    lh_str_view_t text;

    lh_str_view_init_by_size(&text, source, strlen(source));
    if (!cnor_compiler_compile_source(text, &config, &machine->image, &diag))
    {
        fprintf(stderr, "%s: compile failed: %s (line %u)\n", name,
                cnor_error_get_message(cnor_diag_get_code(&diag)),
                (unsigned) cnor_diag_get_line(&diag));
        return 0;
    }
    vm_config.max_steps = 0U;
    cnor_vm_init(&machine->vm, &vm_config);
    if (!cnor_vm_load(&machine->vm, machine->image.data, machine->image.size))
    {
        fprintf(stderr, "%s: load failed: %s\n", name,
                cnor_error_get_message(cnor_diag_get_code(cnor_vm_get_diag(&machine->vm))));
        cnor_image_deinit(&machine->image);
        cnor_vm_deinit(&machine->vm);
        return 0;
    }
    return 1;
}

static void
tree_bench_vm_close(struct tree_bench_vm *machine)
{
    cnor_image_deinit(&machine->image);
    cnor_vm_deinit(&machine->vm);
}

static int
tree_bench_vm_run(struct tree_bench_vm *machine, const char *name, int64_t expected, uint64_t *elapsed)
{
    uint64_t start;
    lh_value_t result;

    cnor_vm_reset(&machine->vm);
    start = cnor_bench_clock_ns();
    if (!cnor_vm_run(&machine->vm))
    {
        fprintf(stderr, "%s: the machine failed: %s\n", name,
                cnor_error_get_message(cnor_diag_get_code(cnor_vm_get_diag(&machine->vm))));
        return 0;
    }
    *elapsed = cnor_bench_clock_ns() - start;
    result = cnor_vm_get_result(&machine->vm);
    if (!tree_bench_result_is(&result, expected))
    {
        fprintf(stderr, "%s: the machine computed the wrong result\n", name);
        return 0;
    }
    return 1;
}

/* The tree-walker: the program is read and resolved once. */
static int
tree_bench_tree_run(const struct tree_program *program, const char *name, int64_t expected,
                    uint64_t *elapsed, uint64_t *nodes)
{
    uint64_t start;
    lh_value_t result;

    start = cnor_bench_clock_ns();
    if (!tree_program_run(program, &result, nodes))
    {
        fprintf(stderr, "%s: the tree-walker failed\n", name);
        return 0;
    }
    *elapsed = cnor_bench_clock_ns() - start;
    if (!tree_bench_result_is(&result, expected))
    {
        fprintf(stderr, "%s: the tree-walker computed the wrong result\n", name);
        return 0;
    }
    return 1;
}

/* -- the report ------------------------------------------------------------- */

struct tree_bench_case
{
    const char *name;
    const char *source;
    int64_t expected;
    uint64_t ops;
};

int
main(int argc, char **argv)
{
    static const struct tree_bench_case cases[] = {
        {"fib", cnor_bench_fib, 75025, 242785U},
        {"loop", cnor_bench_loop, 49999995000000LL, 10000000U},
        {"nested", cnor_bench_nested, 2570569, 1000000U},
    };
    struct tree_bench_options options;
    size_t i;

    if (!tree_bench_parse_options(argc, argv, &options))
    {
        return 2;
    }
    printf("the same programs, run by the bytecode machine and by a tree-walker\n");
    printf("(%u rounds after %u warmup rounds, the two machines interleaved)\n\n",
           options.iterations, options.warmup);
    printf("%-8s %11s %11s %8s %11s %11s %11s %11s\n", "workload", "machine ms", "tree ms",
           "slower", "machine ns", "tree ns", "machine st", "tree nodes");
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const struct tree_bench_case *const item = &cases[i];
        struct tree_bench_vm machine;
        struct tree_program *program;
        cnor_diag_t diag;
        uint64_t vm_samples[TREE_BENCH_MAX_ITERATIONS];
        uint64_t tree_samples[TREE_BENCH_MAX_ITERATIONS];
        double ratios[TREE_BENCH_MAX_ITERATIONS];
        uint64_t vm_median = 0U;
        uint64_t vm_min = 0U;
        uint64_t tree_median = 0U;
        uint64_t tree_min = 0U;
        uint64_t vm_steps = 0U;
        uint64_t tree_nodes = 0U;
        double best_ratio = 0.0;
        unsigned round;

        if (!tree_bench_vm_open(&machine, item->name, item->source))
        {
            return 1;
        }
        program = tree_program_compile(item->source, &diag);
        if (program == lh_null)
        {
            fprintf(stderr, "%s: the tree-walker could not read the program: %s\n", item->name,
                    cnor_error_get_message(cnor_diag_get_code(&diag)));
            tree_bench_vm_close(&machine);
            return 1;
        }
        for (round = 0U; round < options.warmup + options.iterations; ++round)
        {
            uint64_t vm_elapsed = 0U;
            uint64_t tree_elapsed = 0U;

            /* The two machines in turn: a busy machine hits both alike. */
            if (!tree_bench_vm_run(&machine, item->name, item->expected, &vm_elapsed)
                || !tree_bench_tree_run(program, item->name, item->expected, &tree_elapsed,
                                        &tree_nodes))
            {
                tree_program_free(program);
                tree_bench_vm_close(&machine);
                return 1;
            }
            if (round >= options.warmup)
            {
                vm_samples[round - options.warmup] = vm_elapsed;
                tree_samples[round - options.warmup] = tree_elapsed;
                /* The ratio of one round: the round where the machine was
                 * quietest for both is the fairest number. */
                ratios[round - options.warmup] =
                    vm_elapsed == 0U ? 0.0 : (double) tree_elapsed / (double) vm_elapsed;
            }
        }
        vm_steps = cnor_vm_get_steps(&machine.vm);
        tree_bench_summarise(vm_samples, options.iterations, &vm_median, &vm_min);
        tree_bench_summarise(tree_samples, options.iterations, &tree_median, &tree_min);
        best_ratio = ratios[0];
        for (round = 1U; round < options.iterations; ++round)
        {
            if (ratios[round] < best_ratio)
            {
                best_ratio = ratios[round];
            }
        }
        printf("%-8s %11.3f %11.3f %7.1fx %11.2f %11.2f %11llu %11llu\n", item->name,
               (double) vm_median / 1e6, (double) tree_median / 1e6, best_ratio,
               (double) vm_min / (double) item->ops, (double) tree_min / (double) item->ops,
               (unsigned long long) vm_steps, (unsigned long long) tree_nodes);
        tree_program_free(program);
        tree_bench_vm_close(&machine);
    }
    return 0;
}
