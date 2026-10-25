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

static void
double_it(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
         cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    *result = lh_value_make_sint(lh_value_get_data(&args[0])->i * 2);
}

/* Counts its calls through the context, to observe short-circuiting. */
static void
touch(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
      cnor_error_t *error)
{
    (void) args;
    (void) argc;
    (void) error;
    (*(int *) ctx)++;
    *result = lh_value_make_bool(lh_bool_true);
}

/* Gives its argument back, of whatever kind. */
static void
echo(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result, cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    *result = args[0];
}

static const cnor_native_t m_natives[] = {
    cnor_native_initializer("double_it", double_it, 1U, 1U),
    cnor_native_initializer("touch", touch, 0U, 0U),
    cnor_native_initializer("echo", echo, 1U, 1U),
};
static const cnor_native_table_t m_table = cnor_native_table_initializer(m_natives, 3U);
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
                       "while i < echo(\"a\"):\n"
                       "    i = i + 1\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 5U);
    CHECK_EQ(run_error("let mut s = 0\n"
                       "for k in 0..echo(\"a\"):\n"
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

/* -- operand forms and for_loop -------------------------------------------- */

/* Binary operators on locals and small integers run as operand forms (`_ll`,
 * `_li`, `_i`); on globals they run on the stack. Same operands, same answer,
 * for every operator in @p ops: the count of disagreements, or -1 when the
 * program fails. */
static long long
operand_forms_disagree(const char *left, const char *right, const char *const *ops,
                       size_t op_count, lh_bool_t immediates)
{
    static char src[8192];
    size_t used = 0U;
    size_t k;

    used += (size_t) snprintf(src + used, sizeof(src) - used,
                              "let ga = echo(%s)\nlet gb = echo(%s)\nlet g7 = 7\nlet gm = -3\n"
                              "fn f(a: i32, b: i32) -> i32:\n    let mut bad = 0\n",
                              left, right);
    for (k = 0U; k < op_count; ++k)
    {
        const char *o = ops[k];

        used += (size_t) snprintf(src + used, sizeof(src) - used,
                                  "    if (a %s b) != (ga %s gb):\n        bad = bad + 1\n"
                                  "    if (b %s a) != (gb %s ga):\n        bad = bad + 1\n",
                                  o, o, o, o);
        if (!immediates)
        {
            continue;
        }
        used += (size_t) snprintf(src + used, sizeof(src) - used,
                                  "    if (a %s 7) != (ga %s g7):\n        bad = bad + 1\n"
                                  "    if (b %s -3) != (gb %s gm):\n        bad = bad + 1\n"
                                  "    if (ga %s 7) != (ga %s g7):\n        bad = bad + 1\n"
                                  "    if (gb %s -3) != (gb %s gm):\n        bad = bad + 1\n",
                                  o, o, o, o, o, o, o, o);
    }
    (void) snprintf(src + used, sizeof(src) - used, "    return bad\nf(ga, gb)\n");
    {
        const long long bad = run_sint(src);

        return bad == -999999 ? -1 : bad;
    }
}

CNOR_TEST(computes_operand_forms_as_the_stack_does)
{
    static const char *const all[] = {"+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">="};
    static const char *const text[] = {"+", "==", "!="};
    static const char *const pairs[][2] = {
        {"17", "5"},
        {"-17", "5"},
        {"17", "-5"},
        {"1", "-1"},
        {"-1", "1"},
        {"-7", "7"},
        {"2147483647", "2147483647"},
        {"0x40000000 * 0x40000000 * 8", "-1"},
        {"0xFFFFFFFF", "3"},
        {"0x80000000", "0xFFFFFFFF"},
        {"5", "0xFFFFFFFF"},
        {"0xFFFFFFFF * 0xFFFFFFFF", "-3"},
#if LH_REAL_ENABLED
        {"2.5", "0.5"},
        {"-3", "1.5"},
        {"7.0", "-3"},
#endif
    };
    size_t k;

    for (k = 0U; k < sizeof(pairs) / sizeof(pairs[0]); ++k)
    {
        const long long bad = operand_forms_disagree(pairs[k][0], pairs[k][1], all, 11U,
                                                     lh_bool_true);

        if (bad != 0)
        {
            printf("    pair %s, %s\n", pairs[k][0], pairs[k][1]);
        }
        CHECK_EQ(bad, 0);
    }
    CHECK_EQ(operand_forms_disagree("\"ab\"", "\"cd\"", text, 3U, lh_bool_false), 0);
    CHECK_EQ(operand_forms_disagree("\"ab\"", "\"ab\"", text, 3U, lh_bool_false), 0);
    CHECK_EQ(operand_forms_disagree("\"ab\"", "7", text + 1, 2U, lh_bool_false), 0);
    CHECK(run_text_is("fn f(a: str, b: str) -> str:\n    return a + b\nf(\"x\", \"y\")\n", "xy"));
}

CNOR_TEST(fails_operand_forms_the_way_the_stack_does)
{
    cnor_line_t line = 0U;

    CHECK_EQ(run_error("fn f(a: i32, z: i32) -> i32:\n    return a / z\nf(1, 0)\n", &line),
             cnor_error_div_by_zero);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("fn f(a: i32) -> i32:\n    let x = 1\n    return a % 0\nf(1)\n", &line),
             cnor_error_div_by_zero);
    CHECK_EQ(line, 3U);
    CHECK_EQ(run_error("fn f(a: i32) -> i32:\n    return (a + 1) / 0\nf(1)\n", &line),
             cnor_error_div_by_zero);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("fn f() -> bool:\n    let s = echo(\"x\")\n    return s < 3\nf()\n", &line),
             cnor_error_type);
    CHECK_EQ(line, 3U);
    CHECK_EQ(run_error("fn f() -> i32:\n    let s = echo(\"x\")\n    let n = 1\n"
                       "    return n - s\nf()\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 4U);
    CHECK_EQ(run_error("fn f() -> i32:\n    let s = echo(\"x\")\n    return (s + s) * 2\nf()\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 3U);
    /* The smallest i32 is an immediate too; it divides and wraps like the rest. */
    CHECK_EQ(run_sint("fn f(a: i32) -> i32:\n    return a / -2147483648\nf(-2147483647 - 1)\n"), 1);
    CHECK_EQ(run_sint("fn f(a: i32) -> i32:\n    return a * -1\nf(-2147483648)\n"), 2147483648LL);
}

/* An integer arm tests the subject in place; equality is still by value,
 * across kinds, and text never equals a number. */
CNOR_TEST(matches_integer_arms_by_value)
{
    const char *src = "fn pick(x: i32) -> i32:\n"
                      "    match x:\n"
                      "        -2147483648 => return 1\n"
                      "        -1 => return 2\n"
                      "        0 => return 3\n"
                      "        2147483647 => return 4\n"
                      "        _ => return 5\n"
                      "pick(echo(%s))\n";
    static const struct
    {
        const char *subject;
        long long arm;
    } cases[] = {
        {"-2147483647 - 1", 1}, {"-1", 2}, {"0", 3}, {"2147483647", 4},
        {"2147483647 + 1", 5},  {"0xFFFFFFFF", 5}, {"0x7FFFFFFF", 4}, {"\"0\"", 5},
        {"0x80000000 - 0x80000000", 3},
#if LH_REAL_ENABLED
        {"-1.0", 2}, {"0.5", 5},
#endif
    };
    char buffer[512];
    size_t k;

    for (k = 0U; k < sizeof(cases) / sizeof(cases[0]); ++k)
    {
        (void) snprintf(buffer, sizeof(buffer), src, cases[k].subject);
        CHECK_EQ(run_sint(buffer), cases[k].arm);
    }
}

/* The loop variable steps by one until it reaches the bound, which is read
 * once; ranges that start at or past their end run zero times. */
CNOR_TEST(counts_ranges_at_their_edges)
{
    static const char *const fn = "fn count(a: i32, b: i32) -> i32:\n"
                                  "    let mut n = 0\n"
                                  "    for i in a..b:\n"
                                  "        n = n + 1\n"
                                  "    return n\n";
    static const struct
    {
        const char *a;
        const char *b;
        long long n;
    } cases[] = {
        {"0", "0", 0},
        {"5", "5", 0},
        {"7", "3", 0},
        {"3", "-3", 0},
        {"-5", "-2", 3},
        {"-3", "3", 6},
        {"0x7FFFFFFE", "0x80000001", 3},
        {"0x80000000", "0x80000003", 3},
        {"0xFFFFFFFF", "0", 0},
        {"0x40000000 * 0x40000000 * 8", "0x40000000 * 0x40000000 * 8 + 2", 2},
        {"0x40000000 * 0x40000000 * 8 - 3", "0x40000000 * 0x40000000 * 8 - 1", 2},
#if LH_REAL_ENABLED
        {"0.5", "3", 3},
        {"-1", "0.5", 2},
        {"0.25", "0.25", 0},
#endif
    };
    char buffer[512];
    size_t k;

    for (k = 0U; k < sizeof(cases) / sizeof(cases[0]); ++k)
    {
        (void) snprintf(buffer, sizeof(buffer), "%scount(echo(%s), echo(%s))\n", fn, cases[k].a,
                        cases[k].b);
        if (run_sint(buffer) != cases[k].n)
        {
            printf("    range %s..%s\n", cases[k].a, cases[k].b);
        }
        CHECK_EQ(run_sint(buffer), cases[k].n);
    }
    /* Negative values sum like any others, at top level and in a function. */
    CHECK_EQ(run_sint("let mut s = 0\nfor i in -5..-2:\n    s = s + i\ns\n"), -12);
    CHECK_EQ(run_sint("fn f() -> i32:\n    let mut s = 0\n    for i in -5..-2:\n"
                      "        s = s + i\n    return s\nf()\n"),
             -12);
    /* The bound is evaluated once; the body cannot move it. */
    CHECK_EQ(run_sint("fn f() -> i32:\n    let mut n = 3\n    let mut c = 0\n"
                      "    for i in 0..n:\n        n = n + 1\n        c = c + 1\n"
                      "    return c\nf()\n"),
             3);
    /* continue goes to the step, break past it, in nested loops too. */
    CHECK_EQ(run_sint("fn f() -> i32:\n    let mut s = 0\n    for i in 0..6:\n"
                      "        for j in 0..6:\n            if j == i:\n                break\n"
                      "            if j % 2 == 1:\n                continue\n"
                      "            s = s + 1\n    return s\nf()\n"),
             9);
}

/* The largest signed value plus one wraps to the smallest, as `+` does, so a
 * range up to an unsigned bound beyond it keeps going. */
CNOR_TEST(wraps_a_loop_variable_like_addition)
{
    const char *src = "fn f() -> i32:\n"
                      "    let max = 0x40000000 * 0x40000000 * 8 - 1\n"
                      "    let huge = 0xFFFFFFFF * 0xFFFFFFFF\n"
                      "    let mut n = 0\n"
                      "    for i in max - 1..huge:\n"
                      "        n = n + 1\n"
                      "        if i < 0:\n"
                      "            break\n"
                      "    return n\n"
                      "f()\n";
    const char *endless = "fn f() -> i32:\n"
                          "    let max = 0x40000000 * 0x40000000 * 8 - 1\n"
                          "    let mut n = 0\n"
                          "    for i in max - 1..0xFFFFFFFF * 0xFFFFFFFF:\n"
                          "        n = n + 1\n"
                          "    return n\n"
                          "f()\n";
    cnor_step_count_t steps = 0U;

    CHECK_EQ(run_sint(src), 3);
    CHECK_EQ(run_with_budget(endless, 777U, &steps), cnor_error_step_limit);
    CHECK_EQ(steps, 777U);
}

/* A remainder by a power of two is a mask in the dispatch loop, but the rule
 * of `%` is the C rule: a negative dividend keeps its sign, and a divisor of
 * 1, 0 or -1 is not a power of two to mask with. */
CNOR_TEST(keeps_the_rule_of_the_remainder_by_a_power_of_two)
{
    /* A local and an immediate: the divisor is a power of two, and the
     * dividend is not negative, so the remainder is a mask. */
    CHECK_EQ(run_sint("let mut x = 17\nlet mut n = 0\nn = x % 8\nn\n"), 1);
    CHECK_EQ(run_sint("let mut x = 40\nlet mut n = 0\nn = x % 8\nn\n"), 0);
    /* The negative dividend keeps the C rule: the remainder is not a mask. */
    CHECK_EQ(run_sint("let mut x = -9\nlet mut n = 0\nn = x % 8\nn\n"), -1);
    CHECK_EQ(run_sint("let mut x = -16\nlet mut n = 0\nn = x % 8\nn\n"), 0);
    CHECK_EQ(run_sint("let mut x = -17\nlet mut n = 0\nn = x % 8\nn\n"), -1);
    /* 1 is a power of two, and the remainder by 1 is 0. */
    CHECK_EQ(run_sint("let mut x = 17\nlet mut n = 0\nn = x % 1\nn\n"), 0);
    CHECK_EQ(run_sint("let mut x = -17\nlet mut n = 0\nn = x % 1\nn\n"), 0);
    /* A divisor that is no power of two keeps the plain remainder. */
    CHECK_EQ(run_sint("let mut x = 17\nlet mut n = 0\nn = x % 3\nn\n"), 2);
    CHECK_EQ(run_sint("let mut x = -17\nlet mut n = 0\nn = x % 3\nn\n"), -2);
    CHECK_EQ(run_sint("let mut x = 17\nlet mut n = 0\nn = x % 6\nn\n"), 5);
    CHECK_EQ(run_sint("let mut x = 17\nlet mut n = 0\nn = x % 16\nn\n"), 1);
    CHECK_EQ(run_sint("let mut x = -17\nlet mut n = 0\nn = x % 16\nn\n"), -1);
    /* The remainder of a computed dividend, on the stack. */
    CHECK_EQ(run_sint("let mut x = 17\nlet mut n = 0\nn = (x + 3) % 8\nn\n"), 4);
    CHECK_EQ(run_sint("let mut x = -17\nlet mut n = 0\nn = (x + 3) % 8\nn\n"), -6);
    /* And in a loop. */
    CHECK_EQ(run_sint("let mut n = 0\nfor i in 0..40:\n    n = n + i % 8\nn\n"), 140);
}

/* The instructions a program executes. */
static cnor_step_count_t
steps_of(const char *source)
{
    cnor_step_count_t steps = 0U;

    (void) run_with_budget(source, 0U, &steps);
    return steps;
}

/*
 * The index forms and the plain opcode must agree: the text in a local and
 * the index in a local or a literal take a form, the same through a global
 * takes the plain opcode.
 */
static long long
index_forms_disagree(const char *text, const char *index)
{
    static char src[4096];
    size_t used = 0U;

    used += (size_t) snprintf(src + used, sizeof(src) - used,
                              "let gt = echo(%s)\n"
                              "fn f(t: str, i: i32) -> i32:\n"
                              "    let mut bad = 0\n"
                              "    if t[i] != gt[i]:\n        bad = bad + 1\n"
                              "    if t[2] != gt[2]:\n        bad = bad + 1\n"
                              "    if echo(%s)[2] != gt[2]:\n        bad = bad + 1\n"
                              "    return bad\n"
                              "f(%s, %s)\n",
                              text, text, text, index);
    {
        const long long bad = run_sint(src);

        return bad == -999999 ? -1 : bad;
    }
}

CNOR_TEST(computes_index_forms_as_the_stack_does)
{
    CHECK_EQ(index_forms_disagree("\"hello\"", "2"), 0);
    CHECK_EQ(index_forms_disagree("\"hello\"", "0"), 0);
    CHECK_EQ(index_forms_disagree("\"hello\"", "4"), 0);
    CHECK_EQ(index_forms_disagree("\"abc\"", "1"), 0);
    CHECK_EQ(run_text_is("fn f(t: str, i: i32) -> str:\n    return t[i]\nf(\"hello\", 2)\n",
                         "l"),
             1);
}

/* The text in a local is not copied onto the stack first: one instruction
 * less per index than the same through a global. */
CNOR_TEST(takes_the_index_of_a_local_in_one_instruction)
{
    static const char local_text[] = "fn f(n: i32) -> i32:\n"
                                     "    let mut s = 0\n"
                                     "    let t = \"%s\"\n"
                                     "    for i in 0..n:\n"
                                     "        if t[i] == \"a\":\n"
                                     "            s = s + 1\n"
                                     "    return s\n"
                                     "f(%d)\n";
    static const char global_text[] = "let g = \"%s\"\n"
                                      "fn f(n: i32) -> i32:\n"
                                      "    let mut s = 0\n"
                                      "    for i in 0..n:\n"
                                      "        if g[i] == \"a\":\n"
                                      "            s = s + 1\n"
                                      "    return s\n"
                                      "f(%d)\n";
    char text[257];
    char small[512];
    char large[512];
    cnor_step_count_t local_small;
    cnor_step_count_t local_large;
    cnor_step_count_t global_small;
    cnor_step_count_t global_large;

    memset(text, 'a', 256U);
    text[256] = '\0';
    (void) snprintf(small, sizeof small, local_text, text, 100);
    (void) snprintf(large, sizeof large, local_text, text, 200);
    local_small = steps_of(small);
    local_large = steps_of(large);
    (void) snprintf(small, sizeof small, global_text, text, 100);
    (void) snprintf(large, sizeof large, global_text, text, 200);
    global_small = steps_of(small);
    global_large = steps_of(large);
    /* A hundred iterations more: the global text takes two instructions more
     * per index, because it is loaded onto the stack and the index is pushed
     * first, while the form reads both from the frame. */
    CHECK_EQ((global_large - global_small) - (local_large - local_small), 200U);
    CHECK_EQ(run_sint(large), 200LL);
}

/* An array is a declaration: its elements are slots of the frame, so it is
 * only a local, and only in a `let`. */
CNOR_TEST(makes_an_array_and_writes_its_elements)
{
    CHECK_EQ(run_sint("fn f() -> i32:\n"
                      "    let mut scores = [0; 8]\n"
                      "    for i in 0..8:\n"
                      "        scores[i] = i * 2\n"
                      "    let mut sum = 0\n"
                      "    for i in 0..8:\n"
                      "        sum = sum + scores[i]\n"
                      "    return sum\n"
                      "f()\n"),
             56);
    /* The elements hold what they were given. */
    CHECK_EQ(run_text_is("fn f() -> str:\n"
                         "    let mut names = [\"a\"; 3]\n"
                         "    names[1] = \"bee\"\n"
                         "    return names[1]\n"
                         "f()\n",
                         "bee"),
             1);
    /* An array of one element, and an empty one. */
    CHECK_EQ(run_sint("fn f() -> i32:\n"
                      "    let mut one = [7; 1]\n"
                      "    return one[0]\n"
                      "f()\n"),
             7);
    /* An index outside the array fails the run. */
    CHECK_EQ(run_with_budget("fn f() -> i32:\n"
                              "    let mut scores = [0; 4]\n"
                              "    return scores[7]\n"
                              "f()\n",
                              0U, lh_null),
             cnor_error_index);
    /* The array needs `mut` to be written into. */
    CHECK_EQ(compile_error("fn f() -> i32:\n"
                            "    let scores = [0; 4]\n"
                            "    scores[0] = 1\n"
                            "    return scores[0]\n"
                            "f()\n",
                            lh_null, lh_null),
             cnor_error_immutable);
    /* An array is a local: a global one would outlive its frame. */
    CHECK_EQ(compile_error("let mut top = [0; 4]\n", lh_null, lh_null),
             cnor_error_not_a_declaration);
    /* And it is a declaration, not a value. */
    CHECK_EQ(compile_error("fn f() -> i32:\n"
                            "    let mut x = [0; 4]\n"
                            "    return x\n"
                            "f()\n",
                            lh_null, lh_null),
             cnor_error_type_mismatch);
}

/* The division takes the narrowest width that is exact: the value keeps its
 * 64-bit width, and the result is the same either way. */
CNOR_TEST(divides_at_the_width_the_value_fits_in)
{
    CHECK_EQ(run_sint("let mut a = 2147483647\nlet mut n = 0\nn = a / 2\nn\n"), 1073741823);
    CHECK_EQ(run_sint("let mut a = 2147483648\nlet mut n = 0\nn = a / 2\nn\n"), 1073741824);
    CHECK_EQ(run_sint("let mut a = -2147483648\nlet mut n = 0\nn = a / 2\nn\n"), -1073741824);
    /* A value that no longer fits in 32 bits keeps the 64-bit rule. */
    CHECK_EQ(run_sint("let mut a = 0xFFFFFFFF\nlet mut n = 0\n"
                      "a = a + 1\nn = a / 7\nn\n"),
             613566756);
    /* The remainder keeps the C rule: the sign of the dividend. */
    CHECK_EQ(run_sint("let mut a = 17\nlet mut n = 0\nn = a % 5\nn\n"), 2);
    CHECK_EQ(run_sint("let mut a = -17\nlet mut n = 0\nn = a % 5\nn\n"), -2);
    CHECK_EQ(run_sint("let mut a = 0xFFFFFFFF\nlet mut n = 0\n"
                      "a = a + 1\nn = a % 7\nn\n"),
             4);
}

/* One pass of `for` over a local sum is three instructions: add_ll, its
 * store and the for_loop. */
CNOR_TEST(runs_a_for_step_in_one_instruction)
{
    const char *src = "fn f(n: i32) -> i32:\n"
                      "    let mut s = 0\n"
                      "    for i in 0..n:\n"
                      "        s = s + i\n"
                      "    return s\n"
                      "f(%d)\n";
    char buffer[256];
    cnor_step_count_t small = 0U;
    cnor_step_count_t large = 0U;

    (void) snprintf(buffer, sizeof(buffer), src, 1000);
    REQUIRE(run_with_budget(buffer, 0U, &small) == cnor_error_none);
    (void) snprintf(buffer, sizeof(buffer), src, 2000);
    REQUIRE(run_with_budget(buffer, 0U, &large) == cnor_error_none);
    CHECK_EQ(large - small, 3000U);
    CHECK_EQ(run_sint(buffer), 1999LL * 2000LL / 2LL);
}

/* Like the fused pairs above, for the operand forms, match arms, `and` and
 * the for_loop: every budget short of the total stops after exactly that. */
CNOR_TEST(stops_after_exactly_the_budget_inside_for_loops)
{
    const char *src = "fn g(n: i32) -> i32:\n"
                      "    let mut s = 0\n"
                      "    for i in -2..n:\n"
                      "        let r = i % 3\n"
                      "        match r:\n"
                      "            0 => s = s + 1\n"
                      "            -1 => s = s - 2\n"
                      "            _ => s = s * 2\n"
                      "        if s > 5 and i != 1:\n"
                      "            continue\n"
                      "        s = s + i\n"
                      "    for j in 4..2:\n"
                      "        s = 0\n"
                      "    return s\n"
                      "let mut t = 0\n"
                      "for k in 0..4:\n"
                      "    t = t + g(k)\n"
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

/* -- strings ------------------------------------------------------------------ */

/* Compile and run with a string pool of @p pool_size bytes; the VM's error
 * code and line, and an integer result through @p result. */
static cnor_error_t
run_with_pool(const char *source, lh_u32_t pool_size, cnor_line_t *line, long long *result)
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
    config.str_pool_size = pool_size;
    cnor_vm_init(&vm, &config);
    if (cnor_vm_load(&vm, b.image.data, b.image.size))
    {
        (void) cnor_vm_run(&vm);
    }
    code = cnor_diag_get_code(cnor_vm_get_diag(&vm));
    if (line != lh_null)
    {
        *line = cnor_diag_get_line(cnor_vm_get_diag(&vm));
    }
    if (result != lh_null)
    {
        const lh_value_t value = cnor_vm_get_result(&vm);

        *result = lh_value_get_kind(&value) == lh_value_kind_sint
                      ? (long long) lh_value_get_data(&value)->i
                      : -999999;
    }
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
    return code;
}

/* Equality on locals (`eq_ll`) and on globals (the stack): 3 when equal, 12
 * when not, anything else when the two disagree or the program fails. */
static long long
text_equality(const char *left, const char *right)
{
    char src[1024];

    (void) snprintf(src, sizeof(src),
                    "let ga = echo(%s)\nlet gb = echo(%s)\n"
                    "fn f(a: str, b: str) -> i32:\n"
                    "    let mut r = 0\n"
                    "    if a == b:\n        r = r + 1\n"
                    "    if ga == gb:\n        r = r + 2\n"
                    "    if a != b:\n        r = r + 4\n"
                    "    if ga != gb:\n        r = r + 8\n"
                    "    return r\n"
                    "f(ga, gb)\n",
                    left, right);
    return run_sint(src);
}

/* Strings are equal when their bytes are: no case folding, NUL and non-ASCII
 * bytes are bytes like any other, and a string never equals another kind. */
CNOR_TEST(compares_text_by_its_bytes)
{
    static const struct
    {
        const char *left;
        const char *right;
        int equal;
    } cases[] = {
        {"\"\"", "\"\"", 1},
        {"\"\"", "\"a\"", 0},
        {"\"a\"", "\"\"", 0},
        {"\"abc\"", "\"abc\"", 1},
        {"\"abc\"", "\"ab\"", 0},
        {"\"ab\"", "\"abc\"", 0},
        {"\"abc\"", "\"xbc\"", 0},
        {"\"abc\"", "\"axc\"", 0},
        {"\"abc\"", "\"abx\"", 0},
        {"\"A\"", "\"a\"", 0},
        {"\"a\\0b\"", "\"a\\0b\"", 1},
        {"\"a\\0b\"", "\"a\\0c\"", 0},
        {"\"a\\0\"", "\"a\"", 0},
        {"\"\\0\"", "\"\"", 0},
        {"\"h\xc3\xa9\"", "\"h\xc3\xa9\"", 1},
        {"\"\xc3\x89\"", "\"\xc3\xa9\"", 0},
        {"\"abc\"[1]", "\"abc\"[1]", 1},
        {"\"abc\"[1]", "\"cba\"[1]", 1},
        {"\"0123456789\" + \"abcdefghij\"", "\"0123456789abcdefghij\"", 1},
        {"\"0123456789\" + \"abcdefghij\"", "\"0123456789abcdefghiJ\"", 0},
        {"\"0123456789\" + \"0123456789abcdefghijklmnopqrst\"",
         "\"01234567890123456789abcdefghijklmnopqrst\"", 1},
        {"\"0123456789\" + \"0123456789abcdefghijklmnopqrst\"",
         "\"01234567890123456789abcdefghijKlmnopqrst\"", 0},
        {"\"0123456789\" + \"0123456789abcdefghijklmnopqrst\"",
         "\"x1234567890123456789abcdefghijklmnopqrst\"", 0},
        {"\"1\"", "1", 0},
        {"\"\"", "0", 0},
        {"\"a\"", "none", 0},
        {"\"true\"", "true", 0},
    };
    size_t k;

    for (k = 0U; k < sizeof(cases) / sizeof(cases[0]); ++k)
    {
        const long long r = text_equality(cases[k].left, cases[k].right);

        if (r != (cases[k].equal ? 3 : 12))
        {
            printf("    %s vs %s\n", cases[k].left, cases[k].right);
        }
        CHECK_EQ(r, cases[k].equal ? 3 : 12);
    }
    /* A value equals itself; integer arms never match text. */
    CHECK_EQ(run_sint("fn f(a: str) -> i32:\n    if a == a:\n        return 1\n    return 0\n"
                      "f(\"\") + f(\"x\")\n"),
             2);
    CHECK_EQ(run_sint("fn f(a: str) -> i32:\n    match echo(a):\n        1 => return 1\n"
                      "        \"1\" => return 2\n        _ => return 3\nf(\"1\")\n"),
             2);
}

/* Joins on locals (`add_ll`, stored directly) and on the stack give the same
 * strings, empty and long ones included. */
CNOR_TEST(joins_text_on_locals_and_the_stack)
{
    static const char *const fn = "fn f(a: str, b: str) -> str:\n"
                                  "    let c = a + b\n"
                                  "    let d = (a + \"\") + b\n"
                                  "    if c != d:\n"
                                  "        return \"(differ)\"\n"
                                  "    return c\n";
    char src[1024];

    (void) snprintf(src, sizeof(src), "%sf(\"ab\", \"cd\")\n", fn);
    CHECK(run_text_is(src, "abcd"));
    (void) snprintf(src, sizeof(src), "%sf(\"\", \"cd\")\n", fn);
    CHECK(run_text_is(src, "cd"));
    (void) snprintf(src, sizeof(src), "%sf(\"ab\", \"\")\n", fn);
    CHECK(run_text_is(src, "ab"));
    (void) snprintf(src, sizeof(src), "%sf(\"\", \"\")\n", fn);
    CHECK(run_text_is(src, ""));
    (void) snprintf(src, sizeof(src), "%sf(\"h\xc3\xa9\", \"!\")\n", fn);
    CHECK(run_text_is(src, "h\xc3\xa9!"));
    (void) snprintf(src, sizeof(src),
                    "%sf(\"0123456789012345678901234567890123456789\", "
                    "\"abcdefghijabcdefghijabcdefghijabcdefghij\")\n",
                    fn);
    CHECK(run_text_is(src, "0123456789012345678901234567890123456789"
                           "abcdefghijabcdefghijabcdefghijabcdefghij"));
    /* Embedded NUL bytes are copied and counted. */
    CHECK_EQ(run_sint("fn f(a: str, b: str) -> i32:\n    let c = a + b\n"
                      "    if c[1] == \"\\0\" and c[3] == \"\\0\":\n        return len(c)\n"
                      "    return -1\nf(\"a\\0\", \"b\\0\")\n"),
             4);
    /* A join is a new string; the operands are unchanged. */
    CHECK(run_text_is("fn f(a: str) -> str:\n    let b = a + a\n    let c = b + a\n"
                      "    return a + \"|\" + b + \"|\" + c\nf(\"x\")\n",
                      "x|xx|xxx"));
}

CNOR_TEST(refuses_to_join_text_with_anything_else)
{
    cnor_line_t line = 0U;

    CHECK_EQ(run_error("let x = echo(\"a\")\nlet y = 1\nx + y\n", &line), cnor_error_type);
    CHECK_EQ(line, 3U);
    CHECK_EQ(run_error("fn f(a: i32, b: i32) -> i32:\n    return a + b\nf(echo(\"a\"), 1)\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("fn f(a: i32, b: i32) -> i32:\n    return b + a\nf(echo(\"a\"), 1)\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("fn f(a: i32) -> i32:\n    let x = 0\n    return a + 1\nf(echo(\"a\"))\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 3U);
    CHECK_EQ(run_error("fn f(a: i32) -> i32:\n    return 1 + a\nf(echo(\"a\"))\n", &line),
             cnor_error_type);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("echo(\"a\") + echo(none)\n", &line), cnor_error_type);
    CHECK_EQ(run_error("echo(\"a\") - echo(\"b\")\n", &line), cnor_error_type);
    /* With the types known, each of these is refused before it runs. */
    CHECK_EQ(compile_error("let x = \"a\"\nlet y = 1\nx + y\n", &line, lh_null),
             cnor_error_operand_type);
    CHECK_EQ(line, 3U);
    CHECK_EQ(compile_error("fn f(a: i32, b: i32) -> i32:\n    return a + b\nf(\"a\", 1)\n", &line,
                           lh_null),
             cnor_error_type_mismatch);
    CHECK_EQ(line, 3U);
    CHECK_EQ(compile_error("\"a\" + none\n", &line, lh_null), cnor_error_operand_type);
    CHECK_EQ(compile_error("\"a\" - \"b\"\n", &line, lh_null), cnor_error_operand_type);
}

/* A join that does not fit the pool fails on its line, on locals and on the
 * stack alike; one that fits exactly succeeds, and two empty strings need no
 * room at all. */
CNOR_TEST(stops_a_join_at_the_end_of_the_pool)
{
    const char *locals = "fn f(a: str, b: str) -> i32:\n"
                         "    let c = a + b\n"
                         "    return len(c)\n"
                         "f(\"abcd\", \"efgh\")\n";
    const char *stack = "let x = \"abcd\"\nlet y = \"efgh\"\nlen(x + y)\n";
    cnor_line_t line = 0U;
    long long result = 0;

    CHECK_EQ(run_with_pool(locals, 8U, &line, &result), cnor_error_none);
    CHECK_EQ(result, 8);
    CHECK_EQ(run_with_pool(locals, 7U, &line, lh_null), cnor_error_str_pool_full);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_with_pool(stack, 8U, &line, &result), cnor_error_none);
    CHECK_EQ(result, 8);
    CHECK_EQ(run_with_pool(stack, 7U, &line, lh_null), cnor_error_str_pool_full);
    CHECK_EQ(line, 3U);
    CHECK_EQ(run_with_pool("let x = \"\"\nlen(x + x)\n", 0U, lh_null, &result),
             cnor_error_none);
    CHECK_EQ(result, 0);
    CHECK_EQ(run_with_pool("let x = \"\"\nlen(x + \"a\")\n", 0U, &line, lh_null),
             cnor_error_str_pool_full);
    CHECK_EQ(line, 2U);
    /* Pieces of 4 bytes into 10: the third join is the one that fails. */
    CHECK_EQ(run_with_pool("fn f(a: str) -> i32:\n    let mut n = 0\n    for i in 0..3:\n"
                           "        let c = a + a\n        n = n + len(c)\n    return n\n"
                           "f(\"ab\")\n",
                           10U, &line, lh_null),
             cnor_error_str_pool_full);
    CHECK_EQ(line, 4U);
}

CNOR_TEST(indexes_and_measures_text_at_its_edges)
{
    cnor_line_t line = 0U;

    CHECK_EQ(run_sint("len(\"\")"), 0);
    CHECK_EQ(run_sint("len(\"h\xc3\xa9\")"), 3);
    CHECK_EQ(run_sint("len(\"a\\0b\")"), 3);
    CHECK_EQ(run_sint("fn f(s: str) -> i32:\n    return len(s[0]) + len(s[len(s) - 1])\n"
                      "f(\"abc\")\n"),
             2);
    CHECK(run_text_is("fn f(s: str) -> str:\n    return s[0] + s[len(s) - 1]\nf(\"xyz\")\n",
                      "xz"));
    /* Bytes, not characters. */
    CHECK_EQ(run_sint("let s = \"h\xc3\xa9\"\nlet mut r = 0\n"
                      "if s[1] != s[2]:\n    r = r + 1\n"
                      "if s[1] + s[2] == \"\xc3\xa9\":\n    r = r + 2\n"
                      "if s[0] == \"h\":\n    r = r + 4\nr\n"),
             7);
    /* An unsigned index works like a signed one. */
    CHECK(run_text_is("\"abc\"[0xFFFFFFFF - 0xFFFFFFFE]\n", "b"));
    CHECK(run_text_is("\"abc\"[2147483648 - 2147483646]\n", "c"));
    CHECK_EQ(run_error("let s = \"abc\"\nlet i = 0 - 1\ns[i]\n", &line), cnor_error_index);
    CHECK_EQ(line, 3U);
    CHECK_EQ(run_error("let s = \"abc\"\ns[3]\n", &line), cnor_error_index);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("let s = \"\"\ns[0]\n", &line), cnor_error_index);
    CHECK_EQ(run_error("\"abc\"[4000000000]\n", &line), cnor_error_index);
    CHECK_EQ(run_error("let n = echo(5)\nn[0]\n", &line), cnor_error_type);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("\"abc\"[echo(\"a\")]\n", &line), cnor_error_type);
    CHECK_EQ(run_error("\"abc\"[echo(true)]\n", &line), cnor_error_type);
    CHECK_EQ(run_error("let x = echo(1)\nlet y = len(x)\n", &line), cnor_error_type);
    CHECK_EQ(line, 2U);
    CHECK_EQ(run_error("len(echo(none))\n", &line), cnor_error_type);
#if LH_REAL_ENABLED
    CHECK_EQ(run_error("\"abc\"[echo(1.0)]\n", &line), cnor_error_type);
    CHECK_EQ(compile_error("\"abc\"[1.0]\n", &line, lh_null), cnor_error_type_mismatch);
#endif
    /* With the types known, each of these is refused before it runs. */
    CHECK_EQ(compile_error("let n = 5\nn[0]\n", &line, lh_null), cnor_error_type_mismatch);
    CHECK_EQ(line, 2U);
    CHECK_EQ(compile_error("\"abc\"[\"a\"]\n", &line, lh_null), cnor_error_type_mismatch);
    CHECK_EQ(compile_error("\"abc\"[true]\n", &line, lh_null), cnor_error_type_mismatch);
    CHECK_EQ(compile_error("let x = 1\nlet y = len(x)\n", &line, lh_null),
             cnor_error_type_mismatch);
    CHECK_EQ(line, 2U);
    CHECK_EQ(compile_error("len(none)\n", &line, lh_null), cnor_error_type_mismatch);
}

/* The string fast paths (joins stored directly, indexing, `len`, equality)
 * count each instruction like the slow path: every budget short of the total
 * stops after exactly that many. */
CNOR_TEST(stops_after_exactly_the_budget_inside_string_work)
{
    const char *src = "fn g(t: str, n: i32) -> i32:\n"
                      "    let mut total = 0\n"
                      "    for i in 0..n:\n"
                      "        let p = t[i % len(t)] + t[(i + 1) % len(t)]\n"
                      "        let q = p + p\n"
                      "        if q == \"abab\" or p != t:\n"
                      "            total = total + len(q)\n"
                      "    return total\n"
                      "g(\"abc\", 4)\n";
    cnor_step_count_t needed = 0U;
    cnor_step_count_t budget;
    cnor_step_count_t steps = 0U;

    REQUIRE(run_with_budget(src, 0U, &needed) == cnor_error_none);
    CHECK_EQ(run_sint(src), 16);
    for (budget = 1U; budget < needed; ++budget)
    {
        steps = 0U;
        CHECK_EQ(run_with_budget(src, budget, &steps), cnor_error_step_limit);
        CHECK_EQ(steps, budget);
    }
    CHECK_EQ(run_with_budget(src, needed, &steps), cnor_error_none);
    CHECK_EQ(steps, needed);
}

/* Strings of every size around the word boundaries of the inline compare and
 * copy (4, 8, 64 bytes), built one join at a time. Equal strings in different
 * storage must compare equal, one byte apart at any position unequal, and
 * every joined byte must land where it belongs. `make(n, p)` is n bytes of "a"
 * with a "b" at p. */
static const char m_sized_text[] = "fn fill(n: i32, c: str) -> str:\n"
                                   "    let mut s = \"\"\n"
                                   "    for i in 0..n:\n"
                                   "        s = s + c\n"
                                   "    return s\n"
                                   "fn make(n: i32, p: i32) -> str:\n"
                                   "    return fill(p, \"a\") + \"b\" + fill(n - p - 1, \"a\")\n"
                                   "fn check(n: i32) -> i32:\n"
                                   "    let base = fill(n, \"a\")\n"
                                   "    let mut bad = 0\n"
                                   "    if base != fill(n, \"a\") or len(base) != n:\n"
                                   "        bad = bad + 1000\n"
                                   "    for p in 0..n:\n"
                                   "        let other = make(n, p)\n"
                                   "        if base == other or other == base or len(other) != n:\n"
                                   "            bad = bad + 1\n"
                                   "        if not (other == make(n, p)):\n"
                                   "            bad = bad + 1\n"
                                   "        for k in 0..n:\n"
                                   "            if k == p:\n"
                                   "                if other[k] != \"b\":\n"
                                   "                    bad = bad + 1\n"
                                   "            else if other[k] != \"a\":\n"
                                   "                bad = bad + 1\n"
                                   "    return bad\n"
                                   "fn joins(n: i32, m: i32) -> i32:\n"
                                   "    let c = fill(n, \"a\") + fill(m, \"b\")\n"
                                   "    let mut bad = 0\n"
                                   "    if len(c) != n + m:\n"
                                   "        return 1000\n"
                                   "    for k in 0..n + m:\n"
                                   "        let mut want = \"a\"\n"
                                   "        if k >= n:\n"
                                   "            want = \"b\"\n"
                                   "        if c[k] != want:\n"
                                   "            bad = bad + 1\n"
                                   "    return bad\n";

CNOR_TEST(compares_and_joins_text_of_every_size)
{
    static const int sizes[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                                15, 16, 17, 23, 24, 25, 31, 32, 33, 63, 64, 65, 100};
    char src[sizeof(m_sized_text) + 64];
    size_t k;
    long long result = 0;

    for (k = 0U; k < sizeof(sizes) / sizeof(sizes[0]); ++k)
    {
        (void) snprintf(src, sizeof(src), "%scheck(%d)\n", m_sized_text, sizes[k]);
        REQUIRE(run_with_pool(src, 1U << 22, lh_null, &result) == cnor_error_none);
        if (result != 0)
        {
            printf("    size %d\n", sizes[k]);
        }
        CHECK_EQ(result, 0);
        (void) snprintf(src, sizeof(src), "%sjoins(%d, %d)\n", m_sized_text, sizes[k],
                        sizes[sizeof(sizes) / sizeof(sizes[0]) - 1U - k]);
        REQUIRE(run_with_pool(src, 1U << 22, lh_null, &result) == cnor_error_none);
        CHECK_EQ(result, 0);
    }
}

/* Joins take their bytes from the pool; a reset gives them back, another run
 * without one keeps what the last run took (globals may still point at it). */
CNOR_TEST(gives_the_pool_back_on_reset_only)
{
    built_t b;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();

    compile(&b, "let x = \"0123456789abcdef\"\nlen(x + x + x)\n");
    REQUIRE(b.ok);
    config.natives = &m_table;
    config.ctx = &m_touched;
    config.str_pool_size = 80U; /* 32 + 48 bytes: exactly one run. */
    cnor_vm_init(&vm, &config);
    REQUIRE(cnor_vm_load(&vm, b.image.data, b.image.size));
    CHECK(cnor_vm_run(&vm));
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(cnor_diag_get_code(cnor_vm_get_diag(&vm)), cnor_error_str_pool_full);
    cnor_vm_reset(&vm);
    CHECK(cnor_vm_run(&vm));
    {
        const lh_value_t result = cnor_vm_get_result(&vm);

        CHECK_EQ(lh_value_get_data(&result)->i, 48);
    }
    cnor_vm_reset(&vm);
    CHECK(cnor_vm_run(&vm));
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
}

/* -- conditions, tables and globals ----------------------------------------- */

/* `if` and `while` test `and`, `or` and `not` by jumping; the branch taken is
 * the one the value of the same expression picks, for every operand kind that
 * has a truth (numbers, text, booleans, none). */
CNOR_TEST(branches_on_logic_like_its_value)
{
    static const char *const exprs[] = {
        "a and b",
        "a or b",
        "not a",
        "not (a and b)",
        "not (a or not b)",
        "a and b or c",
        "a or b and c",
        "(a or b) and (c or not a)",
        "not not a",
        "a and not b and c",
        "not a or not b or not c",
        "(a and (b or (c and not a))) or (not b and c)",
    };
    static const char *const values[] = {"0", "1", "-2", "\"\"", "\"x\"", "true", "false", "none"};
    static char src[16384];
    size_t used;
    size_t k;
    size_t a;

    for (a = 0U; a < sizeof(values) / sizeof(values[0]); ++a)
    {
        size_t b;

        for (b = 0U; b < sizeof(values) / sizeof(values[0]); ++b)
        {
            used = (size_t) snprintf(src, sizeof(src),
                                     "fn f(a: i32, b: i32, c: i32) -> i32:\n    let mut bad = 0\n");
            for (k = 0U; k < sizeof(exprs) / sizeof(exprs[0]); ++k)
            {
                used += (size_t) snprintf(
                    src + used, sizeof(src) - used,
                    "    let v%u = %s\n"
                    "    let mut x%u = 0\n"
                    "    if %s:\n        x%u = 1\n"
                    "    else:\n        x%u = 2\n"
                    "    if v%u:\n        x%u = x%u + 10\n"
                    "    else:\n        x%u = x%u + 20\n"
                    "    if x%u != 11 and x%u != 22:\n        bad = bad + 1\n"
                    "    let mut n%u = 0\n"
                    "    while n%u < 2 and (%s):\n        n%u = n%u + 1\n"
                    "    if (n%u == 2) != (not (not v%u)):\n        bad = bad + 1\n",
                    (unsigned) k, exprs[k], (unsigned) k, exprs[k], (unsigned) k, (unsigned) k,
                    (unsigned) k, (unsigned) k, (unsigned) k, (unsigned) k, (unsigned) k,
                    (unsigned) k, (unsigned) k, (unsigned) k, (unsigned) k, exprs[k],
                    (unsigned) k, (unsigned) k, (unsigned) k, (unsigned) k);
            }
            (void) snprintf(src + used, sizeof(src) - used,
                            "    return bad\nf(echo(%s), echo(%s), echo(%s)) * 100"
                            " + f(echo(%s), echo(%s), echo(%s))\n",
                            values[a],
                            values[b], values[(a + b) % 8U], values[b], values[a],
                            values[(a + 3U) % 8U]);
            if (run_sint(src) != 0)
            {
                printf("    a = %s, b = %s\n", values[a], values[b]);
            }
            CHECK_EQ(run_sint(src), 0);
        }
    }
}

/* A condition evaluates its operands in order and only as far as `and` and
 * `or` would: the log of calls is the same as the value's. */
CNOR_TEST(short_circuits_a_condition_like_its_value)
{
    const char *src = "let mut log = 0\n"
                      "fn t(k: i32, v: bool) -> bool:\n"
                      "    log = log * 10 + k\n"
                      "    return v\n"
                      "fn bit(m: i32, k: i32) -> bool:\n"
                      "    let mut x = m\n"
                      "    for i in 0..k:\n"
                      "        x = x / 2\n"
                      "    return x % 2 == 1\n"
                      "let mut bad = 0\n"
                      "for m in 0..16:\n"
                      "    let a = bit(m, 0)\n"
                      "    let b = bit(m, 1)\n"
                      "    let c = bit(m, 2)\n"
                      "    let d = bit(m, 3)\n"
                      "    log = 0\n"
                      "    let v = t(1, a) and t(2, b) or not t(3, c) and t(4, d)\n"
                      "    let value_log = log\n"
                      "    log = 0\n"
                      "    let mut taken = false\n"
                      "    if t(1, a) and t(2, b) or not t(3, c) and t(4, d):\n"
                      "        taken = true\n"
                      "    if log != value_log or taken != (not (not v)):\n"
                      "        bad = bad + 1\n"
                      "    log = 0\n"
                      "    let mut n = 0\n"
                      "    while n == 0 and (t(1, a) or t(2, b) and not t(3, c)):\n"
                      "        n = 1\n"
                      "    let w_log = log\n"
                      "    log = 0\n"
                      "    let w = t(1, a) or t(2, b) and not t(3, c)\n"
                      "    if w_log != log or (n == 1) != (not (not w)):\n"
                      "        bad = bad + 1\n"
                      "bad\n";

    CHECK_EQ(run_sint(src), 0);
}

/* An operand of a condition fails on its own line, and a condition costs the
 * tests and jumps it runs, without the dup and pop of a value: here a pass
 * is the for_loop, `i % 3`, `== 0` and its jump, plus that again for `i % 5`
 * when the first holds, plus the add and store when both do. */
CNOR_TEST(runs_a_condition_as_jumps)
{
    const char *fn = "fn f(n: i32) -> i32:\n"
                     "    let mut s = 0\n"
                     "    for i in 0..n:\n"
                     "        if i %% 3 == 0 and i %% 5 == 0:\n"
                     "            s = s + 1\n"
                     "    return s\n"
                     "f(%d)\n";
    char buffer[256];
    cnor_step_count_t small = 0U;
    cnor_step_count_t large = 0U;
    cnor_line_t line = 0U;

    (void) snprintf(buffer, sizeof(buffer), fn, 15);
    REQUIRE(run_with_budget(buffer, 0U, &small) == cnor_error_none);
    (void) snprintf(buffer, sizeof(buffer), fn, 30);
    REQUIRE(run_with_budget(buffer, 0U, &large) == cnor_error_none);
    /* i = 15..29: five multiples of 3, one of 15. */
    CHECK_EQ(large - small, 15U * 4U + 5U * 3U + 1U * 2U);
    CHECK_EQ(run_sint(buffer), 2);
    CHECK_EQ(run_error("let s = \"x\"\nlet n = 1\nif n == 1 and\\\n", &line), cnor_error_count);
    CHECK_EQ(run_error("let s = echo(\"x\")\nlet n = 1\nif n == 1 and s < 3:\n    n\n", &line),
             cnor_error_type);
    CHECK_EQ(line, 3U);
    CHECK_EQ(run_error("let s = echo(\"x\")\nlet mut n = 1\nwhile not (n > 3 or s > 3):\n"
                       "    n = n + 1\n",
                       &line),
             cnor_error_type);
    CHECK_EQ(line, 3U);
}

/* Integer arms dispatch through a table when they are dense; the arm a value
 * reaches is the one the tests in turn reach. The `"z"` arm, which no number
 * equals, forces those tests, so each table compares against them. */
CNOR_TEST(matches_dense_arms_like_tests_in_turn)
{
    static const char *const arm_sets[] = {
        "0 1 2 3 4 5",       "0 1 2 4 5 7",          "-3 -2 -1 0 1 2", "5 3 1 2 4",
        "7 6 5 4",           "-2147483648 -2147483647 -2147483646 -2147483645",
        "2147483644 2147483645 2147483646 2147483647", "1 3 5 7 9 11 13",
    };
    static const char *const subjects[] = {
        "-4", "-3", "-1", "0", "1", "2", "3", "4", "5", "6", "7", "8", "11", "13", "14",
        "-2147483647 - 1", "-2147483647", "2147483647", "2147483646", "2147483647 + 1",
        "-2147483647 - 2", "0x40000000 * 0x40000000", "-0x40000000 * 0x40000000",
        "0xFFFFFFFF", "0x80000000 - 0x7FFFFFFE", "\"1\"", "none", "true",
#if LH_REAL_ENABLED
        "2.0", "2.5", "-1.0", "2147483647.0",
#endif
    };
    static char src[4096];
    size_t s;
    size_t d;

    for (s = 0U; s < sizeof(arm_sets) / sizeof(arm_sets[0]); ++s)
    {
        for (d = 0U; d < 2U; ++d)
        {
            size_t used = 0U;
            size_t form;

            for (form = 0U; form < 2U; ++form)
            {
                const char *at = arm_sets[s];
                int arm = 1;

                used += (size_t) snprintf(src + used, sizeof(src) - used,
                                          "fn f%u(x: i32) -> i32:\n    let mut r = 0\n"
                                          "    match echo(x):\n",
                                          (unsigned) form);
                if (form == 1U)
                {
                    used += (size_t) snprintf(src + used, sizeof(src) - used,
                                              "        \"z\" => r = -1\n");
                }
                while (*at != '\0')
                {
                    int length = 0;

                    while (at[length] != '\0' && at[length] != ' ')
                    {
                        ++length;
                    }
                    used += (size_t) snprintf(src + used, sizeof(src) - used,
                                              "        %.*s => r = %d\n", length, at, arm++);
                    at += at[length] == ' ' ? length + 1 : length;
                }
                if (d == 1U)
                {
                    used += (size_t) snprintf(src + used, sizeof(src) - used,
                                              "        _ => r = 99\n");
                }
                used += (size_t) snprintf(src + used, sizeof(src) - used, "    return r\n");
            }
            used += (size_t) snprintf(src + used, sizeof(src) - used, "let mut bad = 0\n");
            {
                size_t k;

                for (k = 0U; k < sizeof(subjects) / sizeof(subjects[0]); ++k)
                {
                    used += (size_t) snprintf(src + used, sizeof(src) - used,
                                              "if f0(echo(%s)) != f1(echo(%s)):\n"
                                              "    bad = bad + 1\n",
                                              subjects[k], subjects[k]);
                }
            }
            (void) snprintf(src + used, sizeof(src) - used, "bad\n");
            if (run_sint(src) != 0)
            {
                printf("    arms %s, default %u\n", arm_sets[s], (unsigned) d);
            }
            CHECK_EQ(run_sint(src), 0);
        }
    }
}

/* A table costs the same steps whichever arm runs (the switch and its jump);
 * repeated patterns, a `_` before the end and sparse or few arms keep the
 * tests in turn, and the first equal arm still wins. */
CNOR_TEST(takes_a_table_only_where_it_is_the_same)
{
    const char *dense = "fn f(x: i32) -> i32:\n"
                        "    let mut r = 0\n"
                        "    match x:\n"
                        "        0 => r = 1\n"
                        "        1 => r = 2\n"
                        "        2 => r = 3\n"
                        "        3 => r = 4\n"
                        "        _ => r = 5\n"
                        "    return r\n"
                        "f(echo(%s))\n";
    char buffer[512];
    cnor_step_count_t first = 0U;
    cnor_step_count_t steps = 0U;
    static const char *const subjects[] = {"1", "2", "3", "9", "\"a\""};
    size_t k;

    (void) snprintf(buffer, sizeof(buffer), dense, "0");
    REQUIRE(run_with_budget(buffer, 0U, &first) == cnor_error_none);
    for (k = 0U; k < sizeof(subjects) / sizeof(subjects[0]); ++k)
    {
        (void) snprintf(buffer, sizeof(buffer), dense, subjects[k]);
        REQUIRE(run_with_budget(buffer, 0U, &steps) == cnor_error_none);
        /* The default sits right after the table, and the last arm needs no
         * jump to the end: one jump fewer each. */
        CHECK_EQ(steps, k < 2U ? first : first - 1U);
    }
    CHECK_EQ(run_sint("fn f(x: i32) -> i32:\n    match x:\n        1 => return 10\n"
                      "        2 => return 20\n        1 => return 30\n        3 => return 40\n"
                      "        4 => return 50\n    return 0\nf(1) + f(4)\n"),
             60);
    CHECK_EQ(run_sint("fn f(x: i32) -> i32:\n    match x:\n        1 => return 10\n"
                      "        _ => return 20\n        2 => return 30\n        3 => return 40\n"
                      "        4 => return 50\n    return 0\nf(1) + f(4)\n"),
             30);
    CHECK_EQ(run_sint("fn f(x: i32) -> i32:\n    match x:\n        1 => return 10\n"
                      "        100 => return 20\n        200 => return 30\n"
                      "        300 => return 40\n    return 0\nf(200) + f(5)\n"),
             30);
    CHECK_EQ(run_sint("fn f(x: i32) -> i32:\n    match x:\n        1 => return 10\n"
                      "        2 => return 20\n    return 0\nf(2) + f(3)\n"),
             20);
    /* Nothing matching and no `_` does nothing; arms may fall off the end. */
    CHECK_EQ(run_sint("let mut r = 7\nfor i in 0..9:\n    match i:\n        1 => r = r + 1\n"
                      "        2 => r = r + 2\n        3 => r = r + 3\n        4 => r = r + 4\n"
                      "        6 => r = r + 6\nr\n"),
             23);
}

/* `g = g op x` stores straight into the global; the values are those of the
 * plain instructions, wrapping and text included. */
CNOR_TEST(updates_globals_in_place)
{
    CHECK_EQ(run_sint("let mut g = 0\nfn f(n: i32) -> i32:\n    for i in 0..n:\n"
                      "        g = g + i\n        g = g - 1\n    return g\nf(100)\n"),
             4950 - 100);
    CHECK_EQ(run_sint("let mut g = 1\nlet mut h = 3\nfor i in 0..70:\n    g = g * 2\n"
                      "    h = h * i - h\ng\n"),
             0);
    CHECK_EQ(run_sint("let mut g = 0x40000000 * 0x40000000 * 8 - 1\nlet k = 1\n"
                      "g = g + k\ng\n"),
             (long long) (-9223372036854775807LL - 1LL));
    CHECK(run_text_is("let mut g = \"a\"\nfn f() -> str:\n    let b = \"b\"\n"
                      "    for i in 0..3:\n        g = g + b\n    return g\nf()\n",
                      "abbb"));
    CHECK_EQ(run_sint("let mut g = 10\nfn f(x: i32) -> i32:\n    g = g / x\n    g = g % 7\n"
                      "    return g\nf(3)\n"),
             3);
}

/* Every budget short of the total stops after exactly that many steps, inside
 * `and` / `or` conditions, a table and the global updates too. */
CNOR_TEST(stops_after_exactly_the_budget_inside_tables_and_conditions)
{
    const char *src = "let mut g = 0\n"
                      "fn h(n: i32) -> i32:\n"
                      "    let mut s = 0\n"
                      "    for i in 0..n:\n"
                      "        match i % 5:\n"
                      "            0 => s = s + 1\n"
                      "            1 => s = s + 2\n"
                      "            2 => s = s * 3\n"
                      "            4 => s = s - 1\n"
                      "            _ => s = s + 7\n"
                      "        if i > 2 and s % 2 == 0 or not (i < 4):\n"
                      "            g = g + s\n"
                      "        while s > 3 and (g > 0 or i == 0):\n"
                      "            s = s - 4\n"
                      "        g = g - i\n"
                      "    return s + g\n"
                      "let mut t = 0\n"
                      "for k in 0..5:\n"
                      "    t = t + h(k + 2)\n"
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

/* -- calls and value moves --------------------------------------------------- */

/* Every kind of value keeps its kind and contents through what moves values
 * around: parameters, locals, a global, `or`'s dup, a native, a nested
 * return and the final one. */
CNOR_TEST(moves_every_kind_through_calls_and_slots)
{
    static const char *const fn = "let mut g = 0\n"
                                  "fn id(x: i32) -> i32:\n"
                                  "    return x\n"
                                  "fn pass(x: i32) -> i32:\n"
                                  "    let y = x\n"
                                  "    g = y\n"
                                  "    let z = g\n"
                                  "    let w = z or z\n"
                                  "    return id(echo(id(w)))\n"
                                  "pass(echo(%s))\n";
    static const struct
    {
        const char *value;
        lh_value_kind_t kind;
        long long number;
    } cases[] = {
        {"none", lh_value_kind_none, 0},
        {"true", lh_value_kind_bool, 1},
        {"false", lh_value_kind_bool, 0},
        {"0", lh_value_kind_sint, 0},
        {"-5", lh_value_kind_sint, -5},
        {"0x40000000 * 0x40000000 * 8 - 1", lh_value_kind_sint, 9223372036854775807LL},
        {"0xFFFFFFFF", lh_value_kind_uint, 0xFFFFFFFFLL},
    };
    static const char *const texts[][2] = {
        {"\"\"", ""},
        {"\"x\"", "x"},
        {"\"a longer text than one word of storage\"", "a longer text than one word of storage"},
        {"\"ab\" + \"cd\"", "abcd"},
    };
    char buffer[1024];
    size_t k;

    for (k = 0U; k < sizeof(cases) / sizeof(cases[0]); ++k)
    {
        lh_value_t result;

        (void) snprintf(buffer, sizeof(buffer), fn, cases[k].value);
        REQUIRE(run(buffer, &result));
        CHECK_EQ(lh_value_get_kind(&result), cases[k].kind);
        if (cases[k].kind == lh_value_kind_bool)
        {
            CHECK_EQ(lh_value_get_data(&result)->b ? 1 : 0, cases[k].number);
        }
        else if (cases[k].kind != lh_value_kind_none)
        {
            CHECK_EQ(lh_value_get_data(&result)->i, cases[k].number);
        }
    }
    for (k = 0U; k < sizeof(texts) / sizeof(texts[0]); ++k)
    {
        (void) snprintf(buffer, sizeof(buffer), fn, texts[k][0]);
        CHECK(run_text_is(buffer, texts[k][1]));
    }
#if LH_REAL_ENABLED
    {
        lh_value_t result;

        (void) snprintf(buffer, sizeof(buffer), fn, "-2.5");
        REQUIRE(run(buffer, &result));
        REQUIRE(lh_value_get_kind(&result) == lh_value_kind_real);
        CHECK(lh_value_get_data(&result)->r == -2.5);
    }
#endif
    /* A string next to numbers keeps its length when the values around it
     * move (the length is the one field past the first word). */
    CHECK(run_text_is("fn f(a: i32, b: str, c: i32) -> str:\n    let t = b\n"
                      "    let n = a + c\n    if n == 3:\n        return echo(t)\n"
                      "    return \"no\"\nf(1, \"middle\", 2)\n",
                      "middle"));
}

/* Compile @p source and run it twice on one machine with a call depth of
 * @p depth and @p slots stack slots; both runs must end alike, with the error
 * through the return value and its line through @p line. */
static cnor_error_t
run_limited(const char *source, cnor_call_depth_t depth, cnor_stack_slots_t slots,
            cnor_line_t *line, long long *result)
{
    built_t b;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_error_t first = cnor_error_count;
    int run_index;

    compile(&b, source);
    if (!b.ok)
    {
        return cnor_error_count;
    }
    config.natives = &m_table;
    config.ctx = &m_touched;
    config.max_depth = depth;
    config.stack_slots = slots;
    cnor_vm_init(&vm, &config);
    if (!cnor_vm_load(&vm, b.image.data, b.image.size))
    {
        first = cnor_diag_get_code(cnor_vm_get_diag(&vm));
    }
    else
    {
        for (run_index = 0; run_index < 2; ++run_index)
        {
            const lh_bool_t ok = cnor_vm_run(&vm);
            const cnor_error_t code = ok ? cnor_error_none
                                         : cnor_diag_get_code(cnor_vm_get_diag(&vm));

            if (run_index == 0)
            {
                first = code;
                if (line != lh_null)
                {
                    *line = cnor_diag_get_line(cnor_vm_get_diag(&vm));
                }
                if (ok && result != lh_null)
                {
                    const lh_value_t value = cnor_vm_get_result(&vm);

                    *result = lh_value_get_kind(&value) == lh_value_kind_sint
                                  ? (long long) lh_value_get_data(&value)->i
                                  : -999999;
                }
            }
            else if (code != first)
            {
                first = cnor_error_count;
            }
            cnor_vm_reset(&vm);
        }
    }
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&b.image);
    return first;
}

/* The depth limit counts calls exactly: with a limit of N, N nested calls
 * run and one more fails on the line of the call, and the machine runs again
 * afterwards. A stack too small for the frames fails the same way. */
CNOR_TEST(limits_calls_by_depth_and_stack_exactly)
{
    static const char *const fn = "fn f(n: i32) -> i32:\n"
                                  "    if n == 0:\n"
                                  "        return 0\n"
                                  "    return 1 + f(n - 1)\n"
                                  "f(%d)\n";
    static const char *const wide = "fn f(n: i32, a: i32, b: i32, c: i32) -> i32:\n"
                                    "    let x = a + b\n"
                                    "    let y = x + c\n"
                                    "    if n == 0:\n"
                                    "        return y\n"
                                    "    return f(n - 1, a, b, c) + 1\n"
                                    "f(%d, 1, 2, 3)\n";
    char buffer[512];
    cnor_line_t line = 0U;
    long long result = 0;
    int k;
    int deepest = -1;

    for (k = 0; k < 12; ++k)
    {
        (void) snprintf(buffer, sizeof(buffer), fn, k);
        result = 0;
        if (k < 8)
        {
            CHECK_EQ(run_limited(buffer, 8U, 4096U, lh_null, &result), cnor_error_none);
            CHECK_EQ(result, k);
        }
        else
        {
            CHECK_EQ(run_limited(buffer, 8U, 4096U, &line, lh_null), cnor_error_call_depth);
            CHECK_EQ(line, 4U);
        }
    }
    /* f(n) needs n + 1 frames of 6 slots and up to 4 operands; find the last
     * n that fits and check that every n beyond overflows on the call line. */
    for (k = 0; k < 40; ++k)
    {
        cnor_error_t code;

        (void) snprintf(buffer, sizeof(buffer), wide, k);
        code = run_limited(buffer, 1000U, 128U, &line, &result);
        if (code == cnor_error_none)
        {
            CHECK_EQ(result, 6 + k);
            CHECK_EQ(deepest, k - 1);
            deepest = k;
        }
        else
        {
            CHECK_EQ(code, cnor_error_stack_overflow);
            CHECK(line == 6U || line == 7U);
        }
    }
    CHECK(deepest > 5 && deepest < 30);
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
    CNOR_TEST_CASE(computes_operand_forms_as_the_stack_does),
    CNOR_TEST_CASE(fails_operand_forms_the_way_the_stack_does),
    CNOR_TEST_CASE(matches_integer_arms_by_value),
    CNOR_TEST_CASE(counts_ranges_at_their_edges),
    CNOR_TEST_CASE(wraps_a_loop_variable_like_addition),
    CNOR_TEST_CASE(computes_index_forms_as_the_stack_does),
    CNOR_TEST_CASE(divides_at_the_width_the_value_fits_in),
    CNOR_TEST_CASE(makes_an_array_and_writes_its_elements),
    CNOR_TEST_CASE(takes_the_index_of_a_local_in_one_instruction),
    CNOR_TEST_CASE(keeps_the_rule_of_the_remainder_by_a_power_of_two),
    CNOR_TEST_CASE(runs_a_for_step_in_one_instruction),
    CNOR_TEST_CASE(stops_after_exactly_the_budget_inside_for_loops),
    CNOR_TEST_CASE(compares_text_by_its_bytes),
    CNOR_TEST_CASE(joins_text_on_locals_and_the_stack),
    CNOR_TEST_CASE(refuses_to_join_text_with_anything_else),
    CNOR_TEST_CASE(stops_a_join_at_the_end_of_the_pool),
    CNOR_TEST_CASE(indexes_and_measures_text_at_its_edges),
    CNOR_TEST_CASE(stops_after_exactly_the_budget_inside_string_work),
    CNOR_TEST_CASE(compares_and_joins_text_of_every_size),
    CNOR_TEST_CASE(gives_the_pool_back_on_reset_only),
    CNOR_TEST_CASE(branches_on_logic_like_its_value),
    CNOR_TEST_CASE(short_circuits_a_condition_like_its_value),
    CNOR_TEST_CASE(runs_a_condition_as_jumps),
    CNOR_TEST_CASE(matches_dense_arms_like_tests_in_turn),
    CNOR_TEST_CASE(takes_a_table_only_where_it_is_the_same),
    CNOR_TEST_CASE(updates_globals_in_place),
    CNOR_TEST_CASE(stops_after_exactly_the_budget_inside_tables_and_conditions),
    CNOR_TEST_CASE(moves_every_kind_through_calls_and_slots),
    CNOR_TEST_CASE(limits_calls_by_depth_and_stack_exactly),
};

int
main(int argc, char **argv)
{
    return cnor_test_main("compiler", m_cases, CNOR_TEST_COUNT(m_cases), argc, argv);
}
