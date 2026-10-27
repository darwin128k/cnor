/* Parser: ported from the original suite (adapted to the cnor tree shape and
 * the spec's match arms), plus cases for what the rework added. */

#include "harness.h"

#include <cnor/arena.h>
#include <cnor/parser.h>
#include <lh/null.h>
#include <lh/real.h>

/* -- helpers --------------------------------------------------------------- */

typedef struct parsed
{
    cnor_arena_t arena;
    cnor_parser_t parser;
    const cnor_node_t *tree;
    cnor_error_t error;
    cnor_line_t line;
} parsed_t;

static void
parse(parsed_t *out, const char *text)
{
    lh_str_view_t src;

    /* lh refuses a sized view of zero bytes; an empty source is the empty view. */
    if (strlen(text) == 0U)
    {
        lh_str_view_init_empty(&src);
    }
    else
    {
        lh_str_init_by_size(&src, text, strlen(text));
    }
    cnor_arena_init(&out->arena, 0U);
    cnor_parser_init(&out->parser, src, &out->arena);
    out->tree = cnor_parser_parse(&out->parser);
    out->error = cnor_diag_get_code(cnor_parser_get_diag(&out->parser));
    out->line = cnor_diag_get_line(cnor_parser_get_diag(&out->parser));
}

static void
done(parsed_t *p)
{
    cnor_arena_deinit(&p->arena);
}

static int
name_is(const cnor_node_t *node, const char *expected)
{
    return node != lh_null && lh_str_view_get_size(&node->name) == strlen(expected)
           && memcmp(lh_str_view_get_data(&node->name), expected, strlen(expected)) == 0;
}

/* The expression of `let a = <expr>` or of an expression statement. */
static const cnor_node_t *
init_of(const cnor_node_t *node)
{
    if (node == lh_null)
    {
        return lh_null;
    }
    return node->kind == cnor_node_let ? node->data.let.init : node->data.expr_stmt.expr;
}

/* -- ported ---------------------------------------------------------------- */

