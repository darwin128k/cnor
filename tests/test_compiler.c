/* Compiler, end to end: source -> image -> VM. Ported from the original suite
 * (adapted where cnor decided differently) plus the constructs the original
 * compiler never compiled: if, loops, match, and/or, unary minus. */

#include "harness.h"

#include <cnor/compiler.h>
#include <cnor/native/initializer.h>
#include <cnor/vm.h>
#include <lh/null.h>
#include <lh/real.h>

/* -- helpers --------------------------------------------------------------- */

static lh_value_t
double_it(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    return lh_value_make_sint(lh_value_get_data(&args[0])->i * 2);
}

/* Counts its calls through the context, to observe short-circuiting. */
static lh_value_t
touch(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, cnor_error_t *error)
{
    (void) args;
    (void) argc;
    (void) error;
    (*(int *) ctx)++;
    return lh_value_make_bool(lh_bool_true);
}

static const cnor_native_t m_natives[] = {
    cnor_native_initializer("double_it", double_it, 1U, 1U),
    cnor_native_initializer("touch", touch, 0U, 0U),
};
static const cnor_native_table_t m_table = cnor_native_table_initializer(m_natives, 2U);
static int m_touched;

static lh_str_view_t
view_of(const char *text)
{
    lh_str_view_t view;

    lh_str_init_by_size(&view, text, strlen(text));
    return view;
}

typedef struct built
{
    cnor_image_t image;
    cnor_diag_t diag;
    lh_bool_t ok;
} built_t;

static void
compile_with(built_t *out, const char *source, lh_bool_t lines)
{
    cnor_compiler_config_t config = cnor_compiler_config_initializer();

    config.natives = &m_table;
    config.file = view_of("main.cn");
    config.emit_lines = lines;
    out->ok = cnor_compiler_compile_source(view_of(source), &config, &out->image, &out->diag);
    if (!out->ok)
    {
        printf("    compile failed: %s at line %u\n",
               cnor_error_get_message(cnor_diag_get_code(&out->diag)),
               (unsigned) cnor_diag_get_line(&out->diag));
    }
}

static void
compile(built_t *out, const char *source)
{
    compile_with(out, source, lh_bool_true);
}

static void
vm_open(cnor_vm_t *vm)
{
    cnor_vm_config_t config = cnor_vm_config_initializer();

    config.natives = &m_table;
    config.ctx = &m_touched;
    cnor_vm_init(vm, &config);
}

/* Compile, load, run; @p result receives the value. */
static int
run(const char *source, lh_value_t *result)
{
    built_t b;
    cnor_vm_t vm;
    int ok;

    compile(&b, source);
    if (!b.ok)
    {
        return 0;
    }
    vm_open(&vm);
    ok = cnor_vm_load(&vm, b.image.data, b.image.size) && cnor_vm_run(&vm);
    if (!ok)
    {
        printf("    vm failed: %s at line %u\n",
               cnor_error_get_message(cnor_diag_get_code(cnor_vm_get_diag(&vm))),
               (unsigned) cnor_diag_get_line(cnor_vm_get_diag(&vm)));
    }
    *result = cnor_vm_get_result(&vm);
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
    return ok;
}

/* Run and read a signed integer result; a sentinel on any failure. */
static long long
run_sint(const char *source)
{
    lh_value_t result;

    if (!run(source, &result) || lh_value_get_kind(&result) != lh_value_kind_sint)
    {
        return -999999;
    }
    return (long long) lh_value_get_data(&result)->i;
}

/* The run's result is a string equal to @p expected. Strings point into the
 * VM, so this compares before the VM goes. */
static int
run_text_is(const char *source, const char *expected)
{
    built_t b;
    cnor_vm_t vm;
    int same = 0;

    compile(&b, source);
    if (!b.ok)
    {
        return 0;
    }
    vm_open(&vm);
    if (cnor_vm_load(&vm, b.image.data, b.image.size) && cnor_vm_run(&vm))
    {
        const lh_value_t result = cnor_vm_get_result(&vm);

        if (lh_value_get_kind(&result) == lh_value_kind_str)
        {
            const lh_str_view_t text = lh_value_get_data(&result)->s;

            same = lh_str_view_get_size(&text) == strlen(expected)
                   && memcmp(lh_str_view_get_data(&text), expected, strlen(expected)) == 0;
        }
    }
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
    return same;
}

