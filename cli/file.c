#include "file.h"

#include <lh/fs/file/mode.h>
#include <lh/null.h>
#include <lh/os/fs/file.h>
#include <lh/runtime/allocator.h>
#include <stdio.h>

#if defined(_WIN32)
#    include <fcntl.h>
#    include <io.h>
#endif

#define CNOR_CLI_FILE_CHUNK 65536U

/** @brief Reads up to @p size bytes from @p source: the count, 0 at the end, < 0 on failure. */
typedef lh_ssize_t (*cnor_cli_read_fn_t)(void *source, lh_byte_t *to, lh_usize_t size);

/* -- buffer ---------------------------------------------------------------- */

void
cnor_cli_buffer_deinit(cnor_cli_buffer_t *self)
{
    if (self->data != lh_null)
    {
        lh_runtime_allocator_free(self->data);
    }
    self->data = lh_null;
    self->size = 0U;
}

/* Room for at least @p more bytes after the content; @p capacity follows. */
static lh_bool_t
cnor_cli_buffer_reserve(cnor_cli_buffer_t *self, lh_usize_t *capacity, lh_usize_t more)
{
    lh_usize_t wanted = *capacity;
    lh_byte_t *data;

    if (self->size + more <= *capacity)
    {
        return lh_bool_true;
    }
    while (wanted < self->size + more)
    {
        wanted = wanted == 0U ? CNOR_CLI_FILE_CHUNK : wanted * 2U;
    }
    data = self->data == lh_null
               ? (lh_byte_t *) lh_runtime_allocator_alloc(wanted)
               : (lh_byte_t *) lh_runtime_allocator_realloc(self->data, *capacity, wanted);
    if (data == lh_null)
    {
        return lh_bool_false;
    }
    self->data = data;
    *capacity = wanted;
    return lh_bool_true;
}

/* -- reading --------------------------------------------------------------- */

/* Read with @p read_fn until it reports the end (0) or a failure (< 0). */
static lh_bool_t
cnor_cli_file_read_all(cnor_cli_buffer_t *out, cnor_cli_read_fn_t read_fn, void *source)
{
    lh_usize_t capacity = 0U;

    for (;;)
    {
        lh_ssize_t got;

        if (out->size >= CNOR_CLI_FILE_MAX_SIZE
            || !cnor_cli_buffer_reserve(out, &capacity, CNOR_CLI_FILE_CHUNK))
        {
            return lh_bool_false;
        }
        got = read_fn(source, out->data + out->size, CNOR_CLI_FILE_CHUNK);
        if (got < 0)
        {
            return lh_bool_false;
        }
        if (got == 0)
        {
            return out->size <= CNOR_CLI_FILE_MAX_SIZE;
        }
        out->size += (lh_usize_t) got;
    }
}

static lh_ssize_t
cnor_cli_file_read_stdin(void *source, lh_byte_t *to, lh_usize_t size)
{
    const lh_usize_t got = fread(to, 1U, size, (FILE *) source);

    if (got == 0U && ferror((FILE *) source))
    {
        return -1;
    }
    return (lh_ssize_t) got;
}

static lh_ssize_t
cnor_cli_file_read_file(void *source, lh_byte_t *to, lh_usize_t size)
{
    return lh_os_fs_file_read(source, to, size);
}

lh_bool_t
cnor_cli_file_is_std(const lh_char_t *path)
{
    return path[0] == '-' && path[1] == '\0' ? lh_bool_true : lh_bool_false;
}

cnor_cli_file_status_t
cnor_cli_file_load(const lh_char_t *path, cnor_cli_buffer_t *out)
{
    cnor_cli_file_status_t status = cnor_cli_file_ok;
    lh_os_fs_file_t file;

    out->data = lh_null;
    out->size = 0U;
    lh_os_fs_file_init(&file);
    if (!lh_os_fs_file_open_cstr(&file, path, lh_fs_file_mode_read))
    {
        status = cnor_cli_file_cannot_open;
    }
    else if (!cnor_cli_file_read_all(out, cnor_cli_file_read_file, &file))
    {
        status = cnor_cli_file_cannot_read;
        cnor_cli_buffer_deinit(out);
    }
    lh_os_fs_file_deinit(&file);
    return status;
}

lh_bool_t
cnor_cli_file_read(const lh_char_t *path, cnor_cli_buffer_t *out)
{
    lh_bool_t ok;

    if (!cnor_cli_file_is_std(path))
    {
        return cnor_cli_file_load(path, out) == cnor_cli_file_ok ? lh_bool_true : lh_bool_false;
    }
    out->data = lh_null;
    out->size = 0U;
#if defined(_WIN32)
    (void) _setmode(_fileno(stdin), _O_BINARY);
#endif
    ok = cnor_cli_file_read_all(out, cnor_cli_file_read_stdin, stdin);
    if (!ok)
    {
        cnor_cli_buffer_deinit(out);
    }
    return ok;
}

/* -- writing --------------------------------------------------------------- */

lh_bool_t
cnor_cli_file_write(const lh_char_t *path, const lh_byte_t *data, lh_usize_t size)
{
    lh_os_fs_file_t file;
    lh_bool_t ok;

    if (cnor_cli_file_is_std(path))
    {
#if defined(_WIN32)
        (void) _setmode(_fileno(stdout), _O_BINARY);
#endif
        return fwrite(data, 1U, size, stdout) == size && fflush(stdout) == 0 ? lh_bool_true
                                                                             : lh_bool_false;
    }
    lh_os_fs_file_init(&file);
    ok = lh_os_fs_file_open_cstr(&file, path, lh_fs_file_mode_write)
         && lh_os_fs_file_write_all(&file, (const lh_ptr) data, size);
    lh_os_fs_file_deinit(&file);
    return ok;
}
