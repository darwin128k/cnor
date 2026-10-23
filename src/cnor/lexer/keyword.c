#include <cnor/lexer.h>

/* Indexed by ::cnor_keyword_t; slot 0 is "not a keyword" and is never matched. */
static const lh_str_view_t m_keywords[cnor_keyword_count] = {
    lh_str_view_empty_initializer(),        lh_str_view_initializer_lit("fn"),
    lh_str_view_initializer_lit("let"),     lh_str_view_initializer_lit("mut"),
    lh_str_view_initializer_lit("if"),      lh_str_view_initializer_lit("else"),
    lh_str_view_initializer_lit("match"),   lh_str_view_initializer_lit("for"),
    lh_str_view_initializer_lit("in"),      lh_str_view_initializer_lit("while"),
    lh_str_view_initializer_lit("return"),  lh_str_view_initializer_lit("break"),
    lh_str_view_initializer_lit("continue"), lh_str_view_initializer_lit("true"),
    lh_str_view_initializer_lit("false"),   lh_str_view_initializer_lit("none"),
    lh_str_view_initializer_lit("and"),     lh_str_view_initializer_lit("or"),
    lh_str_view_initializer_lit("not"),     lh_str_view_initializer_lit("import"),
    lh_str_view_initializer_lit("native"),
};

/* Keywords are 2 to 8 bytes long: anything else is a name without a single
 * comparison, and only keywords of the word's length and first byte are
 * compared in full. */
#define CNOR_KEYWORD_MIN_SIZE 2U
#define CNOR_KEYWORD_MAX_SIZE 8U

cnor_keyword_t
cnor_lexer_keyword_of(lh_str_view_t word)
{
    const lh_usize_t size = lh_str_view_get_size(&word);
    const lh_char_t *text = lh_str_view_get_data(&word);
    lh_uint_t i;

    if (size < CNOR_KEYWORD_MIN_SIZE || size > CNOR_KEYWORD_MAX_SIZE)
    {
        return cnor_keyword_none;
    }
    for (i = 1U; i < (lh_uint_t) cnor_keyword_count; ++i)
    {
        const lh_str_view_t *keyword = &m_keywords[i];

        if (keyword->size == size && *(const lh_char_t *) keyword->begin == text[0]
            && lh_str_view_equals(&word, keyword, lh_bool_false))
        {
            return (cnor_keyword_t) i;
        }
    }
    return cnor_keyword_none;
}