/* Compile only; the error code (none on success). The diagnostic's name is
 * copied out as text: it lives inside the diagnostic, which is gone on return. */
static cnor_error_t
compile_error(const char *source, cnor_line_t *line, char *name)
{
    built_t b;

    compile(&b, source);
    if (line != lh_null)
    {
        *line = cnor_diag_get_line(&b.diag);
    }
    if (name != lh_null)
    {
        const lh_str_view_t view = cnor_diag_get_name(&b.diag);
        const size_t size = lh_str_view_get_size(&view);

        if (size > 0U)
        {
            memcpy(name, lh_str_view_get_data(&view), size);
        }
        name[size] = '\0';
    }
    cnor_image_deinit(&b.image);
    return b.ok ? cnor_error_none : cnor_diag_get_code(&b.diag);
}

/* Compile and run; the VM's error code. */
static cnor_error_t
run_error(const char *source, cnor_line_t *line)
{
    built_t b;
    cnor_vm_t vm;
    cnor_error_t code;

    compile(&b, source);
    if (!b.ok)
    {
        return cnor_error_count;
    }
    vm_open(&vm);
    if (cnor_vm_load(&vm, b.image.data, b.image.size))
    {
        (void) cnor_vm_run(&vm);
    }
    code = cnor_diag_get_code(cnor_vm_get_diag(&vm));
    if (line != lh_null)
    {
        *line = cnor_diag_get_line(cnor_vm_get_diag(&vm));
    }
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
    return code;
}

/* Compile and run with an instruction budget; the VM's error code, and the
 * instructions the run executed through @p steps. */
static cnor_error_t
run_with_budget(const char *source, cnor_step_count_t max_steps, cnor_step_count_t *steps)
{
    built_t b;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_error_t code;

    compile(&b, source);
    if (!b.ok)
    {
        return cnor_error_count;
    }
    config.natives = &m_table;
    config.ctx = &m_touched;
    config.max_steps = max_steps;
    cnor_vm_init(&vm, &config);
    if (cnor_vm_load(&vm, b.image.data, b.image.size))
    {
        (void) cnor_vm_run(&vm);
    }
    code = cnor_diag_get_code(cnor_vm_get_diag(&vm));
    if (steps != lh_null)
    {
        *steps = cnor_vm_get_steps(&vm);
    }
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
    return code;
}

static int
name_is(lh_str_view_t name, const char *expected)
{
    return lh_str_view_get_size(&name) == strlen(expected)
           && memcmp(lh_str_view_get_data(&name), expected, strlen(expected)) == 0;
}

#define NAME_BUFFER_SIZE (CNOR_DIAG_NAME_CAPACITY + 1U)

/* -- ported ---------------------------------------------------------------- */

CNOR_TEST(runs_a_program_that_is_one_expression)
{
    CHECK_EQ(run_sint("40 + 2"), 42);
}

CNOR_TEST(calls_a_function_defined_earlier)
{
    CHECK_EQ(run_sint("fn add(a: i32, b: i32) -> i32:\n"
                      "    return a + b\n"
                      "add(40, 2)\n"),
             42);
}

CNOR_TEST(calls_a_function_defined_later_in_the_file)
{
    CHECK_EQ(run_sint("add(40, 2)\n"
                      "fn add(a: i32, b: i32) -> i32:\n"
                      "    return a + b\n"),
             42);
}

CNOR_TEST(reads_parameters_out_of_the_frame)
{
    CHECK_EQ(run_sint("fn twice(x: i32) -> i32:\n"
                      "    let a = x\n"
                      "    let b = a\n"
                      "    return b\n"
                      "twice(21)\n"),
             21);
}

CNOR_TEST(keeps_globals_across_the_end_of_a_run)
{
    CHECK_EQ(run_sint("let g = 7\ng + 1\n"), 8);
}

CNOR_TEST(folds_nothing_and_evaluates_left_to_right)
{
    CHECK_EQ(run_sint("10 - 4 - 3"), 3);
}

CNOR_TEST(builds_strings_and_joins_them)
{
    CHECK(run_text_is("\"hello, \" + \"world\"", "hello, world"));
}

CNOR_TEST(applies_escapes_into_the_constant_table)
{
    CHECK(run_text_is("\"a\\nb\"", "a\nb"));
}