CNOR_TEST(reads_a_function_with_a_body)
{
    parsed_t p;

    parse(&p, "fn f(x: i32) -> i32:\n    return x\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(p.tree->kind, cnor_node_fn);
    CHECK(name_is(p.tree, "f"));
    CHECK_EQ(p.tree->data.fn.returns, cnor_type_i32);
    CHECK_EQ(p.tree->data.fn.param_count, 1U);
    CHECK(name_is(p.tree->data.fn.params, "x"));
    REQUIRE(p.tree->data.fn.body != lh_null);
    CHECK_EQ(p.tree->data.fn.body->kind, cnor_node_return);
    done(&p);
}

CNOR_TEST(reads_several_functions_into_one_list)
{
    parsed_t p;

    parse(&p, "fn a():\n    return 1\nfn b():\n    return 2\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(cnor_node_list_length(p.tree), 2U);
    CHECK(name_is(p.tree, "a"));
    CHECK(name_is(p.tree->next, "b"));
    done(&p);
}

CNOR_TEST(binds_names_with_let)
{
    parsed_t p;

    parse(&p, "let mut total = 1 + 2\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->kind == cnor_node_let);
    CHECK(p.tree->data.let.is_mutable);
    CHECK(name_is(p.tree, "total"));
    REQUIRE(init_of(p.tree) != lh_null);
    CHECK_EQ(init_of(p.tree)->kind, cnor_node_binary);
    CHECK_EQ(init_of(p.tree)->data.binary.op, cnor_binary_add);
    done(&p);
}

CNOR_TEST(reads_a_number_in_three_bases)
{
    parsed_t p;

    parse(&p, "let a = 0x1F\nlet b = 0b101\nlet c = 42\n");
    REQUIRE(p.tree != lh_null && p.tree->next != lh_null && p.tree->next->next != lh_null);
    CHECK_EQ(init_of(p.tree)->data.int_.value, 31U);
    CHECK_EQ(init_of(p.tree->next)->data.int_.value, 5U);
    CHECK_EQ(init_of(p.tree->next->next)->data.int_.value, 42U);
    done(&p);
}

CNOR_TEST(refuses_a_digit_the_base_does_not_have)
{
    parsed_t p;

    parse(&p, "let a = 0b12\n");
    CHECK(p.tree == lh_null);
    CHECK_EQ(p.error, cnor_error_bad_number);
    done(&p);
}

CNOR_TEST(keeps_a_string_without_its_quotes)
{
    parsed_t p;

    parse(&p, "let a = \"hi\"\n");
    REQUIRE(p.tree != lh_null);
    CHECK(name_is(init_of(p.tree), "hi"));
    done(&p);
}

CNOR_TEST(binds_multiplication_tighter_than_addition)
{
    parsed_t p;

    parse(&p, "let a = 1 + 2 * 3\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(init_of(p.tree)->data.binary.op, cnor_binary_add);
    CHECK_EQ(init_of(p.tree)->data.binary.right->data.binary.op, cnor_binary_mul);
    done(&p);
}

CNOR_TEST(binds_comparison_looser_than_addition)
{
    parsed_t p;

    parse(&p, "let a = 1 + 2 < 4\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(init_of(p.tree)->data.binary.op, cnor_binary_lt);
    CHECK_EQ(init_of(p.tree)->data.binary.left->data.binary.op, cnor_binary_add);
    done(&p);
}

CNOR_TEST(reads_chained_comparison_left_to_right)
{
    parsed_t p;

    parse(&p, "let a = 1 < 2 < 3\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(init_of(p.tree)->data.binary.op, cnor_binary_lt);
    CHECK_EQ(init_of(p.tree)->data.binary.left->data.binary.op, cnor_binary_lt);
    done(&p);
}

CNOR_TEST(reads_a_prefix_minus)
{
    parsed_t p;

    parse(&p, "let a = -5\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(init_of(p.tree)->kind, cnor_node_unary);
    CHECK_EQ(init_of(p.tree)->data.unary.op, cnor_unary_neg);
    CHECK_EQ(init_of(p.tree)->data.unary.operand->data.int_.value, 5U);
    done(&p);
}

CNOR_TEST(reads_a_call_with_its_arguments)
{
    parsed_t p;

    parse(&p, "let a = f(1, 2)\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(init_of(p.tree)->kind == cnor_node_call);
    CHECK(name_is(init_of(p.tree), "f"));
    CHECK_EQ(init_of(p.tree)->data.call.argc, 2U);
    CHECK_EQ(cnor_node_list_length(init_of(p.tree)->data.call.args), 2U);
    done(&p);
}

CNOR_TEST(reads_a_call_with_no_arguments)
{
    parsed_t p;

    parse(&p, "f()\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(p.tree->kind, cnor_node_expr_stmt);
    CHECK_EQ(init_of(p.tree)->kind, cnor_node_call);
    CHECK(init_of(p.tree)->data.call.args == lh_null);
    done(&p);
}

CNOR_TEST(reads_an_assignment)
{
    parsed_t p;

    parse(&p, "x = 7\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->kind == cnor_node_assign);
    CHECK(name_is(p.tree->data.assign.target, "x"));
    CHECK_EQ(p.tree->data.assign.value->data.int_.value, 7U);
    done(&p);
}

CNOR_TEST(reads_an_if_with_an_else)
{
    parsed_t p;

    parse(&p, "if x > 1:\n    return 1\nelse:\n    return 2\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->kind == cnor_node_if);
    CHECK_EQ(p.tree->data.if_.cond->kind, cnor_node_binary);
    CHECK_EQ(p.tree->data.if_.then_body->kind, cnor_node_return);
    REQUIRE(p.tree->data.if_.else_body != lh_null);
    CHECK_EQ(p.tree->data.if_.else_body->kind, cnor_node_return);
    done(&p);
}

CNOR_TEST(chains_an_else_if)
{
    parsed_t p;

    parse(&p, "if x:\n    return 1\nelse if y:\n    return 2\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->data.if_.else_body != lh_null);
    CHECK_EQ(p.tree->data.if_.else_body->kind, cnor_node_if);
    CHECK(p.tree->data.if_.else_body->data.if_.else_body == lh_null);
    done(&p);
}

CNOR_TEST(reads_a_while_loop)
{
    parsed_t p;

    parse(&p, "while x < 10:\n    x = x + 1\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->kind == cnor_node_while);
    CHECK_EQ(p.tree->data.while_.cond->data.binary.op, cnor_binary_lt);
    REQUIRE(p.tree->data.while_.body != lh_null);
    CHECK_EQ(p.tree->data.while_.body->kind, cnor_node_assign);
    done(&p);
}

CNOR_TEST(reads_a_for_loop_over_a_range)
{
    parsed_t p;

    parse(&p, "for i in 0..10:\n    return i\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->kind == cnor_node_for);
    CHECK(name_is(p.tree, "i"));
    CHECK_EQ(p.tree->data.for_.from->data.int_.value, 0U);
    CHECK_EQ(p.tree->data.for_.to->data.int_.value, 10U);
    CHECK(p.tree->data.for_.body != lh_null);
    done(&p);
}

CNOR_TEST(reads_a_match_with_its_arms)
{
    parsed_t p;
    const cnor_node_t *arm;

    parse(&p, "match x:\n    1 => 10\n    2 => 20\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->kind == cnor_node_match);
    CHECK(name_is(p.tree->data.match.subject, "x"));
    CHECK_EQ(cnor_node_list_length(p.tree->data.match.arms), 2U);
    arm = p.tree->data.match.arms;
    CHECK_EQ(arm->data.arm.pattern->data.int_.value, 1U);
    CHECK_EQ(init_of(arm->data.arm.body)->data.int_.value, 10U);
    CHECK_EQ(arm->next->data.arm.pattern->data.int_.value, 2U);
    CHECK_EQ(init_of(arm->next->data.arm.body)->data.int_.value, 20U);
    done(&p);
}

CNOR_TEST(keeps_the_underscore_arm_without_a_pattern)
{
    parsed_t p;
    const cnor_node_t *arm;

    /* cnor keeps `_` in source order as an arm with no pattern. */
    parse(&p, "match x:\n    1 => 10\n    _ => 0\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(cnor_node_list_length(p.tree->data.match.arms), 2U);
    arm = p.tree->data.match.arms->next;
    REQUIRE(arm != lh_null);
    CHECK(arm->data.arm.pattern == lh_null);
    CHECK_EQ(init_of(arm->data.arm.body)->data.int_.value, 0U);
    done(&p);
}

CNOR_TEST(refuses_a_name_as_a_match_pattern)
{
    parsed_t p;

    /* The original bound a name here; the spec's arms are literals or `_`. */
    parse(&p, "match x:\n    other => 1\n");
    CHECK(p.tree == lh_null);
    CHECK_EQ(p.error, cnor_error_unexpected_token);
    done(&p);
}

CNOR_TEST(reads_a_bare_return_as_a_return_with_no_value)
{
    parsed_t p;

    parse(&p, "fn f():\n    return\n");
    REQUIRE(p.tree != lh_null);
    REQUIRE(p.tree->data.fn.body != lh_null);
    CHECK_EQ(p.tree->data.fn.body->kind, cnor_node_return);
    CHECK(p.tree->data.fn.body->data.return_.value == lh_null);
    done(&p);
}

CNOR_TEST(reads_a_native_declaration)
{
    parsed_t p;

    parse(&p, "native fn tick()\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(p.tree->kind, cnor_node_native_fn);
    CHECK(name_is(p.tree, "tick"));
    done(&p);
}

CNOR_TEST(keeps_names_out_of_the_source_buffer)
{
    parsed_t p;
    char text[] = "let a = \"hi\"\n";

    parse(&p, text);
    REQUIRE(p.tree != lh_null);
    memset(text, 'z', sizeof(text) - 1U);
    CHECK(name_is(p.tree, "a"));
    CHECK(name_is(init_of(p.tree), "hi"));
    done(&p);
}

CNOR_TEST(reports_the_line_a_syntax_error_is_on)
{
    parsed_t p;

    parse(&p, "fn f():\n    return 1\nfn g():\n    let = 2\n");
    CHECK(p.tree == lh_null);
    CHECK(p.error != cnor_error_none);
    CHECK_EQ(p.line, 4U);
    done(&p);
}

CNOR_TEST(keeps_a_named_type_for_the_compiler)
{
    parsed_t p;

    parse(&p, "fn f(x: widget) -> i32:\n    return 1\n");
    CHECK(p.tree != lh_null);
    CHECK_EQ(p.tree->data.fn.params->data.param.type, cnor_type_named);
    CHECK_EQ(lh_str_view_get_size(&p.tree->data.fn.params->type_name), 6U);
    CHECK(memcmp(lh_str_view_get_data(&p.tree->data.fn.params->type_name), "widget", 6U) == 0);
    done(&p);
}

CNOR_TEST(refuses_a_block_with_nothing_in_it)
{
    parsed_t p;

    parse(&p, "fn f():\n");
    CHECK(p.tree == lh_null);
    CHECK_EQ(p.error, cnor_error_empty_block);
    done(&p);
}

CNOR_TEST(refuses_source_with_nothing_in_it_at_all)
{
    parsed_t p;

    parse(&p, "");
    CHECK(p.tree == lh_null);
    CHECK_EQ(p.error, cnor_error_empty_block);
    done(&p);
}

CNOR_TEST(refuses_a_bad_character_with_its_line)
{
    parsed_t p;

    parse(&p, "fn f():\n    return 1\nlet x = @\n");
    CHECK(p.tree == lh_null);
    CHECK_EQ(p.error, cnor_error_bad_character);
    CHECK_EQ(p.line, 3U);
    done(&p);
}

CNOR_TEST(keeps_comments_out_of_the_tree)
{
    parsed_t p;

    parse(&p, "# leading\nfn f():\n    # inside\n    return 1  # done\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(cnor_node_list_length(p.tree), 1U);
    CHECK_EQ(p.tree->data.fn.body->kind, cnor_node_return);
    done(&p);
}

CNOR_TEST(releases_its_arena)
{
    parsed_t p;

    parse(&p, "fn f():\n    return 1\n");
    REQUIRE(p.tree != lh_null);
    CHECK(cnor_arena_get_total(&p.arena) > 0U);
    done(&p);
    CHECK_EQ(cnor_arena_get_total(&p.arena), 0U);
}

/* -- new ------------------------------------------------------------------- */

CNOR_TEST(binds_and_tighter_than_or_and_not_tightest)
{
    parsed_t p;
    const cnor_node_t *e;

    parse(&p, "let a = not x or y and z\n");
    REQUIRE(p.tree != lh_null);
    e = init_of(p.tree);
    CHECK_EQ(e->data.binary.op, cnor_binary_or);
    CHECK_EQ(e->data.binary.left->kind, cnor_node_unary);
    CHECK_EQ(e->data.binary.right->data.binary.op, cnor_binary_and);
    done(&p);
}

CNOR_TEST(reads_statements_split_by_semicolons)
{
    parsed_t p;

    parse(&p, "let a = 1; let b = 2;\nb = a\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(cnor_node_list_length(p.tree), 3U);
    done(&p);
}

CNOR_TEST(reads_a_real_literal_as_text)
{
    parsed_t p;

    parse(&p, "let r = 2.5\n");
#if LH_REAL_ENABLED
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(init_of(p.tree)->kind, cnor_node_real);
    CHECK(name_is(init_of(p.tree), "2.5"));
#else
    CHECK_EQ(p.error, cnor_error_no_reals);
#endif
    done(&p);
}

CNOR_TEST(reads_an_index_and_a_typed_let)
{
    parsed_t p;

    parse(&p, "let c: str = s[1]\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(p.tree->data.let.type, cnor_type_str);
    CHECK_EQ(init_of(p.tree)->kind, cnor_node_index);
    done(&p);
}

CNOR_TEST(unescapes_a_string)
{
    parsed_t p;

    parse(&p, "let s = \"a\\n\\\"b\"\n");
    REQUIRE(p.tree != lh_null);
    CHECK(name_is(init_of(p.tree), "a\n\"b"));
    done(&p);
}

CNOR_TEST(reads_imports_at_the_top_of_a_file_only)
{
    parsed_t p;

    parse(&p, "# header\nimport \"a.cn\"\nimport \"lib/b.cn\";\nlet x = 1\n");
    REQUIRE(p.tree != lh_null);
    CHECK_EQ(p.tree->kind, cnor_node_import);
    CHECK(name_is(p.tree, "a.cn"));
    CHECK_EQ(p.tree->line, 2U);
    REQUIRE(p.tree->next != lh_null);
    CHECK_EQ(p.tree->next->kind, cnor_node_import);
    CHECK(name_is(p.tree->next, "lib/b.cn"));
    CHECK_EQ(p.tree->next->next->kind, cnor_node_let);
    done(&p);
    parse(&p, "let x = 1\nimport \"a.cn\"\n");
    CHECK(p.tree == lh_null);
    CHECK_EQ(p.error, cnor_error_misplaced_import);
    CHECK_EQ(p.line, 2U);
    done(&p);
    parse(&p, "fn f():\n    import \"a.cn\"\n");
    CHECK_EQ(p.error, cnor_error_misplaced_import);
    done(&p);
    parse(&p, "if true:\n    import \"a.cn\"\n");
    CHECK_EQ(p.error, cnor_error_misplaced_import);
    done(&p);
    /* The path is a string literal. */
    parse(&p, "import a\n");
    CHECK(p.tree == lh_null);
    CHECK(p.error != cnor_error_none);
    done(&p);
}


CNOR_TEST(reads_collection_annotations_and_lists)
{
    parsed_t p;
    parse(&p, "let a: array[i32] = [1, 2, 3,]\n");
    CHECK(p.tree != lh_null);
    CHECK_EQ(p.tree->data.let.type, cnor_type_array);
    CHECK_EQ(p.tree->element_type, cnor_type_i32);
    CHECK(init_of(p.tree)->data.array.is_list);
    CHECK_EQ(init_of(p.tree)->data.array.count, 3U);
    CHECK_EQ(cnor_node_list_length(init_of(p.tree)->data.array.value), 3U);
    done(&p);
    parse(&p, "let a = []\n");
    CHECK(p.tree != lh_null);
    CHECK(init_of(p.tree)->data.array.is_list);
    CHECK_EQ(init_of(p.tree)->data.array.count, 0U);
    CHECK(init_of(p.tree)->data.array.value == lh_null);
    done(&p);
    parse(&p, "fn f(d: dict[State]) -> array[str]:\n    return\n");
    CHECK(p.tree != lh_null);
    CHECK_EQ(p.tree->element_type, cnor_type_str);
    CHECK_EQ(p.tree->data.fn.params->element_type, cnor_type_named);
    CHECK_EQ(lh_str_view_get_size(&p.tree->data.fn.params->element_type_name), 5U);
    done(&p);
    parse(&p, "let a = [0; 4]\n");
    CHECK(p.tree != lh_null);
    CHECK(!init_of(p.tree)->data.array.is_list);
    CHECK_EQ(init_of(p.tree)->data.array.count, 4U);
    done(&p);
}

CNOR_TEST(refuses_malformed_lists_and_collection_annotations)
{
    static const char *const cases[] = {
        "let a = [1,, 2]\n", "let a = [1, 2; 3]\n", "let a = [1 2]\n",
        "let a = [; 3]\n", "let a = [1,\n", "let a = [1; 2, 3]\n",
        "let a: array[] = []\n", "let a: array[i32, str] = []\n",
        "let a: array[array[i32]] = []\n", "let a: i32[str] = 1\n",
        "let a: array[i32 = []\n",
    };
    size_t i;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        parsed_t p;
        parse(&p, cases[i]);
        CHECK(p.tree == lh_null);
        CHECK(p.error != cnor_error_none);
        done(&p);
    }
}


CNOR_TEST(reads_aliased_imports_and_qualified_names)
{
    parsed_t p;
    parse(&p, "import \"math.cn\" as math\nmath.clamp(1, 0, 10)\n");
    CHECK(p.tree != lh_null);
    CHECK_EQ(p.tree->kind, cnor_node_import);
    CHECK(name_is(p.tree, "math.cn"));
    CHECK_EQ(lh_str_view_get_size(&p.tree->data.import_.alias), 4U);
    CHECK(memcmp(lh_str_view_get_data(&p.tree->data.import_.alias), "math", 4U) == 0);
    CHECK(name_is(init_of(p.tree->next), "math.clamp"));
    CHECK_EQ(init_of(p.tree->next)->data.call.argc, 3U);
    done(&p);
    parse(&p, "import \"game.cn\" as game\nlet p: game.Player = game.Player(10)\n");
    CHECK(p.tree != lh_null);
    CHECK_EQ(p.tree->next->data.let.type, cnor_type_named);
    CHECK_EQ(lh_str_view_get_size(&p.tree->next->type_name), 11U);
    CHECK(name_is(init_of(p.tree->next), "game.Player"));
    done(&p);
    parse(&p, "let states: array[game.State] = []\n");
    CHECK(p.tree != lh_null);
    CHECK_EQ(p.tree->element_type, cnor_type_named);
    CHECK_EQ(lh_str_view_get_size(&p.tree->element_type_name), 10U);
    done(&p);
    parse(&p, "match state:\n    game.State.idle => 1\n");
    CHECK(p.tree != lh_null);
    CHECK_EQ(p.tree->data.match.arms->data.arm.pattern->kind, cnor_node_member);
    done(&p);
}

CNOR_TEST(refuses_malformed_namespace_imports)
{
    static const char *const cases[] = {
        "import \"a.cn\" as\n", "import \"a.cn\" as 1\n",
        "import \"a.cn\" as a.b\n", "import \"a.cn\" as if\n",
        "import \"a.cn\" a\n", "import \"a.cn\" as a as b\n",
        "let x: game. = 1\n", "let x: array[game.] = []\n",
        "game.(1)\n",
    };
    size_t i;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        parsed_t p;
        parse(&p, cases[i]);
        CHECK(p.tree == lh_null);
        CHECK(p.error != cnor_error_none);
        done(&p);
    }
}

static const cnor_test_case_t m_cases[] = {
    CNOR_TEST_CASE(reads_aliased_imports_and_qualified_names),
    CNOR_TEST_CASE(refuses_malformed_namespace_imports),
    CNOR_TEST_CASE(reads_collection_annotations_and_lists),
    CNOR_TEST_CASE(refuses_malformed_lists_and_collection_annotations),
    CNOR_TEST_CASE(reads_a_function_with_a_body),
    CNOR_TEST_CASE(reads_several_functions_into_one_list),
    CNOR_TEST_CASE(binds_names_with_let),
    CNOR_TEST_CASE(reads_a_number_in_three_bases),
    CNOR_TEST_CASE(refuses_a_digit_the_base_does_not_have),
    CNOR_TEST_CASE(keeps_a_string_without_its_quotes),
    CNOR_TEST_CASE(binds_multiplication_tighter_than_addition),
    CNOR_TEST_CASE(binds_comparison_looser_than_addition),
    CNOR_TEST_CASE(reads_chained_comparison_left_to_right),
    CNOR_TEST_CASE(reads_a_prefix_minus),
    CNOR_TEST_CASE(reads_a_call_with_its_arguments),
    CNOR_TEST_CASE(reads_a_call_with_no_arguments),
    CNOR_TEST_CASE(reads_an_assignment),
    CNOR_TEST_CASE(reads_an_if_with_an_else),
    CNOR_TEST_CASE(chains_an_else_if),
    CNOR_TEST_CASE(reads_a_while_loop),
    CNOR_TEST_CASE(reads_a_for_loop_over_a_range),
    CNOR_TEST_CASE(reads_a_match_with_its_arms),
    CNOR_TEST_CASE(keeps_the_underscore_arm_without_a_pattern),
    CNOR_TEST_CASE(refuses_a_name_as_a_match_pattern),
    CNOR_TEST_CASE(reads_a_bare_return_as_a_return_with_no_value),
    CNOR_TEST_CASE(reads_a_native_declaration),
    CNOR_TEST_CASE(keeps_names_out_of_the_source_buffer),
    CNOR_TEST_CASE(reports_the_line_a_syntax_error_is_on),
    CNOR_TEST_CASE(keeps_a_named_type_for_the_compiler),
    CNOR_TEST_CASE(refuses_a_block_with_nothing_in_it),
    CNOR_TEST_CASE(refuses_source_with_nothing_in_it_at_all),
    CNOR_TEST_CASE(refuses_a_bad_character_with_its_line),
    CNOR_TEST_CASE(keeps_comments_out_of_the_tree),
    CNOR_TEST_CASE(releases_its_arena),
    CNOR_TEST_CASE(binds_and_tighter_than_or_and_not_tightest),
    CNOR_TEST_CASE(reads_statements_split_by_semicolons),
    CNOR_TEST_CASE(reads_a_real_literal_as_text),
    CNOR_TEST_CASE(reads_an_index_and_a_typed_let),
    CNOR_TEST_CASE(unescapes_a_string),
    CNOR_TEST_CASE(reads_imports_at_the_top_of_a_file_only),
};

int
main(int argc, char **argv)
{
    return cnor_test_main("parser", m_cases, CNOR_TEST_COUNT(m_cases), argc, argv);
}
