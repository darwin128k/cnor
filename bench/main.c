/**
 * @file main.c
 * @brief The benchmark runner: compile, load and run every workload, report
 *        median and minimum times, ns per unit of work and VM instructions per
 *        second, then the C references.
 *
 * Usage: cnor_bench [--iterations N] [--warmup N] [--only NAME] [--csv]
 *
 * Every phase is timed separately: lex + parse + compile
 * (::cnor_compiler_compile_source), load (copy, verify, bind) and run
 * (::cnor_vm_reset then ::cnor_vm_run, budget off). Warmup rounds are run and
 * discarded first. Every run's result is checked.
 */

#include "clock.h"
#include "reference.h"
#include "workloads.h"

#include <cnor/compiler.h>
#include <cnor/error.h>
#include <cnor/vm.h>
#include <lh/str/view.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CNOR_BENCH_MAX_ITERATIONS 1000U
#define CNOR_BENCH_GENERATED_FUNCTIONS 1000U
#define CNOR_BENCH_STR_POOL_SIZE (1024U * 1024U)

/* -- options --------------------------------------------------------------- */

struct cnor_bench_options
{
    unsigned iterations;
    unsigned warmup;
    const char *only;
    int csv;
};
typedef struct cnor_bench_options cnor_bench_options_t;

static int
cnor_bench_parse_options(int argc, char **argv, cnor_bench_options_t *out)
{
    int i;

    out->iterations = 10U;
    out->warmup = 2U;
    out->only = NULL;
    out->csv = 0;
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
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            out->only = argv[++i];
        }
        else if (strcmp(argv[i], "--csv") == 0)
        {
            out->csv = 1;
        }
        else
        {
            fprintf(stderr, "usage: %s [--iterations N] [--warmup N] [--only NAME] [--csv]\n",
                    argv[0]);
            return 0;
        }
    }
    if (out->iterations == 0U || out->iterations > CNOR_BENCH_MAX_ITERATIONS)
    {
        fprintf(stderr, "--iterations must be 1..%u\n", CNOR_BENCH_MAX_ITERATIONS);
        return 0;
    }
    return 1;
}

/* -- samples --------------------------------------------------------------- */

struct cnor_bench_stats
{
    uint64_t median;
    uint64_t min;
};
typedef struct cnor_bench_stats cnor_bench_stats_t;

static int
cnor_bench_compare_u64(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *) a;
    const uint64_t y = *(const uint64_t *) b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

static cnor_bench_stats_t
cnor_bench_summarise(uint64_t *samples, unsigned count)
{
    cnor_bench_stats_t stats;

    qsort(samples, count, sizeof(samples[0]), cnor_bench_compare_u64);
    stats.min = samples[0];
    stats.median = count % 2U != 0U ? samples[count / 2U]
                                    : (samples[count / 2U - 1U] + samples[count / 2U]) / 2U;
    return stats;
}

/* -- phases ---------------------------------------------------------------- */

static int
cnor_bench_result_is(const lh_value_t *value, int64_t expected)
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

static void
cnor_bench_report_diag(const char *name, const char *phase, const cnor_diag_t *diag)
{
    fprintf(stderr, "%s: %s failed: %s (line %u)\n", name, phase,
            cnor_error_get_message(cnor_diag_get_code(diag)), (unsigned) cnor_diag_get_line(diag));
}

/* Compile @p source warmup + iterations times; keeps the last image. */
static int
cnor_bench_compile(const char *name, const char *source, const cnor_bench_options_t *options,
                   cnor_image_t *image, cnor_bench_stats_t *stats)
{
    uint64_t samples[CNOR_BENCH_MAX_ITERATIONS];
    cnor_compiler_config_t config = cnor_compiler_config_initializer();
    lh_str_view_t text;
    cnor_diag_t diag;
    unsigned round;

    config.natives = cnor_bench_natives();
    lh_str_view_init_by_size(&text, source, strlen(source));
    for (round = 0U; round < options->warmup + options->iterations; ++round)
    {
        const uint64_t start = cnor_bench_clock_ns();
        const lh_bool_t ok = cnor_compiler_compile_source(text, &config, image, &diag);
        const uint64_t elapsed = cnor_bench_clock_ns() - start;

        if (!ok)
        {
            cnor_bench_report_diag(name, "compile", &diag);
            return 0;
        }
        if (round >= options->warmup)
        {
            samples[round - options->warmup] = elapsed;
        }
        if (round + 1U < options->warmup + options->iterations)
        {
            cnor_image_deinit(image);
        }
    }
    *stats = cnor_bench_summarise(samples, options->iterations);
    return 1;
}