CNOR_TEST(pushes_real_booleans_rather_than_numbers)
{
    lh_value_t result;

    REQUIRE(run("true", &result));
    CHECK_EQ(lh_value_get_kind(&result), lh_value_kind_bool);
}

CNOR_TEST(compares_with_a_boolean_result)
{
    lh_value_t result;

    REQUIRE(run("1 < 2", &result));
    CHECK_EQ(lh_value_get_kind(&result), lh_value_kind_bool);
    CHECK(lh_value_get_data(&result)->b);
}

CNOR_TEST(short_circuits_and)
{
    lh_value_t result;

    /* Adapted: names are resolved at compile time in cnor, so the right side
     * is a call that would fail at run time if it were evaluated. */
    REQUIRE(run("fn boom() -> i32:\n    return 1 / 0\nfalse and boom()\n", &result));
    CHECK_EQ(lh_value_get_kind(&result), lh_value_kind_bool);
    CHECK(!lh_value_get_data(&result)->b);
}

CNOR_TEST(reports_an_unknown_name_with_its_line)
{
    cnor_line_t line = 0U;
    char name[NAME_BUFFER_SIZE];

    /* Adapted: a name that is no variable is unknown_name, not no_such_native. */
    CHECK_EQ(compile_error("fn f() -> i32:\n    return nowhere\n", &line, name),
             cnor_error_unknown_name);
    CHECK(strcmp(name, "nowhere") == 0);
    CHECK_EQ(line, 2U);
}

CNOR_TEST(binds_a_native_by_name)
{
    CHECK_EQ(run_sint("double_it(21)"), 42);
}

CNOR_TEST(reports_a_native_the_host_did_not_provide)
{
    char name[NAME_BUFFER_SIZE];

    CHECK_EQ(compile_error("no_such_native(1)", lh_null, name), cnor_error_no_such_native);
    CHECK(strcmp(name, "no_such_native") == 0);
}

CNOR_TEST(produces_a_loadable_image_with_a_header)
{
    built_t b;
    cnor_image_header_t header;

    compile(&b, "1");
    REQUIRE(b.ok);
    CHECK_EQ(cnor_image_header_read(b.image.data, b.image.size, &header), cnor_error_none);
    CHECK_EQ(header.version, CNOR_IMAGE_VERSION);
    CHECK(header.sections[cnor_image_section_code].size > 0U);
    cnor_image_deinit(&b.image);
}

CNOR_TEST(gives_a_program_the_stack_it_needs)
{
    built_t b;
    cnor_image_header_t header;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();

    compile(&b, "fn deep() -> i32:\n    return 1 + 2 + 3 + 4\ndeep()\n");
    REQUIRE(b.ok);
    REQUIRE(cnor_image_header_read(b.image.data, b.image.size, &header) == cnor_error_none);
    /* Exactly the largest frame: two operands in `deep`, result slot + call in main. */
    CHECK_EQ(header.stack_slots, 2U);
    config.stack_slots = 1U;
    cnor_vm_init(&vm, &config);
    CHECK(!cnor_vm_load(&vm, b.image.data, b.image.size));
    CHECK_EQ(cnor_diag_get_code(cnor_vm_get_diag(&vm)), cnor_error_stack_too_big);
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
}

CNOR_TEST(names_the_source_so_a_failure_can_point_at_a_file)
{
    built_t b;
    cnor_image_header_t header;
    cnor_vm_t vm;
    lh_str_view_t file;

    compile(&b, "1");
    REQUIRE(b.ok);
    REQUIRE(cnor_image_header_read(b.image.data, b.image.size, &header) == cnor_error_none);
    CHECK((header.flags & CNOR_IMAGE_FLAG_LINES) != 0U);
    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, b.image.data, b.image.size));
    file = cnor_vm_get_file(&vm);
    CHECK(name_is(file, "main.cn"));
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
}

/* -- control flow ---------------------------------------------------------- */

CNOR_TEST(takes_the_branch_the_condition_picks)
{
    CHECK_EQ(run_sint("let mut r = 0\nif 1 < 2:\n    r = 10\nelse:\n    r = 20\nr\n"), 10);
    CHECK_EQ(run_sint("let mut r = 0\nif 2 < 1:\n    r = 10\nelse:\n    r = 20\nr\n"), 20);
    CHECK_EQ(run_sint("let mut r = 5\nif false:\n    r = 10\nr\n"), 5);
}

