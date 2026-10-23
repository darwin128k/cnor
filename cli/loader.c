#include "loader.h"

#include <lh/initializer.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>
#include <lh/str/view/initializer.h>
#include <string.h>

/* The longest import path the file system is asked for. */
#define CNOR_CLI_LOADER_PATH_CAPACITY 4096U

/* -- text ------------------------------------------------------------------ */

lh_str_view_t
cnor_cli_loader_text(const cnor_cli_buffer_t *buffer)
{
    static const lh_byte_t bom[3] = {0xEFU, 0xBBU, 0xBFU};
    lh_usize_t skip = 0U;

    /* A UTF-8 byte order mark is not part of the program. */
    if (buffer->size >= 3U && memcmp(buffer->data, bom, 3U) == 0)
    {
        skip = 3U;
    }
    return lh_initializer_of(lh_str_view_t, lh_str_view_initializer,
                             (const lh_char_t *) buffer->data + skip, buffer->size - skip);
}

/* -- loading --------------------------------------------------------------- */

/* A file that cannot be opened is not there, and the compiler tries the next
 * search path; one that opens but cannot be read stops the compilation. */
static cnor_error_t
cnor_cli_loader_load(lh_ptr ctx, lh_str_view_t path, cnor_source_t *out)
{
    lh_char_t name[CNOR_CLI_LOADER_PATH_CAPACITY];
    const lh_usize_t size = lh_str_view_get_size(&path);
    cnor_cli_buffer_t buffer;

    (void) ctx;
    if (size >= sizeof(name))
    {
        return cnor_error_no_such_module;
    }
    memcpy(name, lh_str_view_get_data(&path), size);
    name[size] = '\0';
    switch (cnor_cli_file_load(name, &buffer))
    {
    case cnor_cli_file_ok:
        break;
    case cnor_cli_file_cannot_open:
        return cnor_error_no_such_module;
    default:
        return cnor_error_bad_module;
    }
    out->text = cnor_cli_loader_text(&buffer);
    out->handle = buffer.data;
    return cnor_error_none;
}

static void
cnor_cli_loader_release(lh_ptr ctx, cnor_source_t *source)
{
    (void) ctx;
    if (source->handle != lh_null)
    {
        lh_runtime_allocator_free(source->handle);
    }
    source->handle = lh_null;
}

void
cnor_cli_loader_configure(cnor_cli_loader_t *self, const cnor_cli_options_t *options,
                          cnor_compiler_config_t *config)
{
    lh_u32_t i;

    for (i = 0U; i < options->import_path_count; ++i)
    {
        lh_str_view_init(&self->search_paths[i], options->import_paths[i]);
    }
    config->load = cnor_cli_loader_load;
    config->release = cnor_cli_loader_release;
    config->load_ctx = lh_null;
    config->search_paths = self->search_paths;
    config->search_path_count = options->import_path_count;
}
