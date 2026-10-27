#include "repl.h"

#include "command.h"
#include "file.h"
#include "natives.h"
#include "text.h"

#include <cnor/compiler.h>
#include <cnor/diag.h>
#include <cnor/error.h>
#include <cnor/image.h>
#include <cnor/vm.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>
#include <lh/str/view.h>
#include <stdio.h>
#include <string.h>

/* -- the session ------------------------------------------------------------- */

/* The program read so far, and the lines of the input being read. */
struct cnor_cli_repl
{
    lh_char_t *source;         /**< The program so far. */
    lh_usize_t source_size;
    lh_usize_t source_capacity;
    lh_char_t *pending;        /**< The lines of the input being read. */
    lh_usize_t pending_size;
    lh_usize_t pending_capacity;
    lh_usize_t shown_out;       /**< How much of the output was shown already. */
    lh_usize_t shown_err;       /**< And of the error output. */
    lh_bool_t have_line;        /**< A line was read and belongs to the next statement. */
    lh_bool_t statement;        /**< The input begins a statement that has no value. */
    lh_value_t last_result;     /**< What the last run produced. */
};

static void
cnor_cli_repl_free(struct cnor_cli_repl *self)
{
    lh_runtime_allocator_free(self->source);
    lh_runtime_allocator_free(self->pending);
    self->source = lh_null;
    self->pending = lh_null;
}

/* Add @p size bytes of @p text to the buffer at @p data (which grows). */
static lh_bool_t
cnor_cli_repl_grow(lh_char_t **data, lh_usize_t *size, lh_usize_t *capacity, lh_usize_t more)
{
    if (*size + more + 1U > *capacity)
    {
        lh_usize_t wanted = *capacity == 0U ? 1024U : *capacity;
        lh_char_t *grown;

        while (wanted < *size + more + 1U)
        {
            wanted *= 2U;
        }
        grown = (lh_char_t *) lh_runtime_allocator_alloc(wanted);
        if (grown == lh_null)
        {
            return lh_bool_false;
        }
        if (*data != lh_null)
        {
            memcpy(grown, *data, *size);
            lh_runtime_allocator_free(*data);
        }
        *data = grown;
        *capacity = wanted;
    }
    return lh_bool_true;
}

/* Add @p text to the buffer at @p data. */
static lh_bool_t
cnor_cli_repl_append(lh_char_t **data, lh_usize_t *size, lh_usize_t *capacity,
                     const lh_char_t *text)
{
    const lh_usize_t length = strlen(text);

    if (!cnor_cli_repl_grow(data, size, capacity, length))
    {
        return lh_bool_false;
    }
    memcpy(*data + *size, text, length);
    *size += length;
    (*data)[*size] = '\0';
    return lh_bool_true;
}

/* -- reading ---------------------------------------------------------------- */

/* Read one line into @p self's pending buffer.
 * @return Whether a line was read; the end of the input is not a line. */
static lh_bool_t
cnor_cli_repl_read_line(struct cnor_cli_repl *self)
{
    char chunk[512];
    lh_bool_t any = lh_bool_false;

    self->pending_size = 0U;
    if (self->pending != lh_null)
    {
        self->pending[0] = '\0';
    }
    for (;;)
    {
        char *const got = fgets(chunk, (int) sizeof(chunk), stdin);

        if (got == NULL)
        {
            break;
        }
        any = lh_bool_true;
        if (!cnor_cli_repl_append(&self->pending, &self->pending_size, &self->pending_capacity,
                                  chunk))
        {
            return lh_bool_false;
        }
        if (strchr(chunk, '\n') != NULL)
        {
            /* The line is whole; a longer one comes in pieces. */
            break;
        }
    }
    return any;
}

/* Whether @p line opens a block: it ends with a colon. */
static lh_bool_t
cnor_cli_repl_opens_block(const lh_char_t *line)
{
    lh_usize_t size = strlen(line);

    while (size > 0U && (line[size - 1U] == '\n' || line[size - 1U] == '\r'
                         || line[size - 1U] == ' ' || line[size - 1U] == '\t'))
    {
        --size;
    }
    return size > 0U && line[size - 1U] == ':';
}

/* Whether @p line is the command @p name, without its line ending. */
static lh_bool_t
cnor_cli_repl_is_command(const lh_char_t *line, const lh_char_t *name)
{
    lh_usize_t size = strlen(line);

    while (size > 0U && (line[size - 1U] == '\n' || line[size - 1U] == '\r'))
    {
        --size;
    }
    return strlen(name) == size && memcmp(line, name, size) == 0;
}

/* Whether @p line is indented: it belongs to the block above it. */
static lh_bool_t
cnor_cli_repl_is_indented(const lh_char_t *line)
{
    return line[0] == ' ' || line[0] == '\t';
}

