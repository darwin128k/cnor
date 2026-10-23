#include <cnor/lexer/internal.h>
#include <lh/char/digit.h>
#include <lh/null.h>
#include <lh/util/return.h>

/* -- shared helpers -------------------------------------------------------- */

lh_char_t
cnor_lexer_char_at(const cnor_lexer_t *self, cnor_source_offset_t offset)
{
    if (offset >= self->src.size)
    {
        return 0;
    }
    return ((const lh_char_t *) self->src.begin)[offset];
}

lh_bool_t
cnor_lexer_fail(cnor_lexer_t *self, cnor_error_t code, cnor_source_offset_t pos)
{
    return cnor_diag_set(&self->diag, code, self->line, pos);
}

lh_bool_t
cnor_lexer_emit(cnor_lexer_t *self, cnor_token_t *out, cnor_token_kind_t kind,
                cnor_source_offset_t start, cnor_line_t line)
{
    out->kind = kind;
    out->punct = cnor_punct_none;
    out->keyword = cnor_keyword_none;
    out->start = start;
    out->end = self->pos;
    out->line = line;
    if (kind != cnor_token_newline && kind != cnor_token_indent && kind != cnor_token_dedent
        && kind != cnor_token_eof)
    {
        self->line_has_tokens = lh_bool_true;
    }
    return lh_bool_true;
}

lh_str_view_t
cnor_token_get_text(const cnor_token_t *self, lh_str_view_t src)
{
    lh_str_view_t view = lh_str_view_empty();

    lh_return_ifn(self, view);
    if (self->end > src.size || self->end < self->start)
    {
        return view;
    }
    view.begin = (const lh_char_t *) src.begin + self->start;
    view.size = self->end - self->start;
    return view;
}

/* -- init ------------------------------------------------------------------ */

void
cnor_lexer_init(cnor_lexer_t *self, lh_str_view_t src)
{
    lh_uint_t i;

    lh_return_ifn(self);
    self->src = src;
    self->pos = 0U;
    self->line = 1U;
    for (i = 0U; i < CNOR_LEXER_MAX_INDENTS; ++i)
    {
        self->indents[i] = 0U;
    }
    /* Column zero is the outermost level, so the first indent has something to
     * compare against and the end of the file has somewhere to dedent to. */
    self->depth = 1U;
    self->brackets = 0U;
    self->pending_dedents = 0U;
    self->at_line_start = lh_bool_true;
    self->line_has_tokens = lh_bool_false;
    cnor_diag_init(&self->diag);
}

const cnor_diag_t *
cnor_lexer_get_diag(const cnor_lexer_t *self)
{
    lh_return_ifn(self, lh_null);
    return &self->diag;
}

/* -- line ends ------------------------------------------------------------- */

/* Close the current logical line: a newline token if it carried anything. */
static lh_bool_t
cnor_lexer_end_line(cnor_lexer_t *self, cnor_token_t *out, cnor_source_offset_t at,
                    cnor_line_t line)
{
    if (self->line_has_tokens == lh_bool_false)
    {
        return lh_bool_false;
    }
    self->line_has_tokens = lh_bool_false;
    cnor_lexer_emit(self, out, cnor_token_newline, at, line);
    return lh_bool_true;
}

/* The end of the source closes the last line, then every open block. */
static lh_bool_t
cnor_lexer_end_source(cnor_lexer_t *self, cnor_token_t *out)
{
    if (cnor_lexer_end_line(self, out, self->pos, self->line))
    {
        return lh_bool_true;
    }
    if (self->depth > 1U)
    {
        self->depth--;
        return cnor_lexer_emit(self, out, cnor_token_dedent, self->pos, self->line);
    }
    return cnor_lexer_emit(self, out, cnor_token_eof, self->pos, self->line);
}

static void
cnor_lexer_skip_comment(cnor_lexer_t *self)
{
    while (self->pos < self->src.size && cnor_lexer_char_at(self, self->pos) != '\n')
    {
        self->pos++;
    }
}

/* -- tokens ---------------------------------------------------------------- */

static lh_bool_t
cnor_lexer_scan_token(cnor_lexer_t *self, cnor_token_t *out, lh_char_t c)
{
    if (c == '"')
    {
        return cnor_lexer_scan_string(self, out);
    }
    if (lh_char_is_digit(c))
    {
        return cnor_lexer_scan_number(self, out);
    }
    if (cnor_lexer_is_word_start(c))
    {
        return cnor_lexer_scan_word(self, out);
    }
    return cnor_lexer_scan_punct(self, out);
}

/* One pass of the scanning loop. Sets @p done when @p out holds a token. */
static lh_bool_t
cnor_lexer_step(cnor_lexer_t *self, cnor_token_t *out, lh_bool_t *done)
{
    lh_char_t c;

    *done = lh_bool_true;
    if (self->pending_dedents > 0U)
    {
        self->pending_dedents--;
        return cnor_lexer_emit(self, out, cnor_token_dedent, self->pos, self->line);
    }
    if (self->at_line_start && self->brackets == 0U && self->pos < self->src.size)
    {
        lh_bool_t emitted;

        if (!cnor_lexer_read_line_start(self, out, &emitted))
        {
            return lh_bool_false;
        }
        *done = emitted;
        return lh_bool_true;
    }
    if (self->pos >= self->src.size)
    {
        return cnor_lexer_end_source(self, out);
    }
    c = cnor_lexer_char_at(self, self->pos);
    if (c == '\n')
    {
        const cnor_line_t line = self->line;
        const cnor_source_offset_t at = self->pos;

        self->pos++;
        self->line++;
        if (self->brackets > 0U)
        {
            *done = lh_bool_false;
            return lh_bool_true;
        }
        self->at_line_start = lh_bool_true;
        *done = cnor_lexer_end_line(self, out, at, line);
        return lh_bool_true;
    }
    *done = lh_bool_false;
    if (c == ' ' || c == '\t' || c == '\r')
    {
        self->pos++;
        return lh_bool_true;
    }
    if (c == '#')
    {
        cnor_lexer_skip_comment(self);
        return lh_bool_true;
    }
    *done = lh_bool_true;
    return cnor_lexer_scan_token(self, out, c);
}

lh_bool_t
cnor_lexer_next(cnor_lexer_t *self, cnor_token_t *out)
{
    lh_bool_t done = lh_bool_false;

    lh_return_ifn(self, lh_bool_false);
    lh_return_ifn(out, lh_bool_false);
    lh_return_ifn(cnor_diag_is_ok(&self->diag), lh_bool_false);
    while (!done)
    {
        if (!cnor_lexer_step(self, out, &done))
        {
            return lh_bool_false;
        }
    }
    return lh_bool_true;
}
