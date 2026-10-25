#include "natives.h"

#include "text.h"

#include <cnor/types.h>
#include <lh/bool.h>

/* -- writing --------------------------------------------------------------- */

/* The arguments, separated by one space, then a newline when asked. */
static void
cnor_cli_natives_write(FILE *to, const lh_value_t *args, cnor_argc_t argc, lh_bool_t newline,
                       lh_value_t *result, cnor_error_t *error)
{
    lh_bool_t ok = lh_bool_true;
    cnor_argc_t i;

    for (i = 0U; i < argc && ok; ++i)
    {
        ok = (i == 0U || fputc(' ', to) != EOF) && cnor_cli_text_write_value(to, &args[i]);
    }
    if (ok && newline)
    {
        ok = fputc('\n', to) != EOF ? lh_bool_true : lh_bool_false;
    }
    if (!ok || ferror(to))
    {
        *error = cnor_error_native_fail;
    }
    *result = lh_value_make_none();
}

/* -- natives --------------------------------------------------------------- */

static void
cnor_cli_native_print(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
                      cnor_error_t *error)
{
    cnor_cli_natives_write(((cnor_cli_streams_t *) ctx)->out, args, argc, lh_bool_false, result,
                            error);
}

static void
cnor_cli_native_println(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
                        cnor_error_t *error)
{
    cnor_cli_natives_write(((cnor_cli_streams_t *) ctx)->out, args, argc, lh_bool_true, result,
                            error);
}

static void
cnor_cli_native_eprint(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
                       cnor_error_t *error)
{
    cnor_cli_natives_write(((cnor_cli_streams_t *) ctx)->err, args, argc, lh_bool_false, result,
                            error);
}

static void
cnor_cli_native_eprintln(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
                         cnor_error_t *error)
{
    cnor_cli_natives_write(((cnor_cli_streams_t *) ctx)->err, args, argc, lh_bool_true, result,
                            error);
}

/* -- table ----------------------------------------------------------------- */

static const cnor_native_t m_natives[] = {
    cnor_native_initializer("print", cnor_cli_native_print, 0U, CNOR_ARGC_MAX),
    cnor_native_initializer("println", cnor_cli_native_println, 0U, CNOR_ARGC_MAX),
    cnor_native_initializer("eprint", cnor_cli_native_eprint, 0U, CNOR_ARGC_MAX),
    cnor_native_initializer("eprintln", cnor_cli_native_eprintln, 0U, CNOR_ARGC_MAX),
};

static const cnor_native_table_t m_native_table =
    cnor_native_table_initializer(m_natives, sizeof(m_natives) / sizeof(m_natives[0]));

const cnor_native_table_t *
cnor_cli_natives(void)
{
    return &m_native_table;
}
