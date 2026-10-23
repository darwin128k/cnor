#include <cnor/parser/internal.h>
#include <lh/null.h>
#include <lh/numeric/fixed/limits.h>
#include <lh/real.h>
#include <lh/str/parse/radix.h>
#if LH_REAL_ENABLED
#    include <lh/str/parse/real.h>
#endif

/* -- whole numbers --------------------------------------------------------- */

/* Split `0x..` / `0b..` into radix and digits; anything else is decimal. */
static lh_uint_t
cnor_parser_number_radix(lh_str_view_t *digits)
{
    const lh_char_t *text = (const lh_char_t *) digits->begin;
    lh_uint_t radix = 10U;

    if (digits->size > 2U && text[0] == '0')
    {
        if (text[1] == 'x' || text[1] == 'X')
        {
            radix = 16U;
        }
        else if (text[1] == 'b' || text[1] == 'B')
        {
            radix = 2U;
        }
    }
    if (radix != 10U)
    {
        digits->begin = text + 2;
        digits->size -= 2U;
    }
    return radix;
}

static cnor_node_t *
cnor_parser_int(cnor_parser_t *self)
{
    lh_str_view_t digits = cnor_parser_text(self);
    const lh_uint_t radix = cnor_parser_number_radix(&digits);
    lh_uint_t value = 0U;
    cnor_node_t *node;

    if (!lh_str_ptr_parse_radix((const lh_char_t *) digits.begin, digits.size, radix,
                                LH_U32_T_MAX, &value))
    {
        return cnor_parser_fail(self, cnor_error_bad_number);
    }
    node = cnor_parser_node(self, cnor_node_int);
    if (node != lh_null)
    {
        node->data.int_.value = (cnor_int_literal_t) value;
        cnor_parser_advance(self);
    }
    return node;
}

/* -- reals ----------------------------------------------------------------- */

static cnor_node_t *
cnor_parser_real(cnor_parser_t *self)
{
#if LH_REAL_ENABLED
    const lh_str_view_t text = cnor_parser_text(self);
    lh_real_t value;
    cnor_node_t *node;

    /* Validated here, kept as text: the image stores reals as text so that a
     * float build and a double build read the same file. */
    if (!lh_str_ptr_parse_real((const lh_char_t *) text.begin, text.size, &value))
    {
        return cnor_parser_fail(self, cnor_error_bad_number);
    }
    node = cnor_parser_node(self, cnor_node_real);
    if (node != lh_null && cnor_parser_take_name(self, node))
    {
        cnor_parser_advance(self);
        return node;
    }
    return lh_null;
#else
    return cnor_parser_fail(self, cnor_error_no_reals);
#endif
}

/* -- strings --------------------------------------------------------------- */

/* The byte an escape stands for; @p ok is cleared for an unknown escape. */
static lh_char_t
cnor_parser_escape_of(lh_char_t c, lh_bool_t *ok)
{
    *ok = lh_bool_true;
    switch (c)
    {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case '\\':
        return '\\';
    case '"':
        return '"';
    case '0':
        return '\0';
    default:
        *ok = lh_bool_false;
        return 0;
    }
}

/* Unescape @p raw (quotes excluded) into @p out; the length goes to @p written. */
static lh_bool_t
cnor_parser_unescape(const lh_char_t *raw, lh_usize_t size, lh_char_t *out, lh_usize_t *written)
{
    lh_usize_t read = 0U;
    lh_usize_t count = 0U;

    while (read < size)
    {
        lh_char_t c = raw[read++];

        if (c == '\\')
        {
            lh_bool_t ok;

            if (read >= size)
            {
                return lh_bool_false;
            }
            c = cnor_parser_escape_of(raw[read++], &ok);
            if (!ok)
            {
                return lh_bool_false;
            }
        }
        out[count++] = c;
    }
    *written = count;
    return lh_bool_true;
}

static cnor_node_t *
cnor_parser_string(cnor_parser_t *self)
{
    const lh_str_view_t text = cnor_parser_text(self);
    const lh_usize_t raw_size = text.size - 2U;
    lh_char_t *buffer = (lh_char_t *) cnor_arena_alloc(self->arena, raw_size);
    lh_usize_t written = 0U;
    cnor_node_t *node;

    if (buffer == lh_null)
    {
        return cnor_parser_fail(self, cnor_error_no_memory);
    }
    if (!cnor_parser_unescape((const lh_char_t *) text.begin + 1, raw_size, buffer, &written))
    {
        return cnor_parser_fail(self, cnor_error_bad_escape);
    }
    node = cnor_parser_node(self, cnor_node_string);
    if (node != lh_null)
    {
        node->name.begin = buffer;
        node->name.size = written;
        cnor_parser_advance(self);
    }
    return node;
}

/* -- dispatch -------------------------------------------------------------- */

cnor_node_t *
cnor_parser_literal(cnor_parser_t *self)
{
    switch (self->token.kind)
    {
    case cnor_token_int:
        return cnor_parser_int(self);
    case cnor_token_real:
        return cnor_parser_real(self);
    case cnor_token_string:
        return cnor_parser_string(self);
    default:
        return cnor_parser_fail(self, cnor_error_not_an_expression);
    }
}