CNOR_TEST(chains_else_if)
{
    const char *src = "fn sign(x: i32) -> i32:\n"
                      "    if x < 0:\n"
                      "        return -1\n"
                      "    else if x == 0:\n"
                      "        return 0\n"
                      "    else:\n"
                      "        return 1\n"
                      "sign(-7) * 100 + sign(0) * 10 + sign(3)\n";

    CHECK_EQ(run_sint(src), -99);
}

CNOR_TEST(loops_while_the_condition_holds)
{
    CHECK_EQ(run_sint("let mut i = 0\nlet mut s = 0\nwhile i < 5:\n    i = i + 1\n"
                      "    s = s + i\ns\n"),
             15);
}

CNOR_TEST(loops_over_a_half_open_range)
{
    CHECK_EQ(run_sint("let mut s = 0\nfor i in 0..5:\n    s = s + i\ns\n"), 10);
    CHECK_EQ(run_sint("let mut s = 0\nfor i in 5..5:\n    s = s + 1\ns\n"), 0);
}

CNOR_TEST(breaks_and_continues)
{
    const char *src = "let mut s = 0\n"
                      "for i in 0..100:\n"
                      "    if i == 10:\n"
                      "        break\n"
                      "    if i % 2 == 0:\n"
                      "        continue\n"
                      "    s = s + i\n"
                      "s\n";
    const char *src_while = "let mut i = 0\n"
                            "let mut s = 0\n"
                            "while true:\n"
                            "    i = i + 1\n"
                            "    if i > 4:\n"
                            "        break\n"
                            "    if i == 2:\n"
                            "        continue\n"
                            "    s = s + i\n"
                            "s\n";

    CHECK_EQ(run_sint(src), 25);
    CHECK_EQ(run_sint(src_while), 8);
}

CNOR_TEST(nests_loops_with_their_own_breaks)
{
    const char *src = "let mut n = 0\n"
                      "for i in 0..3:\n"
                      "    for j in 0..10:\n"
                      "        if j == 2:\n"
                      "            break\n"
                      "        n = n + 1\n"
                      "n\n";

    CHECK_EQ(run_sint(src), 6);
}

CNOR_TEST(matches_the_first_equal_arm)
{
    const char *src = "fn pick(x: i32) -> i32:\n"
                      "    let mut r = 0\n"
                      "    match x:\n"
                      "        1 => r = 10\n"
                      "        -2 => r = 20\n"
                      "        _ => r = 99\n"
                      "    return r\n"
                      "pick(1) + pick(-2) + pick(5)\n";

    CHECK_EQ(run_sint(src), 129);
    CHECK(run_text_is("let mut r = \"?\"\nmatch \"b\":\n    \"a\" => r = \"A\"\n"
                      "    \"b\" => r = \"B\"\nr\n",
                      "B"));
}

/* -- functions ------------------------------------------------------------- */

CNOR_TEST(recurses)
{
    CHECK_EQ(run_sint("fn fib(n: i32) -> i32:\n"
                      "    if n < 2:\n"
                      "        return n\n"
                      "    return fib(n - 1) + fib(n - 2)\n"
                      "fib(15)\n"),
             610);
}

CNOR_TEST(returns_none_when_falling_off_the_end)
{
    lh_value_t result;

    REQUIRE(run("fn f():\n    let x = 1\nf()\n", &result));
    CHECK_EQ(lh_value_get_kind(&result), lh_value_kind_none);
}

CNOR_TEST(writes_globals_from_a_function)
{
    CHECK_EQ(run_sint("let mut count = 0\nfn bump():\n    count = count + 1\n"
                      "bump()\nbump()\ncount\n"),
             2);
}

CNOR_TEST(stops_runaway_recursion)
{
    CHECK_EQ(run_error("fn f(n: i32) -> i32:\n    return f(n + 1)\nf(0)\n", lh_null),
             cnor_error_call_depth);
}

/* -- operators ------------------------------------------------------------- */

