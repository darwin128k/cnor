#include <cnor/builtin.h>
#include <lh/null.h>

/* The names and the argument counts of the builtins. The compiler reads the
 * names to recognize a call (`src/cnor/compiler/builtin.c`), the machine the
 * counts to check a call (`src/cnor/vm/op/builtin.c`); both read this table,
 * so they cannot drift apart. */

static const lh_char_t *const m_names[cnor_builtin_count] = {
    [cnor_builtin_find] = "find",
    [cnor_builtin_contains] = "contains",
    [cnor_builtin_replace] = "replace",
    [cnor_builtin_trim] = "trim",
    [cnor_builtin_upper] = "upper",
    [cnor_builtin_lower] = "lower",
    [cnor_builtin_starts_with] = "starts_with",
    [cnor_builtin_ends_with] = "ends_with",
    [cnor_builtin_substr] = "substr",
    [cnor_builtin_ord] = "ord",
    [cnor_builtin_char] = "char",
    [cnor_builtin_str] = "str",
    [cnor_builtin_parse_int] = "parse_int",
    [cnor_builtin_split_into] = "split_into",
};

static const lh_u8_t m_argc[cnor_builtin_count] = {
    [cnor_builtin_find] = 2U,
    [cnor_builtin_contains] = 2U,
    [cnor_builtin_replace] = 3U,
    [cnor_builtin_trim] = 1U,
    [cnor_builtin_upper] = 1U,
    [cnor_builtin_lower] = 1U,
    [cnor_builtin_starts_with] = 2U,
    [cnor_builtin_ends_with] = 2U,
    [cnor_builtin_substr] = 3U,
    [cnor_builtin_ord] = 1U,
    [cnor_builtin_char] = 1U,
    [cnor_builtin_str] = 1U,
    [cnor_builtin_parse_int] = 1U,
    [cnor_builtin_split_into] = 3U,
};

const lh_char_t *
cnor_builtin_name(cnor_builtin_t builtin)
{
    if ((lh_uint_t) builtin >= (lh_uint_t) cnor_builtin_count)
    {
        return lh_null;
    }
    return m_names[builtin];
}

lh_u8_t
cnor_builtin_argc(cnor_builtin_t builtin)
{
    if ((lh_uint_t) builtin >= (lh_uint_t) cnor_builtin_count)
    {
        return 0U;
    }
    return m_argc[builtin];
}
