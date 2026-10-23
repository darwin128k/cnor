#include <cnor/lexer/internal.h>

/* -- measuring ------------------------------------------------------------- */

static lh_bool_t
cnor_lexer_is_blank(lh_char_t c)
{
    return (c == ' ' || c == '\t' || c == '\r') ? lh_bool_true : lh_bool_false;
}

/* Columns of leading blanks from @p pos; @p rest receives the first other byte. */
static cnor_column_t
cnor_lexer_measure(const cnor_lexer_t *self, cnor_source_offset_t pos,
                   cnor_source_offset_t *rest)
{
    cnor_column_t column = 0U;
    lh_char_t c = cnor_lexer_char_at(self, pos);

    while (cnor_lexer_is_blank(c))
    {
        column += (c == '\t') ? CNOR_LEXER_TAB_WIDTH : (c == ' ' ? 1U : 0U);
        c = cnor_lexer_char_at(self, ++pos);
    }
    *rest = pos;
    return column;
}

/* -- blank lines ----------------------------------------------------------- */

/* Step over a line with nothing on it but blanks or a comment. */
static void
cnor_lexer_skip_empty_line(cnor_lexer_t *self, cnor_source_offset_t rest)
{
    while (rest < self->src.size && cnor_lexer_char_at(self, rest) != '\n')
    {
        rest++;
    }
    self->pos = rest;
    if (rest < self->src.size)
    {
        self->pos = rest + 1U;
        self->line++;
        self->at_line_start = lh_bool_true;
    }
}

/* -- levels ---------------------------------------------------------------- */

static lh_bool_t
cnor_lexer_close_levels(cnor_lexer_t *self, cnor_column_t column, cnor_source_offset_t start)
{
    while (self->depth > 1U && column < self->indents[self->depth - 1U])
    {
        self->depth--;
        self->pending_dedents++;
    }
    if (column != self->indents[self->depth - 1U])
    {
        return cnor_lexer_fail(self, cnor_error_bad_indent, start);
    }
    return lh_bool_true;
}

static lh_bool_t
cnor_lexer_open_level(cnor_lexer_t *self, cnor_token_t *out, cnor_column_t column,
                      cnor_source_offset_t start)
{
    if (self->depth >= CNOR_LEXER_MAX_INDENTS)
    {
        return cnor_lexer_fail(self, cnor_error_too_deep, start);
    }
    self->indents[self->depth++] = column;
    cnor_lexer_emit(self, out, cnor_token_indent, start, self->line);
    out->end = self->pos;
    return lh_bool_true;
}

lh_bool_t
cnor_lexer_read_line_start(cnor_lexer_t *self, cnor_token_t *out, lh_bool_t *emitted)
{
    const cnor_source_offset_t start = self->pos;
    cnor_source_offset_t rest;
    const cnor_column_t column = cnor_lexer_measure(self, start, &rest);
    const lh_char_t first = cnor_lexer_char_at(self, rest);

    *emitted = lh_bool_false;
    self->at_line_start = lh_bool_false;
    if (first == '\n' || first == '#' || rest >= self->src.size)
    {
        cnor_lexer_skip_empty_line(self, rest);
        return lh_bool_true;
    }
    self->pos = rest;
    if (column > self->indents[self->depth - 1U])
    {
        *emitted = lh_bool_true;
        return cnor_lexer_open_level(self, out, column, start);
    }
    return cnor_lexer_close_levels(self, column, start);
}
