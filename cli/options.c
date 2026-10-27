#include "options.h"

#include <lh/null.h>
#include <lh/size.h>
#include <stdio.h>
#include <string.h>

/* -- help ------------------------------------------------------------------ */

void
cnor_cli_options_print_help(void)
{
    fputs("usage: cnor run <file.cn> [options]     compile and run a script\n"
          "       cnor compile <file.cn> [-o <out.cnb>] [--no-lines]\n"
          "                                        write the image of a script\n"
          "       cnor exec <file.cnb> [options]   load and run an image\n"
          "       cnor repl [options]              read lines and run them\n"
          "       cnor <file.cn> [options]         same as `cnor run <file.cn>`\n"
          "       cnor --version | --help\n"
          "\n"
          "A file named `-` is the standard input (`-o -`: the standard output).\n"
          "\n"
          "options of run and compile:\n"
          "  -I, --import-path DIR  look for imported files in DIR too, after the\n"
          "                         importing file's directory (repeatable, up to 16)\n"
          "\n"
          "options of run and exec:\n"
          "  --max-steps N   stop after N instructions; 0 is unlimited (default 0)\n"
          "  --stack N       value stack, in values (default 65536)\n"
          "  --depth N       nested calls allowed (default 4096)\n"
          "  --pool N        string pool, in bytes (default 16777216)\n"
          "\n"
          "natives: print, println, eprint, eprintln (any arguments, space separated)\n"
          "\n"
          "exit status: 0 done, 1 runtime error, 2 bad command line,\n"
          "             3 compile error or bad image, 4 file error\n",
          stdout);
#if !CNOR_CLI_HAS_COMPILER
    fputs("\nThis build has no compiler: it runs images only. `run` and `compile`\n"
          "are not there; use `cnor exec`.\n",
          stdout);
#endif
}

/* -- parsing --------------------------------------------------------------- */

static cnor_cli_exit_t
cnor_cli_options_fail(const lh_char_t *what, const lh_char_t *arg)
{
    fprintf(stderr, "cnor: %s%s%s\ntry 'cnor --help'\n", what, arg != lh_null ? ": " : "",
            arg != lh_null ? arg : "");
    return cnor_cli_exit_usage;
}

static lh_bool_t
cnor_cli_options_is(const lh_char_t *arg, const lh_char_t *name)
{
    return strcmp(arg, name) == 0 ? lh_bool_true : lh_bool_false;
}

/* A path ending in `.cn`. */
static lh_bool_t
cnor_cli_options_is_script(const lh_char_t *arg)
{
    const lh_usize_t size = strlen(arg);

    return size > 3U && strcmp(arg + size - 3U, ".cn") == 0 ? lh_bool_true : lh_bool_false;
}

/* A decimal number from 0 to 2^32 - 1, nothing else. */
static lh_bool_t
cnor_cli_options_number(const lh_char_t *text, lh_u32_t *out)
{
    lh_u32_t value = 0U;
    const lh_char_t *at;

    if (text[0] == '\0')
    {
        return lh_bool_false;
    }
    for (at = text; *at != '\0'; ++at)
    {
        const lh_u32_t digit = (lh_u32_t) (*at - '0');

        if (*at < '0' || *at > '9' || value > (0xFFFFFFFFU - digit) / 10U)
        {
            return lh_bool_false;
        }
        value = value * 10U + digit;
    }
    *out = value;
    return lh_bool_true;
}

/* `--name N` at args[*i]; moves past N. */
static cnor_cli_exit_t
cnor_cli_options_value(const cnor_cli_args_t *args, int *i, lh_u32_t *out, lh_bool_t nonzero)
{
    const lh_char_t *name = args->values[*i];

    if (*i + 1 >= args->count)
    {
        return cnor_cli_options_fail("missing value for", name);
    }
    ++*i;
    if (!cnor_cli_options_number(args->values[*i], out) || (nonzero && *out == 0U))
    {
        return cnor_cli_options_fail(nonzero ? "expected a positive number" : "expected a number",
                                     args->values[*i]);
    }
    return cnor_cli_exit_ok;
}

/* `-I DIR` at args[*i]; moves past DIR. */
static cnor_cli_exit_t
cnor_cli_options_import_path(cnor_cli_options_t *out, const cnor_cli_args_t *args, int *i)
{
    const lh_char_t *name = args->values[*i];

    if (*i + 1 >= args->count)
    {
        return cnor_cli_options_fail("missing value for", name);
    }
    if (out->import_path_count == CNOR_CLI_MAX_IMPORT_PATHS)
    {
        return cnor_cli_options_fail("too many import paths", args->values[*i + 1]);
    }
    ++*i;
    out->import_paths[out->import_path_count++] = args->values[*i];
    return cnor_cli_exit_ok;
}

/* Whether @p arg is `-I`, which run and compile take. */
static lh_bool_t
cnor_cli_options_is_import_path(const cnor_cli_options_t *out, const lh_char_t *arg)
{
    return out->command != cnor_cli_command_exec
                   && (cnor_cli_options_is(arg, "-I") || cnor_cli_options_is(arg, "--import-path"))
               ? lh_bool_true
               : lh_bool_false;
}

