/* The language rules that span a whole program: static types, the order
 * globals are initialized in, and `import` (through an in-memory loader). */

#include "harness.h"

#include <cnor/compiler.h>
#include <cnor/native/initializer.h>
#include <cnor/vm.h>
#include <lh/null.h>
#include <lh/real.h>

/* -- natives --------------------------------------------------------------- */

/* Gives its argument back, of whatever kind: a value of type `any`. */
static lh_value_t
echo(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    return args[0];
}

/* The notes taken by `note(n)`, in order, to observe when code runs. */
static long long m_notes[64];
static size_t m_note_count;

static lh_value_t
note(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    if (m_note_count < sizeof(m_notes) / sizeof(m_notes[0]))
    {
        m_notes[m_note_count++] = (long long) lh_value_get_data(&args[0])->i;
    }
    return args[0];
}

static const cnor_native_t m_natives[] = {
    cnor_native_initializer("echo", echo, 1U, 1U),
    cnor_native_initializer("note", note, 1U, 1U),
};
static const cnor_native_table_t m_table = cnor_native_table_initializer(m_natives, 2U);

/* -- files ----------------------------------------------------------------- */

/** @brief One file the in-memory loader serves. */
typedef struct memory_file
{
    const char *path;
    const char *text; /**< ::lh_null: found, but cannot be read. */
} memory_file_t;

/** @brief The loader's state: its files and what was asked of it. */
typedef struct memory_loader
{
    const memory_file_t *files;
    size_t count;
    int loads[128]; /**< Per file, successful loads. */
    int releases;   /**< Texts given back. */
    int handed_out; /**< Texts handed over. */
} memory_loader_t;

static lh_str_view_t
view_of(const char *text)
{
    lh_str_view_t view;

    if (text[0] == '\0')
    {
        return lh_str_view_empty();
    }
    lh_str_init_by_size(&view, text, strlen(text));
    return view;
}

static lh_bool_t
view_is(lh_str_view_t view, const char *text)
{
    return lh_str_view_get_size(&view) == strlen(text)
                   && (strlen(text) == 0U
                       || memcmp(lh_str_view_get_data(&view), text, strlen(text)) == 0)
               ? lh_bool_true
               : lh_bool_false;
}

static cnor_error_t
memory_load(lh_ptr ctx, lh_str_view_t path, cnor_source_t *out)
{
    memory_loader_t *loader = (memory_loader_t *) ctx;
    size_t i;

    for (i = 0U; i < loader->count; ++i)
    {
        if (view_is(path, loader->files[i].path))
        {
            if (loader->files[i].text == lh_null)
            {
                return cnor_error_bad_module;
            }
            loader->loads[i]++;
            loader->handed_out++;
            out->text = view_of(loader->files[i].text);
            out->handle = (lh_ptr) &loader->files[i];
            return cnor_error_none;
        }
    }
    return cnor_error_no_such_module;
}

static void
memory_release(lh_ptr ctx, cnor_source_t *source)
{
    (void) source;
    ((memory_loader_t *) ctx)->releases++;
}

/* -- running --------------------------------------------------------------- */

/** @brief What compiling and running a program came to. */
typedef struct outcome
{
    lh_bool_t compiled;
    cnor_error_t code;  /**< The compiler's or the machine's; none when both went. */
    cnor_line_t line;
    char name[256];     /**< The diagnostic's name or detail. */
    char file[256];     /**< The file it points at. */
    long long value;    /**< The result, when it is an integer; -999999 otherwise. */
} outcome_t;

static void
copy_view(char *to, size_t capacity, lh_str_view_t view)
{
    size_t size = lh_str_view_get_size(&view);

    if (size >= capacity)
    {
        size = capacity - 1U;
    }
    if (size > 0U)
    {
        memcpy(to, lh_str_view_get_data(&view), size);
    }
    to[size] = '\0';
}

static void
take_diag(outcome_t *out, const cnor_diag_t *diag)
{
    out->code = cnor_diag_get_code(diag);
    out->line = cnor_diag_get_line(diag);
    copy_view(out->name, sizeof(out->name), cnor_diag_get_name(diag));
    copy_view(out->file, sizeof(out->file), cnor_diag_get_file(diag));
}

/* Compile @p files[0] (the main file, named by its path) with the others
 * behind the loader and @p search_paths, then run it @p runs times. */
