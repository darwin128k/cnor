/**
 * @file clock.h
 * @brief A monotonic clock in nanoseconds for the benchmark runner.
 *
 * @note Candidate lh brick: lh_os_tick_us has microsecond resolution, which is
 *       too coarse for runs of a few microseconds.
 */

#ifndef CNOR_BENCH_CLOCK_H
#define CNOR_BENCH_CLOCK_H

#include <stdint.h>

/** @brief Nanoseconds since an arbitrary fixed point; never goes backwards. */
uint64_t
cnor_bench_clock_ns(void);

#endif /* CNOR_BENCH_CLOCK_H */
