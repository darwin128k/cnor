/* Lexer: ported from the original suite, adapted to newline tokens and the spec's
 * `#` comments, plus cases for what the rework added. */

#include "harness.h"

#include <cnor/lexer.h>
#include <lh/str/view.h>

#define MAX_TOKENS 128

/* A source that does not end in a line break still ends its last line: the
 * lexer emits the NEWLINE before EOF, so counts below include it. */

/* -- helpers --------------------------------------------------------------- */

typedef struct lexed
{
    cnor_token_t tokens[MAX_TOKENS];
    unsigned count;
    cnor_error_t error;
    lh_str_view_t src;
} lexed_t;

static lh_str_view_t
view_of(const char *text)
{
    lh_str_view_t view;

    /* lh refuses a sized view of zero bytes; an empty source is the empty view. */
    if (strlen(text) == 0U)
    {
        lh_str_view_init_empty(&view);
        return view;
    }
    lh_str_view_init_by_size(&view, text, strlen(text));
    return view;
}

/* Every token a source produces, up to and including eof. */
static void
lex_all(const char *text, lexed_t *out)
{
    cnor_lexer_t lexer;

    out->src = view_of(text);
    out->count = 0U;
    cnor_lexer_init(&lexer, out->src);
    while (out->count < MAX_TOKENS)
    {
        cnor_token_t token;

        if (!cnor_lexer_next(&lexer, &token))
        {
            break;
        }
        out->tokens[out->count++] = token;
        if (token.kind == cnor_token_eof)
        {
            break;
        }
    }
    out->error = cnor_diag_get_code(cnor_lexer_get_diag(&lexer));
}

static int
text_is(const lexed_t *lexed, unsigned index, const char *expected)
{
    const lh_str_view_t text = cnor_token_get_text(&lexed->tokens[index], lexed->src);

    return lh_str_view_get_size(&text) == strlen(expected)
           && memcmp(lh_str_view_get_data(&text), expected, strlen(expected)) == 0;
}

static unsigned
count_kind(const lexed_t *lexed, cnor_token_kind_t kind)
{
    unsigned n = 0U;
    unsigned i;

    for (i = 0U; i < lexed->count; ++i)
    {
        n += lexed->tokens[i].kind == kind ? 1U : 0U;
    }
    return n;
}

/* -- ported ---------------------------------------------------------------- */

CNOR_TEST(reads_names_numbers_and_punctuation)
{
    lexed_t l;

    lex_all("let x = 42", &l);
    REQUIRE(l.count == 6U);
    CHECK_EQ(l.tokens[0].kind, cnor_token_keyword);
    CHECK_EQ(l.tokens[0].keyword, cnor_keyword_let);
    CHECK_EQ(l.tokens[1].kind, cnor_token_ident);
    CHECK(text_is(&l, 1U, "x"));
    CHECK_EQ(l.tokens[2].punct, cnor_punct_assign);
    CHECK_EQ(l.tokens[3].kind, cnor_token_int);
    CHECK(text_is(&l, 3U, "42"));
    CHECK_EQ(l.tokens[4].kind, cnor_token_newline);
    CHECK_EQ(l.tokens[5].kind, cnor_token_eof);
}

CNOR_TEST(keeps_a_number_a_name_and_the_arrow_apart)
{
    lexed_t l;
    const cnor_punct_t expected[] = {cnor_punct_lparen, cnor_punct_colon, cnor_punct_rparen,
                                     cnor_punct_arrow, cnor_punct_colon};
    unsigned seen = 0U;
    unsigned i;

    lex_all("fn f(x: i32) -> i32:", &l);
    for (i = 0U; i < l.count; ++i)
    {
        if (l.tokens[i].kind == cnor_token_punct)
        {
            REQUIRE(seen < 5U);
            CHECK_EQ(l.tokens[i].punct, expected[seen]);
            seen++;
        }
    }
    CHECK_EQ(seen, 5U);
}

CNOR_TEST(reads_hexadecimal_and_binary)
{
    lexed_t l;

    lex_all("0xFF 0b1010 7", &l);
    REQUIRE(l.count == 5U);
    CHECK(text_is(&l, 0U, "0xFF"));
    CHECK(text_is(&l, 1U, "0b1010"));
    CHECK(text_is(&l, 2U, "7"));
}

CNOR_TEST(spans_a_string_including_its_quotes)
{
    lexed_t l;

    lex_all("\"hi\"", &l);
    REQUIRE(l.count == 3U);
    CHECK_EQ(l.tokens[0].kind, cnor_token_string);
    CHECK(text_is(&l, 0U, "\"hi\""));
}