CNOR_TEST(negates_with_unary_minus)
{
    CHECK_EQ(run_sint("-5"), -5);
    CHECK_EQ(run_sint("let x = 7\n-x\n"), -7);
    CHECK_EQ(run_sint("-(2 + 3) * 2"), -10);
    CHECK_EQ(run_sint("3 - -2"), 5);
}

CNOR_TEST(short_circuits_and_and_or)
{
    lh_value_t result;

    m_touched = 0;
    REQUIRE(run("false and touch()", &result));
    CHECK_EQ(m_touched, 0);
    REQUIRE(run("true or touch()", &result));
    CHECK_EQ(m_touched, 0);
    REQUIRE(run("true and touch()", &result));
    CHECK_EQ(m_touched, 1);
    REQUIRE(run("false or touch()", &result));
    CHECK_EQ(m_touched, 2);
    /* The value is the operand that decided, like the spec says. */
    CHECK_EQ(run_sint("0 or 5"), 5);
    CHECK_EQ(run_sint("3 and 4"), 4);
}

CNOR_TEST(negates_with_not)
{
    lh_value_t result;

    REQUIRE(run("not (1 < 2)", &result));
    CHECK_EQ(lh_value_get_kind(&result), lh_value_kind_bool);
    CHECK(!lh_value_get_data(&result)->b);
}

CNOR_TEST(measures_and_indexes_text)
{
    CHECK_EQ(run_sint("len(\"hello\")"), 5);
    CHECK(run_text_is("let s = \"abc\"\ns[1]\n", "b"));
    CHECK_EQ(run_error("\"abc\"[3]", lh_null), cnor_error_index);
}

CNOR_TEST(keeps_unsigned_literals_unsigned)
{
    lh_value_t result;

    REQUIRE(run("4000000000", &result));
    CHECK_EQ(lh_value_get_kind(&result), lh_value_kind_uint);
    CHECK_EQ(run_sint("4000000000 - 4000000001"), -1);
}

/* -- errors ---------------------------------------------------------------- */

CNOR_TEST(refuses_writes_it_must_refuse)
{
    CHECK_EQ(compile_error("let x = 1\nx = 2\n", lh_null, lh_null), cnor_error_immutable);
    CHECK_EQ(compile_error("fn f(a: i32):\n    a = 1\n", lh_null, lh_null),
             cnor_error_immutable);
    CHECK_EQ(compile_error("y = 2\n", lh_null, lh_null), cnor_error_unknown_name);
    CHECK_EQ(compile_error("fn f():\n    return 1\nf = 2\n", lh_null, lh_null),
             cnor_error_not_assignable);
}

CNOR_TEST(refuses_jumps_outside_a_loop)
{
    CHECK_EQ(compile_error("break\n", lh_null, lh_null), cnor_error_outside_loop);
    CHECK_EQ(compile_error("fn f():\n    continue\n", lh_null, lh_null),
             cnor_error_outside_loop);
}

CNOR_TEST(refuses_wrong_argument_counts_and_duplicates)
{
    CHECK_EQ(compile_error("fn f(a: i32):\n    return a\nf(1, 2)\n", lh_null, lh_null),
             cnor_error_wrong_arg_count);
    /* A host native's arity is checked at compile time when the table is given. */
    CHECK_EQ(compile_error("double_it()\n", lh_null, lh_null), cnor_error_wrong_arg_count);
    CHECK_EQ(compile_error("fn f():\n    return 1\nfn f():\n    return 2\n", lh_null, lh_null),
             cnor_error_duplicate_name);
}

CNOR_TEST(scopes_block_locals)
{
    char name[NAME_BUFFER_SIZE];

    CHECK_EQ(compile_error("fn f():\n    if true:\n        let inner = 1\n    return inner\n",
                           lh_null, name),
             cnor_error_unknown_name);
    CHECK(strcmp(name, "inner") == 0);
    CHECK_EQ(run_sint("fn f(x: i32) -> i32:\n    let y = x + 1\n    if true:\n"
                      "        let y = 100\n    return y\nf(1)\n"),
             2);
}

CNOR_TEST(maps_a_runtime_failure_to_its_line)
{
    cnor_line_t line = 0U;

    CHECK_EQ(run_error("let a = 1\nlet b = 0\na / b\n", &line), cnor_error_div_by_zero);
    CHECK_EQ(line, 3U);
}

