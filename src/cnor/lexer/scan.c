#include <cnor/lexer/internal.h>
#include <lh/char/digit.h>
#include <lh/char/letter.h>

/* -- character classes ----------------------------------------------------- */

lh_bool_t
cnor_lexer_is_word_start(lh_char_t c)
{
    return (lh_char_is_letter(c) || c == '_') ? lh_bool_true : lh_bool_false;
}

lh_bool_t
cnor_lexer_is_word_part(lh_char_t c)
{
    return (cnor_lexer_is_word_start(c) || lh_char_is_digit(c)) ? lh_bool_true : lh_bool_false;
}

/* -- strings --------------------------------------------------------------- */

lh_bool_t
cnor_lexer_scan_string(cnor_lexer_t *self, cnor_token_t *out)
{
    const cnor_source_offset_t start = self->pos;
    cnor_source_offset_t scan = start + 1U;

    for (;;)
    {
        const lh_char_t c = cnor_lexer_char_at(self, scan);

        if (c == 0 || c == '\n')
        {
            return cnor_lexer_fail(self, cnor_error_unterminated_string, start);
        }
        if (c == '"')
        {
            break;
        }
        /* The escaped character is skipped whole, so `\"` cannot end the string.
         * Which escapes exist is the parser's question, not the lexer's. */
        scan += (c == '\\' && cnor_lexer_char_at(self, scan + 1U) != '\n') ? 2U : 1U;
    }
    self->pos = scan + 1U;
    return cnor_lexer_emit(self, out, cnor_token_string, start, self->line);
}

/* -- numbers --------------------------------------------------------------- */

static cnor_source_offset_t
cnor_lexer_skip_word_parts(const cnor_lexer_t *self, cnor_source_offset_t scan)
{
    while (cnor_lexer_is_word_part(cnor_lexer_char_at(self, scan)))
    {
        scan++;
    }
    return scan;
}

static cnor_source_offset_t
cnor_lexer_skip_digits(const cnor_lexer_t *self, cnor_source_offset_t scan)
{
    while (lh_char_is_digit(cnor_lexer_char_at(self, scan)))
    {
        scan++;
    }
    return scan;
}

static lh_bool_t
cnor_lexer_has_radix_prefix(const cnor_lexer_t *self, cnor_source_offset_t at)
{
    const lh_char_t marker = cnor_lexer_char_at(self, at + 1U);

    return (cnor_lexer_char_at(self, at) == '0'
            && (marker == 'x' || marker == 'X' || marker == 'b' || marker == 'B'))
               ? lh_bool_true
               : lh_bool_false;
}

lh_bool_t
cnor_lexer_scan_number(cnor_lexer_t *self, cnor_token_t *out)
{
    const cnor_source_offset_t start = self->pos;
    cnor_token_kind_t kind = cnor_token_int;
    cnor_source_offset_t scan;

    if (cnor_lexer_has_radix_prefix(self, start))
    {
        /* Digits the radix does not have are kept in the token and refused by
         * the parser, which knows the radix. */
        scan = cnor_lexer_skip_word_parts(self, start + 2U);
    }
    else
    {
        scan = cnor_lexer_skip_digits(self, start);
        /* A fraction needs a digit after the dot, so `0..10` stays a range. */
        if (cnor_lexer_char_at(self, scan) == '.'
            && lh_char_is_digit(cnor_lexer_char_at(self, scan + 1U)))
        {
            kind = cnor_token_real;
            scan = cnor_lexer_skip_digits(self, scan + 1U);
        }
        scan = cnor_lexer_skip_word_parts(self, scan);
    }
    self->pos = scan;
    return cnor_lexer_emit(self, out, kind, start, self->line);
}

/* -- words ----------------------------------------------------------------- */

lh_bool_t
cnor_lexer_scan_word(cnor_lexer_t *self, cnor_token_t *out)
{
    const cnor_source_offset_t start = self->pos;
    cnor_keyword_t keyword;

    self->pos = cnor_lexer_skip_word_parts(self, start);
    cnor_lexer_emit(self, out, cnor_token_ident, start, self->line);
    keyword = cnor_lexer_keyword_of(cnor_token_get_text(out, self->src));
    if (keyword != cnor_keyword_none)
    {
        out->kind = cnor_token_keyword;
        out->keyword = keyword;
    }
    return lh_bool_true;
}
