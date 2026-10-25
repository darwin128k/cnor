/* Benchmark harness for Pawn (the AMX abstract machine).
 *
 * Loads a compiled .amx, runs it `--iterations` times after `--warmup` runs
 * and prints "median_ns min_ns result", like the Lua harness does, so the
 * three can be compared with the same script.
 *
 * Only the execution is timed: the program is re-initialised from a pristine
 * copy of the file for every round, and the copy, amx_Init() and the natives
 * are registered outside the measured window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "amx.h"
#include "amxaux.h"

#if defined(_WIN32)
#    include <windows.h>
static double
now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;

    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    return (double) counter.QuadPart * 1000.0 / (double) freq.QuadPart;
}
#else
#    include <sys/time.h>
static double
now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec * 1000.0 + (double) tv.tv_usec / 1000.0;
}
#endif

/* absval(value): |value|, the native the cnor benchmark calls.
 * params[0] is the number of arguments, the arguments start at params[1]. */
static cell AMXAPI
n_absval(AMX *amx, const cell *params)
{
    (void) amx;
    {
        cell value = params[1];

        return value < 0 ? -value : value;
    }
}

/* strlen(text): the length of a string. Pawn 4's unpacked strings keep one
 * character per cell, so the length is counted in cells, not in bytes. The
 * argument is an address in the machine's memory: on a 64-bit host with
 * 32-bit cells it is an offset, and amx_Address turns it into a pointer. */
static cell AMXAPI
n_strlen(AMX *amx, const cell *params)
{
    const cell *text = (const cell *) amx_Address(amx, params[1]);
    cell length = 0;

    while (text[length] != 0)
    {
        ++length;
    }
    return length;
}

/* -- loading --------------------------------------------------------------- */

struct program
{
    unsigned char *bytes;
    size_t size;
};

static int
program_load(struct program *program, const char *path)
{
    FILE *file = fopen(path, "rb");
    AMX_HEADER header;
    size_t size;
    unsigned char *bytes;

    if (file == NULL)
    {
        fprintf(stderr, "pawnbench: cannot open %s\n", path);
        return 0;
    }
    if (fread(&header, sizeof header, 1, file) != 1)
    {
        fprintf(stderr, "pawnbench: %s is not an AMX file\n", path);
        fclose(file);
        return 0;
    }
    amx_Align16(&header.magic);
    amx_Align16((uint16_t *) &header.flags);
    amx_Align32((uint32_t *) &header.size);
    amx_Align32((uint32_t *) &header.cod);
    amx_Align32((uint32_t *) &header.dat);
    amx_Align32((uint32_t *) &header.hea);
    amx_Align32((uint32_t *) &header.stp);
    if (header.magic != AMX_MAGIC)
    {
        fprintf(stderr, "pawnbench: %s is not an AMX file\n", path);
        fclose(file);
        return 0;
    }
    size = (size_t) header.stp;
    bytes = (unsigned char *) malloc(size);
    if (bytes == NULL)
    {
        fclose(file);
        return 0;
    }
    rewind(file);
    if (fread(bytes, 1, (size_t) header.size, file) != (size_t) header.size)
    {
        fprintf(stderr, "pawnbench: %s is truncated\n", path);
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    program->bytes = bytes;
    program->size = size;
    return 1;
}

static int
compare_double(const void *left, const void *right)
{
    const double a = *(const double *) left;
    const double b = *(const double *) right;

    return a < b ? -1 : (a > b ? 1 : 0);
}

/* -- main ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    const char *path;
    unsigned iterations = 5U;
    unsigned warmup = 1U;
    struct program program;
    AMX amx;
    cell result = 0;
    double *samples;
    unsigned round;
    unsigned i;
    int error;

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s FILE.amx [iterations] [warmup]\n", argv[0]);
        return 2;
    }
    path = argv[1];
    if (argc > 2)
    {
        iterations = (unsigned) strtoul(argv[2], NULL, 10);
    }
    if (argc > 3)
    {
        warmup = (unsigned) strtoul(argv[3], NULL, 10);
    }
    if (iterations == 0U)
    {
        fprintf(stderr, "pawnbench: iterations must be at least 1\n");
        return 2;
    }
    if (!program_load(&program, path))
    {
        return 2;
    }
    samples = (double *) malloc(sizeof(double) * iterations);
    if (samples == NULL)
    {
        free(program.bytes);
        return 2;
    }
    for (round = 0U; round < warmup + iterations; ++round)
    {
        unsigned char *copy = (unsigned char *) malloc(program.size);
        double start;
        double elapsed;

        if (copy == NULL)
        {
            break;
        }
        memcpy(copy, program.bytes, program.size);
        memset(&amx, 0, sizeof amx);
        error = amx_Init(&amx, copy);
        if (error != AMX_ERR_NONE)
        {
            fprintf(stderr, "pawnbench: amx_Init failed: %d\n", error);
            free(copy);
            break;
        }
        amx_RegisterFunc(&amx, "absval", n_absval);
        amx_RegisterFunc(&amx, "strlen", n_strlen);
        start = now_ms();
        error = amx_Exec(&amx, &result, AMX_EXEC_MAIN);
        elapsed = now_ms() - start;
        if (error != AMX_ERR_NONE)
        {
            fprintf(stderr, "pawnbench: run failed: %d\n", error);
            amx_Cleanup(&amx);
            free(copy);
            break;
        }
        if (round >= warmup)
        {
            samples[round - warmup] = elapsed;
        }
        amx_Cleanup(&amx);
        free(copy);
    }
    qsort(samples, iterations, sizeof(double), compare_double);
    printf("%lld %lld %ld\n", (long long) (samples[iterations / 2U] * 1000000.0),
           (long long) (samples[0] * 1000000.0), (long) result);
    for (i = 0U; i < 0U; ++i)
    {
        /* keeps the compiler from warning about the unused loop */
    }
    free(samples);
    free(program.bytes);
    return 0;
}
