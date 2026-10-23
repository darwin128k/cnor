/**
 * @file workloads.h
 * @brief The programs the benchmark runs (::cnor_bench_workload_t).
 *
 * Every workload is a cnor source whose result is a known integer, so a run
 * that computes the wrong thing fails the benchmark instead of reporting a
 * fast wrong answer. The same programs in Lua and Python live in compare/.
 */

#ifndef CNOR_BENCH_WORKLOADS_H
#define CNOR_BENCH_WORKLOADS_H

#include <cnor/native.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @struct cnor_bench_workload
 * @brief One program and what to expect from it.
 */
struct cnor_bench_workload
{
    const char *name;     /**< Short name, also the compare/ script name. */
    const char *what;     /**< One line for the report. */
    const char *source;   /**< cnor source; ::cnor_bench_generated_source for `compile`. */
    int64_t expected;     /**< The program's result. */
    uint64_t ops;         /**< Units of work in one run, for ns per op. */
    const char *op_name;  /**< What one unit is. */
};
/** @copybrief cnor_bench_workload */
typedef struct cnor_bench_workload cnor_bench_workload_t;

/** @brief The workloads, in report order. */
const cnor_bench_workload_t *
cnor_bench_workloads(size_t *count);

/** @brief The natives every workload may call. */
const cnor_native_table_t *
cnor_bench_natives(void);

/**
 * @brief Build the large generated program: @p functions functions with an
 *        if, a while and a match each, and a top level calling every one.
 * @param[out] expected Its result.
 * @param[out] lines    Source lines.
 * @return A malloc'ed, NUL-terminated source; the caller frees it.
 */
char *
cnor_bench_generated_source(unsigned functions, int64_t *expected, uint64_t *lines);

#endif /* CNOR_BENCH_WORKLOADS_H */
