#include "command.h"

#include "file.h"
#include "natives.h"
#include "text.h"

#include <cnor/compiler.h>
#include <cnor/diag.h>
#include <cnor/error.h>
#include <cnor/image.h>
#include <cnor/vm.h>
#include <lh/initializer.h>
#include <lh/null.h>
#include <lh/str/view.h>
#include <lh/str/view/initializer.h>
#include <stdio.h>
#include <string.h>

/* What diagnostics call the standard input. */
#define CNOR_CLI_STDIN_NAME "<stdin>"

/* The longest output path `compile` derives from its input. */
#define CNOR_CLI_PATH_CAPACITY 4096U

/* -- diagnostics ----------------------------------------------------------- */

/* The name diagnostics use for @p path. */
static const lh_char_t *
cnor_cli_display_name(const lh_char_t *path)
{
    return cnor_cli_file_is_std(path) ? CNOR_CLI_STDIN_NAME : path;
}

static lh_str_view_t
cnor_cli_view(const lh_char_t *text)
{
    lh_str_view_t view;

    lh_str_view_init(&view, text);
    return view;
}

/* `file:line: message` (`file: message` without a line), then `: name`. */
static void
cnor_cli_report(lh_str_view_t file, const cnor_diag_t *diag)
{
    const lh_str_view_t name = cnor_diag_get_name(diag);
    const cnor_line_t line = cnor_diag_get_line(diag);

    (void) fflush(stdout);
    (void) fwrite(lh_str_view_get_data(&file), 1U, lh_str_view_get_size(&file), stderr);
    if (line != 0U)
    {
        fprintf(stderr, ":%lu", (unsigned long) line);
    }
    fprintf(stderr, ": %s", cnor_error_get_message(cnor_diag_get_code(diag)));
    if (lh_str_view_get_size(&name) > 0U)
    {
        fputs(": ", stderr);
        (void) fwrite(lh_str_view_get_data(&name), 1U, lh_str_view_get_size(&name), stderr);
    }
    fputc('\n', stderr);
}

static cnor_cli_exit_t
cnor_cli_report_io(const lh_char_t *path, const lh_char_t *what)
{
    (void) fflush(stdout);
    fprintf(stderr, "cnor: %s: %s\n", cnor_cli_display_name(path), what);
    return cnor_cli_exit_io;
}

/* -- compiling ------------------------------------------------------------- */

/* Read and compile options->input into @p image. */
static cnor_cli_exit_t
cnor_cli_compile_input(const cnor_cli_options_t *options, cnor_image_t *image)
{
    static const lh_byte_t bom[3] = {0xEFU, 0xBBU, 0xBFU};
    cnor_compiler_config_t config = cnor_compiler_config_initializer();
    cnor_cli_buffer_t source;
    lh_str_view_t text;
    cnor_diag_t diag;
    lh_usize_t skip = 0U;
    lh_bool_t ok;

    cnor_image_init(image);
    if (!cnor_cli_file_read(options->input, &source))
    {
        return cnor_cli_report_io(options->input, "cannot read the file");
    }
    /* A UTF-8 byte order mark is not part of the program. */
    if (source.size >= 3U && memcmp(source.data, bom, 3U) == 0)
    {
        skip = 3U;
    }
    text = lh_initializer_of(lh_str_view_t, lh_str_view_initializer,
                             (const lh_char_t *) source.data + skip, source.size - skip);
    config.natives = cnor_cli_natives();
    config.file = cnor_cli_view(cnor_cli_display_name(options->input));
    config.emit_lines = options->lines;
    cnor_diag_init(&diag);
    ok = cnor_compiler_compile_source(text, &config, image, &diag);
    cnor_cli_buffer_deinit(&source);
    if (!ok)
    {
        cnor_cli_report(config.file, &diag);
        return cnor_cli_exit_compile;
    }
    return cnor_cli_exit_ok;
}

/* -- running --------------------------------------------------------------- */

/* Where a run's diagnostics point: the source the image was compiled from,
 * when it recorded one, else @p fallback. */
static lh_str_view_t
cnor_cli_run_file(const cnor_vm_t *vm, const lh_char_t *fallback)
{
    const lh_str_view_t recorded = cnor_vm_get_file(vm);

    return lh_str_view_get_size(&recorded) > 0U ? recorded
                                                : cnor_cli_view(cnor_cli_display_name(fallback));
}

