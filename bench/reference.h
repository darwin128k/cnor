/**
 * @file reference.h
 * @brief The same work written in C, to give the interpreter a ratio.
 *
 * Both functions live in their own translation unit and take their input at
 * run time, so the compiler cannot fold them into constants.
 */

#ifndef CNOR_BENCH_REFERENCE_H
#define CNOR_BENCH_REFERENCE_H

#include <stdint.h>

/** @brief Naive recursive Fibonacci, as in the `fib` workload. */
int64_t
cnor_bench_ref_fib(int64_t n);

/** @brief 0 + 1 + ... + (n - 1) one step at a time, as in the `loop` workload. */
int64_t
cnor_bench_ref_loop(int64_t n);

#endif /* CNOR_BENCH_REFERENCE_H */
