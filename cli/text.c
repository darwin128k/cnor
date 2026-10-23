#include "text.h"

#include <lh/null.h>
#include <lh/real.h>
#include <lh/size.h>
#include <stdlib.h>
#include <string.h>

#if LH_REAL_ENABLED

/* -- reals ----------------------------------------------------------------- */

/* Digits that always read back as the same `float` / `double`. */
#    define CNOR_CLI_TEXT_REAL_DIGITS (sizeof(lh_real_t) == sizeof(float) ? 9 : 17)

/* The shortest `%g` text of @p value that reads back as @p value. */
static void
cnor_cli_text_format_real(lh_real_t value, char *to, lh_usize_t capacity)
{
    int digits;

    for (digits = 1; digits < CNOR_CLI_TEXT_REAL_DIGITS; ++digits)
    {
        (void) snprintf(to, capacity, "%.*g", digits, (double) value);
        if ((lh_real_t) strtod(to, lh_null) == value)
        {
            return;
        }
    }
    (void) snprintf(to, capacity, "%.*g", CNOR_CLI_TEXT_REAL_DIGITS, (double) value);
}

static int
cnor_cli_text_write_real(FILE *to, lh_real_t value)
{
    char text[40];

    if (value != value)
    {
        return fputs("nan", to);
    }
    if (value - value != value - value)
    {
        return fputs(value < 0 ? "-inf" : "inf", to);
    }
    cnor_cli_text_format_real(value, text, sizeof(text));
    if (strpbrk(text, ".e") == lh_null)
    {
        return fprintf(to, "%s.0", text);
    }
    return fputs(text, to);
}

#endif

/* -- values ---------------------------------------------------------------- */

lh_bool_t
cnor_cli_text_write_value(FILE *to, const lh_value_t *value)
{
    const lh_value_data_t *data = lh_value_get_data(value);
    int written;

    switch (lh_value_get_kind(value))
    {
    case lh_value_kind_none:
        written = fputs("none", to);
        break;
    case lh_value_kind_bool:
        written = fputs(data->b ? "true" : "false", to);
        break;
    case lh_value_kind_sint:
        written = fprintf(to, "%lld", (long long) data->i);
        break;
    case lh_value_kind_uint:
        written = fprintf(to, "%llu", (unsigned long long) data->u);
        break;
#if LH_REAL_ENABLED
    case lh_value_kind_real:
        written = cnor_cli_text_write_real(to, data->r);
        break;
#endif
    case lh_value_kind_str:
    {
        const lh_usize_t size = lh_str_view_get_size(&data->s);

        written = size == 0U || fwrite(lh_str_view_get_data(&data->s), 1U, size, to) == size
                      ? 0
                      : EOF;
        break;
    }
    default:
        written = fputs("<ptr>", to);
        break;
    }
    return written < 0 ? lh_bool_false : lh_bool_true;
}
