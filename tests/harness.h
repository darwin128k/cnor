/**
 * @file harness.h
 * @brief A tiny test harness: plain C, one executable per suite, no framework.
 *
 * A suite lists its cases with ::CNOR_TEST_CASE and hands them to
 * ::cnor_test_main. A failed check prints its location and marks the case
 * failed, then the case carries on, so one run reports every broken check. The
 * process exits non-zero when any case failed, which is what CTest reads.
 *
 * `suite NAME` on the command line runs only the case called NAME.
 */

#ifndef CNOR_TESTS_HARNESS_H
#define CNOR_TESTS_HARNESS_H

#include <stdio.h>
#include <string.h>

/** @brief One test case: a name and a function. */
struct cnor_test_case
{
    const char *name;   /**< Printed in the report. */
    void (*fn)(void);   /**< The case itself. */
};
/** @copybrief cnor_test_case */
typedef struct cnor_test_case cnor_test_case_t;

/** @brief Checks failed in the case being run. */
static int g_cnor_test_failures;

/** @brief Define a case. */
#define CNOR_TEST(name) static void name(void)

/** @brief Entry of a case table. */
#define CNOR_TEST_CASE(name) { #name, name }

/** @brief Record a failure at the caller's location. */
static void
cnor_test_fail(const char *file, int line, const char *what)
{
    g_cnor_test_failures++;
    printf("    %s:%d: check failed: %s\n", file, line, what);
}

/** @brief Fail unless @p cond holds. */
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            cnor_test_fail(__FILE__, __LINE__, #cond);                                             \
        }                                                                                          \
    } while (0)

/** @brief Fail unless two integers are equal; prints both. */
#define CHECK_EQ(a, b)                                                                             \
    do                                                                                             \
    {                                                                                              \
        const long long cnor_test_a = (long long) (a);                                             \
        const long long cnor_test_b = (long long) (b);                                             \
        if (cnor_test_a != cnor_test_b)                                                            \
        {                                                                                          \
            cnor_test_fail(__FILE__, __LINE__, #a " == " #b);                                      \
            printf("      left %lld, right %lld\n", cnor_test_a, cnor_test_b);                    \
        }                                                                                          \
    } while (0)

/** @brief Fail and leave the case unless @p cond holds. */
#define REQUIRE(cond)                                                                              \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            cnor_test_fail(__FILE__, __LINE__, #cond);                                             \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/** @brief Run @p cases (or the one named in argv[1]); 0 when all passed. */
static int
cnor_test_main(const char *suite, const cnor_test_case_t *cases, size_t count, int argc,
               char **argv)
{
    size_t passed = 0U;
    size_t run = 0U;
    size_t i;

    for (i = 0U; i < count; ++i)
    {
        if (argc > 1 && strcmp(argv[1], cases[i].name) != 0)
        {
            continue;
        }
        g_cnor_test_failures = 0;
        cases[i].fn();
        run++;
        if (g_cnor_test_failures == 0)
        {
            passed++;
            printf("[ ok ] %s.%s\n", suite, cases[i].name);
        }
        else
        {
            printf("[FAIL] %s.%s\n", suite, cases[i].name);
        }
    }
    printf("%s: %u/%u passed\n", suite, (unsigned) passed, (unsigned) run);
    return (run > 0U && passed == run) ? 0 : 1;
}

/** @brief Size of a case table. */
#define CNOR_TEST_COUNT(cases) (sizeof(cases) / sizeof((cases)[0]))

#endif /* CNOR_TESTS_HARNESS_H */