CNOR_TEST(leaves_the_line_table_out_on_request)
{
    built_t b;
    cnor_image_header_t header;
    cnor_vm_t vm;

    compile_with(&b, "let a = 0\n1 / a\n", lh_bool_false);
    REQUIRE(b.ok);
    REQUIRE(cnor_image_header_read(b.image.data, b.image.size, &header) == cnor_error_none);
    CHECK_EQ(header.flags & CNOR_IMAGE_FLAG_LINES, 0U);
    CHECK_EQ(header.sections[cnor_image_section_lines].size, 0U);
    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, b.image.data, b.image.size));
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(cnor_diag_get_code(cnor_vm_get_diag(&vm)), cnor_error_div_by_zero);
    CHECK_EQ(cnor_diag_get_line(cnor_vm_get_diag(&vm)), 0U);
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
}

/* -- reals -------------------------------------------------------------------- */

CNOR_TEST(computes_with_reals_only_when_lh_has_them)
{
#if LH_REAL_ENABLED
    lh_value_t result;

    REQUIRE(run("let r = 1.5\nr * 2 + 0.25\n", &result));
    REQUIRE(lh_value_get_kind(&result) == lh_value_kind_real);
    CHECK(lh_value_get_data(&result)->r > (lh_real_t) 3.24
          && lh_value_get_data(&result)->r < (lh_real_t) 3.26);
    REQUIRE(run("7.5 % 2", &result));
    CHECK(lh_value_get_data(&result)->r > (lh_real_t) 1.49
          && lh_value_get_data(&result)->r < (lh_real_t) 1.51);
    CHECK_EQ(run_sint("let mut n = 0\nmatch 2.5:\n    2.5 => n = 1\n    _ => n = 2\nn\n"), 1);
#else
    CHECK_EQ(compile_error("let r = 1.5\n", lh_null, lh_null), cnor_error_no_reals);
#endif
}

/* An infinity minus itself is NaN, which is unordered: every comparison but
 * `!=` is false, even against itself. `%` follows fmod for NaN and infinities. */
CNOR_TEST(compares_nan_as_unordered)
{
#if LH_REAL_ENABLED
    CHECK_EQ(run_sint("let mut x = 10.0\n"
                      "for i in 0..400:\n"
                      "    x = x * 10.0\n"
                      "let n = x - x\n"
                      "let mut r = 0\n"
                      "if n == n:\n    r = r + 1\n"
                      "if n < 1.0:\n    r = r + 2\n"
                      "if n >= 1.0:\n    r = r + 4\n"
                      "if n != n:\n    r = r + 8\n"
                      "if n > x:\n    r = r + 16\n"
                      "if x > 1.0:\n    r = r + 32\n"
                      "let m = n % 2.0\n"
                      "if m != m:\n    r = r + 64\n"
                      "let k = x % 2.0\n"
                      "if k != k:\n    r = r + 128\n"
                      "if 5.0 % x == 5.0:\n    r = r + 256\n"
                      "r\n"),
             488);
#endif
}

/* 0xFFFFFFFF squared is above 2^63: it still orders above every signed value,
 * and an unsigned subtraction that goes far below zero wraps instead of
 * overflowing. */
CNOR_TEST(keeps_large_unsigned_values_in_order)
{
    CHECK_EQ(run_sint("let x = 0xFFFFFFFF * 0xFFFFFFFF\n"
                      "let mut r = 0\n"
                      "if x > -1:\n    r = r + 1\n"
                      "if x > 2147483647:\n    r = r + 2\n"
                      "if x == x:\n    r = r + 4\n"
                      "if -1 < x:\n    r = r + 8\n"
                      "if x != -1:\n    r = r + 16\n"
                      "let d = 0x80000000 - x\n"
                      "if d == 2147483648 - x:\n    r = r + 32\n"
                      "let e = -x\n"
                      "r\n"),
             63);
}

/* -- instruction budget ------------------------------------------------------- */

CNOR_TEST(stops_an_endless_loop_with_the_default_budget)
{
    cnor_line_t line = 0U;

    CHECK_EQ(run_error("let mut i = 0\nwhile true:\n    i = i + 1\n", &line),
             cnor_error_step_limit);
    CHECK(line >= 2U && line <= 3U);
}

