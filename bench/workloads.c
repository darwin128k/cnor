#include "workloads.h"

#include <lh/value.h>
#include <stdio.h>
#include <stdlib.h>

/* -- natives --------------------------------------------------------------- */

/* absval(x): |x| of an integer.
 *
 * The fields of a value are read directly, not through lh's accessors: those
 * are ordinary functions in another library, and a host that calls a native
 * in a loop pays a call per field. This is how a native should be written. */
static void
cnor_bench_native_absval(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
                         cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    /* The argument is read before the result is written: the result shares the
     * slot of the arguments. */
    switch (args[0].kind)
    {
    case lh_value_kind_sint:
    {
        const lh_sllong_t x = args[0].data.i;

        result->kind = lh_value_kind_sint;
        result->data.i = x < 0 ? -x : x;
        return;
    }
    case lh_value_kind_uint:
    {
        const lh_ullong_t x = args[0].data.u;

        result->kind = lh_value_kind_uint;
        result->data.u = x;
        return;
    }
    default:
        *error = cnor_error_type;
        *result = lh_value_make_none();
        return;
    }
}

static const cnor_native_t m_natives[] = {
    cnor_native_initializer("absval", cnor_bench_native_absval, 1U, 1U),
};

static const cnor_native_table_t m_native_table =
    cnor_native_table_initializer(m_natives, sizeof(m_natives) / sizeof(m_natives[0]));

const cnor_native_table_t *
cnor_bench_natives(void)
{
    return &m_native_table;
}

/* -- programs -------------------------------------------------------------- */

const char cnor_bench_fib[] = "fn fib(n: i32) -> i32:\n"
                             "    if n < 2:\n"
                             "        return n\n"
                             "    return fib(n - 1) + fib(n - 2)\n"
                             "fib(25)\n";

const char cnor_bench_loop[] = "fn sum(n: i32) -> i32:\n"
                              "    let mut s = 0\n"
                              "    let mut i = 0\n"
                              "    while i < n:\n"
                              "        s = s + i\n"
                              "        i = i + 1\n"
                              "    return s\n"
                              "sum(10000000)\n";

const char cnor_bench_nested[] = "fn nested(n: i32) -> i32:\n"
                                "    let mut s = 0\n"
                                "    for i in 0..n:\n"
                                "        for j in 0..n:\n"
                                "            s = s + (i * j) % 7\n"
                                "    return s\n"
                                "nested(1000)\n";

static const char m_text[] = "fn scan(text: str, rounds: i32) -> i32:\n"
                             "    let mut hits = 0\n"
                             "    let mut joined = 0\n"
                             "    let n = len(text)\n"
                             "    for r in 0..rounds:\n"
                             "        for i in 0..n:\n"
                             "            if text[i] == \"o\":\n"
                             "                hits = hits + 1\n"
                             "        let pair = text[r % n] + text[(r + 1) % n]\n"
                             "        joined = joined + len(pair)\n"
                             "    return hits + joined\n"
                             "scan(\"the quick brown fox jumps over the lazy dog\", 20000)\n";

static const char m_native[] = "fn calls(n: i32) -> i32:\n"
                               "    let mut s = 0\n"
                               "    for i in 0..n:\n"
                               "        s = s + absval(i - 500000)\n"
                               "    return s\n"
                               "calls(1000000)\n";

static const char m_match[] = "fn classify(n: i32) -> i32:\n"
                              "    let mut s = 0\n"
                              "    for i in 0..n:\n"
                              "        match i % 8:\n"
                              "            0 => s = s + 1\n"
                              "            1 => s = s + 3\n"
                              "            2 => s = s - 2\n"
                              "            3 => s = s + 5\n"
                              "            4 => s = s * 1\n"
                              "            5 => s = s + 7\n"
                              "            _ => s = s + 0\n"
                              "    return s\n"
                              "classify(1000000)\n";