static void
run_program(const memory_file_t *files, size_t count, const char *const *search_paths,
            size_t search_count, memory_loader_t *loader, int runs, outcome_t *out)
{
    cnor_compiler_config_t config = cnor_compiler_config_initializer();
    lh_str_view_t paths[4];
    cnor_image_t image;
    cnor_diag_t diag;
    size_t i;

    memset(out, 0, sizeof(*out));
    memset(loader, 0, sizeof(*loader));
    out->value = -999999;
    loader->files = files;
    loader->count = count;
    for (i = 0U; i < search_count; ++i)
    {
        paths[i] = view_of(search_paths[i]);
    }
    config.natives = &m_table;
    config.file = view_of(files[0].path);
    config.load = memory_load;
    config.release = memory_release;
    config.load_ctx = loader;
    config.search_paths = paths;
    config.search_path_count = (lh_u32_t) search_count;
    m_note_count = 0U;
    out->compiled = cnor_compiler_compile_source(view_of(files[0].text), &config, &image, &diag);
    if (!out->compiled)
    {
        take_diag(out, &diag);
        return;
    }
    {
        cnor_vm_config_t vm_config = cnor_vm_config_initializer();
        cnor_vm_t vm;
        int run;

        vm_config.natives = &m_table;
        cnor_vm_init(&vm, &vm_config);
        if (!cnor_vm_load(&vm, image.data, image.size))
        {
            take_diag(out, cnor_vm_get_diag(&vm));
        }
        for (run = 0; run < runs && out->code == cnor_error_none; ++run)
        {
            if (!cnor_vm_run(&vm))
            {
                take_diag(out, cnor_vm_get_diag(&vm));
            }
            else
            {
                const lh_value_t result = cnor_vm_get_result(&vm);

                if (lh_value_get_kind(&result) == lh_value_kind_sint)
                {
                    out->value = (long long) lh_value_get_data(&result)->i;
                }
            }
        }
        cnor_vm_deinit(&vm);
    }
    cnor_image_deinit(&image);
}

/* One file, run once. */
static outcome_t
run_source(const char *text)
{
    const memory_file_t files[] = {{"main.cn", text}};
    memory_loader_t loader;
    outcome_t out;

    run_program(files, 1U, lh_null, 0U, &loader, 1, &out);
    return out;
}

static long long
value_of(const char *text)
{
    const outcome_t out = run_source(text);

    if (out.code != cnor_error_none)
    {
        printf("    failed: %s at %s:%u: %s\n", cnor_error_get_message(out.code), out.file,
               (unsigned) out.line, out.name);
    }
    return out.value;
}

/* The compile error of @p text, its line through @p line. */
static cnor_error_t
error_of(const char *text, cnor_line_t *line)
{
    const outcome_t out = run_source(text);

    *line = out.line;
    return out.compiled ? cnor_error_none : out.code;
}

/* -- types ------------------------------------------------------------------- */

CNOR_TEST(runs_well_typed_programs)
{
    CHECK_EQ(value_of("fn add(a: i32, b: i32) -> i32:\n    return a + b\nadd(2, 3)\n"), 5);
    /* i32 and u32 are one family of integers. */
    CHECK_EQ(value_of("fn f(a: u32) -> i32:\n    return a + 1\nf(4) + f(0xFFFFFFFF - 4)\n"),
             5 + 0xFFFFFFFFLL - 3);
    CHECK_EQ(value_of("let s: str = \"ab\"\nlet n: i32 = len(s + s)\nn\n"), 4);
    CHECK_EQ(value_of("let b: bool = 1 < 2\nfn pick(c: bool) -> i32:\n    if c:\n"
                      "        return 1\n    else:\n        return 2\npick(b) + pick(not b)\n"),
             3);
    /* Inferred from the initializer; `none` or nothing infers nothing. */
    CHECK_EQ(value_of("let mut x = none\nx = 5\nx = \"five\"\nlet mut y\ny = 3\ny\n"), 3);
    /* Every kind has a truth: a condition takes any type. */
    CHECK_EQ(value_of("let mut n = 0\nif \"x\":\n    n = n + 1\nwhile n:\n    n = n - 1\n"
                      "if none:\n    n = 9\nn\n"),
             0);
    /* `==` compares anything with `none`, and values of one family. */
    CHECK_EQ(value_of("let s = \"a\"\nlet mut n = 0\nif s != none and s == \"a\":\n"
                      "    n = 1\nn\n"),
             1);
#if LH_REAL_ENABLED
    CHECK_EQ(value_of("fn scale(x: f32) -> f32:\n    return x * 2.0\nlet r = scale(1.5)\n"
                      "let mut n = 0\nif r == 3.0 and r > 2:\n    n = 1\nn\n"),
             1);
#endif
}