CNOR_TEST(stops_endless_recursion_through_a_loop_with_a_small_budget)
{
    cnor_step_count_t steps = 0U;

    CHECK_EQ(run_with_budget("fn spin():\n    while true:\n        let x = 1\nspin()\n", 500U,
                             &steps),
             cnor_error_step_limit);
    CHECK_EQ(steps, 500U);
}

CNOR_TEST(lets_a_program_finish_within_its_budget)
{
    const char *src = "let mut s = 0\nfor i in 0..1000:\n    s = s + i\ns\n";
    cnor_step_count_t needed = 0U;
    cnor_step_count_t steps = 0U;

    /* Unlimited first, to learn the exact cost. */
    REQUIRE(run_with_budget(src, 0U, &needed) == cnor_error_none);
    CHECK(needed > 1000U);
    CHECK_EQ(run_with_budget(src, needed, &steps), cnor_error_none);
    CHECK_EQ(steps, needed);
    CHECK_EQ(run_with_budget(src, needed - 1U, &steps), cnor_error_step_limit);
    CHECK_EQ(steps, needed - 1U);
    /* The default budget is ample for real work. */
    CHECK_EQ(run_sint("fn fib(n: i32) -> i32:\n    if n < 2:\n        return n\n"
                      "    return fib(n - 1) + fib(n - 2)\nfib(18)\n"),
             2584);
}

/* Enough functions and globals (400 names) to make the compiler's name index
 * grow several times, and few enough globals for the default stack: calls and
 * globals still resolve, and a clash with any of them is still a duplicate. */
#define MANY_NAMES 200

CNOR_TEST(resolves_names_among_many_functions_and_globals)
{
    static char src[MANY_NAMES * 96 + 256];
    char name[NAME_BUFFER_SIZE];
    size_t used = 0U;
    int k;

    for (k = 0; k < MANY_NAMES; ++k)
    {
        used += (size_t) snprintf(src + used, sizeof(src) - used,
                                  "let g%d = %d\nfn f%d(a: i32) -> i32:\n    return a + g%d\n", k,
                                  k, k, k);
    }
    (void) snprintf(src + used, sizeof(src) - used, "f%d(1) + f0(2) + f150(3)\n",
                    MANY_NAMES - 1);
    CHECK_EQ(run_sint(src), (MANY_NAMES - 1 + 1) + (0 + 2) + (150 + 3));
    (void) snprintf(src + used, sizeof(src) - used, "fn g150():\n    return\n");
    CHECK_EQ(compile_error(src, lh_null, name), cnor_error_duplicate_name);
    CHECK(strcmp(name, "g150") == 0);
}

/* Loop conditions are compiled after the body (tested at the bottom); a
 * failure in one still names the loop's own line. */