CNOR_TEST(does_not_end_a_string_on_an_escaped_quote)
{
    lexed_t l;

    lex_all("\"a\\\"b\" 1", &l);
    REQUIRE(l.count == 4U);
    CHECK(text_is(&l, 0U, "\"a\\\"b\""));
    CHECK_EQ(l.tokens[1].kind, cnor_token_int);
}

CNOR_TEST(turns_a_deeper_line_into_an_indent)
{
    lexed_t l;

    /* fn f ( ) : NEWLINE INDENT return 1 NEWLINE DEDENT EOF */
    lex_all("fn f():\n    return 1\n", &l);
    REQUIRE(l.count == 12U);
    CHECK_EQ(l.tokens[5].kind, cnor_token_newline);
    CHECK_EQ(l.tokens[6].kind, cnor_token_indent);
    CHECK_EQ(l.tokens[9].kind, cnor_token_newline);
    CHECK_EQ(l.tokens[10].kind, cnor_token_dedent);
}

CNOR_TEST(closes_every_level_it_entered)
{
    lexed_t l;

    lex_all("fn f():\n    if x:\n        return 1\n", &l);
    CHECK_EQ(count_kind(&l, cnor_token_indent), 2U);
    CHECK_EQ(count_kind(&l, cnor_token_dedent), 2U);
}

CNOR_TEST(counts_a_tab_as_four_columns)
{
    lexed_t l;

    lex_all("if x:\n\treturn 1\n", &l);
    CHECK_EQ(count_kind(&l, cnor_token_indent), 1U);
}

CNOR_TEST(ignores_blank_lines_when_measuring_a_block)
{
    lexed_t l;

    lex_all("if x:\n\n\n    return 1\n", &l);
    CHECK_EQ(count_kind(&l, cnor_token_indent), 1U);
    CHECK_EQ(count_kind(&l, cnor_token_dedent), 1U);
}

CNOR_TEST(holds_a_line_together_inside_brackets)
{
    lexed_t l;

    lex_all("let x = add(\n    1,\n    2,\n)\n", &l);
    CHECK_EQ(count_kind(&l, cnor_token_indent), 0U);
    CHECK_EQ(count_kind(&l, cnor_token_dedent), 0U);
    CHECK_EQ(count_kind(&l, cnor_token_newline), 1U);
}

CNOR_TEST(drops_a_line_comment_entirely)
{
    lexed_t l;

    /* The spec's comment is `#`; `//` is the original lexer's and is gone. */
    lex_all("let x = 1 # and some words\n", &l);
    CHECK_EQ(count_kind(&l, cnor_token_ident), 1U);
    CHECK_EQ(l.error, cnor_error_none);
}

CNOR_TEST(refuses_a_string_the_line_never_closes)
{
    lexed_t l;

    lex_all("let x = \"oops\n", &l);
    CHECK_EQ(l.error, cnor_error_unterminated_string);
}

CNOR_TEST(refuses_an_indent_nobody_opened)
{
    lexed_t l;

    lex_all("fn f():\n    return 1\n  return 2\n", &l);
    CHECK_EQ(l.error, cnor_error_bad_indent);
}

CNOR_TEST(closes_open_blocks_at_the_end_of_the_source)
{
    lexed_t l;

    lex_all("fn f():\n    if x:\n        return 1", &l);
    CHECK_EQ(l.error, cnor_error_none);
    CHECK_EQ(count_kind(&l, cnor_token_dedent), 2U);
}

CNOR_TEST(refuses_a_character_that_starts_nothing)
{
    lexed_t l;

    lex_all("let x = @", &l);
    CHECK_EQ(l.error, cnor_error_bad_character);
}

CNOR_TEST(counts_lines_from_one)
{
    lexed_t l;

    /* Five tokens and a newline per line. */
    lex_all("let a = 1\nlet b = 2\nlet c = 3\n", &l);
    REQUIRE(l.count > 12U);
    CHECK_EQ(l.tokens[0].line, 1U);
    CHECK_EQ(l.tokens[5].line, 2U);
    CHECK_EQ(l.tokens[10].line, 3U);
}

CNOR_TEST(tells_a_keyword_from_a_name_that_starts_like_one)
{
    lexed_t l;

    lex_all("fn ifx if", &l);
    CHECK_EQ(l.tokens[0].keyword, cnor_keyword_fn);
    CHECK_EQ(l.tokens[1].kind, cnor_token_ident);
    CHECK_EQ(l.tokens[2].keyword, cnor_keyword_if);
}

CNOR_TEST(keeps_yielding_eof_once_the_source_is_used_up)
{
    cnor_lexer_t lexer;
    cnor_token_t token;
    unsigned i;

    cnor_lexer_init(&lexer, view_of("x"));
    for (i = 0U; i < 3U; ++i)
    {
        REQUIRE(cnor_lexer_next(&lexer, &token));
    }
    CHECK_EQ(token.kind, cnor_token_eof);
    CHECK(cnor_lexer_next(&lexer, &token));
    CHECK_EQ(token.kind, cnor_token_eof);
    CHECK(cnor_diag_is_ok(cnor_lexer_get_diag(&lexer)));
}