static void
cnor_bench_vm_config(cnor_vm_config_t *config)
{
    *config = (cnor_vm_config_t) cnor_vm_config_initializer();
    config->natives = cnor_bench_natives();
    config->str_pool_size = CNOR_BENCH_STR_POOL_SIZE;
    config->max_steps = 0U;
}

/* Load @p image warmup + iterations times into @p vm; it stays loaded. */
static int
cnor_bench_load(const char *name, const cnor_image_t *image, const cnor_bench_options_t *options,
                cnor_vm_t *vm, cnor_bench_stats_t *stats)
{
    uint64_t samples[CNOR_BENCH_MAX_ITERATIONS];
    unsigned round;

    for (round = 0U; round < options->warmup + options->iterations; ++round)
    {
        const uint64_t start = cnor_bench_clock_ns();
        const lh_bool_t ok = cnor_vm_load(vm, image->data, image->size);
        const uint64_t elapsed = cnor_bench_clock_ns() - start;

        if (!ok)
        {
            cnor_bench_report_diag(name, "load", cnor_vm_get_diag(vm));
            return 0;
        }
        if (round >= options->warmup)
        {
            samples[round - options->warmup] = elapsed;
        }
    }
    *stats = cnor_bench_summarise(samples, options->iterations);
    return 1;
}

static int
cnor_bench_run(const char *name, int64_t expected, const cnor_bench_options_t *options,
               cnor_vm_t *vm, cnor_bench_stats_t *stats, cnor_step_count_t *steps)
{
    uint64_t samples[CNOR_BENCH_MAX_ITERATIONS];
    unsigned round;

    for (round = 0U; round < options->warmup + options->iterations; ++round)
    {
        uint64_t start;
        uint64_t elapsed;
        lh_bool_t ok;
        lh_value_t result;

        cnor_vm_reset(vm);
        start = cnor_bench_clock_ns();
        ok = cnor_vm_run(vm);
        elapsed = cnor_bench_clock_ns() - start;
        if (!ok)
        {
            cnor_bench_report_diag(name, "run", cnor_vm_get_diag(vm));
            return 0;
        }
        result = cnor_vm_get_result(vm);
        if (!cnor_bench_result_is(&result, expected))
        {
            fprintf(stderr, "%s: wrong result (expected %lld)\n", name, (long long) expected);
            return 0;
        }
        if (round >= options->warmup)
        {
            samples[round - options->warmup] = elapsed;
        }
    }
    *steps = cnor_vm_get_steps(vm);
    *stats = cnor_bench_summarise(samples, options->iterations);
    return 1;
}

/* -- report ---------------------------------------------------------------- */

#if defined(__VERSION__)
#    define CNOR_BENCH_COMPILER __VERSION__
#else
#    define CNOR_BENCH_COMPILER "unknown compiler"
#endif

#if defined(__OPTIMIZE__)
#    define CNOR_BENCH_OPTIMISED "optimised"
#else
#    define CNOR_BENCH_OPTIMISED "not known to be optimised: use a Release build"
#endif

static void
cnor_bench_print_header(const cnor_bench_options_t *options)
{
    if (options->csv)
    {
        printf("workload,compile_median_ns,compile_min_ns,load_median_ns,run_median_ns,"
               "run_min_ns,steps,steps_per_sec,ops,ns_per_op,op\n");
        return;
    }
    printf("cnor bench: %u iterations after %u warmup; run times are median (min)\n",
           options->iterations, options->warmup);
    printf("built with %s, %s\n\n", CNOR_BENCH_COMPILER, CNOR_BENCH_OPTIMISED);
    printf("%-8s %12s %10s %20s %12s %9s %12s\n", "workload", "compile us", "load us",
           "run ms", "steps", "Msteps/s", "ns/op");
}

/* ns per op is run time per unit of work, except for the compile workload,
 * where it is compile time per source line. */