CNOR_TEST(reports_a_bottom_tested_condition_on_the_loop_line)
{
    cnor_line_t line = 0U;

    CHECK_EQ(run_error("let mut i = 0\n"
                       "while i < 3:\n"
                       "    i = i + 1\n"
                       "    let x = 2\n"
                       "while i < \"a\":\n"
                       "    i = i + 1\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 5U);
    CHECK_EQ(run_error("let mut s = 0\n"
                       "for k in 0..\"a\":\n"
                       "    s = s + k\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 2U);
    /* Zero iterations: the body never runs, the condition is tested once. */
    CHECK_EQ(run_sint("let mut s = 7\nfor k in 5..5:\n    s = 0\nwhile s > 9:\n    s = 0\ns\n"),
             7);
}

/* The dispatch loop runs some instruction pairs together (a comparison and
 * its jump, arithmetic and its store, two loads); each half still counts, and
 * every budget short of the total must stop after exactly that many. */
CNOR_TEST(stops_after_exactly_the_budget_inside_fused_pairs)
{
    const char *src = "fn f(n: i32) -> i32:\n"
                      "    let mut s = 0\n"
                      "    let mut i = 0\n"
                      "    while i < n:\n"
                      "        s = s + i * 2\n"
                      "        if s - 3 < i:\n"
                      "            s = s + 1\n"
                      "        i = i + 1\n"
                      "    return s\n"
                      "let mut t = 0\n"
                      "for k in 0..3:\n"
                      "    t = t + f(k + 4)\n"
                      "t\n";
    cnor_step_count_t needed = 0U;
    cnor_step_count_t budget;
    cnor_step_count_t steps = 0U;

    REQUIRE(run_with_budget(src, 0U, &needed) == cnor_error_none);
    for (budget = 1U; budget < needed; ++budget)
    {
        steps = 0U;
        CHECK_EQ(run_with_budget(src, budget, &steps), cnor_error_step_limit);
        CHECK_EQ(steps, budget);
    }
    CHECK_EQ(run_with_budget(src, needed, &steps), cnor_error_none);
    CHECK_EQ(steps, needed);
}

static const cnor_test_case_t m_cases[] = {
    CNOR_TEST_CASE(runs_a_program_that_is_one_expression),
    CNOR_TEST_CASE(calls_a_function_defined_earlier),
    CNOR_TEST_CASE(calls_a_function_defined_later_in_the_file),
    CNOR_TEST_CASE(reads_parameters_out_of_the_frame),
    CNOR_TEST_CASE(keeps_globals_across_the_end_of_a_run),
    CNOR_TEST_CASE(folds_nothing_and_evaluates_left_to_right),
    CNOR_TEST_CASE(builds_strings_and_joins_them),
    CNOR_TEST_CASE(applies_escapes_into_the_constant_table),
    CNOR_TEST_CASE(pushes_real_booleans_rather_than_numbers),
    CNOR_TEST_CASE(compares_with_a_boolean_result),
    CNOR_TEST_CASE(short_circuits_and),
    CNOR_TEST_CASE(reports_an_unknown_name_with_its_line),
    CNOR_TEST_CASE(binds_a_native_by_name),
    CNOR_TEST_CASE(reports_a_native_the_host_did_not_provide),
    CNOR_TEST_CASE(produces_a_loadable_image_with_a_header),
    CNOR_TEST_CASE(gives_a_program_the_stack_it_needs),
    CNOR_TEST_CASE(names_the_source_so_a_failure_can_point_at_a_file),
    CNOR_TEST_CASE(takes_the_branch_the_condition_picks),
    CNOR_TEST_CASE(chains_else_if),
    CNOR_TEST_CASE(loops_while_the_condition_holds),
    CNOR_TEST_CASE(loops_over_a_half_open_range),
    CNOR_TEST_CASE(breaks_and_continues),
    CNOR_TEST_CASE(nests_loops_with_their_own_breaks),
    CNOR_TEST_CASE(matches_the_first_equal_arm),
    CNOR_TEST_CASE(recurses),
    CNOR_TEST_CASE(returns_none_when_falling_off_the_end),
    CNOR_TEST_CASE(writes_globals_from_a_function),
    CNOR_TEST_CASE(stops_runaway_recursion),
    CNOR_TEST_CASE(negates_with_unary_minus),
    CNOR_TEST_CASE(short_circuits_and_and_or),
    CNOR_TEST_CASE(negates_with_not),
    CNOR_TEST_CASE(measures_and_indexes_text),
    CNOR_TEST_CASE(keeps_unsigned_literals_unsigned),
    CNOR_TEST_CASE(refuses_writes_it_must_refuse),
    CNOR_TEST_CASE(refuses_jumps_outside_a_loop),
    CNOR_TEST_CASE(refuses_wrong_argument_counts_and_duplicates),
    CNOR_TEST_CASE(scopes_block_locals),
    CNOR_TEST_CASE(maps_a_runtime_failure_to_its_line),
    CNOR_TEST_CASE(leaves_the_line_table_out_on_request),
    CNOR_TEST_CASE(computes_with_reals_only_when_lh_has_them),
    CNOR_TEST_CASE(compares_nan_as_unordered),
    CNOR_TEST_CASE(keeps_large_unsigned_values_in_order),
    CNOR_TEST_CASE(stops_an_endless_loop_with_the_default_budget),
    CNOR_TEST_CASE(stops_endless_recursion_through_a_loop_with_a_small_budget),
    CNOR_TEST_CASE(lets_a_program_finish_within_its_budget),
    CNOR_TEST_CASE(resolves_names_among_many_functions_and_globals),
    CNOR_TEST_CASE(reports_a_bottom_tested_condition_on_the_loop_line),
    CNOR_TEST_CASE(stops_after_exactly_the_budget_inside_fused_pairs),
};

int
main(int argc, char **argv)
{
    return cnor_test_main("compiler", m_cases, CNOR_TEST_COUNT(m_cases), argc, argv);
}