CNOR_TEST(reads_an_empty_source_as_just_the_end)
{
    lexed_t l;

    lex_all("", &l);
    REQUIRE(l.count == 1U);
    CHECK_EQ(l.tokens[0].kind, cnor_token_eof);
}

/* -- new ------------------------------------------------------------------- */

CNOR_TEST(reads_a_real_but_leaves_a_range_alone)
{
    lexed_t l;

    lex_all("1.5 0..10", &l);
    REQUIRE(l.count == 6U);
    CHECK_EQ(l.tokens[0].kind, cnor_token_real);
    CHECK(text_is(&l, 0U, "1.5"));
    CHECK_EQ(l.tokens[1].kind, cnor_token_int);
    CHECK_EQ(l.tokens[2].punct, cnor_punct_range);
    CHECK_EQ(l.tokens[3].kind, cnor_token_int);
}

CNOR_TEST(reads_two_character_operators_greedily)
{
    lexed_t l;
    const cnor_punct_t expected[] = {cnor_punct_eq, cnor_punct_ne, cnor_punct_le, cnor_punct_ge,
                                     cnor_punct_fat_arrow, cnor_punct_arrow, cnor_punct_lt};
    unsigned i;

    lex_all("== != <= >= => -> <", &l);
    REQUIRE(l.count == 9U);
    for (i = 0U; i < 7U; ++i)
    {
        CHECK_EQ(l.tokens[i].punct, expected[i]);
    }
}

CNOR_TEST(accepts_a_semicolon_between_statements)
{
    lexed_t l;

    lex_all("a = 1; b = 2", &l);
    CHECK_EQ(l.error, cnor_error_none);
    REQUIRE(l.count == 9U);
    CHECK_EQ(l.tokens[3].punct, cnor_punct_semicolon);
}

CNOR_TEST(knows_every_keyword_of_the_spec)
{
    const char *words[] = {"fn",    "let",      "mut",  "if",    "else", "match",
                           "for",   "in",       "while", "return", "break", "continue",
                           "true",  "false",    "none", "and",   "or",   "not",
                           "import", "native"};
    unsigned i;

    for (i = 0U; i < sizeof(words) / sizeof(words[0]); ++i)
    {
        CHECK(cnor_lexer_keyword_of(view_of(words[i])) != cnor_keyword_none);
    }
    CHECK_EQ(cnor_lexer_keyword_of(view_of("fnx")), cnor_keyword_none);
}

static const cnor_test_case_t m_cases[] = {
    CNOR_TEST_CASE(reads_names_numbers_and_punctuation),
    CNOR_TEST_CASE(keeps_a_number_a_name_and_the_arrow_apart),
    CNOR_TEST_CASE(reads_hexadecimal_and_binary),
    CNOR_TEST_CASE(spans_a_string_including_its_quotes),
    CNOR_TEST_CASE(does_not_end_a_string_on_an_escaped_quote),
    CNOR_TEST_CASE(turns_a_deeper_line_into_an_indent),
    CNOR_TEST_CASE(closes_every_level_it_entered),
    CNOR_TEST_CASE(counts_a_tab_as_four_columns),
    CNOR_TEST_CASE(ignores_blank_lines_when_measuring_a_block),
    CNOR_TEST_CASE(holds_a_line_together_inside_brackets),
    CNOR_TEST_CASE(drops_a_line_comment_entirely),
    CNOR_TEST_CASE(refuses_a_string_the_line_never_closes),
    CNOR_TEST_CASE(refuses_an_indent_nobody_opened),
    CNOR_TEST_CASE(closes_open_blocks_at_the_end_of_the_source),
    CNOR_TEST_CASE(refuses_a_character_that_starts_nothing),
    CNOR_TEST_CASE(counts_lines_from_one),
    CNOR_TEST_CASE(tells_a_keyword_from_a_name_that_starts_like_one),
    CNOR_TEST_CASE(keeps_yielding_eof_once_the_source_is_used_up),
    CNOR_TEST_CASE(reads_an_empty_source_as_just_the_end),
    CNOR_TEST_CASE(reads_a_real_but_leaves_a_range_alone),
    CNOR_TEST_CASE(reads_two_character_operators_greedily),
    CNOR_TEST_CASE(accepts_a_semicolon_between_statements),
    CNOR_TEST_CASE(knows_every_keyword_of_the_spec),
};

int
main(int argc, char **argv)
{
    return cnor_test_main("lexer", m_cases, CNOR_TEST_COUNT(m_cases), argc, argv);
}
