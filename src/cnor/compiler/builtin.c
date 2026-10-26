#include <cnor/builtin.h>
#include <cnor/compiler/internal.h>
#include <lh/null.h>
#include <lh/str/view.h>
#include <string.h>

/* What each builtin of the language takes and gives. The names and the
 * argument counts live in `src/cnor/builtin.c`, which the machine reads too;
 * the types are the compiler's business alone. */

struct cnor_builtin_rule
{
    cnor_static_type_t params[3];
    cnor_static_type_t result;
};

static const struct cnor_builtin_rule m_rules[cnor_builtin_count] = {
    [cnor_builtin_find] = {{cnor_static_str, cnor_static_str}, cnor_static_i32},
    [cnor_builtin_contains] = {{cnor_static_str, cnor_static_str}, cnor_static_bool},
    [cnor_builtin_replace] = {{cnor_static_str, cnor_static_str, cnor_static_str},
                              cnor_static_str},
    [cnor_builtin_trim] = {{cnor_static_str}, cnor_static_str},
    [cnor_builtin_upper] = {{cnor_static_str}, cnor_static_str},
    [cnor_builtin_lower] = {{cnor_static_str}, cnor_static_str},
    [cnor_builtin_starts_with] = {{cnor_static_str, cnor_static_str}, cnor_static_bool},
    [cnor_builtin_ends_with] = {{cnor_static_str, cnor_static_str}, cnor_static_bool},
    [cnor_builtin_substr] = {{cnor_static_str, cnor_static_i32, cnor_static_i32},
                              cnor_static_str},
    [cnor_builtin_ord] = {{cnor_static_str}, cnor_static_i32},
    [cnor_builtin_char] = {{cnor_static_i32}, cnor_static_str},
    [cnor_builtin_str] = {{cnor_static_any}, cnor_static_str},
    [cnor_builtin_parse_int] = {{cnor_static_str}, cnor_static_i32},
};

lh_bool_t
cnor_compiler_builtin_find(lh_str_view_t name, cnor_builtin_t *out)
{
    lh_uint_t i;

    for (i = 0U; i < (lh_uint_t) cnor_builtin_count; ++i)
    {
        const lh_char_t *const spelling = cnor_builtin_name((cnor_builtin_t) i);
        lh_str_view_t view;

        if (spelling == lh_null)
        {
            continue;
        }
        view.begin = (void *) spelling;
        view.size = strlen(spelling);
        if (lh_str_view_equals(&name, &view, lh_bool_false))
        {
            *out = (cnor_builtin_t) i;
            return lh_bool_true;
        }
    }
    return lh_bool_false;
}

const cnor_static_type_t *
cnor_compiler_builtin_params(cnor_builtin_t builtin)
{
    if ((lh_uint_t) builtin >= (lh_uint_t) cnor_builtin_count)
    {
        return lh_null;
    }
    return m_rules[builtin].params;
}

cnor_static_type_t
cnor_compiler_builtin_result(cnor_builtin_t builtin)
{
    if ((lh_uint_t) builtin >= (lh_uint_t) cnor_builtin_count)
    {
        return cnor_static_any;
    }
    return m_rules[builtin].result;
}
