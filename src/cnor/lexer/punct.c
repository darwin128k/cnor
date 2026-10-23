#include <cnor/lexer/internal.h>

/* -- punctuation table ----------------------------------------------------- */

struct cnor_punct_spelling
{
    lh_char_t first;
    lh_char_t second; /* 0 for a one-character punctuator */
    cnor_punct_t punct;
};

/* Two-character entries come first, so `->` is never read as `-` then `>`. */
static const struct cnor_punct_spelling m_spellings[] = {
    {'.', '.', cnor_punct_range},  {'=', '=', cnor_punct_eq},      {'!', '=', cnor_punct_ne},
    {'<', '=', cnor_punct_le},     {'>', '=', cnor_punct_ge},      {'-', '>', cnor_punct_arrow},
    {'=', '>', cnor_punct_fat_arrow},
    {'(', 0, cnor_punct_lparen},   {')', 0, cnor_punct_rparen},    {'[', 0, cnor_punct_lbracket},
    {']', 0, cnor_punct_rbracket}, {':', 0, cnor_punct_colon},     {';', 0, cnor_punct_semicolon},
    {',', 0, cnor_punct_comma},    {'.', 0, cnor_punct_dot},       {'+', 0, cnor_punct_plus},
    {'-', 0, cnor_punct_minus},    {'*', 0, cnor_punct_star},      {'/', 0, cnor_punct_slash},
    {'%', 0, cnor_punct_percent},  {'=', 0, cnor_punct_assign},    {'<', 0, cnor_punct_lt},
    {'>', 0, cnor_punct_gt},
};

#define CNOR_PUNCT_SPELLING_COUNT (sizeof(m_spellings) / sizeof(m_spellings[0]))

/* -- brackets -------------------------------------------------------------- */

static void
cnor_lexer_track_bracket(cnor_lexer_t *self, cnor_punct_t punct)
{
    if (punct == cnor_punct_lparen || punct == cnor_punct_lbracket)
    {
        self->brackets++;
    }
    else if ((punct == cnor_punct_rparen || punct == cnor_punct_rbracket) && self->brackets > 0U)
    {
        self->brackets--;
    }
}

/* -- scanning -------------------------------------------------------------- */

lh_bool_t
cnor_lexer_scan_punct(cnor_lexer_t *self, cnor_token_t *out)
{
    const cnor_source_offset_t start = self->pos;
    const lh_char_t first = cnor_lexer_char_at(self, start);
    const lh_char_t second = cnor_lexer_char_at(self, start + 1U);
    lh_uint_t i;

    for (i = 0U; i < CNOR_PUNCT_SPELLING_COUNT; ++i)
    {
        const struct cnor_punct_spelling *entry = &m_spellings[i];

        if (entry->first != first || (entry->second != 0 && entry->second != second))
        {
            continue;
        }
        self->pos = start + (entry->second != 0 ? 2U : 1U);
        cnor_lexer_track_bracket(self, entry->punct);
        cnor_lexer_emit(self, out, cnor_token_punct, start, self->line);
        out->punct = entry->punct;
        return lh_bool_true;
    }
    return cnor_lexer_fail(self, cnor_error_bad_character, start);
}