CNOR_TEST(accepts_values_of_unknown_type_anywhere)
{
    /* A host native's result has no static type: any declared type takes it,
     * and the machine checks kinds as it always did. */
    CHECK_EQ(value_of("fn f(a: i32) -> i32:\n    return a * 2\nf(echo(21))\n"), 42);
    CHECK_EQ(value_of("let s: str = echo(\"abc\")\nlen(s)\n"), 3);
    CHECK_EQ(value_of("let x = echo(1)\nlet mut y = x\ny = \"text\"\nlen(y)\n"), 4);
    {
        const outcome_t out =
            run_source("fn f(a: i32) -> i32:\n    return a * 2\nf(echo(\"x\"))\n");

        CHECK(out.compiled);
        CHECK_EQ(out.code, cnor_error_type);
        CHECK_EQ(out.line, 2U);
    }
}

CNOR_TEST(refuses_arguments_of_the_wrong_type)
{
    outcome_t out = run_source("fn f(a: i32, b: str) -> i32:\n    return a\nf(1, 2)\n");

    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK_EQ(out.line, 3U);
    CHECK(strcmp(out.name, "expected str, found i32") == 0);
    CHECK(strcmp(out.file, "main.cn") == 0);
    out = run_source("fn f(a: bool) -> i32:\n    return 1\nlet x = 1\n\nf(x)\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK_EQ(out.line, 5U);
    CHECK(strcmp(out.name, "expected bool, found i32") == 0);
    /* A `native fn` declaration states the types of a host's native. */
    out = run_source("native fn echo(x: i32) -> i32\necho(\"a\")\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK_EQ(out.line, 2U);
    CHECK_EQ(value_of("native fn echo(x: i32) -> i32\nlet n = echo(4)\nn + 1\n"), 5);
    out = run_source("native fn echo(x: i32) -> str\nlet n: i32 = echo(4)\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK(strcmp(out.name, "expected i32, found str") == 0);
#if LH_REAL_ENABLED
    /* Reals are not integers. */
    out = run_source("fn f(a: i32) -> i32:\n    return a\nf(1.5)\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK(strcmp(out.name, "expected i32, found f32") == 0);
#endif
}

CNOR_TEST(refuses_arity_mismatches)
{
    cnor_line_t line = 0U;

    CHECK_EQ(error_of("fn f(a: i32) -> i32:\n    return a\nf()\n", &line),
             cnor_error_wrong_arg_count);
    CHECK_EQ(line, 3U);
    CHECK_EQ(error_of("fn f() -> i32:\n    return 1\n\nf(1)\n", &line),
             cnor_error_wrong_arg_count);
    CHECK_EQ(line, 4U);
    CHECK_EQ(error_of("len(\"a\", \"b\")\n", &line), cnor_error_wrong_arg_count);
    CHECK_EQ(error_of("echo()\n", &line), cnor_error_wrong_arg_count);
}

CNOR_TEST(checks_returns_against_the_declared_type)
{
    cnor_line_t line = 0U;
    outcome_t out;

    out = run_source("fn f() -> i32:\n    return \"x\"\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK_EQ(out.line, 2U);
    CHECK(strcmp(out.name, "expected i32, found str") == 0);
    out = run_source("fn f() -> i32:\n    return\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK(strcmp(out.name, "expected i32, found none") == 0);
    /* Without a return type a function returns anything, `none` included. */
    CHECK_EQ(error_of("fn f(n: i32):\n    if n > 0:\n        return n\n    return \"no\"\n",
                      &line),
             cnor_error_none);
    /* Falling off the end returns `none`: refused where a type is declared. */
    CHECK_EQ(error_of("fn f(n: i32) -> i32:\n    if n > 0:\n        return 1\n", &line),
             cnor_error_missing_return);
    CHECK_EQ(line, 1U);
    CHECK_EQ(error_of("fn f(n: i32) -> i32:\n    for i in 0..n:\n        return i\n", &line),
             cnor_error_missing_return);
    CHECK_EQ(error_of("fn f(n: i32) -> i32:\n    while true:\n        if n > 1:\n"
                      "            break\n        return 1\n",
                      &line),
             cnor_error_missing_return);
    CHECK_EQ(error_of("fn f(n: i32) -> i32:\n    match n:\n        1 => return 1\n"
                      "        2 => return 2\n",
                      &line),
             cnor_error_missing_return);
    /* The forms that always return. */
    CHECK_EQ(value_of("fn f(n: i32) -> i32:\n    if n > 0:\n        return 1\n"
                      "    else if n < 0:\n        return -1\n    else:\n        return 0\n"
                      "f(5) * 100 + f(-5) * 10 + f(0)\n"),
             90);
    CHECK_EQ(value_of("fn f(n: i32) -> i32:\n    match n:\n        1 => return 10\n"
                      "        _ => return 20\nf(1) + f(2)\n"),
             30);
    CHECK_EQ(value_of("fn f(n: i32) -> i32:\n    let mut i = 0\n    while true:\n"
                      "        for k in 0..3:\n            if k == 1:\n                break\n"
                      "        i = i + 1\n        if i == n:\n            return i\nf(4)\n"),
             4);
}

CNOR_TEST(checks_assignments_against_the_variable_type)
{
    outcome_t out;

    out = run_source("let mut x: i32 = 1\nx = \"a\"\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK_EQ(out.line, 2U);
    out = run_source("let mut x = 1\nfn f():\n    x = true\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK_EQ(out.line, 3U);
    CHECK(strcmp(out.name, "expected i32, found bool") == 0);
    out = run_source("fn f():\n    let mut s = \"a\"\n    s = s + \"b\"\n    s = 2\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK_EQ(out.line, 4U);
    out = run_source("let x: str = 1\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    /* A typed variable needs a value of its type from the start. */
    out = run_source("let x: i32\n");
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK(strcmp(out.name, "expected i32, found none") == 0);
}

CNOR_TEST(refuses_operators_that_would_always_fail)
{
    static const char *const bad[] = {
        "\"a\" - 1", "1 + \"a\"",    "-\"a\"",    "true + 1", "none * 2", "\"a\" < \"b\"",
        "1 < \"a\"", "1 == \"a\"",   "true != 1", "\"a\" % 2", "len(5)",  "5[0]",
        "\"a\"[\"b\"]",
    };
    static const char *const good[] = {
        "1 + 0xFFFFFFFF", "\"a\" + \"b\"", "1 == none",    "none == \"a\"", "-5",
        "not \"a\"",      "1 and \"a\"",   "\"a\" or 2",  "echo(1) + 2",   "echo(\"a\") < 2",
    };
    char src[128];
    size_t k;
    cnor_line_t line = 0U;

    for (k = 0U; k < sizeof(bad) / sizeof(bad[0]); ++k)
    {
        cnor_error_t code;

        (void) snprintf(src, sizeof(src), "let a = 1\n%s\n", bad[k]);
        code = error_of(src, &line);
        if (code != cnor_error_operand_type && code != cnor_error_type_mismatch)
        {
            printf("    accepted: %s\n", bad[k]);
        }
        CHECK(code == cnor_error_operand_type || code == cnor_error_type_mismatch);
        CHECK_EQ(line, 2U);
    }
    for (k = 0U; k < sizeof(good) / sizeof(good[0]); ++k)
    {
        (void) snprintf(src, sizeof(src), "let a = 1\n%s\n", good[k]);
        if (error_of(src, &line) != cnor_error_none)
        {
            printf("    refused: %s\n", good[k]);
        }
        CHECK_EQ(error_of(src, &line), cnor_error_none);
    }
#if LH_REAL_ENABLED
    /* Integers and reals mix in arithmetic and comparisons, as they run. */
    CHECK_EQ(error_of("let r = 1 + 2.5\nr < 4\n", &line), cnor_error_none);
#endif
}

CNOR_TEST(checks_match_patterns_and_ranges)
{
    cnor_line_t line = 0U;

    CHECK_EQ(error_of("let s = \"a\"\nmatch s:\n    \"a\" => 1\n    2 => 2\n", &line),
             cnor_error_type_mismatch);
    CHECK_EQ(line, 4U);
    CHECK_EQ(error_of("match 3:\n    none => 1\n    _ => 2\n", &line), cnor_error_none);
    CHECK_EQ(value_of("let mut r = 0\nmatch echo(\"a\"):\n    1 => r = 1\n    \"a\" => r = 2\nr\n"),
             2);
    CHECK_EQ(error_of("for i in 0..\"a\":\n    i\n", &line), cnor_error_type_mismatch);
    CHECK_EQ(line, 1U);
    CHECK_EQ(error_of("for i in true..3:\n    i\n", &line), cnor_error_type_mismatch);
    /* The loop variable has the type of the first bound. */
    CHECK_EQ(error_of("for i in 0..3:\n    let s: str = i\n", &line), cnor_error_type_mismatch);
    CHECK_EQ(line, 2U);
}

/* -- initialization order ----------------------------------------------------- */

CNOR_TEST(initializes_globals_in_source_order)
{
    outcome_t out;
    memory_loader_t loader;
    const memory_file_t files[] = {{"main.cn", "let a = note(1)\n"
                                               "fn later() -> i32:\n"
                                               "    return a * 10 + note(2)\n"
                                               "let b = later()\n"
                                               "let c = note(3) + b\n"
                                               "c\n"}};

    run_program(files, 1U, lh_null, 0U, &loader, 1, &out);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 3 + 12);
    REQUIRE(m_note_count == 3U);
    CHECK_EQ(m_notes[0], 1);
    CHECK_EQ(m_notes[1], 2);
    CHECK_EQ(m_notes[2], 3);
    /* Every run runs the initializers again: a global starts from its `let`. */
    run_program((const memory_file_t[]){{"main.cn", "let mut n = note(5)\nn = n + 1\nn\n"}}, 1U,
                lh_null, 0U, &loader, 3, &out);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 6);
    CHECK_EQ(m_note_count, 3U);
}

CNOR_TEST(refuses_globals_used_before_their_let)
{
    outcome_t out;

    out = run_source("let b = a + 1\nlet a = 1\n");
    CHECK_EQ(out.code, cnor_error_not_initialized);
    CHECK_EQ(out.line, 1U);
    CHECK(strcmp(out.name, "a") == 0);
    out = run_source("let g = g + 1\n");
    CHECK_EQ(out.code, cnor_error_not_initialized);
    out = run_source("let mut n = 0\nif n == 0:\n    n = g\nlet g = 1\n");
    CHECK_EQ(out.code, cnor_error_not_initialized);
    CHECK_EQ(out.line, 3U);
    out = run_source("g = 1\nlet mut g = 0\n");
    CHECK_EQ(out.code, cnor_error_not_initialized);
    CHECK_EQ(out.line, 1U);
    /* A function may name any global; calling it before that global's `let`
     * is the error, at the call, through any chain of calls. */
    out = run_source("fn f() -> i32:\n    return g\nlet x = f()\nlet g = 5\n");
    CHECK_EQ(out.code, cnor_error_not_initialized);
    CHECK_EQ(out.line, 3U);
    CHECK(strcmp(out.name, "g") == 0);
    out = run_source("fn h() -> i32:\n    return k()\nfn k() -> i32:\n    return g\n"
                     "let mut n = 0\nfor i in 0..2:\n    n = n + h()\nlet g = 1\n");
    CHECK_EQ(out.code, cnor_error_not_initialized);
    CHECK_EQ(out.line, 7U);
    out = run_source("fn f(n: i32) -> i32:\n    if n == 0:\n        return 0\n"
                     "    return f(n - 1) + g\nlet x = f(3)\nlet g = 1\n");
    CHECK_EQ(out.code, cnor_error_not_initialized);
    CHECK_EQ(out.line, 5U);
    /* After the `let`, or never called from the top level: fine. */
    CHECK_EQ(value_of("fn f() -> i32:\n    return g\nlet g = 5\nf()\n"), 5);
    CHECK_EQ(value_of("fn f() -> i32:\n    return g\nfn unused() -> i32:\n    return f()\n"
                      "let g = 5\ng\n"),
             5);
}

/* -- imports ------------------------------------------------------------------ */

CNOR_TEST(links_imported_files_into_one_image)
{
    static const memory_file_t files[] = {
        {"app/main.cn", "import \"lib/math.cn\"\nimport \"lib/../shared.cn\"\n"
                        "let total = twice(base) + note(3)\ntotal\n"},
        {"app/lib/math.cn", "import \"../shared.cn\"\nfn twice(n: i32) -> i32:\n"
                            "    return n * 2\nlet ready = note(2)\n"},
        {"app/shared.cn", "let base = note(1) * 10\n"},
    };
    memory_loader_t loader;
    outcome_t out;

    run_program(files, 3U, lh_null, 0U, &loader, 2, &out);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 20 + 3);
    /* Each file is loaded and run once per run, imported files first. */
    CHECK_EQ(loader.loads[1], 1);
    CHECK_EQ(loader.loads[2], 1);
    CHECK_EQ(loader.releases, loader.handed_out);
    REQUIRE(m_note_count == 6U);
    CHECK_EQ(m_notes[0], 1);
    CHECK_EQ(m_notes[1], 2);
    CHECK_EQ(m_notes[2], 3);
    CHECK_EQ(m_notes[3], 1);
}

CNOR_TEST(looks_beside_the_importer_then_in_the_search_paths)
{
    static const memory_file_t files[] = {
        {"src/main.cn", "import \"a.cn\"\nimport \"b.cn\"\nwhich_a() * 10 + which_b()\n"},
        {"src/a.cn", "fn which_a() -> i32:\n    return 1\n"},
        {"lib/a.cn", "fn which_a() -> i32:\n    return 2\n"},
        {"lib/b.cn", "fn which_b() -> i32:\n    return 3\n"},
        {"/opt/b.cn", "fn which_b() -> i32:\n    return 4\n"},
    };
    static const char *const first[] = {"lib", "/opt"};
    static const char *const second[] = {"/opt/", "lib"};
    memory_loader_t loader;
    outcome_t out;

    run_program(files, 5U, first, 2U, &loader, 1, &out);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 13);
    run_program(files, 5U, second, 2U, &loader, 1, &out);
    CHECK_EQ(out.value, 14);
    /* Not found anywhere. */
    run_program(files, 1U, first, 2U, &loader, 1, &out);
    CHECK_EQ(out.code, cnor_error_no_such_module);
    CHECK_EQ(out.line, 1U);
    CHECK(strcmp(out.name, "a.cn") == 0);
    CHECK(strcmp(out.file, "src/main.cn") == 0);
}

CNOR_TEST(reports_files_that_cannot_be_imported)
{
    memory_loader_t loader;
    outcome_t out;

    {
        const memory_file_t files[] = {{"main.cn", "import \"bad.cn\"\n"}, {"bad.cn", lh_null}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_bad_module);
        CHECK(strcmp(out.name, "bad.cn") == 0);
    }
    {
        /* Without a loader every import fails. */
        cnor_compiler_config_t config = cnor_compiler_config_initializer();
        cnor_image_t image;
        cnor_diag_t diag;

        config.file = view_of("main.cn");
        CHECK(!cnor_compiler_compile_source(view_of("import \"x.cn\"\n1\n"), &config, &image,
                                            &diag));
        CHECK_EQ(cnor_diag_get_code(&diag), cnor_error_no_such_module);
    }
    {
        /* A syntax error points into the imported file. */
        const memory_file_t files[] = {{"main.cn", "import \"lib.cn\"\n1\n"},
                                       {"lib.cn", "let a = 1\nlet = 2\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_unexpected_token);
        CHECK_EQ(out.line, 2U);
        CHECK(strcmp(out.file, "lib.cn") == 0);
        CHECK_EQ(loader.releases, loader.handed_out);
    }
    {
        /* So does a type error. */
        const memory_file_t files[] = {{"main.cn", "import \"lib.cn\"\n1\n"},
                                       {"lib.cn", "\nfn f() -> i32:\n    return \"x\"\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_type_mismatch);
        CHECK_EQ(out.line, 3U);
        CHECK(strcmp(out.file, "lib.cn") == 0);
    }
    {
        /* `import` comes first. */
        const memory_file_t files[] = {{"main.cn", "let a = 1\nimport \"lib.cn\"\n"}};

        run_program(files, 1U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_misplaced_import);
        CHECK_EQ(out.line, 2U);
    }
}

CNOR_TEST(refuses_import_cycles)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"a.cn\"\n1\n"},
        {"a.cn", "import \"b.cn\"\n"},
        {"b.cn", "let x = 1\n\nimport \"./a.cn\"\n"},
    };
    memory_loader_t loader;
    outcome_t out;

    run_program(files, 3U, lh_null, 0U, &loader, 1, &out);
    CHECK_EQ(out.code, cnor_error_misplaced_import);
    {
        static const memory_file_t cycle[] = {
            {"main.cn", "import \"a.cn\"\n1\n"},
            {"a.cn", "import \"b.cn\"\n"},
            {"b.cn", "# b\nimport \"./a.cn\"\n"},
        };

        run_program(cycle, 3U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_import_cycle);
        CHECK_EQ(out.line, 2U);
        CHECK(strcmp(out.file, "b.cn") == 0);
        CHECK(strcmp(out.name, "./a.cn") == 0);
        CHECK_EQ(loader.releases, loader.handed_out);
    }
    {
        static const memory_file_t self[] = {{"main.cn", "import \"main.cn\"\n"}};

        run_program(self, 1U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_import_cycle);
    }
}

CNOR_TEST(stops_imports_nested_too_deep)
{
    static char texts[CNOR_COMPILER_MAX_IMPORT_DEPTH + 2U][32];
    static char paths[CNOR_COMPILER_MAX_IMPORT_DEPTH + 2U][16];
    memory_file_t files[CNOR_COMPILER_MAX_IMPORT_DEPTH + 2U];
    memory_loader_t loader;
    outcome_t out;
    size_t count;
    size_t i;

    for (count = CNOR_COMPILER_MAX_IMPORT_DEPTH + 1U; count <= CNOR_COMPILER_MAX_IMPORT_DEPTH + 2U;
         ++count)
    {
        for (i = 0U; i < count; ++i)
        {
            (void) snprintf(paths[i], sizeof(paths[i]), "f%u.cn", (unsigned) i);
            if (i + 1U < count)
            {
                (void) snprintf(texts[i], sizeof(texts[i]), "import \"f%u.cn\"\n",
                                (unsigned) (i + 1U));
            }
            else
            {
                (void) snprintf(texts[i], sizeof(texts[i]), "let deepest = %u\n", (unsigned) i);
            }
            files[i].path = paths[i];
            files[i].text = texts[i];
        }
        /* The main file's own one-line program: the result is the deepest. */
        run_program(files, count, lh_null, 0U, &loader, 1, &out);
        if (count == CNOR_COMPILER_MAX_IMPORT_DEPTH + 1U)
        {
            CHECK_EQ(out.code, cnor_error_none);
        }
        else
        {
            CHECK_EQ(out.code, cnor_error_imports_too_deep);
        }
    }
}

CNOR_TEST(gives_each_file_its_own_names)
{
    memory_loader_t loader;
    outcome_t out;

    {
        /* Names come from the files a file imports itself, not theirs. */
        const memory_file_t files[] = {{"main.cn", "import \"a.cn\"\nfrom_b\n"},
                                       {"a.cn", "import \"b.cn\"\nlet from_a = from_b\n"},
                                       {"b.cn", "let from_b = 1\n"}};

        run_program(files, 3U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_unknown_name);
        CHECK_EQ(out.line, 2U);
        CHECK(strcmp(out.file, "main.cn") == 0);
    }
    {
        /* A file's own names shadow imported ones; each file calls its own. */
        const memory_file_t files[] = {
            {"main.cn", "import \"a.cn\"\nfn f() -> i32:\n    return 1\nlet v = 5\n"
                        "f() * 100 + g() * 10 + v\n"},
            {"a.cn", "fn f() -> i32:\n    return 2\nfn g() -> i32:\n    return f() + v\n"
                     "let v = 1\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_none);
        CHECK_EQ(out.value, 100 + 30 + 5);
    }
    {
        /* The same name from two imports is an error where it is used, only. */
        const memory_file_t files[] = {{"main.cn", "import \"a.cn\"\nimport \"b.cn\"\nx + 1\n"},
                                       {"a.cn", "let x = 1\nlet y = 2\n"},
                                       {"b.cn", "fn x() -> i32:\n    return 1\nlet z = 3\n"}};

        run_program(files, 3U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_ambiguous_name);
        CHECK_EQ(out.line, 3U);
        CHECK(strcmp(out.name, "x") == 0);
    }
    {
        const memory_file_t files[] = {{"main.cn", "import \"a.cn\"\nimport \"b.cn\"\ny + z\n"},
                                       {"a.cn", "let x = 1\nlet y = 2\n"},
                                       {"b.cn", "let x = 5\nlet z = 3\n"}};

        run_program(files, 3U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_none);
        CHECK_EQ(out.value, 5);
    }
    {
        /* Imported globals keep their mutability and types. */
        const memory_file_t files[] = {{"main.cn", "import \"a.cn\"\nn = n + 1\nc = 2\n"},
                                       {"a.cn", "let mut n = 0\nlet c = 1\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_immutable);
        CHECK_EQ(out.line, 3U);
    }
    {
        const memory_file_t files[] = {{"main.cn", "import \"a.cn\"\nlet s: str = n\n"},
                                       {"a.cn", "let n = 0\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_type_mismatch);
    }
    {
        /* One host native declared by two files is imported once. */
        const memory_file_t files[] = {
            {"main.cn", "import \"a.cn\"\nnative fn echo(x: i32) -> i32\necho(1) + twice(2)\n"},
            {"a.cn", "native fn echo(x: i32) -> i32\nfn twice(n: i32) -> i32:\n"
                     "    return echo(n) * 2\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_none);
        CHECK_EQ(out.value, 5);
    }
}

CNOR_TEST(initializes_imported_files_first)
{
    memory_loader_t loader;
    outcome_t out;

    {
        /* An imported file's code may not use its own later globals either. */
        const memory_file_t files[] = {
            {"main.cn", "import \"a.cn\"\n1\n"},
            {"a.cn", "fn f() -> i32:\n    return late\nlet early = f()\nlet late = 1\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_not_initialized);
        CHECK_EQ(out.line, 3U);
        CHECK(strcmp(out.file, "a.cn") == 0);
    }
    {
        /* The importing file's code runs after: imported globals are ready. */
        const memory_file_t files[] = {
            {"main.cn", "import \"a.cn\"\nlet mine = get() + 1\nmine\n"},
            {"a.cn", "let value = 41\nfn get() -> i32:\n    return value\n"}};

        run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_none);
        CHECK_EQ(out.value, 42);
    }
}

CNOR_TEST(points_runtime_errors_at_the_imported_file)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"lib/div.cn\"\nlet a = 1\n\ndiv(a, 0)\n"},
        {"lib/div.cn", "# divides\nfn div(a: i32, b: i32) -> i32:\n    return a / b\n"},
    };
    memory_loader_t loader;
    outcome_t out;

    run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.line, 3U);
    CHECK(strcmp(out.file, "lib/div.cn") == 0);
    {
        static const memory_file_t top[] = {
            {"main.cn", "import \"lib/div.cn\"\nlet a = 1\n\na / 0\n"},
            {"lib/div.cn", "fn div(a: i32, b: i32) -> i32:\n    return a / b\n"},
        };

        run_program(top, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK_EQ(out.code, cnor_error_div_by_zero);
        CHECK_EQ(out.line, 4U);
        CHECK(strcmp(out.file, "main.cn") == 0);
    }
}

static const cnor_test_case_t m_cases[] = {
    CNOR_TEST_CASE(runs_well_typed_programs),
    CNOR_TEST_CASE(accepts_values_of_unknown_type_anywhere),
    CNOR_TEST_CASE(refuses_arguments_of_the_wrong_type),
    CNOR_TEST_CASE(refuses_arity_mismatches),
    CNOR_TEST_CASE(checks_returns_against_the_declared_type),
    CNOR_TEST_CASE(checks_assignments_against_the_variable_type),
    CNOR_TEST_CASE(refuses_operators_that_would_always_fail),
    CNOR_TEST_CASE(checks_match_patterns_and_ranges),
    CNOR_TEST_CASE(initializes_globals_in_source_order),
    CNOR_TEST_CASE(refuses_globals_used_before_their_let),
    CNOR_TEST_CASE(links_imported_files_into_one_image),
    CNOR_TEST_CASE(looks_beside_the_importer_then_in_the_search_paths),
    CNOR_TEST_CASE(reports_files_that_cannot_be_imported),
    CNOR_TEST_CASE(refuses_import_cycles),
    CNOR_TEST_CASE(stops_imports_nested_too_deep),
    CNOR_TEST_CASE(gives_each_file_its_own_names),
    CNOR_TEST_CASE(initializes_imported_files_first),
    CNOR_TEST_CASE(points_runtime_errors_at_the_imported_file),
};

int
main(int argc, char **argv)
{
    return cnor_test_main("language", m_cases, CNOR_TEST_COUNT(m_cases), argc, argv);
}
