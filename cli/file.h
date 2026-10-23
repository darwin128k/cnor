/**
 * @file file.h
 * @brief Whole-file reads and writes for the command line (::cnor_cli_buffer_t).
 *
 * Files go through lh's file API (`lh/os/fs/file.h`), which takes UTF-8 paths
 * on every system (UTF-16 calls on Windows). The path `-` is the standard
 * input or output instead, in binary mode.
 */

#ifndef CNOR_CLI_FILE_H
#define CNOR_CLI_FILE_H

#include <lh/bool.h>
#include <lh/byte.h>
#include <lh/char.h>
#include <lh/size.h>

/**
 * @def CNOR_CLI_FILE_MAX_SIZE
 * @brief Largest file read: images and source offsets are 32-bit.
 */
#define CNOR_CLI_FILE_MAX_SIZE (256U * 1024U * 1024U)

/**
 * @struct cnor_cli_buffer
 * @brief Bytes read from a file, owned.
 */
struct cnor_cli_buffer
{
    lh_byte_t *data; /**< The bytes; runtime-allocator owned, or ::lh_null. */
    lh_usize_t size; /**< Their number. */
};
/** @copybrief cnor_cli_buffer */
typedef struct cnor_cli_buffer cnor_cli_buffer_t;

/** @brief True when @p path names the standard stream (`-`). */
lh_bool_t
cnor_cli_file_is_std(const lh_char_t *path);

/**
 * @brief Read all of @p path (or the standard input) into @p out.
 * @return ::lh_bool_false when it cannot be opened or read, or is larger than
 *         ::CNOR_CLI_FILE_MAX_SIZE; @p out is then empty.
 */
lh_bool_t
cnor_cli_file_read(const lh_char_t *path, cnor_cli_buffer_t *out);

/**
 * @brief Create or truncate @p path (or use the standard output) and write
 *        @p size bytes.
 */
lh_bool_t
cnor_cli_file_write(const lh_char_t *path, const lh_byte_t *data, lh_usize_t size);

/** @brief Release the bytes; safe on an empty buffer and twice. */
void
cnor_cli_buffer_deinit(cnor_cli_buffer_t *self);

#endif /* CNOR_CLI_FILE_H */
