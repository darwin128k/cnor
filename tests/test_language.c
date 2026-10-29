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
static void
echo(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result, cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    *result = args[0];
}

/* The notes taken by `note(n)`, in order, to observe when code runs. */
static long long m_notes[64];
static size_t m_note_count;

static void
note(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result, cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    if (m_note_count < sizeof(m_notes) / sizeof(m_notes[0]))
    {
        m_notes[m_note_count++] = (long long) lh_value_get_data(&args[0])->i;
    }
    *result = args[0];
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
    lh_str_view_init_by_size(&view, text, strlen(text));
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
    lh_u8_t kind;       /**< The kind of the result; 0 when there is none. */
    lh_bool_t truth;    /**< The result, when it is a boolean. */
    char text[256];     /**< The result, when it is a string. */
    cnor_count_t trace_count;
    lh_bool_t trace_truncated;
    struct { char name[64]; char file[256]; cnor_line_t line; } trace[16];
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

static void
take_trace(outcome_t *out, const cnor_vm_t *vm)
{
    cnor_count_t i;
    out->trace_count = cnor_vm_get_trace_count(vm);
    out->trace_truncated = cnor_vm_trace_is_truncated(vm);
    for (i = 0U; i < out->trace_count && i < 16U; ++i)
    {
        cnor_vm_trace_frame_t frame;
        if (!cnor_vm_get_trace_frame(vm, i, &frame)) break;
        copy_view(out->trace[i].name, sizeof(out->trace[i].name), frame.name);
        copy_view(out->trace[i].file, sizeof(out->trace[i].file), frame.file);
        out->trace[i].line = frame.line;
    }
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
                take_trace(out, &vm);
            }
            else
            {
                const lh_value_t result = cnor_vm_get_result(&vm);

                out->kind = (lh_u8_t) lh_value_get_kind(&result);
                if (lh_value_get_kind(&result) == lh_value_kind_bool)
                {
                    out->truth = lh_value_get_data(&result)->b;
                }
                else if (lh_value_get_kind(&result) == lh_value_kind_sint)
                {
                    out->value = (long long) lh_value_get_data(&result)->i;
                }
                else if (lh_value_get_kind(&result) == lh_value_kind_str)
                {
                    const lh_str_view_t text = lh_value_get_data(&result)->s;
                    const size_t size = text.size < sizeof(out->text) - 1U ? text.size
                                                                          : sizeof(out->text) - 1U;

                    memcpy(out->text, text.begin, size);
                    out->text[size] = '\0';
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

/* The error the program fails with, whether the compiler or the machine
 * refused it. */
static cnor_error_t
failure_of(const char *text)
{
    const outcome_t out = run_source(text);

    return out.code;
}

/* The text the program returns. */
static lh_bool_t
text_of(const char *text, char *to, size_t capacity)
{
    const outcome_t out = run_source(text);

    if (out.code != cnor_error_none || out.kind != lh_value_kind_str)
    {
        printf("    failed: %s at %s:%u: %s\n", cnor_error_get_message(out.code), out.file,
               (unsigned) out.line, out.name);
        return lh_bool_false;
    }
    (void) snprintf(to, capacity, "%s", out.text);
    return lh_bool_true;
}

/* Whether the program returns the boolean @p truth. */
static lh_bool_t
bool_of(const char *text, lh_bool_t truth)
{
    const outcome_t out = run_source(text);

    if (out.code != cnor_error_none || out.kind != lh_value_kind_bool)
    {
        printf("    failed: %s at %s:%u: %s\n", cnor_error_get_message(out.code), out.file,
               (unsigned) out.line, out.name);
        return lh_bool_false;
    }
    return out.truth == truth ? lh_bool_true : lh_bool_false;
}

/* -- the bitwise operators --------------------------------------------------- */

CNOR_TEST(has_the_bitwise_operators)
{
    cnor_line_t line = 0U;

    /* The bits of a whole number. */
    CHECK_EQ(value_of("fn f() -> i32:\n    return 0xF0 & 0x3C\nf()\n"), 0x30);
    CHECK_EQ(value_of("fn f() -> i32:\n    return 0xF0 | 0x0F\nf()\n"), 0xFF);
    CHECK_EQ(value_of("fn f() -> i32:\n    return 0xFF ^ 0x0F\nf()\n"), 0xF0);
    CHECK_EQ(value_of("fn f() -> i32:\n    return 1 << 10\nf()\n"), 1024);
    CHECK_EQ(value_of("fn f() -> i32:\n    return 1024 >> 3\nf()\n"), 128);
    CHECK_EQ(value_of("fn f() -> i32:\n    return ~0\nf()\n"), -1);
    /* The flags of a player, as a script would hold them. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut flags = 0\n"
                      "    flags = flags | 1\n"
                      "    flags = flags | 4\n"
                      "    if (flags & 1) != 0:\n"
                      "        return 1\n"
                      "    return 0\n"
                      "f()\n"),
             1);
    /* A shift of more than the width of the number has no answer. */
    CHECK_EQ(failure_of("fn f() -> i32:\n    return 1 << 64\nf()\n"),
             cnor_error_shift_out_of_range);
    CHECK_EQ(failure_of("fn f() -> i32:\n    return 1 >> -1\nf()\n"),
             cnor_error_shift_out_of_range);
    /* The bitwise operators work on whole numbers, not on text. */
    CHECK_EQ(error_of("fn f() -> i32:\n    return \"a\" & 1\nf()\n", &line),
             cnor_error_operand_type);
}

/* -- the error handling -------------------------------------------------------- */

CNOR_TEST(catches_an_error)
{
    char text[256];

    /* The error is caught, and the run goes on. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut caught = 0\n"
                      "    try:\n"
                      "        caught = 10 / 0\n"
                      "    catch e:\n"
                      "        caught = 1\n"
                      "    return caught\n"
                      "f()\n"),
             1);
    /* What the catch gets is a dictionary: the message, the line, the code and
     * the file. */
    REQUIRE(text_of("fn f() -> str:\n"
                    "    try:\n"
                    "        let x = 10 / 0\n"
                    "    catch e:\n"
                    "        return e[\"message\"]\n"
                    "    return \"no\"\n"
                    "f()\n",
                    text, sizeof(text)));
    CHECK_EQ(strcmp(text, "division by zero"), 0);
    /* The line the failure happened on. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    try:\n"
                      "        let x = 10 / 0\n"
                      "    catch e:\n"
                      "        return e[\"line\"]\n"
                      "    return 0\n"
                      "f()\n"),
             3);
    /* The code of the failure. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    try:\n"
                      "        let x = 10 / 0\n"
                      "    catch e:\n"
                      "        return e[\"code\"]\n"
                      "    return 0\n"
                      "f()\n"),
             (lh_u32_t) cnor_error_div_by_zero);
    /* And its kind: the name a script reads, which does not change with the
     * order of the codes. */
    REQUIRE(text_of("fn f() -> str:\n"
                    "    try:\n"
                    "        let x = 10 / 0\n"
                    "    catch e:\n"
                    "        return e[\"kind\"]\n"
                    "    return \"no\"\n"
                    "f()\n",
                    text, sizeof(text)));
    CHECK_EQ(strcmp(text, "div_by_zero"), 0);
    /* The file it happened in. */
    REQUIRE(text_of("fn f() -> str:\n"
                    "    try:\n"
                    "        let x = 10 / 0\n"
                    "    catch e:\n"
                    "        return e[\"file\"]\n"
                    "    return \"no\"\n"
                    "f()\n",
                    text, sizeof(text)));
    CHECK_EQ(strcmp(text, "main.cn"), 0);
    /* The catch may leave the name out: the error is then ignored. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut caught = 0\n"
                      "    try:\n"
                      "        let x = 10 / 0\n"
                      "    catch:\n"
                      "        caught = 1\n"
                      "    return caught\n"
                      "f()\n"),
             1);
    /* A `try` that does not fail runs its block and skips the catch. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut caught = 0\n"
                      "    try:\n"
                      "        caught = 10 / 2\n"
                      "    catch e:\n"
                      "        caught = 1\n"
                      "    return caught\n"
                      "f()\n"),
             5);
}

CNOR_TEST(catches_an_error_from_a_function)
{
    /* The failure flies through the call and is caught above it. */
    CHECK_EQ(value_of("fn boom() -> i32:\n"
                      "    return 10 / 0\n"
                      "fn f() -> i32:\n"
                      "    try:\n"
                      "        let x = boom()\n"
                      "    catch e:\n"
                      "        return e[\"line\"]\n"
                      "    return 0\n"
                      "f()\n"),
             2);
    /* A `try` inside a function catches what the function does. */
    CHECK_EQ(value_of("fn boom() -> i32:\n"
                      "    try:\n"
                      "        let x = 10 / 0\n"
                      "    catch e:\n"
                      "        return 1\n"
                      "    return 0\n"
                      "fn f() -> i32:\n"
                      "    return boom()\n"
                      "f()\n"),
             1);
}

CNOR_TEST(leaves_a_try_when_the_code_goes_away)
{
    /* A `break` out of a `try` closes its handler: what happens after the
     * loop is not caught by it. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut total = 0\n"
                      "    for i in 0..5:\n"
                      "        try:\n"
                      "            if i == 2:\n"
                      "                break\n"
                      "            total = total + i\n"
                      "        catch e:\n"
                      "            total = total + 100\n"
                      "    return total\n"
                      "f()\n"),
             1);
    /* A `return` out of a `try` closes its handler too. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    try:\n"
                      "        return 7\n"
                      "    catch e:\n"
                      "        return 9\n"
                      "    return 0\n"
                      "f()\n"),
             7);
    /* A `try` inside a `try`: the inner one takes the failure first. */
    CHECK_EQ(value_of("fn boom() -> i32:\n"
                      "    return 10 / 0\n"
                      "fn f() -> i32:\n"
                      "    let mut n = 0\n"
                      "    try:\n"
                      "        try:\n"
                      "            boom()\n"
                      "        catch inner:\n"
                      "            n = n + 1\n"
                      "            boom()\n"
                      "    catch outer:\n"
                      "        n = n + 10\n"
                      "    return n\n"
                      "f()\n"),
             11);
}

CNOR_TEST(refuses_a_try_where_it_does_not_fit)
{
    cnor_line_t line = 0U;

    /* A `try` needs a catch or a finally. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    try:\n"
                      "        let x = 1\n"
                      "    return 0\n"
                      "f()\n",
                      &line),
             cnor_error_unexpected_token);
    /* An error nobody catches still fails the run. */
    CHECK_EQ(failure_of("fn f() -> i32:\n    return 10 / 0\nf()\n"),
             cnor_error_div_by_zero);
    /* And the error is not caught by a `try` that has already ended. */
    CHECK_EQ(failure_of("fn f() -> i32:\n"
                        "    try:\n"
                        "        let x = 1\n"
                        "    catch e:\n"
                        "        let y = 2\n"
                        "    return 10 / 0\n"
                        "f()\n"),
             cnor_error_div_by_zero);
}

CNOR_TEST(assigns_with_an_operator)
{
    cnor_line_t line = 0U;

    /* `+=` and the rest: the value on the right is combined with the one on
     * the left, and the result is stored back. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut x = 10\n"
                      "    x += 5\n"
                      "    x -= 3\n"
                      "    x *= 2\n"
                      "    x /= 4\n"
                      "    x %= 5\n"
                      "    return x\n"
                      "f()\n"),
             1);
    /* The bitwise ones. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut x = 12\n"
                      "    x &= 10\n"
                      "    x |= 1\n"
                      "    x ^= 3\n"
                      "    return x\n"
                      "f()\n"),
             10);
    /* An element of an array. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut a = [0; 4]\n"
                      "    a[0] = 5\n"
                      "    a[0] += 7\n"
                      "    return a[0]\n"
                      "f()\n"),
             12);
    /* A pair of a dictionary. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut d = dict(4)\n"
                      "    d[\"hp\"] = 10\n"
                      "    d[\"hp\"] += 5\n"
                      "    return d[\"hp\"]\n"
                      "f()\n"),
             15);
    /* A global. */
    CHECK_EQ(value_of("let mut g = 10\n"
                      "fn f() -> i32:\n"
                      "    g += 5\n"
                      "    return g\n"
                      "f()\n"),
             15);
    /* A pair of a global dictionary. */
    CHECK_EQ(value_of("let mut d = dict(4)\n"
                      "d[\"hp\"] = 10\n"
                      "d[\"hp\"] += 5\n"
                      "d[\"hp\"]\n"),
             15);
    /* An element of a global array. */
    CHECK_EQ(value_of("let mut a = [0; 3]\n"
                      "a[0] = 5\n"
                      "a[0] += 7\n"
                      "a[0]\n"),
             12);
    /* An immutable variable is refused, as a plain assignment is. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    let x = 10\n"
                      "    x += 5\n"
                      "    return x\n"
                      "f()\n",
                      &line),
             cnor_error_immutable);
    /* The result must have the type of the target. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    let mut x = 10\n"
                      "    x += 1.5\n"
                      "    return x\n"
                      "f()\n",
                      &line),
             cnor_error_type_mismatch);
}

CNOR_TEST(picks_a_value_with_a_ternary)
{
    /* `value if condition else other`. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let x = 5\n"
                      "    let y = 10\n"
                      "    return x if x > y else y\n"
                      "f()\n"),
             10);
    /* The branch that is not picked does not run. */
    CHECK_EQ(value_of("let mut n = 0\n"
                      "fn bump() -> i32:\n"
                      "    n = n + 1\n"
                      "    return n\n"
                      "fn f() -> i32:\n"
                      "    let x = bump()\n"
                      "    let y = x if x > 0 else bump()\n"
                      "    return n * 1000 + y\n"
                      "f()\n"),
             1001);
    /* The branches may be of different kinds, as long as they agree on what
     * the value is. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let x = 5\n"
                      "    let s = \"big\" if x > 3 else \"small\"\n"
                      "    return len(s)\n"
                      "f()\n"),
             3);
    /* A ternary of its own in the other branch. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let x = 1\n"
                      "    return 10 if x > 0 else 20 if x < 0 else 30\n"
                      "f()\n"),
             10);
}

CNOR_TEST(puts_values_into_a_string)
{
    /* `$name` in a string is the value of the name. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let hp = 42\n"
                      "    let name = \"player\"\n"
                      "    let s = \"hp = $hp, name = $name\"\n"
                      "    if s == \"hp = 42, name = player\":\n"
                      "        return 1\n"
                      "    return 0\n"
                      "f()\n"),
             1);
    /* A string with no names in it stays as it is. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let s = \"nothing here\"\n"
                      "    if s == \"nothing here\":\n"
                      "        return 1\n"
                      "    return 0\n"
                      "f()\n"),
             1);
    /* A `$` that is not a name is just a sign. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let s = \"it costs $5\"\n"
                      "    if s == \"it costs $5\":\n"
                      "        return 1\n"
                      "    return 0\n"
                      "f()\n"),
             1);
    /* A name that is not there is refused. */
    CHECK_EQ(failure_of("fn f() -> i32:\n"
                        "    let s = \"hp = $hp\"\n"
                        "    return 0\n"
                        "f()\n"),
             cnor_error_unknown_name);
}

CNOR_TEST(splits_a_text_into_an_array)
{
    /* The pieces go into the array the program made, and the count comes
     * back. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut parts = [\"\"; 3]\n"
                      "    let n = split_into(\"a,b,c\", \",\", parts)\n"
                      "    if parts[0] == \"a\" and parts[1] == \"b\" and parts[2] == \"c\":\n"
                      "        return n\n"
                      "    return 0\n"
                      "f()\n"),
             3);
    /* A separator that is not in the text: one piece, the text itself. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut parts = [\"\"; 2]\n"
                      "    let n = split_into(\"abc\", \",\", parts)\n"
                      "    if parts[0] == \"abc\":\n"
                      "        return n\n"
                      "    return 0\n"
                      "f()\n"),
             1);
    /* A separator that is empty has no answer. */
    CHECK_EQ(failure_of("fn f() -> i32:\n"
                        "    let mut parts = [\"\"; 2]\n"
                        "    let n = split_into(\"abc\", \"\", parts)\n"
                        "    return n\n"
                        "f()\n"),
             cnor_error_empty_separator);
    /* An array with too few places for the pieces fails the run. */
    CHECK_EQ(failure_of("fn f() -> i32:\n"
                        "    let mut parts = [\"\"; 2]\n"
                        "    let n = split_into(\"a,b,c\", \",\", parts)\n"
                        "    return n\n"
                        "f()\n"),
             cnor_error_array_too_small);
}

CNOR_TEST(keeps_a_function_as_a_value)
{
    /* A function is a value: it can be put in a variable and called from
     * there. */
    CHECK_EQ(value_of("fn double(n: i32) -> i32:\n"
                      "    return n * 2\n"
                      "fn main() -> i32:\n"
                      "    let g = double\n"
                      "    return g(21)\n"
                      "main()\n"),
             42);
    /* And it can be given to a function. */
    CHECK_EQ(value_of("fn double(n: i32) -> i32:\n"
                      "    return n * 2\n"
                      "fn apply(f: function, n: i32) -> i32:\n"
                      "    return f(n)\n"
                      "fn main() -> i32:\n"
                      "    return apply(double, 21)\n"
                      "main()\n"),
             42);
    /* The machine checks the count of the arguments of a function called
     * through a value. */
    CHECK_EQ(failure_of("fn double(n: i32) -> i32:\n"
                        "    return n * 2\n"
                        "fn main() -> i32:\n"
                        "    let g = double\n"
                        "    return g(1, 2)\n"
                        "main()\n"),
             cnor_error_wrong_arg_count);
}

/* -- functions declared inside another one ---------------------------------- */

CNOR_TEST(calls_a_function_declared_inside_another_one)
{
    /* It sees the variables of the function it is in, and their values are
     * the ones they have when it is called. */
    CHECK_EQ(value_of("fn outer(a: i32) -> i32:\n"
                      "    let mut k = 10\n"
                      "    fn inner(b: i32) -> i32:\n"
                      "        return b * 2 + k\n"
                      "    k = 20\n"
                      "    return inner(a) + 1\n"
                      "outer(21)\n"),
             63);
    /* It may change them. */
    CHECK_EQ(value_of("fn outer() -> i32:\n"
                      "    let mut n = 0\n"
                      "    fn inner() -> i32:\n"
                      "        n += 5\n"
                      "        return n\n"
                      "    inner()\n"
                      "    inner()\n"
                      "    return n\n"
                      "outer()\n"),
             10);
    /* A call in the middle of an expression: what the caller had on the
     * stack is put away and brought back. */
    CHECK_EQ(value_of("fn f(x: i32) -> i32:\n"
                      "    fn helper() -> i32:\n"
                      "        return 7\n"
                      "    return 1 + helper() * x\n"
                      "f(3)\n"),
             22);
    /* One inside another. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    fn middle() -> i32:\n"
                      "        fn deep() -> i32:\n"
                      "            return 5\n"
                      "        return deep() + 1\n"
                      "    return middle() * 2\n"
                      "f()\n"),
             12);
    /* One declared in a block, and one that gives nothing back. */
    CHECK_EQ(value_of("fn f(x: i32) -> i32:\n"
                      "    let mut r = 0\n"
                      "    if x > 0:\n"
                      "        fn helper(y: i32) -> i32:\n"
                      "            return y + 1\n"
                      "        r = helper(x)\n"
                      "    return r\n"
                      "f(4)\n"),
             5);
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    fn inner():\n"
                      "        let x = 1\n"
                      "    inner()\n"
                      "    return 3\n"
                      "f()\n"),
             3);
    /* A `try` inside it, with a `catch` and a `finally`. */
    CHECK_EQ(value_of("fn f(x: i32) -> i32:\n"
                      "    fn risky(y: i32) -> i32:\n"
                      "        try:\n"
                      "            if y > 0:\n"
                      "                return y * 2\n"
                      "            raise\n"
                      "        catch e:\n"
                      "            return -1\n"
                      "        finally:\n"
                      "            let z = 0\n"
                      "        return 0\n"
                      "    return risky(x)\n"
                      "f(5)\n"),
             10);
    /* An array parameter: it is a reference to the elements of the caller. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    fn total(a: array[i32]) -> i32:\n"
                      "        let mut n = 0\n"
                      "        for v in a:\n"
                      "            n += v\n"
                      "        return n\n"
                      "    let xs: array[i32] = [1, 2, 3]\n"
                      "    return total(xs)\n"
                      "f()\n"),
             6);
    /* The variables of the function it is in keep their slots: the ones
     * declared after it are not taken by its own code. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let a: array[i32] = [1, 2, 3]\n"
                      "    fn total() -> i32:\n"
                      "        let mut n = 0\n"
                      "        for v in a:\n"
                      "            n += v\n"
                      "        return n\n"
                      "    let b: array[i32] = [4, 5, 6]\n"
                      "    let mut c = 0\n"
                      "    return total() + b[0] + c\n"
                      "f()\n"),
             10);
    /* One that calls another one. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    fn b() -> i32:\n"
                      "        return 7\n"
                      "    fn a() -> i32:\n"
                      "        return b()\n"
                      "    return a()\n"
                      "f()\n"),
             7);
}

CNOR_TEST(refuses_a_local_function_that_cannot_work)
{
    cnor_line_t line;

    /* It cannot call itself: its parameters live in one set of slots. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    fn inner() -> i32:\n"
                      "        return inner()\n"
                      "    return inner()\n"
                      "f()\n",
                      &line),
             cnor_error_recursive);
    /* It cannot be a value: it lives in the frame of the function it is in. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    fn inner() -> i32:\n"
                      "        return 1\n"
                      "    let g = inner\n"
                      "    return g()\n"
                      "f()\n",
                      &line),
             cnor_error_not_a_value);
    /* Its arguments are checked like any other. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    fn inner(a: i32) -> i32:\n"
                      "        return a\n"
                      "    return inner()\n"
                      "f()\n",
                      &line),
             cnor_error_wrong_arg_count);
    /* A `break` in it has no loop of its own to leave. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    fn inner() -> i32:\n"
                      "        break\n"
                      "        return 1\n"
                      "    return inner()\n"
                      "f()\n",
                      &line),
             cnor_error_outside_loop);
}

/* -- finally and raise ------------------------------------------------------- */

CNOR_TEST(runs_a_finally_block)
{
    /* The finally block runs when the try block went fine. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut total = 0\n"
                      "    try:\n"
                      "        total = 1\n"
                      "    finally:\n"
                      "        total = total + 10\n"
                      "    return total\n"
                      "f()\n"),
             11);
    /* And when it did not: the catch block runs first, the finally block after
     * it. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut total = 0\n"
                      "    try:\n"
                      "        let x = 10 / 0\n"
                      "    catch e:\n"
                      "        total = 1\n"
                      "    finally:\n"
                      "        total = total + 10\n"
                      "    return total\n"
                      "f()\n"),
             11);
    /* A finally block without a catch block runs the same way, and the error
     * goes on outward when it is done. */
    {
        const outcome_t out = run_source("fn f() -> i32:\n"
                                         "    note(1)\n"
                                         "    try:\n"
                                         "        let x = 10 / 0\n"
                                         "    finally:\n"
                                         "        note(2)\n"
                                         "    return 0\n"
                                         "f()\n");

        CHECK_EQ(out.code, cnor_error_div_by_zero);
        CHECK_EQ(m_note_count, 2U);
        CHECK_EQ(m_notes[0], 1);
        CHECK_EQ(m_notes[1], 2);
    }
}

CNOR_TEST(raises_the_error_again)
{
    /* The error goes on outward, and the outer catch gets it as it was: the
     * line it happened on. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    try:\n"
                      "        try:\n"
                      "            let x = 10 / 0\n"
                      "        catch e:\n"
                      "            raise\n"
                      "    catch e2:\n"
                      "        return e2[\"line\"]\n"
                      "    return 0\n"
                      "f()\n"),
             4);
    /* The finally block runs before the error goes on. */
    {
        const outcome_t out = run_source("fn f() -> i32:\n"
                                         "    try:\n"
                                         "        try:\n"
                                         "            let x = 10 / 0\n"
                                         "        catch e:\n"
                                         "            raise\n"
                                         "        finally:\n"
                                         "            note(1)\n"
                                         "    catch e2:\n"
                                         "        note(2)\n"
                                         "        return e2[\"line\"]\n"
                                         "    return 0\n"
                                         "f()\n");

        CHECK_EQ(out.value, 4);
        CHECK_EQ(m_note_count, 2U);
        CHECK_EQ(m_notes[0], 1);
        CHECK_EQ(m_notes[1], 2);
    }
    /* A raise with no error to raise is a failure of its own. */
    CHECK_EQ(failure_of("fn f() -> i32:\n"
                        "    raise\n"
                        "    return 0\n"
                        "f()\n"),
             cnor_error_nothing_to_raise);
}

CNOR_TEST(leaves_a_try_with_a_finally)
{
    /* A break out of a try runs its finally block first. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut total = 0\n"
                      "    for i in 0..5:\n"
                      "        try:\n"
                      "            if i == 2:\n"
                      "                break\n"
                      "            total = total + i\n"
                      "        finally:\n"
                      "            total = total + 100\n"
                      "    return total\n"
                      "f()\n"),
             301);
    /* A continue out of a try runs it too. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut total = 0\n"
                      "    for i in 0..5:\n"
                      "        try:\n"
                      "            if i == 2:\n"
                      "                continue\n"
                      "            total = total + i\n"
                      "        finally:\n"
                      "            total = total + 100\n"
                      "    return total\n"
                      "f()\n"),
             508);
    /* A return out of a try runs it as well, and the function returns the
     * value the return had. */
    {
        const outcome_t out = run_source("fn f() -> i32:\n"
                                         "    try:\n"
                                         "        note(1)\n"
                                         "        return 7\n"
                                         "    finally:\n"
                                         "        note(2)\n"
                                         "    return 0\n"
                                         "f()\n");

        CHECK_EQ(out.value, 7);
        CHECK_EQ(m_note_count, 2U);
        CHECK_EQ(m_notes[0], 1);
        CHECK_EQ(m_notes[1], 2);
    }
    /* Two finally blocks: they run from the inside out. */
    CHECK_EQ(value_of("fn f() -> i32:\n"
                      "    let mut total = 0\n"
                      "    for i in 0..5:\n"
                      "        try:\n"
                      "            try:\n"
                      "                if i == 2:\n"
                      "                    break\n"
                      "                total = total + i\n"
                      "            finally:\n"
                      "                total = total + 10\n"
                      "        finally:\n"
                      "            total = total + 100\n"
                      "    return total\n"
                      "f()\n"),
             331);
}

/* -- arrays ------------------------------------------------------------------ */

CNOR_TEST(passes_an_array_to_a_function_and_walks_it)
{
    /* The elements are shared: what the function writes, the caller sees. */
    CHECK_EQ(value_of("fn bump(a: array, i: i32) -> i32:\n"
                      "    a[i] = a[i] + 10\n"
                      "    return a[i]\n"
                      "fn main() -> i32:\n"
                      "    let mut numbers = [1; 4]\n"
                      "    bump(numbers, 2)\n"
                      "    return numbers[2]\n"
                      "main()\n"),
             11);
    /* The length of an array. */
    CHECK_EQ(value_of("fn size(a: array) -> i32:\n"
                      "    return len(a)\n"
                      "fn main() -> i32:\n"
                      "    let mut numbers = [0; 7]\n"
                      "    return size(numbers)\n"
                      "main()\n"),
             7);
    /* The elements, one by one. */
    CHECK_EQ(value_of("fn total(a: array) -> i32:\n"
                      "    let mut sum = 0\n"
                      "    for x in a:\n"
                      "        sum = sum + x\n"
                      "    return sum\n"
                      "fn main() -> i32:\n"
                      "    let mut numbers = [0; 4]\n"
                      "    numbers[0] = 1\n"
                      "    numbers[1] = 2\n"
                      "    numbers[2] = 3\n"
                      "    numbers[3] = 4\n"
                      "    return total(numbers)\n"
                      "main()\n"),
             10);
    /* An empty array. */
    CHECK_EQ(value_of("fn size(a: array) -> i32:\n"
                      "    return len(a)\n"
                      "fn main() -> i32:\n"
                      "    let mut numbers = [0; 0]\n"
                      "    return size(numbers)\n"
                      "main()\n"),
             0);
}

CNOR_TEST(keeps_a_global_array_whole)
{
    /* The elements of a global array live in the globals: a function writes
     * them, and the top-level code sees it. */
    CHECK_EQ(value_of("let mut scores = [0; 4]\n"
                      "fn bump(i: i32) -> i32:\n"
                      "    scores[i] = scores[i] + 10\n"
                      "    return scores[i]\n"
                      "bump(0)\n"
                      "bump(1)\n"
                      "scores[0] + scores[1]\n"),
             20);
}

CNOR_TEST(refuses_an_array_where_it_does_not_fit)
{
    cnor_line_t line = 0U;

    /* The argument of a function that takes an array must be one. */
    CHECK_EQ(error_of("fn f(a: array) -> i32:\n    return len(a)\nf(3)\n", &line),
             cnor_error_type_mismatch);
    /* An array cannot be returned: it would point at a frame that is gone. */
    CHECK_EQ(error_of("fn f() -> array:\n"
                      "    let mut a = [0; 4]\n"
                      "    return a\n"
                      "f()\n",
                      &line),
             cnor_error_array_returned);
    /* And it is not a number. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    let mut a = [0; 4]\n"
                      "    return a\n"
                      "f()\n",
                      &line),
             cnor_error_type_mismatch);
}

/* -- dictionaries ------------------------------------------------------------ */

CNOR_TEST(has_dictionaries)
{
    char text[256];

    /* A dictionary holds a value under a key. */
    CHECK_EQ(value_of("fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    settings[\"hp\"] = 100\n"
                      "    return settings[\"hp\"]\n"
                      "main()\n"),
             100);
    /* A key that is not there is `none`, and the run goes on. */
    CHECK(bool_of("fn main() -> bool:\n"
                  "    let mut settings = dict(4)\n"
                  "    return settings[\"hp\"] == none\n"
                  "main()\n",
                  lh_bool_true));
    /* Writing under a key again replaces the value. */
    CHECK_EQ(value_of("fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    settings[\"hp\"] = 100\n"
                      "    settings[\"hp\"] = 50\n"
                      "    return settings[\"hp\"]\n"
                      "main()\n"),
             50);
    /* How many pairs it holds. */
    CHECK_EQ(value_of("fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    settings[\"a\"] = 1\n"
                      "    settings[\"b\"] = 2\n"
                      "    return len(settings)\n"
                      "main()\n"),
             2);
    /* The values may be of any kind. */
    REQUIRE(text_of("fn main() -> str:\n"
                    "    let mut settings = dict(4)\n"
                    "    settings[\"name\"] = \"Egor\"\n"
                    "    return settings[\"name\"]\n"
                    "main()\n",
                    text, sizeof(text)));
    CHECK_EQ(strcmp(text, "Egor"), 0);
    /* A dictionary starts empty. */
    CHECK_EQ(value_of("fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    return len(settings)\n"
                      "main()\n"),
             0);
}

CNOR_TEST(walks_the_keys_of_a_dictionary)
{
    char text[256];

    /* The keys, one by one. */
    REQUIRE(text_of("fn main() -> str:\n"
                    "    let mut settings = dict(4)\n"
                    "    settings[\"hp\"] = 100\n"
                    "    settings[\"armor\"] = 25\n"
                    "    let mut keys = \"\"\n"
                    "    for k in settings:\n"
                    "        keys = keys + k + \";\"\n"
                    "    return keys\n"
                    "main()\n",
                    text, sizeof(text)));
    CHECK_EQ(strcmp(text, "hp;armor;"), 0);
    /* An empty dictionary has no keys. */
    CHECK_EQ(value_of("fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    let mut count = 0\n"
                      "    for k in settings:\n"
                      "        count = count + 1\n"
                      "    return count\n"
                      "main()\n"),
             0);
    /* A collection whose kind is not known is walked as it is: the elements
     * of an array, the keys of a dictionary. */
    CHECK_EQ(value_of("fn main() -> i32:\n"
                      "    let mut numbers = [1; 3]\n"
                      "    let mut x = none\n"
                      "    x = numbers\n"
                      "    let mut count = 0\n"
                      "    for k in x:\n"
                      "        count = count + 1\n"
                      "    return count\n"
                      "main()\n"),
             3);
    CHECK_EQ(value_of("fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    settings[\"a\"] = 1\n"
                      "    let mut x = none\n"
                      "    x = settings\n"
                      "    let mut count = 0\n"
                      "    for k in x:\n"
                      "        count = count + 1\n"
                      "    return count\n"
                      "main()\n"),
             1);
}

CNOR_TEST(shares_a_dictionary_with_a_function)
{
    /* The pairs are shared: what the function writes, the caller sees. */
    CHECK_EQ(value_of("fn put(d: dict, k: str, v: i32) -> i32:\n"
                      "    d[k] = v\n"
                      "    return d[k]\n"
                      "fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    put(settings, \"hp\", 100)\n"
                      "    return settings[\"hp\"]\n"
                      "main()\n"),
             100);
    /* The count is shared too. */
    CHECK_EQ(value_of("fn put(d: dict, k: str, v: i32) -> i32:\n"
                      "    d[k] = v\n"
                      "    return len(d)\n"
                      "fn main() -> i32:\n"
                      "    let mut settings = dict(4)\n"
                      "    put(settings, \"hp\", 100)\n"
                      "    return len(settings)\n"
                      "main()\n"),
             1);
}

CNOR_TEST(keeps_a_global_dictionary_whole)
{
    /* The pairs of a global dictionary live in the globals: a function writes
     * them, and the top-level code sees it. */
    CHECK_EQ(value_of("let mut config = dict(4)\n"
                      "fn set(k: str, v: i32) -> i32:\n"
                      "    config[k] = v\n"
                      "    return len(config)\n"
                      "set(\"hp\", 100)\n"
                      "set(\"armor\", 25)\n"
                      "config[\"hp\"] + config[\"armor\"]\n"),
             125);
    /* A global dictionary that follows another one: its index is past the
     * frame of the top-level code, and it lives in the globals all the same. */
    CHECK_EQ(value_of("let mut first = dict(4)\n"
                      "first[\"a\"] = 1\n"
                      "let mut second = dict(4)\n"
                      "second[\"b\"] = 2\n"
                      "first[\"a\"] + second[\"b\"]\n"),
             3);
}

CNOR_TEST(refuses_a_dictionary_where_it_does_not_fit)
{
    cnor_line_t line = 0U;

    /* The room must be a whole number the compiler knows. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    let n = 4\n"
                      "    let mut d = dict(n)\n"
                      "    return 0\n"
                      "f()\n",
                      &line),
             cnor_error_dict_room);
    /* `dict(n)` is a declaration: it may not stand anywhere else. */
    CHECK_EQ(error_of("fn f() -> i32:\n    return dict(4)\nf()\n", &line),
             cnor_error_not_a_declaration);
    /* A dictionary cannot be returned: it would point at a frame that is
     * gone. */
    CHECK_EQ(error_of("fn f() -> dict:\n"
                      "    let mut d = dict(4)\n"
                      "    return d\n"
                      "f()\n",
                      &line),
             cnor_error_dict_returned);
    /* The key is a text. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    let mut d = dict(4)\n"
                      "    d[1] = 2\n"
                      "    return 0\n"
                      "f()\n",
                      &line),
             cnor_error_type_mismatch);
    /* Writing into a dictionary needs `mut`. */
    CHECK_EQ(error_of("fn f() -> i32:\n"
                      "    let d = dict(4)\n"
                      "    d[\"a\"] = 1\n"
                      "    return 0\n"
                      "f()\n",
                      &line),
             cnor_error_immutable);
    /* The argument of a function that takes a dictionary must be one. */
    CHECK_EQ(error_of("fn f(d: dict) -> i32:\n    return len(d)\nf(3)\n", &line),
             cnor_error_type_mismatch);
    /* A dictionary with no room for another key says so. */
    CHECK_EQ(failure_of("fn f() -> i32:\n"
                        "    let mut d = dict(1)\n"
                        "    d[\"a\"] = 1\n"
                        "    d[\"b\"] = 2\n"
                        "    return 0\n"
                        "f()\n"),
             cnor_error_dict_full);
}

/* -- the string functions of the language ------------------------------------ */
CNOR_TEST(has_the_string_functions)
{
    char text[256];

    /* find */
    CHECK_EQ(value_of("fn f() -> i32:\n    return find(\"hello world\", \"wor\")\nf()\n"), 6);
    CHECK_EQ(value_of("fn f() -> i32:\n    return find(\"hello\", \"zz\")\nf()\n"), -1);
    CHECK_EQ(value_of("fn f() -> i32:\n    return find(\"hello\", \"\")\nf()\n"), 0);
    /* contains */
    CHECK(bool_of("fn f() -> bool:\n    return contains(\"hello\", \"ell\")\nf()\n",
                  lh_bool_true));
    CHECK(bool_of("fn f() -> bool:\n    return contains(\"hello\", \"xyz\")\nf()\n",
                  lh_bool_false));
    /* replace */
    REQUIRE(text_of("fn f() -> str:\n    return replace(\"a-b-c\", \"-\", \"+\")\nf()\n", text,
                    sizeof(text)));
    CHECK(strcmp(text, "a+b+c") == 0);
    REQUIRE(text_of("fn f() -> str:\n    return replace(\"aaa\", \"a\", \"bb\")\nf()\n", text,
                    sizeof(text)));
    CHECK(strcmp(text, "bbbbbb") == 0);
    REQUIRE(text_of("fn f() -> str:\n    return replace(\"abc\", \"x\", \"y\")\nf()\n", text,
                    sizeof(text)));
    CHECK(strcmp(text, "abc") == 0);
    /* trim */
    REQUIRE(text_of("fn f() -> str:\n    return trim(\"  padded  \")\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "padded") == 0);
    /* upper and lower */
    REQUIRE(text_of("fn f() -> str:\n    return upper(\"MiXeD\")\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "MIXED") == 0);
    REQUIRE(text_of("fn f() -> str:\n    return lower(\"MiXeD\")\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "mixed") == 0);
    /* starts_with and ends_with */
    CHECK(bool_of("fn f() -> bool:\n    return starts_with(\"hello\", \"he\")\nf()\n",
                  lh_bool_true));
    CHECK(bool_of("fn f() -> bool:\n    return starts_with(\"hello\", \"lo\")\nf()\n",
                  lh_bool_false));
    CHECK(bool_of("fn f() -> bool:\n    return ends_with(\"hello\", \"lo\")\nf()\n", lh_bool_true));
    /* substr */
    REQUIRE(text_of("fn f() -> str:\n    return substr(\"hello world\", 6, 5)\nf()\n", text,
                    sizeof(text)));
    CHECK(strcmp(text, "world") == 0);
    REQUIRE(text_of("fn f() -> str:\n    return substr(\"hello\", 1, 99)\nf()\n", text,
                    sizeof(text)));
    CHECK(strcmp(text, "ello") == 0);
    /* ord and char */
    CHECK_EQ(value_of("fn f() -> i32:\n    return ord(\"A\")\nf()\n"), 65);
    CHECK_EQ(value_of("fn f() -> i32:\n    return ord(\"\")\nf()\n"), -1);
    REQUIRE(text_of("fn f() -> str:\n    return char(66)\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "B") == 0);
    /* str */
    REQUIRE(text_of("fn f() -> str:\n    return str(1234)\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "1234") == 0);
    REQUIRE(text_of("fn f() -> str:\n    return str(-12)\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "-12") == 0);
    REQUIRE(text_of("fn f() -> str:\n    return str(true)\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "true") == 0);
#if LH_REAL_ENABLED
    REQUIRE(text_of("fn f() -> str:\n    return str(1.5)\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "1.5") == 0);
    REQUIRE(text_of("fn f() -> str:\n    return str(2.0)\nf()\n", text, sizeof(text)));
    CHECK(strcmp(text, "2.0") == 0);
#endif
    /* parse_int */
    CHECK_EQ(value_of("fn f() -> i32:\n    return parse_int(\"-567\")\nf()\n"), -567);
    CHECK_EQ(value_of("fn f() -> i32:\n    return parse_int(\"nope\")\nf()\n"), 0);
}

CNOR_TEST(checks_the_arguments_of_the_string_functions)
{
    cnor_line_t line = 0U;

    /* The wrong type of argument is a compile error. */
    CHECK_EQ(error_of("fn f() -> i32:\n    return find(1, \"a\")\nf()\n", &line),
             cnor_error_type_mismatch);
    CHECK_EQ(error_of("fn f() -> i32:\n    return find(\"a\")\nf()\n", &line),
             cnor_error_wrong_arg_count);
    CHECK_EQ(error_of("fn f() -> i32:\n    return find(\"a\", \"b\", \"c\")\nf()\n", &line),
             cnor_error_wrong_arg_count);
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


CNOR_TEST(has_nominal_records_and_enums)
{
    CHECK_EQ(value_of("enum State:\n    idle\n    active\n"
        "struct Player:\n    hp: i32\n    state: State\n"
        "let mut p = Player(80, State.idle)\n"
        "p.hp += 20\np.state = State.active\n"
        "match p.state:\n    State.idle => p.hp = 0\n    State.active => p.hp += 7\n"
        "p.hp\n"), 107);
    CHECK_EQ(value_of("struct Point:\n    x: i32\n    y: i32\n"
        "fn sum(p: Point) -> i32:\n    p.x += 2\n    return p.x + p.y\n"
        "fn test() -> i32:\n    let mut p: Point = Point(3, 4)\n"
        "    let n = sum(p)\n    return n + p.x\ntest()\n"), 14);
    CHECK_EQ(value_of("enum Mode:\n    off\n    on\n"
        "fn mode() -> Mode:\n    return Mode.on\n"
        "let mut result = 0\nmatch mode():\n    Mode.on => result = 42\n"
        "    _ => result = 1\nresult\n"), 42);
    m_note_count = 0U;
    CHECK_EQ(value_of("struct Pair:\n    a: i32\n    b: i32\n"
        "let p = Pair(note(11), note(22))\np.a * 100 + p.b\n"), 1122);
    CHECK_EQ(m_note_count, 2U);
    CHECK_EQ(m_notes[0], 11);
    CHECK_EQ(m_notes[1], 22);
}

CNOR_TEST(checks_record_fields_and_enum_identity)
{
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(\"bad\")\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P()\n"), cnor_error_wrong_arg_count);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(1)\np.missing\n"), cnor_error_unknown_name);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet mut p = P(1)\np.x = true\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(1)\np.x = 2\n"), cnor_error_immutable);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet mut p = P(1)\np.x += \"x\"\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("enum A:\n    x\nenum B:\n    x\nlet a: A = B.x\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("enum A:\n    x\nenum B:\n    x\nA.x == B.x\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("enum A:\n    x\nenum B:\n    x\nmatch A.x:\n    B.x => 1\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("enum A:\n    x\nA.missing\n"), cnor_error_unknown_name);
    CHECK_EQ(failure_of("enum A:\n    x\nlet a: A = 0\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("enum A:\n    x\nA.x + 1\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("struct P:\n    x: i32\n    x: i32\n"), cnor_error_duplicate_name);
    CHECK_EQ(failure_of("enum A:\n    x\n    x\n"), cnor_error_duplicate_name);
    CHECK_EQ(failure_of("struct P:\n    x: Missing\n"), cnor_error_unknown_type);
    CHECK_EQ(failure_of("fn f(x: Missing):\n    return\n"), cnor_error_unknown_type);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nstruct Q:\n    p: P\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nfn f():\n    let p = P(1)\n    return p\nf()\n"), cnor_error_struct_lifetime);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet P = 1\n"), cnor_error_duplicate_name);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nP(1)\n"), cnor_error_not_a_declaration);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nfn f(p: P):\n    return\nf(1)\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nstruct Q:\n    x: i32\n"
        "fn f(p: P):\n    return\nlet q = Q(1)\nf(q)\n"), cnor_error_type_mismatch);
}


CNOR_TEST(keeps_record_references_within_their_lifetime)
{
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(1)\nlet alias = p\n"), cnor_error_struct_lifetime);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet mut p = P(1)\nlet q = P(2)\np = q\n"), cnor_error_struct_lifetime);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(1)\nlet mut a = [0; 1]\na[0] = p\n"), cnor_error_struct_lifetime);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(1)\nlet mut d = dict(1)\nd[\"p\"] = p\n"), cnor_error_struct_lifetime);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(1)\nlet a = [p; 1]\n"), cnor_error_struct_lifetime);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(p.x)\n"), cnor_error_not_initialized);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet p = P(1)\np[0]\n"), cnor_error_type_mismatch);
    {
        const outcome_t out = run_source("enum State:\n    idle\nenum Mode:\n    idle\nlet s: State = Mode.idle\n");
        CHECK(!out.compiled);
        CHECK(strcmp(out.name, "expected State, found Mode") == 0);
    }
}

CNOR_TEST(imports_nominal_types_without_a_file_system)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"types.cn\"\nlet mut p = Player(10, State.idle)\n"
            "p.state = State.active\nadvance(p)\np.hp\n"},
        {"types.cn", "enum State:\n    idle\n    active\n"
            "struct Player:\n    hp: i32\n    state: State\n"
            "fn advance(p: Player):\n    p.hp += 5\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 15);
}


CNOR_TEST(declares_array_lists_and_typed_collections)
{
    CHECK_EQ(value_of("let a = [10, 20, 30]\na[0] + a[1] + a[2]\n"), 60);
    CHECK_EQ(value_of("let a = []\nlen(a)\n"), 0);
    CHECK_EQ(value_of("let a = [7,]\na[0]\n"), 7);
    CHECK_EQ(value_of("let a = [\n    1,\n    2,\n]\nlen(a) + a[1]\n"), 4);
    CHECK_EQ(value_of("let mut a: array[i32] = [10, 20, 30]\na[1] += 2\na[1]\n"), 22);
    CHECK_EQ(value_of("fn f() -> i32:\n    let mut a: array[i32] = [10, 20]\n"
        "    a[0] *= 3\n    a[1] = 7\n    return a[0] + a[1]\nf()\n"), 37);
    CHECK_EQ(value_of("let a: array[i32] = [7; 3]\na[0] + a[2] + len(a)\n"), 17);
    CHECK_EQ(value_of("let a: array[i32] = []\nlen(a)\n"), 0);
    CHECK_EQ(value_of("let mut d: dict[i32] = dict(3)\nd[\"x\"] = 10\n"
        "d[\"x\"] += 7\nd[\"x\"]\n"), 17);
    CHECK_EQ(value_of("fn f() -> i32:\n    let mut d: dict[i32] = dict(2)\n"
        "    d[\"x\"] = 12\n    d[\"x\"] -= 2\n    return d[\"x\"]\nf()\n"), 10);
    CHECK_EQ(value_of("let a: array[i32] = [4]\nlet b: array[i32] = [8]\n(a if true else b)[0]\n"), 4);
    CHECK_EQ(value_of("let mut a: dict[i32] = dict(1)\na[\"x\"] = 9\n"
        "(a if true else a)[\"x\"]\n"), 9);
    CHECK(bool_of("let d: dict[i32] = dict(1)\nd[\"missing\"] == none\n", lh_bool_true));
    m_note_count = 0U;
    CHECK_EQ(value_of("fn f() -> i32:\n    let a = [note(3), note(4), note(5)]\n"
        "    return a[0] * 100 + a[1] * 10 + a[2]\nf()\n"), 345);
    CHECK_EQ(m_note_count, 3U);
    CHECK_EQ(m_notes[0], 3);
    CHECK_EQ(m_notes[1], 4);
    CHECK_EQ(m_notes[2], 5);
}

CNOR_TEST(carries_element_types_through_calls_and_loops)
{
    CHECK_EQ(value_of("fn sum(a: array[i32]) -> i32:\n    let mut n = 0\n"
        "    for v in a:\n        n += v\n    a[0] = 4\n    return n\n"
        "let mut a: array[i32] = [1, 2, 3]\nlet n = sum(a)\nn + a[0]\n"), 10);
    CHECK_EQ(value_of("fn add(d: dict[i32]):\n    d[\"x\"] += 2\n"
        "let mut d: dict[i32] = dict(1)\nd[\"x\"] = 3\nadd(d)\nd[\"x\"]\n"), 5);
    CHECK_EQ(value_of("let a: array[i32] = [4, 5]\nlet alias = a\nalias[0] + alias[1]\n"), 9);
    CHECK_EQ(value_of("enum State:\n    off\n    on\n"
        "let mut a: array[State] = [State.off, State.on]\n"
        "let mut n = 0\nfor state in a:\n    match state:\n"
        "        State.off => n += 1\n        State.on => n += 10\nn\n"), 11);
    CHECK_EQ(value_of("let mut d: dict[str] = dict(2)\nd[\"a\"] = \"one\"\n"
        "let mut n = 0\nfor key in d:\n    n += len(key)\nn + len(d[\"a\"])\n"), 4);
    CHECK_EQ(value_of("let mut parts: array[str] = [\"\"; 3]\n"
        "split_into(\"a,b\", \",\", parts) + len(parts[1])\n"), 3);
    CHECK_EQ(value_of("fn double(n: i32) -> i32:\n    return n * 2\n"
        "let a: array[function] = [double]\nlet f = a[0]\nf(6)\n"), 12);
}

CNOR_TEST(rejects_incompatible_collection_values)
{
    CHECK_EQ(failure_of("let a: array[i32] = [1, \"x\"]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[i32] = [\"x\"; 3]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let mut a: array[i32] = [1]\na[0] = \"x\"\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("fn f():\n    let mut a: array[i32] = [1]\n    a[0] = false\nf()\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let mut d: dict[i32] = dict(1)\nd[\"x\"] = \"x\"\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("fn f():\n    let mut d: dict[i32] = dict(1)\n    d[\"x\"] = false\nf()\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[i32] = [1]\nlet s: str = a[0]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let d: dict[i32] = dict(1)\nlet s: str = d[\"x\"]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[i32] = [1]\nfor v in a:\n    let s: str = v\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let mut a: array[i32] = [1]\na[0] += \"x\"\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("fn f():\n    let mut a: array[i32] = [1]\n    a[0] += \"x\"\nf()\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("let mut d: dict[str] = dict(1)\nd[\"x\"] += 1\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("fn f():\n    let mut d: dict[str] = dict(1)\n    d[\"x\"] += 1\nf()\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("let a: array[i32] = [1]\nlet b: array[str] = a\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("fn f(a: array[str]):\n    return\nlet a: array[i32] = [1]\nf(a)\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("fn f(a: array):\n    a[0] = \"x\"\nlet a: array[i32] = [1]\nf(a)\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("fn f(a: array[i32]):\n    return\nlet a = [1]\nf(a)\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[i32] = [1]\nlet b: array = a\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let d: dict[i32] = dict(1)\nlet e: dict = d\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[i32] = [0; 2]\nsplit_into(\"a,b\", \",\", a)\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[Missing] = []\n"), cnor_error_unknown_type);
    CHECK_EQ(failure_of("struct P:\n    x: i32\nlet a: array[P] = []\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[array] = []\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[dict] = []\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: dict[str] = [1]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let d: array[i32] = dict(1)\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("fn f():\n    let a: array[i32] = [1]\n    return a\nf()\n"), cnor_error_array_returned);
    CHECK_EQ(failure_of("fn f():\n    let d: dict[i32] = dict(1)\n    return d\nf()\n"), cnor_error_dict_returned);
    {
        const outcome_t out = run_source("let a: array[i32] = [1]\nlet b: array[str] = a\n");
        CHECK(!out.compiled);
        CHECK(strcmp(out.name, "expected array[str], found array[i32]") == 0);
    }
}


CNOR_TEST(imports_collection_element_types)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"types.cn\"\nlet modes: array[Mode] = [Mode.off, Mode.on]\n"
            "let mut settings: dict[Mode] = dict(1)\nsettings[\"mode\"] = last(modes)\n"
            "let mut result = 0\nmatch settings[\"mode\"]:\n"
            "    Mode.off => result = 3\n    Mode.on => result = 7\nresult\n"},
        {"types.cn", "enum Mode:\n    off\n    on\n"
            "fn last(modes: array[Mode]) -> Mode:\n    return modes[len(modes) - 1]\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 2U, lh_null, 0U, &loader, 2, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 7);
    {
        static const memory_file_t invalid[] = {
            {"main.cn", "import \"types.cn\"\nlet a: array[str] = [\"x\"]\nlast(a)\n"},
            {"types.cn", "enum Mode:\n    off\n"
                "fn last(a: array[Mode]) -> Mode:\n    return a[0]\n"},
        };
        run_program(invalid, 2U, lh_null, 0U, &loader, 1, &out);
        CHECK(!out.compiled);
        CHECK_EQ(out.code, cnor_error_type_mismatch);
        CHECK(strcmp(out.file, "main.cn") == 0);
    }
}

CNOR_TEST(handles_collection_boundaries_and_preserves_untyped_behavior)
{
    CHECK_EQ(value_of("let a = [1, \"two\", true]\nlen(a)\n"), 3);
    CHECK_EQ(value_of("let mut a = [0; 1]\na[0] = \"text\"\nlen(a[0])\n"), 4);
    CHECK_EQ(value_of("let mut d = dict(1)\nd[\"x\"] = 1\nd[\"x\"] = \"text\"\nlen(d[\"x\"])\n"), 4);
    CHECK_EQ(value_of("let n = 7\nfn f() -> i32:\n"
        "    let n: array[i32] = [n, n + 1]\n    return n[1]\nf()\n"), 8);
    CHECK_EQ(value_of("fn f(a: array[i32]) -> i32:\n    return len(a)\n"
        "let a: array[i32] = []\nf(a)\n"), 0);
    CHECK_EQ(value_of("native fn echo(a: array[i32]) -> array[i32]\n"
        "let a: array[i32] = [3]\nlet b = echo(a)\nb[0]\n"), 3);
    CHECK_EQ(value_of("let mut a: array[i32] = [1]\na[0] += echo(2)\na[0]\n"), 3);
    m_note_count = 0U;
    CHECK_EQ(value_of("let a: array[i32] = [note(9); 3]\na[0] + a[1] + a[2]\n"), 27);
    CHECK_EQ(m_note_count, 1U);
    CHECK_EQ(failure_of("let a = [0; 65535]\n"), cnor_error_too_many_globals);
    CHECK_EQ(failure_of("fn f():\n    let a = [0; 65535]\nf()\n"), cnor_error_too_many_locals);
    CHECK_EQ(failure_of("let a = [0; 65536]\n"), cnor_error_too_many_globals);
    CHECK_EQ(failure_of("let a: array[i32] = [1]\na[\"x\"]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let d: dict[i32] = dict(1)\nd[0]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let mut a: array[i32] = [1]\na[\"x\"] = 2\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let mut d: dict[i32] = dict(1)\nd[0] = 2\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let a: array[i32] = [1]\nlet alias = a\nlet s: str = alias[0]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("enum A:\n    x\nenum B:\n    x\nlet a: array[A] = [B.x]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("let mut a: array[bool] = [true]\na[0] += true\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("let a: array[i32] = [1]\nlet mut n = a[0]\nn += \"x\"\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("let mut a: array[i32] = [1]\na += a\n"), cnor_error_operand_type);
    CHECK_EQ(failure_of("struct P:\n    a: array[i32]\n"), cnor_error_type_mismatch);
    CHECK_EQ(failure_of("struct P:\n    d: dict[str]\n"), cnor_error_type_mismatch);
}


CNOR_TEST(uses_namespaces_for_functions_values_and_global_writes)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"a.cn\" as a\nimport \"b.cn\" as b\n"
            "let f = a.add\na.count = 4\na.count += 2\na.values[0] = 7\n"
            "a.values[1] += 3\na.settings[\"x\"] = 9\na.settings[\"x\"] += 1\n"
            "f(1) + b.add(1) + a.count + a.values[0] + a.values[1] + a.settings[\"x\"]\n"},
        {"a.cn", "let mut count = 0\nlet mut values: array[i32] = [1, 2]\n"
            "let mut settings: dict[i32] = dict(1)\nfn add(n: i32) -> i32:\n    return n + 10\n"},
        {"b.cn", "fn add(n: i32) -> i32:\n    return n + 100\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 3U, lh_null, 0U, &loader, 2, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 140);
    CHECK_EQ(loader.loads[1], 1);
    CHECK_EQ(loader.loads[2], 1);
}

CNOR_TEST(qualifies_record_enum_and_collection_types)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"game.cn\" as game\n"
            "fn heal(p: game.Player):\n    p.hp += 5\n"
            "let mut player: game.Player = game.Player(80, game.State.idle)\n"
            "heal(player)\nplayer.state = game.State.active\ngame.hero.hp += 2\n"
            "let states: array[game.State] = [game.State.idle, game.State.active]\n"
            "let mut settings: dict[game.State] = dict(1)\nsettings[\"state\"] = states[1]\n"
            "let mut result = 0\nmatch game.choose(settings):\n"
            "    game.State.idle => result = 1\n    game.State.active => result = player.hp + game.hero.hp\nresult\n"},
        {"game.cn", "enum State:\n    idle\n    active\n"
            "struct Player:\n    hp: i32\n    state: State\n"
            "let mut hero = Player(10, State.idle)\n"
            "fn choose(settings: dict[State]) -> State:\n    return settings[\"state\"]\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 97);
}

CNOR_TEST(initializes_a_module_once_across_namespace_aliases)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"lib.cn\" as a\nimport \"lib.cn\" as b\nimport \"lib.cn\"\n"
            "let p: a.Point = b.Point(4)\na.value += 1\nb.value + value + p.x\n"},
        {"lib.cn", "note(9)\nlet mut value = 10\nstruct Point:\n    x: i32\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    m_note_count = 0U;
    run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 26);
    CHECK_EQ(m_note_count, 1U);
    CHECK_EQ(m_notes[0], 9);
    CHECK_EQ(loader.loads[1], 1);
}

CNOR_TEST(keeps_namespaces_local_to_the_importing_module)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"a.cn\" as a\nimport \"b.cn\" as b\na.value() + b.value()\n"},
        {"a.cn", "import \"low.cn\" as lib\nfn value() -> i32:\n    return lib.value\n"},
        {"b.cn", "import \"high.cn\" as lib\nfn value() -> i32:\n    return lib.value\n"},
        {"low.cn", "let value = 3\n"},
        {"high.cn", "let value = 7\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 5U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 10);
}

CNOR_TEST(rejects_invalid_namespace_accesses)
{
    static const char *const invalid[] = {
        "import \"lib.cn\" as lib\nvalue\n",
        "import \"lib.cn\" as lib\nadd(1)\n",
        "import \"lib.cn\" as lib\nlib.missing\n",
        "import \"lib.cn\" as lib\nlib.missing()\n",
        "import \"lib.cn\" as lib\nother.add(1)\n",
        "import \"lib.cn\" as lib\nlet p: Missing.Point = lib.Point(1)\n",
        "import \"lib.cn\" as lib\nlet p = Point(1)\n",
        "import \"lib.cn\" as lib\nlet p: lib.Point = lib.Point(\"bad\")\n",
        "import \"lib.cn\" as lib\nlet lib = 1\n",
        "import \"lib.cn\" as lib\nfn lib():\n    return\n",
        "import \"lib.cn\" as lib\nstruct lib:\n    x: i32\n",
        "import \"lib.cn\" as lib\nfn f(lib: i32):\n    return\n",
        "import \"lib.cn\" as lib\nfn f():\n    let lib = 1\nf()\n",
        "import \"lib.cn\" as lib\nfor lib in 0..1:\n    1\n",
        "import \"lib.cn\" as lib\nimport \"lib.cn\" as lib\n1\n",
        "import \"lib.cn\" as i32\n1\n",
        "import \"lib.cn\" as lib\nlib.constant = 2\n",
        "import \"lib.cn\" as lib\nlib.value = \"bad\"\n",
        "import \"lib.cn\" as lib\nlib.echo(1)\n",
        "import \"lib.cn\" as lib\nlib\n",
        "import \"lib.cn\" as lib\nlib()\n",
        "import \"lib.cn\" as lib\nlib.leaf.value\n",
        "import \"lib.cn\" as lib\nlib.private(1)\n",
    };
    const memory_file_t supporting[] = {
        {"lib.cn", "import \"leaf.cn\" as leaf\nlet mut value = 1\nlet constant = 4\n"
            "struct Point:\n    x: i32\nfn add(n: i32) -> i32:\n    return n + 1\n"},
        {"leaf.cn", "let value = 100\nfn private(n: i32) -> i32:\n    return n\n"},
    };
    size_t i;
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        const memory_file_t files[] = {{"main.cn", invalid[i]}, supporting[0], supporting[1]};
        memory_loader_t loader;
        outcome_t out;
        run_program(files, 3U, lh_null, 0U, &loader, 1, &out);
        if (out.compiled) printf("    unexpectedly compiled namespace case %u: %s\n", (unsigned) i, invalid[i]);
        CHECK(!out.compiled);
        CHECK(out.code != cnor_error_none);
    }
}


CNOR_TEST(calls_namespaced_natives_and_function_values)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"lib.cn\" as lib\nlib.callback(3) + lib.echo(4)\n"},
        {"lib.cn", "native fn echo(n: i32) -> i32\n"
            "fn double(n: i32) -> i32:\n    return n * 2\nlet callback = double\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.value, 10);
}

CNOR_TEST(distinguishes_nominal_types_from_different_namespaces)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"a.cn\" as a\nimport \"b.cn\" as b\nlet s: a.State = b.State.idle\n"},
        {"a.cn", "enum State:\n    idle\n"},
        {"b.cn", "enum State:\n    idle\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 3U, lh_null, 0U, &loader, 1, &out);
    CHECK(!out.compiled);
    CHECK_EQ(out.code, cnor_error_type_mismatch);
    CHECK(strcmp(out.name, "expected a.State, found b.State") == 0);
}


CNOR_TEST(keeps_namespaced_runtime_errors_at_the_original_source)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"lib.cn\" as lib\nlib.fail(0)\n"},
        {"lib.cn", "fn fail(n: i32) -> i32:\n    return 1 / n\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.line, 2U);
    CHECK(strcmp(out.file, "lib.cn") == 0);
}


static outcome_t
run_trace_source(const char *source, cnor_count_t capacity, lh_bool_t lines)
{
    cnor_compiler_config_t config = cnor_compiler_config_initializer();
    cnor_vm_config_t vm_config = cnor_vm_config_initializer();
    cnor_diag_t diag;
    cnor_image_t image;
    cnor_vm_t vm;
    outcome_t out;
    memset(&out, 0, sizeof(out));
    config.file = view_of("trace.cn");
    config.emit_lines = lines;
    config.natives = &m_table;
    out.compiled = cnor_compiler_compile_source(view_of(source), &config, &image, &diag);
    if (!out.compiled) { take_diag(&out, &diag); return out; }
    vm_config.natives = &m_table;
    vm_config.trace_capacity = capacity;
    cnor_vm_init(&vm, &vm_config);
    if (cnor_vm_load(&vm, image.data, image.size)) (void) cnor_vm_run(&vm);
    take_diag(&out, cnor_vm_get_diag(&vm));
    take_trace(&out, &vm);
    cnor_vm_deinit(&vm);
    cnor_image_deinit(&image);
    return out;
}

CNOR_TEST(traces_script_callers_at_their_call_sites)
{
    static const memory_file_t files[] = {
        {"main.cn", "import \"lib.cn\" as lib\nfn attack() -> i32:\n"
            "    return lib.divide(\n        0\n    )\nattack()\n"},
        {"lib.cn", "fn divide(n: i32) -> i32:\n    return 10 / n\n"},
    };
    memory_loader_t loader;
    outcome_t out;
    run_program(files, 2U, lh_null, 0U, &loader, 1, &out);
    CHECK(out.compiled);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(!out.trace_truncated);
    CHECK(strcmp(out.trace[0].name, "divide") == 0);
    CHECK(strcmp(out.trace[0].file, "lib.cn") == 0);
    CHECK_EQ(out.trace[0].line, 2U);
    CHECK(strcmp(out.trace[1].name, "attack") == 0);
    CHECK(strcmp(out.trace[1].file, "main.cn") == 0);
    CHECK_EQ(out.trace[1].line, 3U);
    CHECK(out.trace[2].name[0] == '\0');
    CHECK_EQ(out.trace[2].line, 6U);
}

CNOR_TEST(preserves_original_traces_across_finally_and_nested_rethrows)
{
    const char *program = "fn original() -> i32:\n    return 1 / 0\n"
        "fn secondary() -> i32:\n    return 2 / 0\n"
        "fn wrapper():\n    try:\n        return original()\n"
        "    catch:\n        try:\n            secondary()\n        catch:\n            note(3)\n"
        "        raise\nwrapper()\n";
    outcome_t out = run_trace_source(program, 64U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(!out.trace_truncated);
    CHECK(strcmp(out.trace[0].name, "original") == 0);
    CHECK_EQ(out.trace[0].line, 2U);
    CHECK(strcmp(out.trace[1].name, "wrapper") == 0);
    CHECK_EQ(out.trace[1].line, 7U);
    /* The first error fills a tiny pool; a nested error must not overwrite it. */
    out = run_trace_source(program, 3U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(!out.trace_truncated);
    CHECK(strcmp(out.trace[0].name, "original") == 0);
    out = run_trace_source("fn original() -> i32:\n    return 1 / 0\n"
        "fn wrapper():\n    try:\n        return original()\n"
        "    finally:\n        note(9)\nwrapper()\n", 64U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(strcmp(out.trace[0].name, "original") == 0);
    CHECK_EQ(out.trace[1].line, 5U);
    out = run_trace_source("fn original() -> i32:\n    return 1 / 0\n"
        "fn secondary() -> i32:\n    return 2 / 0\n"
        "fn wrapper():\n    try:\n        return original()\n"
        "    finally:\n        secondary()\nwrapper()\n", 64U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(strcmp(out.trace[0].name, "secondary") == 0);
    CHECK_EQ(out.trace[1].line, 9U);
}

CNOR_TEST(bounds_trace_storage_and_recycles_handled_snapshots)
{
    const char *program = "fn leaf() -> i32:\n    return 1 / 0\n"
        "fn middle() -> i32:\n    return leaf()\nmiddle()\n";
    outcome_t out = run_trace_source(program, 2U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 2U);
    CHECK(out.trace_truncated);
    CHECK(strcmp(out.trace[0].name, "leaf") == 0);
    CHECK(strcmp(out.trace[1].name, "middle") == 0);
    out = run_trace_source(program, 0U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 0U);
    CHECK(!out.trace_truncated);
    out = run_trace_source(program, 64U, lh_bool_false);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(strcmp(out.trace[0].name, "leaf") == 0);
    CHECK_EQ(out.trace[0].line, 0U);
    CHECK_EQ(out.trace[1].line, 0U);
    out = run_trace_source("fn leaf() -> i32:\n    return 1 / 0\n"
        "fn wrapper():\n    for i in 0..200:\n        try:\n            leaf()\n"
        "        catch:\n            note(i)\n    return leaf()\nwrapper()\n", 3U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_div_by_zero);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(!out.trace_truncated);
    out = run_trace_source("try:\n    1 / 0\ncatch:\n    42\n", 2U, lh_bool_true);
    CHECK_EQ(out.code, cnor_error_none);
    CHECK_EQ(out.trace_count, 0U);
    CHECK(!out.trace_truncated);
    out = run_trace_source("fn leaf() -> i32:\n    return 1 / 0\n"
        "fn middle() -> i32:\n    let f = leaf\n    return f()\nmiddle()\n", 64U, lh_bool_true);
    CHECK_EQ(out.trace_count, 3U);
    CHECK(strcmp(out.trace[0].name, "leaf") == 0);
    CHECK_EQ(out.trace[1].line, 5U);
}

static const cnor_test_case_t m_cases[] = {
    CNOR_TEST_CASE(traces_script_callers_at_their_call_sites),
    CNOR_TEST_CASE(preserves_original_traces_across_finally_and_nested_rethrows),
    CNOR_TEST_CASE(bounds_trace_storage_and_recycles_handled_snapshots),
    CNOR_TEST_CASE(keeps_namespaced_runtime_errors_at_the_original_source),
    CNOR_TEST_CASE(calls_namespaced_natives_and_function_values),
    CNOR_TEST_CASE(distinguishes_nominal_types_from_different_namespaces),
    CNOR_TEST_CASE(uses_namespaces_for_functions_values_and_global_writes),
    CNOR_TEST_CASE(qualifies_record_enum_and_collection_types),
    CNOR_TEST_CASE(initializes_a_module_once_across_namespace_aliases),
    CNOR_TEST_CASE(keeps_namespaces_local_to_the_importing_module),
    CNOR_TEST_CASE(rejects_invalid_namespace_accesses),
    CNOR_TEST_CASE(imports_collection_element_types),
    CNOR_TEST_CASE(handles_collection_boundaries_and_preserves_untyped_behavior),
    CNOR_TEST_CASE(declares_array_lists_and_typed_collections),
    CNOR_TEST_CASE(carries_element_types_through_calls_and_loops),
    CNOR_TEST_CASE(rejects_incompatible_collection_values),
    CNOR_TEST_CASE(has_nominal_records_and_enums),
    CNOR_TEST_CASE(keeps_record_references_within_their_lifetime),
    CNOR_TEST_CASE(checks_record_fields_and_enum_identity),
    CNOR_TEST_CASE(imports_nominal_types_without_a_file_system),
    CNOR_TEST_CASE(has_the_bitwise_operators),
CNOR_TEST_CASE(catches_an_error),
CNOR_TEST_CASE(catches_an_error_from_a_function),
CNOR_TEST_CASE(leaves_a_try_when_the_code_goes_away),
CNOR_TEST_CASE(refuses_a_try_where_it_does_not_fit),
    CNOR_TEST_CASE(runs_a_finally_block),
    CNOR_TEST_CASE(raises_the_error_again),
    CNOR_TEST_CASE(leaves_a_try_with_a_finally),
    CNOR_TEST_CASE(assigns_with_an_operator),
    CNOR_TEST_CASE(picks_a_value_with_a_ternary),
    CNOR_TEST_CASE(puts_values_into_a_string),
    CNOR_TEST_CASE(splits_a_text_into_an_array),
    CNOR_TEST_CASE(keeps_a_function_as_a_value),
    CNOR_TEST_CASE(calls_a_function_declared_inside_another_one),
    CNOR_TEST_CASE(refuses_a_local_function_that_cannot_work),
    CNOR_TEST_CASE(passes_an_array_to_a_function_and_walks_it),
    CNOR_TEST_CASE(keeps_a_global_array_whole),
    CNOR_TEST_CASE(refuses_an_array_where_it_does_not_fit),
CNOR_TEST_CASE(has_dictionaries),
CNOR_TEST_CASE(walks_the_keys_of_a_dictionary),
CNOR_TEST_CASE(shares_a_dictionary_with_a_function),
CNOR_TEST_CASE(keeps_a_global_dictionary_whole),
CNOR_TEST_CASE(refuses_a_dictionary_where_it_does_not_fit),
    CNOR_TEST_CASE(has_the_string_functions),
    CNOR_TEST_CASE(checks_the_arguments_of_the_string_functions),
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