static const char m_dict[] = "fn lookups(rounds: i32) -> i32:\n"
                             "    let mut settings = dict(8)\n"
                             "    settings[\"hp\"] = 100\n"
                             "    settings[\"armor\"] = 25\n"
                             "    settings[\"name\"] = \"Egor\"\n"
                             "    settings[\"weapon\"] = \"ak47\"\n"
                             "    settings[\"flags\"] = 7\n"
                             "    let mut total = 0\n"
                             "    for r in 0..rounds:\n"
                             "        total = total + settings[\"hp\"] + settings[\"armor\"] + settings[\"flags\"]\n"
                             "    return total\n"
                             "lookups(1000000)\n";

static const char m_try[] = "fn guarded(rounds: i32) -> i32:\n"
                            "    let mut total = 0\n"
                            "    for r in 0..rounds:\n"
                            "        try:\n"
                            "            total = total + r\n"
                            "        catch e:\n"
                            "            total = total + 1000\n"
                            "    return total\n"
                            "guarded(1000000)\n";

static const cnor_bench_workload_t m_workloads[] = {
    {"fib", "recursive fib(25)", cnor_bench_fib, 75025, 242785U, "call"},
    {"loop", "while loop summing 0..10M", cnor_bench_loop, 49999995000000LL, 10000000U,
     "iteration"},
    {"nested", "1000 x 1000 nested for, (i*j)%7", cnor_bench_nested, 2570569, 1000000U,
     "iteration"},
    {"text", "index/compare 43-char text 20000x, join+len", m_text, 120000, 860000U, "char"},
    {"native", "1M native calls absval(i - 500000)", m_native, 250000000000LL, 1000000U, "call"},
    {"match", "1M iterations of a 7-arm match", m_match, 1750000, 1000000U, "iteration"},
    {"dict", "1M rounds of three dictionary lookups", m_dict, 132000000, 3000000U, "lookup"},
    {"try", "1M rounds of a try that does not fail", m_try, 499999500000LL, 1000000U, "iteration"},
    {"compile", "generated program, 1000 functions", lh_null, 0, 0U, "line"},
};

const cnor_bench_workload_t *
cnor_bench_workloads(size_t *count)
{
    *count = sizeof(m_workloads) / sizeof(m_workloads[0]);
    return m_workloads;
}

/* -- the generated program ------------------------------------------------- */

static const char m_fn_template[] = "fn f%u(a: i32, b: i32) -> i32:\n"
                                    "    let mut x = a + b * %u\n"
                                    "    if x > 100:\n"
                                    "        x = x - 7\n"
                                    "    else:\n"
                                    "        x = x + 3\n"
                                    "    while x > 50:\n"
                                    "        x = x / 2\n"
                                    "    match x %% 3:\n"
                                    "        0 => x = x + 1\n"
                                    "        1 => x = x - 1\n"
                                    "        _ => x = x + 2\n"
                                    "    return x\n";
#define CNOR_BENCH_FN_LINES 13U

/* What f<k>(k, 3) returns, computed the same way in C. */
static int64_t
cnor_bench_generated_fn(int64_t k)
{
    int64_t x = k + 3 * k;

    x = x > 100 ? x - 7 : x + 3;
    while (x > 50)
    {
        x /= 2;
    }
    switch (x % 3)
    {
    case 0:
        return x + 1;
    case 1:
        return x - 1;
    default:
        return x + 2;
    }
}

char *
cnor_bench_generated_source(unsigned functions, int64_t *expected, uint64_t *lines)
{
    const size_t capacity = (size_t) functions * (sizeof(m_fn_template) + 64U) + 64U;
    char *out = (char *) malloc(capacity);
    size_t used = 0U;
    unsigned k;

    if (out == NULL)
    {
        return NULL;
    }
    *expected = 0;
    *lines = 0U;
    for (k = 0U; k < functions; ++k)
    {
        used += (size_t) snprintf(out + used, capacity - used, m_fn_template, k, k);
        *lines += CNOR_BENCH_FN_LINES;
    }
    used += (size_t) snprintf(out + used, capacity - used, "let mut total = 0\n");
    *lines += 1U;
    for (k = 0U; k < functions; ++k)
    {
        used += (size_t) snprintf(out + used, capacity - used, "total = total + f%u(%u, 3)\n", k, k);
        *expected += cnor_bench_generated_fn((int64_t) k);
        *lines += 1U;
    }
    (void) snprintf(out + used, capacity - used, "total\n");
    *lines += 1U;
    return out;
}