/* Whether @p line is a clause of the statement above it: `catch`, `finally`
 * or `else`, which come at the level of the statement they belong to. */
static lh_bool_t
cnor_cli_repl_is_clause(const lh_char_t *line)
{
    return strncmp(line, "catch", 5U) == 0 || strncmp(line, "finally", 7U) == 0
           || strncmp(line, "else", 4U) == 0;
}

/* Whether @p value is the same as @p other: the same kind, and the same
 * bytes or the same number. */
static lh_bool_t
cnor_cli_repl_same(const lh_value_t *value, const lh_value_t *other)
{
    if (value->kind != other->kind)
    {
        return lh_bool_false;
    }
    switch (value->kind)
    {
    case lh_value_kind_sint:
        return value->data.i == other->data.i ? lh_bool_true : lh_bool_false;
    case lh_value_kind_uint:
        return value->data.u == other->data.u ? lh_bool_true : lh_bool_false;
    case lh_value_kind_str:
        return lh_str_view_equals(&value->data.s, &other->data.s, lh_bool_false)
                   ? lh_bool_true
                   : lh_bool_false;
    default:
        return lh_bool_true;
    }
}

/* Whether @p line begins a statement that has no value: a keyword, or one of
 * the natives that answer `none`. */
static lh_bool_t
cnor_cli_repl_is_statement(const lh_char_t *line)
{
    static const lh_char_t *const words[] = {
        "let",   "mut",    "fn",     "native", "import", "if",     "else",
        "match", "while",  "for",    "in",     "try",    "catch",  "finally",
        "raise", "return", "break",  "continue", "print", "println", "eprint",
        "eprintln",
    };
    size_t i;

    for (i = 0U; i < sizeof(words) / sizeof(words[0]); ++i)
    {
        const size_t length = strlen(words[i]);

        if (strncmp(line, words[i], length) == 0
            && (line[length] == '\0' || line[length] == '\n' || line[length] == '\r'
                || line[length] == ' ' || line[length] == '\t'))
        {
            return lh_bool_true;
        }
    }
    return lh_bool_false;
}

/* -- running ---------------------------------------------------------------- */

/* The part of the file that was written after @p from, written to @p to. */
static void
cnor_cli_repl_show_new(FILE *from, lh_usize_t *shown, FILE *to)
{
    long size = 0L;

    if (fflush(from) == 0 && (size = ftell(from)) >= 0L)
    {
        char buffer[1024];
        lh_usize_t left = (lh_usize_t) size;

        if (*shown > left)
        {
            *shown = left;
        }
        left -= *shown;
        /* The part that was shown already is skipped: only what the natives
         * wrote since then belongs to the session. */
        if (fseek(from, (long) *shown, SEEK_SET) != 0)
        {
            return;
        }
        while (left > 0U)
        {
            const size_t want = left < sizeof(buffer) ? left : sizeof(buffer);
            const size_t read = fread(buffer, 1U, want, from);

            if (read == 0U)
            {
                break;
            }
            (void) fwrite(buffer, 1U, read, to);
            left -= read;
        }
        *shown = (lh_usize_t) size;
    }
}

/* Compile what the session has read so far and run it.
 * @return Whether the program ran. */
