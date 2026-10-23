#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "clock.h"

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>

uint64_t
cnor_bench_clock_ns(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;

    if (frequency.QuadPart == 0)
    {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&now);
    /* Split to keep the product from overflowing for long uptimes. */
    return (uint64_t) (now.QuadPart / frequency.QuadPart) * 1000000000U
           + (uint64_t) (now.QuadPart % frequency.QuadPart) * 1000000000U
                 / (uint64_t) frequency.QuadPart;
}
#else
#    include <time.h>

uint64_t
cnor_bench_clock_ns(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t) now.tv_sec * 1000000000U + (uint64_t) now.tv_nsec;
}
#endif
