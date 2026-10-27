/* Fixed-seed mutation smoke test, also usable without Clang or sanitizers. */
#include <cnor/image/writer.h>
#include <cnor/op.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
static void test_input(const unsigned char *data, size_t size)
{
    const char *path = getenv("CNOR_FUZZ_REPRO");
    if (path)
    {
        FILE *file = fopen(path, "wb");
        if (!file) abort();
        if (fwrite(data, 1, size, file) != size) abort();
        if (fclose(file)) abort();
    }
    LLVMFuzzerTestOneInput(data, size);
}
static uint32_t random_state = 0x434e4f52U;
static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static int exercise(const unsigned char *seed, size_t size)
{
    unsigned char *data = malloc(size + 16U);
    size_t i, at;
    unsigned bit, round, change;
    if (!data) return 1;
    test_input(seed, size);
    for (i = 0; i < size; ++i)
    {
        /* Every prefix, and every one-bit mutation in every byte. */
        test_input(seed, i);
        for (bit = 0; bit < 8U; ++bit)
        {
            memcpy(data, seed, size);
            data[i] ^= (unsigned char) (1U << bit);
            test_input(data, size);
        }
        /* Boundary bytes target counts, offsets, opcodes and operands. */
        memcpy(data, seed, size);
        data[i] = 0xffU;
        test_input(data, size);
        data[i] = 0U;
        test_input(data, size);
    }
    for (round = 0; round < 10000U; ++round)
    {
        memcpy(data, seed, size);
        for (change = 0; change < 1U + round % 4U; ++change)
        {
            at = next_random() % size;
            data[at] = (unsigned char) next_random();
        }
        test_input(data, size);
    }
    memcpy(data, seed, size);
    memset(data + size, 0xff, 16U);
    for (i = 1; i <= 16U; ++i) test_input(data, size + i);
    free(data);
    printf("mutation checks passed: seed_bytes=%zu random_rounds=10000\n", size);
    return 0;
}

int main(int argc, char **argv)
{
    cnor_byte_writer_t code, consts;
    cnor_image_parts_t parts;
    cnor_image_function_t function;
    cnor_image_t image;
    int result;
    FILE *file;
    if (argc == 2)
    {
        unsigned char *data;
        long size;
        file = fopen(argv[1], "rb");
        if (!file) return 2;
        if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0
            || size > 1024 * 1024 || fseek(file, 0, SEEK_SET))
        { fclose(file); return 2; }
        data = malloc((size_t) size);
        if (!data) { fclose(file); return 2; }
        if (fread(data, 1, (size_t) size, file) != (size_t) size)
        { fclose(file); free(data); return 2; }
        fclose(file);
        result = exercise(data, (size_t) size);
        free(data);
        return result;
    }
    if (argc != 1 && !(argc == 3 && strcmp(argv[1], "--seed") == 0))
    { fprintf(stderr, "usage: cnor_image_mutate [image.cnb | --seed output.cnb]\n"); return 2; }
    cnor_byte_writer_init(&code);
    cnor_byte_writer_init(&consts);
    cnor_image_init(&image);
    memset(&parts, 0, sizeof(parts));
    memset(&function, 0, sizeof(function));
    cnor_byte_writer_put_u8(&code, cnor_op_push_none);
    cnor_byte_writer_put_u8(&code, cnor_op_ret);
    function.size = cnor_byte_writer_get_size(&code);
    function.max_stack = 1U;
    parts.code = &code;
    parts.consts = &consts;
    parts.functions = &function;
    parts.function_count = 1U;
    parts.stack_slots = 1U;
    if (!cnor_image_write(&parts, &image)) return 1;
    if (argc == 3)
    {
        file = fopen(argv[2], "wb");
        if (!file) return 2;
        result = fwrite(image.data, 1, image.size, file) == image.size ? 0 : 2;
        if (fclose(file)) result = 2;
    }
    else result = exercise(image.data, image.size);
    cnor_image_deinit(&image);
    cnor_byte_writer_deinit(&consts);
    cnor_byte_writer_deinit(&code);
    return result;
}