static lh_bool_t
cnor_cli_repl_run(struct cnor_cli_repl *self, const cnor_cli_options_t *options)
{
    cnor_compiler_config_t config = cnor_compiler_config_initializer();
    cnor_vm_config_t vm_config = cnor_vm_config_initializer();
    cnor_cli_streams_t streams;
    cnor_image_t image;
    cnor_diag_t diag;
    lh_str_view_t text;
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    lh_bool_t ok;

    if (out == lh_null || err == lh_null)
    {
        if (out != lh_null)
        {
            fclose(out);
        }
        if (err != lh_null)
        {
            fclose(err);
        }
        return lh_bool_false;
    }
    streams.out = out;
    streams.err = err;
    config.natives = cnor_cli_natives();
    /* What the diagnostics call the program. */
    {
        static const lh_char_t name[] = "<repl>";
        lh_str_view_t view;

        lh_str_view_init(&view, name);
        config.file = view;
    }
    config.emit_lines = lh_bool_true;
    lh_str_init_by_size(&text, self->source, self->source_size);
    cnor_diag_init(&diag);
    ok = cnor_compiler_compile_source(text, &config, &image, &diag);
    if (!ok)
    {
        cnor_cli_report(config.file, &diag);
        (void) fclose(out);
        (void) fclose(err);
        return lh_bool_false;
    }
    vm_config.natives = cnor_cli_natives();
    vm_config.ctx = &streams;
    vm_config.stack_slots = options->stack_slots;
    vm_config.max_depth = options->max_depth;
    vm_config.str_pool_size = options->str_pool_size;
    vm_config.max_steps = options->max_steps;
    {
        cnor_vm_t vm;
        lh_value_t result = lh_value_make_none();

        cnor_vm_init(&vm, &vm_config);
        if (!cnor_vm_load(&vm, image.data, image.size))
        {
            cnor_cli_report(config.file, cnor_vm_get_diag(&vm));
            ok = lh_bool_false;
        }
        else if (!cnor_vm_run(&vm))
        {
            cnor_cli_report(config.file, cnor_vm_get_diag(&vm));
            ok = lh_bool_false;
        }
        else
        {
            result = cnor_vm_get_result(&vm);
        }
        cnor_vm_deinit(&vm);
        /* What the natives wrote comes first, then the result of the line. */
        cnor_cli_repl_show_new(out, &self->shown_out, stdout);
        cnor_cli_repl_show_new(err, &self->shown_err, stderr);
        if (ok && !lh_value_is_none(&result)
            && (!cnor_cli_repl_same(&result, &self->last_result) || !self->statement))
        {
            (void) cnor_cli_text_write_value(stdout, &result);
            (void) fputc('\n', stdout);
        }
        self->last_result = result;
    }
    cnor_image_deinit(&image);
    (void) fclose(out);
    (void) fclose(err);
    (void) fflush(stdout);
    return ok;
}

/* -- the session ------------------------------------------------------------ */

cnor_cli_exit_t
cnor_cli_repl(const cnor_cli_options_t *options)
{
    struct cnor_cli_repl self;
    cnor_cli_exit_t code = cnor_cli_exit_ok;

    memset(&self, 0, sizeof(self));
    self.source = (lh_char_t *) lh_runtime_allocator_alloc(1024U);
    self.pending = (lh_char_t *) lh_runtime_allocator_alloc(1024U);
    if (self.source == lh_null || self.pending == lh_null)
    {
        cnor_cli_repl_free(&self);
        fputs("cnor: out of memory\n", stderr);
        return cnor_cli_exit_io;
    }
    self.source[0] = '\0';
    self.pending[0] = '\0';
    self.source_capacity = 1024U;
    self.pending_capacity = 1024U;
    for (;;)
    {
        lh_usize_t source_size = self.source_size;

        if (!self.have_line)
        {
            (void) fputs("> ", stdout);
            (void) fflush(stdout);
            if (!cnor_cli_repl_read_line(&self))
            {
                break;
            }
        }
        self.have_line = lh_bool_false;
        /* Whether the input begins a statement that has no value of its own. */
        self.statement = cnor_cli_repl_is_statement(self.pending);
        {
            const lh_char_t *const trimmed = self.pending;

            /* `:q` ends the session, `:reset` forgets the program. */
            if (cnor_cli_repl_is_command(trimmed, ":q"))
            {
                break;
            }
            if (cnor_cli_repl_is_command(trimmed, ":reset"))
            {
                self.source_size = 0U;
                self.source[0] = '\0';
                self.shown_out = 0U;
                self.shown_err = 0U;
                continue;
            }
            if (trimmed[0] == '\n' || trimmed[0] == '\0')
            {
                continue;
            }
        }
        if (!cnor_cli_repl_append(&self.source, &self.source_size, &self.source_capacity,
                                  self.pending))
        {
            code = cnor_cli_exit_io;
            break;
        }
        if (cnor_cli_repl_opens_block(self.pending))
        {
            /* A block follows: its lines are indented, and a clause of the
             * statement may follow it (`catch`, `else`) at the same level. */
            for (;;)
            {
                (void) fputs("... ", stdout);
                (void) fflush(stdout);
                if (!cnor_cli_repl_read_line(&self))
                {
                    break;
                }
                if (cnor_cli_repl_is_indented(self.pending)
                    || cnor_cli_repl_is_clause(self.pending))
                {
                    if (!cnor_cli_repl_append(&self.source, &self.source_size,
                                              &self.source_capacity, self.pending))
                    {
                        code = cnor_cli_exit_io;
                        break;
                    }
                    continue;
                }
                /* The line belongs to the next statement: it stays for it. */
                self.have_line = lh_bool_true;
                break;
            }
            if (code != cnor_cli_exit_ok)
            {
                break;
            }
        }
        if (!cnor_cli_repl_run(&self, options))
        {
            /* The line does not belong to the program: it is dropped, so the
             * session can go on. */
            self.source_size = source_size;
            self.source[source_size] = '\0';
            code = cnor_cli_exit_ok;
        }
    }
    cnor_cli_repl_free(&self);
    return code;
}