/* One option of run / exec at args[*i]. */
static cnor_cli_exit_t
cnor_cli_options_run_option(cnor_cli_options_t *out, const cnor_cli_args_t *args, int *i)
{
    const lh_char_t *arg = args->values[*i];

    if (cnor_cli_options_is(arg, "--max-steps"))
    {
        return cnor_cli_options_value(args, i, &out->max_steps, lh_bool_false);
    }
    if (cnor_cli_options_is(arg, "--stack"))
    {
        return cnor_cli_options_value(args, i, &out->stack_slots, lh_bool_true);
    }
    if (cnor_cli_options_is(arg, "--depth"))
    {
        return cnor_cli_options_value(args, i, &out->max_depth, lh_bool_true);
    }
    if (cnor_cli_options_is(arg, "--pool"))
    {
        return cnor_cli_options_value(args, i, &out->str_pool_size, lh_bool_false);
    }
    return cnor_cli_options_fail("unknown option", arg);
}

/* One option of compile at args[*i]. */
static cnor_cli_exit_t
cnor_cli_options_compile_option(cnor_cli_options_t *out, const cnor_cli_args_t *args, int *i)
{
    const lh_char_t *arg = args->values[*i];

    if (cnor_cli_options_is(arg, "-o") || cnor_cli_options_is(arg, "--output"))
    {
        if (*i + 1 >= args->count)
        {
            return cnor_cli_options_fail("missing value for", arg);
        }
        ++*i;
        out->output = args->values[*i];
        return cnor_cli_exit_ok;
    }
    if (cnor_cli_options_is(arg, "--no-lines"))
    {
        out->lines = lh_bool_false;
        return cnor_cli_exit_ok;
    }
    return cnor_cli_options_fail("unknown option", arg);
}

/* The file and the options after the subcommand, from args[first]. */
static cnor_cli_exit_t
cnor_cli_options_parse_rest(cnor_cli_options_t *out, const cnor_cli_args_t *args, int first)
{
    int i;

    for (i = first; i < args->count; ++i)
    {
        const lh_char_t *arg = args->values[i];
        cnor_cli_exit_t code;

        if (arg[0] != '-' || cnor_cli_options_is(arg, "-"))
        {
            if (out->input != lh_null)
            {
                return cnor_cli_options_fail("more than one file", arg);
            }
            out->input = arg;
            continue;
        }
        if (cnor_cli_options_is_import_path(out, arg))
        {
            code = cnor_cli_options_import_path(out, args, &i);
        }
        else
        {
            code = out->command == cnor_cli_command_compile
                       ? cnor_cli_options_compile_option(out, args, &i)
                       : cnor_cli_options_run_option(out, args, &i);
        }
        if (code != cnor_cli_exit_ok)
        {
            return code;
        }
    }
    if (out->input == lh_null && out->command != cnor_cli_command_repl)
    {
        return cnor_cli_options_fail("missing file", lh_null);
    }
    return cnor_cli_exit_ok;
}

static void
cnor_cli_options_init(cnor_cli_options_t *self)
{
    self->command = cnor_cli_command_run;
    self->input = lh_null;
    self->output = lh_null;
    self->lines = lh_bool_true;
    self->max_steps = CNOR_CLI_DEFAULT_MAX_STEPS;
    self->stack_slots = CNOR_CLI_DEFAULT_STACK_SLOTS;
    self->max_depth = CNOR_CLI_DEFAULT_MAX_DEPTH;
    self->str_pool_size = CNOR_CLI_DEFAULT_STR_POOL_SIZE;
    self->import_path_count = 0U;
}

cnor_cli_exit_t
cnor_cli_options_parse(cnor_cli_options_t *out, const cnor_cli_args_t *args)
{
    const lh_char_t *first;

    cnor_cli_options_init(out);
    if (args->count == 0)
    {
#if CNOR_CLI_HAS_COMPILER
        /* No command at all: the session, like `python`. */
        out->command = cnor_cli_command_repl;
        return cnor_cli_exit_ok;
#else
        return cnor_cli_options_fail("missing command", lh_null);
#endif
    }
    first = args->values[0];
    if (cnor_cli_options_is(first, "--help") || cnor_cli_options_is(first, "-h")
        || cnor_cli_options_is(first, "help"))
    {
        out->command = cnor_cli_command_help;
        return cnor_cli_exit_ok;
    }
    if (cnor_cli_options_is(first, "--version") || cnor_cli_options_is(first, "-V"))
    {
        out->command = cnor_cli_command_version;
        return cnor_cli_exit_ok;
    }
    if (cnor_cli_options_is(first, "run"))
    {
        return cnor_cli_options_parse_rest(out, args, 1);
    }
    if (cnor_cli_options_is(first, "compile"))
    {
        out->command = cnor_cli_command_compile;
        return cnor_cli_options_parse_rest(out, args, 1);
    }
    if (cnor_cli_options_is(first, "exec"))
    {
        out->command = cnor_cli_command_exec;
        return cnor_cli_options_parse_rest(out, args, 1);
    }
#if CNOR_CLI_HAS_COMPILER
    if (cnor_cli_options_is(first, "repl"))
    {
        out->command = cnor_cli_command_repl;
        return cnor_cli_options_parse_rest(out, args, 1);
    }
#endif
    if (first[0] == '-' && !cnor_cli_options_is(first, "-"))
    {
        return cnor_cli_options_fail("unknown option", first);
    }
    /* `cnor file.cn`: a script where the command goes is `run`. */
    if (cnor_cli_options_is(first, "-") || cnor_cli_options_is_script(first))
    {
        return cnor_cli_options_parse_rest(out, args, 0);
    }
    return cnor_cli_options_fail("unknown command", first);
}