static void
cnor_bench_print_row(const cnor_bench_options_t *options, const cnor_bench_workload_t *w,
                     const cnor_bench_stats_t *compile, const cnor_bench_stats_t *load,
                     const cnor_bench_stats_t *run, cnor_step_count_t steps, uint64_t ops,
                     const char *op_name, int per_line)
{
    const double run_s = (double) run->median / 1e9;
    const double steps_per_sec = run_s > 0.0 ? (double) steps / run_s : 0.0;
    const uint64_t per_op_time = per_line ? compile->median : run->median;
    const double ns_per_op = ops != 0U ? (double) per_op_time / (double) ops : 0.0;

    if (options->csv)
    {
        printf("%s,%llu,%llu,%llu,%llu,%llu,%lu,%.0f,%llu,%.2f,%s\n", w->name,
               (unsigned long long) compile->median, (unsigned long long) compile->min,
               (unsigned long long) load->median, (unsigned long long) run->median,
               (unsigned long long) run->min, (unsigned long) steps, steps_per_sec,
               (unsigned long long) ops, ns_per_op, op_name);
        return;
    }
    printf("%-8s %12.1f %10.1f %9.3f (%8.3f) %12lu %9.1f %8.2f/%s\n", w->name,
           (double) compile->median / 1e3, (double) load->median / 1e3,
           (double) run->median / 1e6, (double) run->min / 1e6, (unsigned long) steps,
           steps_per_sec / 1e6, ns_per_op, op_name);
}

/* -- C references ---------------------------------------------------------- */

typedef int64_t (*cnor_bench_ref_fn_t)(int64_t);

static void
cnor_bench_reference(const cnor_bench_options_t *options, const char *name,
                     cnor_bench_ref_fn_t fn, int64_t input, int64_t expected)
{
    uint64_t samples[CNOR_BENCH_MAX_ITERATIONS];
    volatile int64_t argument = input;
    cnor_bench_stats_t stats;
    unsigned round;

    for (round = 0U; round < options->warmup + options->iterations; ++round)
    {
        const uint64_t start = cnor_bench_clock_ns();
        const int64_t result = fn(argument);
        const uint64_t elapsed = cnor_bench_clock_ns() - start;

        if (result != expected)
        {
            fprintf(stderr, "C %s: wrong result\n", name);
            return;
        }
        if (round >= options->warmup)
        {
            samples[round - options->warmup] = elapsed;
        }
    }
    stats = cnor_bench_summarise(samples, options->iterations);
    if (options->csv)
    {
        printf("c_%s,0,0,0,%llu,%llu,0,0,0,0,\n", name, (unsigned long long) stats.median,
               (unsigned long long) stats.min);
        return;
    }
    printf("C %-6s %34.3f (%8.3f)\n", name, (double) stats.median / 1e6, (double) stats.min / 1e6);
}

/* -- main ------------------------------------------------------------------ */

static int
cnor_bench_one(const cnor_bench_options_t *options, const cnor_bench_workload_t *w)
{
    char *generated = NULL;
    const char *source = w->source;
    int64_t expected = w->expected;
    uint64_t ops = w->ops;
    cnor_bench_stats_t compile;
    cnor_bench_stats_t load;
    cnor_bench_stats_t run;
    cnor_step_count_t steps = 0U;
    cnor_image_t image;
    cnor_vm_config_t config;
    cnor_vm_t vm;
    int ok;

    if (source == NULL)
    {
        generated = cnor_bench_generated_source(CNOR_BENCH_GENERATED_FUNCTIONS, &expected, &ops);
        if (generated == NULL)
        {
            return 0;
        }
        source = generated;
    }
    ok = cnor_bench_compile(w->name, source, options, &image, &compile);
    if (ok)
    {
        cnor_bench_vm_config(&config);
        cnor_vm_init(&vm, &config);
        ok = cnor_bench_load(w->name, &image, options, &vm, &load)
             && cnor_bench_run(w->name, expected, options, &vm, &run, &steps);
        if (ok)
        {
            cnor_bench_print_row(options, w, &compile, &load, &run, steps, ops, w->op_name,
                                 generated != NULL);
        }
        cnor_vm_deinit(&vm);
        cnor_image_deinit(&image);
    }
    free(generated);
    return ok;
}

int
main(int argc, char **argv)
{
    cnor_bench_options_t options;
    const cnor_bench_workload_t *workloads;
    size_t count;
    size_t i;
    int failed = 0;

    if (!cnor_bench_parse_options(argc, argv, &options))
    {
        return 2;
    }
    workloads = cnor_bench_workloads(&count);
    cnor_bench_print_header(&options);
    for (i = 0U; i < count; ++i)
    {
        if (options.only != NULL && strcmp(options.only, workloads[i].name) != 0)
        {
            continue;
        }
        if (!cnor_bench_one(&options, &workloads[i]))
        {
            failed = 1;
        }
    }
    if (options.only == NULL || strcmp(options.only, "c") == 0)
    {
        if (!options.csv)
        {
            printf("\nC references (same work, run ms):\n");
        }
        cnor_bench_reference(&options, "fib", cnor_bench_ref_fib, 25, 75025);
        cnor_bench_reference(&options, "loop", cnor_bench_ref_loop, 10000000, 49999995000000LL);
    }
    return failed;
}