/* Load @p size bytes of image and run them; @p path names them. */
static cnor_cli_exit_t
cnor_cli_run_image(const cnor_cli_options_t *options, const lh_byte_t *data, lh_usize_t size,
                   const lh_char_t *path)
{
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_cli_streams_t streams;
    cnor_cli_exit_t code = cnor_cli_exit_ok;
    cnor_vm_t vm;

    streams.out = stdout;
    streams.err = stderr;
    config.natives = cnor_cli_natives();
    config.ctx = &streams;
    config.stack_slots = options->stack_slots;
    config.max_depth = options->max_depth;
    config.str_pool_size = options->str_pool_size;
    config.max_steps = options->max_steps;
    cnor_vm_init(&vm, &config);
    if (size > CNOR_CLI_FILE_MAX_SIZE || !cnor_vm_load(&vm, data, (lh_u32_t) size))
    {
        cnor_cli_report(cnor_cli_view(cnor_cli_display_name(path)), cnor_vm_get_diag(&vm));
        code = cnor_cli_exit_compile;
    }
    else if (!cnor_vm_run(&vm))
    {
        cnor_cli_report(cnor_cli_run_file(&vm, path), cnor_vm_get_diag(&vm));
        code = cnor_cli_exit_run;
    }
    else
    {
        const lh_value_t result = cnor_vm_get_result(&vm);

        if (!lh_value_is_none(&result)
            && (!cnor_cli_text_write_value(stdout, &result) || fputc('\n', stdout) == EOF))
        {
            code = cnor_cli_report_io("-", "cannot write the result");
        }
    }
    cnor_vm_deinit(&vm);
    if (fflush(stdout) != 0 && code == cnor_cli_exit_ok)
    {
        code = cnor_cli_report_io("-", "cannot write the output");
    }
    return code;
}

/* -- commands -------------------------------------------------------------- */

cnor_cli_exit_t
cnor_cli_run(const cnor_cli_options_t *options)
{
    cnor_image_t image;
    cnor_cli_exit_t code = cnor_cli_compile_input(options, &image);

    if (code == cnor_cli_exit_ok)
    {
        code = cnor_cli_run_image(options, image.data, image.size, options->input);
    }
    cnor_image_deinit(&image);
    return code;
}

/* `x.cn` -> `x.tbc`, anything else gets `.tbc` appended. */
static lh_bool_t
cnor_cli_default_output(const lh_char_t *input, lh_char_t *out, lh_usize_t capacity)
{
    lh_usize_t size = strlen(input);

    if (size >= 3U && strcmp(input + size - 3U, ".cn") == 0)
    {
        size -= 3U;
    }
    if (size + sizeof(".tbc") > capacity)
    {
        return lh_bool_false;
    }
    memcpy(out, input, size);
    memcpy(out + size, ".tbc", sizeof(".tbc"));
    return lh_bool_true;
}

cnor_cli_exit_t
cnor_cli_compile(const cnor_cli_options_t *options)
{
    lh_char_t derived[CNOR_CLI_PATH_CAPACITY];
    const lh_char_t *output = options->output;
    cnor_image_t image;
    cnor_cli_exit_t code;

    if (output == lh_null)
    {
        if (cnor_cli_file_is_std(options->input))
        {
            fputs("cnor: compile from the standard input needs -o\n", stderr);
            return cnor_cli_exit_usage;
        }
        if (!cnor_cli_default_output(options->input, derived, sizeof(derived)))
        {
            return cnor_cli_report_io(options->input, "path too long");
        }
        output = derived;
    }
    code = cnor_cli_compile_input(options, &image);
    if (code == cnor_cli_exit_ok && !cnor_cli_file_write(output, image.data, image.size))
    {
        code = cnor_cli_report_io(output, "cannot write the file");
    }
    cnor_image_deinit(&image);
    return code;
}

cnor_cli_exit_t
cnor_cli_exec(const cnor_cli_options_t *options)
{
    cnor_cli_buffer_t image;
    cnor_cli_exit_t code;

    if (!cnor_cli_file_read(options->input, &image))
    {
        return cnor_cli_report_io(options->input, "cannot read the file");
    }
    code = cnor_cli_run_image(options, image.data, image.size, options->input);
    cnor_cli_buffer_deinit(&image);
    return code;
}
