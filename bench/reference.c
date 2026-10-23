#include "reference.h"

int64_t
cnor_bench_ref_fib(int64_t n)
{
    if (n < 2)
    {
        return n;
    }
    return cnor_bench_ref_fib(n - 1) + cnor_bench_ref_fib(n - 2);
}

int64_t
cnor_bench_ref_loop(int64_t n)
{
    int64_t s = 0;
    int64_t i;

    for (i = 0; i < n; ++i)
    {
        s += i;
#if defined(__GNUC__)
        /* Keep it a loop: without this GCC replaces it with n * (n - 1) / 2. */
        __asm__ volatile("" : "+r"(s));
#endif
    }
    return s;
}
