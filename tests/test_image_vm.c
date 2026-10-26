/* Image format, verifier and VM, driven by hand-built images. Ported from the
 * original bytecode/VM suite (adapted to the v1 format and to the load-time
 * verifier, which now catches what the original found while running), plus
 * verifier rejections. */

#include "harness.h"

#include <cnor/byte/writer.h>
#include <cnor/image/writer.h>
#include <cnor/native/initializer.h>
#include <cnor/op.h>
#include <cnor/vm.h>
#include <cnor/vm/internal.h>
#include <lh/null.h>

/* -- image builder --------------------------------------------------------- */

#define MAX_FUNCTIONS 4
#define MAX_NATIVES 4

typedef struct program
{
    cnor_byte_writer_t code;
    cnor_byte_writer_t consts;
    cnor_byte_writer_t lines;
    cnor_count_t const_count;
    cnor_count_t line_count;
    cnor_image_function_t functions[MAX_FUNCTIONS];
    cnor_count_t function_count;
    lh_str_view_t natives[MAX_NATIVES];
    cnor_count_t native_count;
    lh_u32_t globals;
    cnor_stack_slots_t stack_slots;
    lh_str_view_t file;
    const lh_str_view_t *files; /**< When set, the file table instead of `file`. */
    cnor_count_t file_count;
    cnor_image_t image;
} program_t;

static lh_str_view_t
view_of(const char *text)
{
    lh_str_view_t view;

    lh_str_init_by_size(&view, text, strlen(text));
    return view;
}

static void
p_init(program_t *p)
{
    memset(p, 0, sizeof(*p));
    cnor_byte_writer_init(&p->code);
    cnor_byte_writer_init(&p->consts);
    cnor_byte_writer_init(&p->lines);
    lh_str_view_init_empty(&p->file);
    p->stack_slots = 16U;
    cnor_image_init(&p->image);
}

static void
p_deinit(program_t *p)
{
    cnor_byte_writer_deinit(&p->code);
    cnor_byte_writer_deinit(&p->consts);
    cnor_byte_writer_deinit(&p->lines);
    cnor_image_deinit(&p->image);
}

static void
op(program_t *p, cnor_op_t code)
{
    cnor_byte_writer_put_u8(&p->code, (lh_u8_t) code);
}

static void
op_u32(program_t *p, cnor_op_t code, lh_u32_t operand)
{
    op(p, code);
    cnor_byte_writer_put_u32(&p->code, operand);
}

static void
op_u16_u8(program_t *p, cnor_op_t code, lh_u16_t a, lh_u8_t b)
{
    op(p, code);
    cnor_byte_writer_put_u16(&p->code, a);
    cnor_byte_writer_put_u8(&p->code, b);
}

static void
add_const(program_t *p, const char *text)
{
    cnor_byte_writer_put_u8(&p->consts, (lh_u8_t) cnor_const_str);
    cnor_byte_writer_put_u16(&p->consts, (lh_u16_t) strlen(text));
    cnor_byte_writer_put_bytes(&p->consts, (const lh_byte_t *) text, (lh_u32_t) strlen(text));
    p->const_count++;
}

static void
add_line(program_t *p, lh_u32_t addr, lh_u32_t line)
{
    cnor_byte_writer_put_u32(&p->lines, addr);
    cnor_byte_writer_put_u32(&p->lines, line);
    p->line_count++;
}

/* Close the function that started at @p entry. */
static void
end_function(program_t *p, cnor_code_addr_t entry, cnor_argc_t params, lh_u8_t slots,
             lh_u16_t max_stack)
{
    cnor_image_function_t *f = &p->functions[p->function_count++];

    f->entry = entry;
    f->size = cnor_byte_writer_get_size(&p->code) - entry;
    f->param_count = params;
    f->slot_count = slots;
    f->max_stack = max_stack;
}

static cnor_code_addr_t
here(const program_t *p)
{
    return cnor_byte_writer_get_size(&p->code);
}

/* One entry function covering all the code, unless functions were closed. */
static void
build(program_t *p, lh_u16_t max_stack)
{
    cnor_image_parts_t parts;

    if (p->function_count == 0U)
    {
        end_function(p, 0U, 0U, 0U, max_stack);
    }
    parts.code = &p->code;
    parts.consts = &p->consts;
    parts.const_count = p->const_count;
    parts.functions = p->functions;
    parts.function_count = p->function_count;
    parts.natives = p->natives;
    parts.native_count = p->native_count;
    parts.lines = p->line_count > 0U ? &p->lines : lh_null;
    parts.line_count = p->line_count;
    parts.files = p->files != lh_null ? p->files : &p->file;
    parts.file_count = p->files != lh_null                      ? p->file_count
                       : lh_str_view_get_size(&p->file) > 0U ? 1U
                                                              : 0U;
    parts.globals_count = p->globals;
    parts.stack_slots = p->stack_slots;
    (void) cnor_image_write(&parts, &p->image);
}

/* -- natives --------------------------------------------------------------- */

static void
double_it(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx, lh_value_t *result,
         cnor_error_t *error)
{
    (void) argc;
    (void) ctx;
    (void) error;
    *result = lh_value_make_sint(lh_value_get_data(&args[0])->i * 2);
}

static const cnor_native_t m_natives[] = {
    cnor_native_initializer("double_it", double_it, 1U, 1U),
};
static const cnor_native_table_t m_table = cnor_native_table_initializer(m_natives, 1U);

static void
vm_open(cnor_vm_t *vm)
{
    cnor_vm_config_t config = cnor_vm_config_initializer();

    config.natives = &m_table;
    cnor_vm_init(vm, &config);
}

static cnor_error_t
vm_code(const cnor_vm_t *vm)
{
    return cnor_diag_get_code(cnor_vm_get_diag(vm));
}

/* Load and run @p p; the error, and the result through @p result. */
static cnor_error_t
load_and_run(program_t *p, lh_value_t *result)
{
    cnor_vm_t vm;
    cnor_error_t code;

    vm_open(&vm);
    if (cnor_vm_load(&vm, p->image.data, p->image.size))
    {
        (void) cnor_vm_run(&vm);
    }
    code = vm_code(&vm);
    if (result != lh_null)
    {
        *result = cnor_vm_get_result(&vm);
    }
    cnor_vm_deinit(&vm);
    return code;
}

static long long
sint_of(const lh_value_t *value)
{
    return lh_value_get_kind(value) == lh_value_kind_sint ? (long long) lh_value_get_data(value)->i
                                                          : -999999;
}

/* -- header ---------------------------------------------------------------- */

CNOR_TEST(refuses_a_buffer_shorter_than_a_header)
{
    lh_byte_t buffer[4] = {0U, 0U, 0U, 0U};
    cnor_image_header_t header;

    CHECK_EQ(cnor_image_header_read(buffer, sizeof(buffer), &header), cnor_error_too_small);
}

CNOR_TEST(refuses_a_buffer_without_the_signature)
{
    program_t p;
    cnor_image_header_t header;

    p_init(&p);
    op(&p, cnor_op_halt);
    build(&p, 0U);
    REQUIRE(p.image.data != lh_null);
    p.image.data[0] = 0U;
    CHECK_EQ(cnor_image_header_read(p.image.data, p.image.size, &header), cnor_error_bad_magic);
    p_deinit(&p);
}

CNOR_TEST(refuses_a_version_it_does_not_speak)
{
    program_t p;
    cnor_image_header_t header;

    p_init(&p);
    op(&p, cnor_op_halt);
    build(&p, 0U);
    REQUIRE(p.image.data != lh_null);
    p.image.data[4] = (lh_byte_t) (CNOR_IMAGE_VERSION + 1U);
    CHECK_EQ(cnor_image_header_read(p.image.data, p.image.size, &header),
             cnor_error_bad_version);
    p_deinit(&p);
}

CNOR_TEST(refuses_a_stack_beyond_what_the_host_offers)
{
    program_t p;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();

    p_init(&p);
    p.stack_slots = 4096U;
    op_u32(&p, cnor_op_push_i32, 1U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    config.stack_slots = 1024U;
    cnor_vm_init(&vm, &config);
    CHECK(!cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK_EQ(vm_code(&vm), cnor_error_stack_too_big);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(reads_the_sections_of_a_well_formed_image)
{
    program_t p;
    cnor_image_header_t header;

    p_init(&p);
    add_const(&p, "hi");
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    REQUIRE(cnor_image_header_read(p.image.data, p.image.size, &header) == cnor_error_none);
    CHECK_EQ(header.sections[cnor_image_section_code].size, 4U);
    CHECK_EQ(header.sections[cnor_image_section_consts].size, 4U + 1U + 2U + 2U);
    CHECK_EQ(header.sections[cnor_image_section_code].offset % CNOR_IMAGE_ALIGN, 0U);
    CHECK_EQ(header.image_size, p.image.size);
    p_deinit(&p);
}

/* -- running --------------------------------------------------------------- */

CNOR_TEST(runs_a_program_that_only_stops)
{
    program_t p;
    lh_value_t result;

    p_init(&p);
    op(&p, cnor_op_halt);
    build(&p, 0U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_none);
    CHECK_EQ(lh_value_get_kind(&result), lh_value_kind_none);
    p_deinit(&p);
}

CNOR_TEST(adds_two_numbers)
{
    program_t p;
    lh_value_t result;

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 2U);
    op_u32(&p, cnor_op_push_i32, 40U);
    op(&p, cnor_op_add);
    op(&p, cnor_op_halt);
    build(&p, 2U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_none);
    CHECK_EQ(sint_of(&result), 42);
    p_deinit(&p);
}

CNOR_TEST(mixes_signed_and_unsigned_without_wrapping)
{
    program_t p;
    lh_value_t result;

    p_init(&p);
    op_u32(&p, cnor_op_push_u32, 4000000000U);
    op_u32(&p, cnor_op_push_i32, 1U);
    op(&p, cnor_op_sub);
    op(&p, cnor_op_halt);
    build(&p, 2U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_none);
    CHECK_EQ(sint_of(&result), 3999999999LL);
    p_deinit(&p);
}

CNOR_TEST(reports_division_by_zero_instead_of_trapping)
{
    program_t p;

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op_u32(&p, cnor_op_push_i32, 0U);
    op(&p, cnor_op_div);
    op(&p, cnor_op_halt);
    build(&p, 2U);
    CHECK_EQ(load_and_run(&p, lh_null), cnor_error_div_by_zero);
    CHECK(strcmp(cnor_error_get_message(cnor_error_div_by_zero), "division by zero") == 0);
    p_deinit(&p);
}

CNOR_TEST(refuses_a_byte_that_is_not_an_opcode_at_load)
{
    program_t p;
    cnor_vm_t vm;

    p_init(&p);
    cnor_byte_writer_put_u8(&p.code, 0xFEU);
    op(&p, cnor_op_halt);
    build(&p, 0U);
    vm_open(&vm);
    CHECK(!cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK_EQ(vm_code(&vm), cnor_error_bad_opcode);
    CHECK_EQ(cnor_diag_get_offset(cnor_vm_get_diag(&vm)), 0U);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(refuses_an_array_whose_elements_leave_the_frame)
{
    program_t p;

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 0U);
    op(&p, cnor_op_array_new);
    cnor_byte_writer_put_u8(&p.code, 0U);  /* the slot */
    cnor_byte_writer_put_u16(&p.code, 4U); /* the count */
    op(&p, cnor_op_halt);
    /* The frame has one slot, so the four elements are outside it. */
    end_function(&p, 0U, 0U, 1U, 2U);
    build(&p, 2U);
    CHECK_EQ(load_and_run(&p, lh_null), cnor_error_bad_operand);
    p_deinit(&p);
}

CNOR_TEST(refuses_an_instruction_that_runs_past_the_code)
{
    program_t p;

    p_init(&p);
    op(&p, cnor_op_push_i32);
    build(&p, 1U);
    CHECK_EQ(load_and_run(&p, lh_null), cnor_error_code_truncated);
    p_deinit(&p);
}

CNOR_TEST(refuses_more_pushes_than_the_declared_depth)
{
    program_t p;
    int i;

    p_init(&p);
    for (i = 0; i < 20; ++i)
    {
        op_u32(&p, cnor_op_push_i32, 1U);
    }
    op(&p, cnor_op_halt);
    build(&p, 16U);
    CHECK_EQ(load_and_run(&p, lh_null), cnor_error_bad_stack);
    p_deinit(&p);
}

CNOR_TEST(overflows_the_stack_it_was_given)
{
    program_t p;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_code_addr_t entry;

    /* f0: call f1; ret.  f1: push 1; call f1; ret -- one more value per call. */
    p_init(&p);
    op_u16_u8(&p, cnor_op_call, 1U, 0U);
    op(&p, cnor_op_ret);
    end_function(&p, 0U, 0U, 0U, 1U);
    entry = here(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op_u16_u8(&p, cnor_op_call, 1U, 0U);
    op(&p, cnor_op_ret);
    end_function(&p, entry, 0U, 0U, 2U);
    p.stack_slots = 2U;
    build(&p, 0U);
    config.stack_slots = 32U;
    config.max_depth = 1000U;
    cnor_vm_init(&vm, &config);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(vm_code(&vm), cnor_error_stack_overflow);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(joins_two_strings_and_measures_them)
{
    program_t p;
    lh_value_t result;

    p_init(&p);
    add_const(&p, "hello, ");
    add_const(&p, "world");
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 1U);
    op(&p, cnor_op_add);
    op(&p, cnor_op_len);
    op(&p, cnor_op_halt);
    build(&p, 2U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_none);
    CHECK_EQ(sint_of(&result), 12);
    p_deinit(&p);
}

CNOR_TEST(calls_a_native_and_keeps_its_result)
{
    program_t p;
    lh_value_t result;

    p_init(&p);
    p.natives[p.native_count++] = view_of("double_it");
    op_u32(&p, cnor_op_push_i32, 21U);
    op_u16_u8(&p, cnor_op_native, 0U, 1U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_none);
    CHECK_EQ(sint_of(&result), 42);
    p_deinit(&p);
}

CNOR_TEST(refuses_a_native_it_was_not_given_at_load)
{
    program_t p;
    cnor_vm_t vm;
    lh_str_view_t name;

    p_init(&p);
    p.natives[p.native_count++] = view_of("missing");
    op_u32(&p, cnor_op_push_i32, 1U);
    op_u16_u8(&p, cnor_op_native, 0U, 1U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    vm_open(&vm);
    CHECK(!cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK_EQ(vm_code(&vm), cnor_error_no_such_native);
    name = cnor_diag_get_name(cnor_vm_get_diag(&vm));
    CHECK(lh_str_view_get_size(&name) == 7U);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(refuses_a_call_with_the_wrong_arity)
{
    program_t p;

    p_init(&p);
    p.natives[p.native_count++] = view_of("double_it");
    op_u16_u8(&p, cnor_op_native, 0U, 0U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    CHECK_EQ(load_and_run(&p, lh_null), cnor_error_bad_arity);
    p_deinit(&p);
}

CNOR_TEST(keeps_globals_across_runs_until_a_reset)
{
    program_t p;
    cnor_vm_t vm;
    const lh_value_t *g;

    p_init(&p);
    p.globals = 1U;
    op(&p, cnor_op_load_global);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op(&p, cnor_op_pop);
    op_u32(&p, cnor_op_push_i32, 7U);
    op(&p, cnor_op_store_global);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    g = cnor_vm_get_global(&vm, 0U);
    REQUIRE(g != lh_null);
    CHECK_EQ(lh_value_get_kind(g), lh_value_kind_none);
    CHECK(cnor_vm_run(&vm));
    CHECK_EQ(sint_of(cnor_vm_get_global(&vm, 0U)), 7);
    CHECK(cnor_vm_run(&vm));
    CHECK_EQ(sint_of(cnor_vm_get_global(&vm, 0U)), 7);
    /* Only the globals remain on the stack between runs. */
    CHECK_EQ(vm.stack.sp, 1U);
    cnor_vm_reset(&vm);
    CHECK_EQ(lh_value_get_kind(cnor_vm_get_global(&vm, 0U)), lh_value_kind_none);
    CHECK(cnor_vm_get_global(&vm, 1U) == lh_null);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(maps_a_failure_back_to_the_source_line)
{
    program_t p;
    cnor_vm_t vm;
    lh_str_view_t file;

    /* 0: push_i32 (line 1), 5: push_i32 (line 2), 10: div (line 3). */
    p_init(&p);
    p.file = view_of("main.cn");
    op_u32(&p, cnor_op_push_i32, 1U);
    op_u32(&p, cnor_op_push_i32, 0U);
    op(&p, cnor_op_div);
    op(&p, cnor_op_halt);
    add_line(&p, 0U, 1U);
    add_line(&p, 5U, 2U);
    add_line(&p, 10U, 3U);
    build(&p, 2U);
    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(vm_code(&vm), cnor_error_div_by_zero);
    CHECK_EQ(cnor_diag_get_line(cnor_vm_get_diag(&vm)), 3U);
    CHECK_EQ(cnor_diag_get_offset(cnor_vm_get_diag(&vm)), 10U);
    CHECK_EQ(cnor_vm_get_line_at(&vm, 7U), 2U);
    file = cnor_vm_get_file(&vm);
    CHECK(lh_str_view_get_size(&file) == 7U
          && memcmp(lh_str_view_get_data(&file), "main.cn", 7U) == 0);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(answers_no_line_for_a_program_built_without_them)
{
    program_t p;
    cnor_vm_t vm;
    lh_str_view_t file;

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op_u32(&p, cnor_op_push_i32, 0U);
    op(&p, cnor_op_div);
    op(&p, cnor_op_halt);
    build(&p, 2U);
    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(cnor_diag_get_line(cnor_vm_get_diag(&vm)), 0U);
    file = cnor_vm_get_file(&vm);
    CHECK_EQ(lh_str_view_get_size(&file), 0U);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(refuses_to_run_before_anything_is_loaded)
{
    cnor_vm_t vm;

    cnor_vm_init(&vm, lh_null);
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(vm_code(&vm), cnor_error_not_loaded);
    cnor_vm_deinit(&vm);
}

CNOR_TEST(reuses_one_machine_for_two_programs)
{
    program_t first;
    program_t second;
    cnor_vm_t vm;
    lh_value_t result;

    p_init(&first);
    op_u32(&first, cnor_op_push_i32, 5U);
    op(&first, cnor_op_neg);
    op(&first, cnor_op_halt);
    build(&first, 1U);
    p_init(&second);
    op_u32(&second, cnor_op_push_i32, 11U);
    op(&second, cnor_op_halt);
    build(&second, 1U);

    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, first.image.data, first.image.size));
    CHECK(cnor_vm_run(&vm));
    result = cnor_vm_get_result(&vm);
    CHECK_EQ(sint_of(&result), -5);
    REQUIRE(cnor_vm_load(&vm, second.image.data, second.image.size));
    CHECK(cnor_vm_run(&vm));
    result = cnor_vm_get_result(&vm);
    CHECK_EQ(sint_of(&result), 11);
    cnor_vm_deinit(&vm);
    cnor_vm_deinit(&vm);
    p_deinit(&first);
    p_deinit(&second);
}

/* -- instruction budget ------------------------------------------------------- */

CNOR_TEST(stops_a_jump_to_itself_and_unwinds)
{
    program_t p;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();

    /* 0: push 7; 5: store_global 0; 8: push 1; 13: pop; 14: jmp back to 8.
     * Jumps are relative to the end of the jump (19), so the offset is -11. */
    p_init(&p);
    p.globals = 1U;
    op_u32(&p, cnor_op_push_i32, 7U);
    op(&p, cnor_op_store_global);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op_u32(&p, cnor_op_push_i32, 1U);
    op(&p, cnor_op_pop);
    op_u32(&p, cnor_op_jmp, (lh_u32_t) -11);
    build(&p, 1U);
    config.max_steps = 100U;
    cnor_vm_init(&vm, &config);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(vm_code(&vm), cnor_error_step_limit);
    CHECK_EQ(cnor_vm_get_steps(&vm), 100U);
    /* Stopped before an instruction of the loop, with only the globals left. */
    CHECK(cnor_diag_get_offset(cnor_vm_get_diag(&vm)) >= 8U);
    CHECK_EQ(vm.stack.sp, 1U);
    CHECK_EQ(vm.depth, 0U);
    CHECK_EQ(sint_of(cnor_vm_get_global(&vm, 0U)), 7);
    /* The machine stays usable: the next run gets a fresh budget. */
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(cnor_vm_get_steps(&vm), 100U);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(runs_unlimited_with_a_zero_budget)
{
    program_t p;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();
    lh_value_t result;

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    config.max_steps = 0U;
    cnor_vm_init(&vm, &config);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK(cnor_vm_run(&vm));
    result = cnor_vm_get_result(&vm);
    CHECK_EQ(sint_of(&result), 1);
    CHECK_EQ(cnor_vm_get_steps(&vm), 2U);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

/* -- verifier -------------------------------------------------------------- */

static cnor_error_t
load_error(program_t *p)
{
    cnor_vm_t vm;
    cnor_error_t code;

    vm_open(&vm);
    code = cnor_vm_load(&vm, p->image.data, p->image.size) ? cnor_error_none : vm_code(&vm);
    cnor_vm_deinit(&vm);
    return code;
}

/* Each function names its source file; a failure points at the file of the
 * code that failed. */
static void
two_file_program(program_t *p, cnor_file_index_t second_file)
{
    cnor_code_addr_t entry;

    /* f0 (main.cn): call f1; ret.  f1 (lib.cn): 1 / 0. */
    op_u16_u8(p, cnor_op_call, 1U, 0U);
    op(p, cnor_op_ret);
    end_function(p, 0U, 0U, 0U, 1U);
    entry = here(p);
    op_u32(p, cnor_op_push_i32, 1U);
    op_u32(p, cnor_op_push_i32, 0U);
    op(p, cnor_op_div);
    op(p, cnor_op_ret);
    end_function(p, entry, 0U, 0U, 2U);
    p->functions[1].file = second_file;
    add_line(p, 0U, 4U);
    add_line(p, entry, 2U);
    build(p, 0U);
}

CNOR_TEST(maps_a_failure_back_to_its_file)
{
    static lh_str_view_t files[2];
    program_t p;
    cnor_vm_t vm;
    lh_str_view_t file;

    files[0] = view_of("main.cn");
    files[1] = view_of("lib/x.cn");
    p_init(&p);
    p.files = files;
    p.file_count = 2U;
    two_file_program(&p, 1U);
    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    file = cnor_vm_get_file(&vm);
    CHECK(lh_str_view_get_size(&file) == 7U
          && memcmp(lh_str_view_get_data(&file), "main.cn", 7U) == 0);
    file = cnor_vm_get_file_at(&vm, 0U);
    CHECK(lh_str_view_get_size(&file) == 7U);
    file = cnor_vm_get_file_at(&vm, 6U);
    CHECK(lh_str_view_get_size(&file) == 8U
          && memcmp(lh_str_view_get_data(&file), "lib/x.cn", 8U) == 0);
    file = cnor_vm_get_file_at(&vm, 1000U);
    CHECK_EQ(lh_str_view_get_size(&file), 0U);
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(vm_code(&vm), cnor_error_div_by_zero);
    CHECK_EQ(cnor_diag_get_line(cnor_vm_get_diag(&vm)), 2U);
    file = cnor_diag_get_file(cnor_vm_get_diag(&vm));
    CHECK(lh_str_view_get_size(&file) == 8U
          && memcmp(lh_str_view_get_data(&file), "lib/x.cn", 8U) == 0);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

CNOR_TEST(rejects_a_function_of_a_file_the_image_does_not_have)
{
    static lh_str_view_t files[2];
    program_t p;

    files[0] = view_of("main.cn");
    files[1] = view_of("lib/x.cn");
    p_init(&p);
    p.files = files;
    p.file_count = 2U;
    two_file_program(&p, 2U);
    CHECK_EQ(load_error(&p), cnor_error_bad_header);
    p_deinit(&p);
    /* Without a file table, every function is in the one unnamed file. */
    p_init(&p);
    two_file_program(&p, 1U);
    CHECK_EQ(load_error(&p), cnor_error_bad_header);
    p_deinit(&p);
    p_init(&p);
    two_file_program(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_none);
    p_deinit(&p);
}

CNOR_TEST(rejects_a_jump_into_the_middle_of_an_instruction)
{
    program_t p;

    /* 0: jmp +2 lands inside the push at 5. */
    p_init(&p);
    op_u32(&p, cnor_op_jmp, 2U);
    op_u32(&p, cnor_op_push_i32, 1U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    CHECK_EQ(load_error(&p), cnor_error_bad_jump);
    p_deinit(&p);
}

CNOR_TEST(rejects_a_jump_out_of_its_function)
{
    program_t p;

    p_init(&p);
    op_u32(&p, cnor_op_jmp, 100U);
    op(&p, cnor_op_halt);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_jump);
    p_deinit(&p);
}

CNOR_TEST(rejects_paths_that_disagree_on_the_stack)
{
    program_t p;

    /* push true; jmp_if_false +5 (skip a push); push 1; halt -- depth 0 or 1 at halt. */
    p_init(&p);
    op(&p, cnor_op_push_true);
    op_u32(&p, cnor_op_jmp_if_false, 5U);
    op_u32(&p, cnor_op_push_i32, 1U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    CHECK_EQ(load_error(&p), cnor_error_bad_stack);
    p_deinit(&p);
}

CNOR_TEST(rejects_an_operand_stack_underflow)
{
    program_t p;

    p_init(&p);
    op(&p, cnor_op_add);
    op(&p, cnor_op_halt);
    build(&p, 2U);
    CHECK_EQ(load_error(&p), cnor_error_bad_stack);
    p_deinit(&p);
}

CNOR_TEST(rejects_code_that_falls_off_its_function)
{
    program_t p;

    p_init(&p);
    op(&p, cnor_op_push_none);
    build(&p, 1U);
    CHECK_EQ(load_error(&p), cnor_error_no_terminator);
    p_deinit(&p);
}

CNOR_TEST(rejects_operands_outside_their_tables)
{
    program_t p;

    p_init(&p);
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);

    p_init(&p);
    op(&p, cnor_op_load_local);
    cnor_byte_writer_put_u8(&p.code, 3U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);
}

CNOR_TEST(rejects_a_call_with_the_wrong_argument_count)
{
    program_t p;
    cnor_code_addr_t entry;

    p_init(&p);
    op_u16_u8(&p, cnor_op_call, 1U, 0U);
    op(&p, cnor_op_ret);
    end_function(&p, 0U, 0U, 0U, 1U);
    entry = here(&p);
    op(&p, cnor_op_push_none);
    op(&p, cnor_op_ret);
    end_function(&p, entry, 1U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);
}

CNOR_TEST(rejects_functions_that_do_not_tile_the_code)
{
    program_t p;

    p_init(&p);
    op(&p, cnor_op_push_none);
    op(&p, cnor_op_ret);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 0U, 1U);
    p.functions[0].size = 2U;
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_header);
    p_deinit(&p);
}

/* -- operand forms and for_loop -------------------------------------------- */

static void
op_slot(program_t *p, cnor_op_t code, lh_u8_t slot)
{
    op(p, code);
    cnor_byte_writer_put_u8(&p->code, slot);
}

static void
op_slots(program_t *p, cnor_op_t code, lh_u8_t a, lh_u8_t b)
{
    op_slot(p, code, a);
    cnor_byte_writer_put_u8(&p->code, b);
}

static void
op_slot_s32(program_t *p, cnor_op_t code, lh_u8_t slot, lh_s32_t k)
{
    op_slot(p, code, slot);
    cnor_byte_writer_put_u32(&p->code, (lh_u32_t) k);
}

static void
op_for_loop(program_t *p, lh_s32_t jump, lh_u8_t var, lh_u8_t bound)
{
    op_u32(p, cnor_op_for_loop, (lh_u32_t) jump);
    cnor_byte_writer_put_u8(&p->code, var);
    cnor_byte_writer_put_u8(&p->code, bound);
}

/* Every operand-form opcode maps back to its plain operator, every encoded
 * size matches its operand layout, and nothing else claims to be a form. */
CNOR_TEST(describes_every_operand_form_in_the_op_table)
{
    lh_u32_t code;
    int forms = 0;

    for (code = 0U; code < (lh_u32_t) cnor_op_count; ++code)
    {
        const cnor_op_info_t *info = cnor_op_get_info((cnor_op_t) code);
        cnor_operand_form_t form = cnor_operand_form_count;
        const cnor_op_t base = cnor_op_get_binary_base((cnor_op_t) code, &form);
        lh_u32_t size = 1U;
        lh_u32_t k;

        for (k = 0U; k < CNOR_OP_MAX_OPERANDS; ++k)
        {
            size += cnor_operand_get_size(info->operands[k]);
        }
        CHECK_EQ(size, info->size);
        CHECK(info->size <= CNOR_OP_MAX_SIZE);
        if (form != cnor_operand_form_stack)
        {
            forms++;
            CHECK_EQ(cnor_op_get_operand_form(base, form), (cnor_op_t) code);
        }
        else
        {
            CHECK_EQ(base, (cnor_op_t) code);
        }
    }
    /* Sixteen binary operators, three forms each. */
    CHECK_EQ(forms, 48);
    CHECK_EQ(cnor_op_get_operand_form(cnor_op_add, cnor_operand_form_stack), cnor_op_add);
    CHECK_EQ(cnor_op_get_operand_form(cnor_op_ge, cnor_operand_form_local_int), cnor_op_ge_li);
    CHECK_EQ(cnor_op_get_operand_form(cnor_op_neg, cnor_operand_form_local_local), cnor_op_count);
    CHECK_EQ(cnor_op_get_operand_form(cnor_op_add_ll, cnor_operand_form_local_int),
             cnor_op_count);
}

/* slot 0 counts 0..4, slot 2 sums it: 6 set-up steps, then per pass three
 * loads/adds, a store and one for_loop, then a load and the halt. */
static void
build_counted_loop(program_t *p, lh_s32_t to)
{
    p_init(p);
    op_u32(p, cnor_op_push_i32, 0U);
    op_slot(p, cnor_op_store_local, 0U);
    op_u32(p, cnor_op_push_i32, (lh_u32_t) to);
    op_slot(p, cnor_op_store_local, 1U);
    op_u32(p, cnor_op_push_i32, 0U);
    op_slot(p, cnor_op_store_local, 2U);
    /* 21: body. */
    op_slot(p, cnor_op_load_local, 2U);
    op_slot(p, cnor_op_load_local, 0U);
    op(p, cnor_op_add);
    op_slot(p, cnor_op_store_local, 2U);
    /* 28: back to 21, relative to the end at 35. */
    op_for_loop(p, -14, 0U, 1U);
    op_slot(p, cnor_op_load_local, 2U);
    op(p, cnor_op_halt);
    end_function(p, 0U, 0U, 3U, 2U);
    build(p, 0U);
}

static cnor_error_t
run_counted(program_t *p, cnor_step_count_t budget, cnor_step_count_t *steps, long long *sum)
{
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_error_t code;

    config.max_steps = budget;
    cnor_vm_init(&vm, &config);
    if (cnor_vm_load(&vm, p->image.data, p->image.size))
    {
        (void) cnor_vm_run(&vm);
    }
    code = vm_code(&vm);
    *steps = cnor_vm_get_steps(&vm);
    if (sum != lh_null)
    {
        const lh_value_t result = cnor_vm_get_result(&vm);

        *sum = sint_of(&result);
    }
    cnor_vm_deinit(&vm);
    return code;
}

CNOR_TEST(runs_a_counted_loop_one_step_per_pass)
{
    program_t p;
    cnor_step_count_t steps = 0U;
    cnor_step_count_t budget;
    long long sum = 0;

    build_counted_loop(&p, 4);
    CHECK_EQ(run_counted(&p, 0U, &steps, &sum), cnor_error_none);
    CHECK_EQ(sum, 0 + 1 + 2 + 3);
    CHECK_EQ(steps, 6U + 4U * 5U + 2U);
    /* Every budget short of that stops after exactly that many. */
    for (budget = 1U; budget < 28U; ++budget)
    {
        CHECK_EQ(run_counted(&p, budget, &steps, lh_null), cnor_error_step_limit);
        CHECK_EQ(steps, budget);
    }
    p_deinit(&p);
    /* The body runs once even when the bound is behind: for_loop only tests
     * after a pass, the compiler guards the entry. */
    build_counted_loop(&p, -3);
    CHECK_EQ(run_counted(&p, 0U, &steps, &sum), cnor_error_none);
    CHECK_EQ(sum, 0);
    CHECK_EQ(steps, 6U + 5U + 2U);
    p_deinit(&p);
}

/* for_loop leaves through its own dispatch whether it jumps back or falls
 * through; a budget that ends on either side stops right there. */
CNOR_TEST(stops_on_both_sides_of_a_for_loop)
{
    program_t p;
    cnor_step_count_t steps = 0U;
    cnor_step_count_t budget;
    lh_s32_t to;

    for (to = -1; to <= 2; ++to)
    {
        const cnor_step_count_t passes = to > 1 ? (cnor_step_count_t) to : 1U;
        const cnor_step_count_t total = 6U + passes * 5U + 2U;
        long long sum = 0;

        build_counted_loop(&p, to);
        CHECK_EQ(run_counted(&p, 0U, &steps, &sum), cnor_error_none);
        CHECK_EQ(steps, total);
        CHECK_EQ(sum, to > 1 ? (long long) to * (to - 1) / 2 : 0);
        CHECK_EQ(run_counted(&p, total, &steps, lh_null), cnor_error_none);
        CHECK_EQ(steps, total);
        for (budget = 1U; budget < total; ++budget)
        {
            CHECK_EQ(run_counted(&p, budget, &steps, lh_null), cnor_error_step_limit);
            CHECK_EQ(steps, budget);
        }
        p_deinit(&p);
    }
}

/* The steps left are counted down; with no limit the count is still exact
 * modulo 2^32. Entered by hand as cnor_vm_run would, a few steps short of the
 * wrap, the loop must run through it and report the count past it. */
CNOR_TEST(counts_unlimited_steps_across_the_wrap)
{
    program_t p;
    cnor_vm_t vm;
    cnor_vm_config_t config = cnor_vm_config_initializer();
    cnor_step_count_t start;

    build_counted_loop(&p, 4);
    config.max_steps = 0U;
    cnor_vm_init(&vm, &config);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    for (start = 0xFFFFFFFFU - 30U; start != 3U; ++start)
    {
        lh_value_t result;

        vm.stack.sp = 0U;
        vm.depth = 0U;
        vm.fp = 0U;
        vm.function = 0U;
        vm.ip = 0U;
        cnor_vm_stack_push_none(&vm.stack, 3U);
        vm.steps = start;
        vm.running = lh_bool_true;
        CHECK(cnor_vm_dispatch(&vm));
        CHECK_EQ(cnor_vm_get_steps(&vm), (cnor_step_count_t) (start + 28U));
        result = cnor_vm_get_result(&vm);
        CHECK_EQ(sint_of(&result), 0 + 1 + 2 + 3);
    }
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

/* What @p k reads back as from a push, the top-of-stack form, the local form
 * and a push fused with the `sub` after it; -1 when one of them differs from
 * the others or fails. */
static long long
s32_read_back(lh_s32_t k)
{
    program_t p;
    lh_value_t result;
    long long seen[4];
    int i;

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, (lh_u32_t) k);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    if (load_and_run(&p, &result) != cnor_error_none)
    {
        p_deinit(&p);
        return -1;
    }
    seen[0] = sint_of(&result);
    p_deinit(&p);

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 0U);
    op_u32(&p, cnor_op_add_i, (lh_u32_t) k);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    if (load_and_run(&p, &result) != cnor_error_none)
    {
        p_deinit(&p);
        return -1;
    }
    seen[1] = sint_of(&result);
    p_deinit(&p);

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 0U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_slot_s32(&p, cnor_op_add_li, 0U, k);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 1U);
    if (load_and_run(&p, &result) != cnor_error_none)
    {
        p_deinit(&p);
        return -1;
    }
    seen[2] = sint_of(&result);
    p_deinit(&p);

    /* 0 - (0 - k): the push of `0 - k` is a plain push, the second one fuses. */
    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 0U);
    op_u32(&p, cnor_op_push_i32, (lh_u32_t) k);
    op(&p, cnor_op_sub);
    op_u32(&p, cnor_op_push_i32, 0U);
    op(&p, cnor_op_swap);
    op(&p, cnor_op_sub);
    op(&p, cnor_op_halt);
    build(&p, 2U);
    if (load_and_run(&p, &result) != cnor_error_none)
    {
        p_deinit(&p);
        return -1;
    }
    seen[3] = sint_of(&result);
    p_deinit(&p);

    for (i = 1; i < 4; ++i)
    {
        if (seen[i] != seen[0])
        {
            return -1;
        }
    }
    return seen[0];
}

/* Immediates are two's complement s32 in the code, decoded without
 * implementation-defined conversions; both ends and the values next to 0. */
CNOR_TEST(decodes_signed_immediates_at_both_ends)
{
    CHECK_EQ(s32_read_back((lh_s32_t) (-2147483647 - 1)), -2147483648LL);
    CHECK_EQ(s32_read_back((lh_s32_t) -2147483647), -2147483647LL);
    CHECK_EQ(s32_read_back((lh_s32_t) -65536), -65536LL);
    CHECK_EQ(s32_read_back((lh_s32_t) -1), -1LL);
    CHECK_EQ(s32_read_back((lh_s32_t) 0), 0LL);
    CHECK_EQ(s32_read_back((lh_s32_t) 1), 1LL);
    CHECK_EQ(s32_read_back((lh_s32_t) 2147483647), 2147483647LL);
}

/* A non-number in the loop variable fails like `add` does, at the for_loop. */
CNOR_TEST(fails_a_for_loop_over_text_like_its_add)
{
    program_t p;
    cnor_vm_t vm;

    p_init(&p);
    add_const(&p, "a");
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_u32(&p, cnor_op_push_i32, 4U);
    op_slot(&p, cnor_op_store_local, 1U);
    /* 12: a loop on itself. */
    op_for_loop(&p, -7, 0U, 1U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 2U, 1U);
    build(&p, 0U);
    vm_open(&vm);
    REQUIRE(cnor_vm_load(&vm, p.image.data, p.image.size));
    CHECK(!cnor_vm_run(&vm));
    CHECK_EQ(vm_code(&vm), cnor_error_type);
    CHECK_EQ(cnor_diag_get_offset(cnor_vm_get_diag(&vm)), 12U);
    cnor_vm_deinit(&vm);
    p_deinit(&p);
}

/* Operand forms on non-integers go the slow way and agree with the stack
 * forms: text joins and compares, a bad mix is a type error. */
CNOR_TEST(runs_operand_forms_on_other_kinds)
{
    program_t p;
    lh_value_t result;

    p_init(&p);
    add_const(&p, "ab");
    add_const(&p, "cd");
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op_slot(&p, cnor_op_store_local, 0U);
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 1U);
    op_slot(&p, cnor_op_store_local, 1U);
    op_slots(&p, cnor_op_add_ll, 0U, 1U);
    op(&p, cnor_op_len);
    op_slots(&p, cnor_op_ne_ll, 0U, 1U);
    op(&p, cnor_op_pop);
    op_u32(&p, cnor_op_add_i, 3U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 2U, 2U);
    build(&p, 0U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_none);
    CHECK_EQ(sint_of(&result), 7);
    p_deinit(&p);

    p_init(&p);
    add_const(&p, "ab");
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_slot_s32(&p, cnor_op_lt_li, 0U, 1);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_type);
    p_deinit(&p);

    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 5U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_slot_s32(&p, cnor_op_mod_li, 0U, 0);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_div_by_zero);
    p_deinit(&p);
}

CNOR_TEST(rejects_slot_operands_outside_the_frame)
{
    program_t p;

    p_init(&p);
    op_slots(&p, cnor_op_add_ll, 0U, 1U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);

    p_init(&p);
    op_slot_s32(&p, cnor_op_lt_li, 3U, 7);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 3U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);

    p_init(&p);
    op_for_loop(&p, -7, 0U, 9U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 2U, 0U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);

    /* In range they load. */
    p_init(&p);
    op_for_loop(&p, -7, 0U, 1U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 2U, 0U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_none);
    p_deinit(&p);
}

CNOR_TEST(rejects_a_for_loop_into_the_middle_of_an_instruction)
{
    program_t p;

    p_init(&p);
    op_for_loop(&p, -3, 0U, 1U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 2U, 0U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_jump);
    p_deinit(&p);
}

CNOR_TEST(rejects_a_stack_form_without_its_operand)
{
    program_t p;

    p_init(&p);
    op_u32(&p, cnor_op_add_i, 1U);
    op(&p, cnor_op_halt);
    build(&p, 1U);
    CHECK_EQ(load_error(&p), cnor_error_bad_stack);
    p_deinit(&p);
}

/* -- switch ---------------------------------------------------------------- */

static void
op_switch(program_t *p, lh_u8_t slot, lh_s32_t first, lh_u8_t count)
{
    op_slot_s32(p, cnor_op_switch, slot, first);
    cnor_byte_writer_put_u8(&p->code, count);
}

/*
 * Slot 0 holds the subject @p subject_op pushes; `switch` on 10..12 picks a
 * result 0, 1 or 2, or 99 after the table:
 *
 *     S: switch 0, 10, 3
 *     S+7: jmp e0; jmp e1; jmp e2
 *     S+22: push 99; halt
 *     S+28: e0: push 0; halt   S+34: e1: push 1; halt   S+40: e2: push 2; halt
 */
static void
build_switch(program_t *p, cnor_op_t subject_op, lh_u32_t subject)
{
    p_init(p);
    if (subject_op == cnor_op_push_const)
    {
        add_const(p, "11");
        op(p, cnor_op_push_const);
        cnor_byte_writer_put_u16(&p->code, 0U);
    }
    else
    {
        op_u32(p, subject_op, subject);
    }
    op_slot(p, cnor_op_store_local, 0U);
    op_switch(p, 0U, 10, 3U);
    op_u32(p, cnor_op_jmp, 16U);
    op_u32(p, cnor_op_jmp, 17U);
    op_u32(p, cnor_op_jmp, 18U);
    op_u32(p, cnor_op_push_i32, 99U);
    op(p, cnor_op_halt);
    op_u32(p, cnor_op_push_i32, 0U);
    op(p, cnor_op_halt);
    op_u32(p, cnor_op_push_i32, 1U);
    op(p, cnor_op_halt);
    op_u32(p, cnor_op_push_i32, 2U);
    op(p, cnor_op_halt);
    end_function(p, 0U, 0U, 1U, 1U);
    build(p, 0U);
}

/* A signed subject indexes the table; others compare by `==`, so an unsigned
 * 11 finds the same entry and text none. The entry's jump is a step of its
 * own, also when it runs fused with the switch. */
CNOR_TEST(switches_on_an_integer_by_its_value)
{
    static const struct
    {
        cnor_op_t op;
        lh_u32_t subject;
        long long result;
        cnor_step_count_t steps;
    } cases[] = {
        {cnor_op_push_i32, 10U, 0, 6U},
        {cnor_op_push_i32, 11U, 1, 6U},
        {cnor_op_push_i32, 12U, 2, 6U},
        {cnor_op_push_i32, 13U, 99, 5U},
        {cnor_op_push_i32, 9U, 99, 5U},
        {cnor_op_push_i32, 0x80000000U, 99, 5U},
        {cnor_op_push_u32, 11U, 1, 6U},
        {cnor_op_push_u32, 0xFFFFFFFFU, 99, 5U},
        {cnor_op_push_const, 0U, 99, 5U},
    };
    size_t k;

    for (k = 0U; k < sizeof(cases) / sizeof(cases[0]); ++k)
    {
        program_t p;
        cnor_step_count_t steps = 0U;
        cnor_step_count_t budget;
        long long result = 0;

        build_switch(&p, cases[k].op, cases[k].subject);
        CHECK_EQ(run_counted(&p, 0U, &steps, &result), cnor_error_none);
        CHECK_EQ(result, cases[k].result);
        CHECK_EQ(steps, cases[k].steps);
        for (budget = 1U; budget < cases[k].steps; ++budget)
        {
            CHECK_EQ(run_counted(&p, budget, &steps, lh_null), cnor_error_step_limit);
            CHECK_EQ(steps, budget);
        }
        p_deinit(&p);
    }
}

CNOR_TEST(rejects_a_switch_without_its_table)
{
    program_t p;

    /* No entries. */
    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_switch(&p, 0U, 0, 0U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);

    /* An entry that is not a jmp. */
    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_switch(&p, 0U, 0, 2U);
    op_u32(&p, cnor_op_jmp, 5U);
    op_u32(&p, cnor_op_push_i32, 1U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);

    /* More entries than the function holds. */
    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_switch(&p, 0U, 0, 3U);
    op_u32(&p, cnor_op_jmp, (lh_u32_t) -19);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);

    /* Nothing after the table for the values without an entry. */
    p_init(&p);
    op_u32(&p, cnor_op_push_i32, 1U);
    op_slot(&p, cnor_op_store_local, 0U);
    op_switch(&p, 0U, 0, 1U);
    op_u32(&p, cnor_op_jmp, (lh_u32_t) -19);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_jump);
    p_deinit(&p);

    /* A subject outside the frame. */
    p_init(&p);
    op_switch(&p, 1U, 0, 1U);
    op_u32(&p, cnor_op_jmp, 0U);
    op(&p, cnor_op_halt);
    end_function(&p, 0U, 0U, 1U, 1U);
    build(&p, 0U);
    CHECK_EQ(load_error(&p), cnor_error_bad_operand);
    p_deinit(&p);
}

/* `swap` and `dup` move whole values: a string keeps its length next to an
 * integer, whichever way round they are. */
CNOR_TEST(swaps_and_duplicates_whole_values)
{
    program_t p;
    lh_value_t result;

    p_init(&p);
    add_const(&p, "abc");
    add_const(&p, "hello");
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 0U);
    op_u32(&p, cnor_op_push_i32, 7U);
    op(&p, cnor_op_swap);
    op(&p, cnor_op_len);
    op(&p, cnor_op_add);
    op(&p, cnor_op_push_const);
    cnor_byte_writer_put_u16(&p.code, 1U);
    op(&p, cnor_op_dup);
    op(&p, cnor_op_len);
    op(&p, cnor_op_swap);
    op(&p, cnor_op_len);
    op(&p, cnor_op_add);
    op(&p, cnor_op_add);
    op(&p, cnor_op_halt);
    build(&p, 3U);
    CHECK_EQ(load_and_run(&p, &result), cnor_error_none);
    CHECK_EQ(sint_of(&result), 7 + 3 + 5 + 5);
    p_deinit(&p);
}

static const cnor_test_case_t m_cases[] = {
    CNOR_TEST_CASE(refuses_a_buffer_shorter_than_a_header),
    CNOR_TEST_CASE(refuses_a_buffer_without_the_signature),
    CNOR_TEST_CASE(refuses_a_version_it_does_not_speak),
    CNOR_TEST_CASE(refuses_a_stack_beyond_what_the_host_offers),
    CNOR_TEST_CASE(reads_the_sections_of_a_well_formed_image),
    CNOR_TEST_CASE(runs_a_program_that_only_stops),
    CNOR_TEST_CASE(adds_two_numbers),
    CNOR_TEST_CASE(mixes_signed_and_unsigned_without_wrapping),
    CNOR_TEST_CASE(reports_division_by_zero_instead_of_trapping),
    CNOR_TEST_CASE(refuses_a_byte_that_is_not_an_opcode_at_load),
    CNOR_TEST_CASE(refuses_an_array_whose_elements_leave_the_frame),
    CNOR_TEST_CASE(refuses_an_instruction_that_runs_past_the_code),
    CNOR_TEST_CASE(refuses_more_pushes_than_the_declared_depth),
    CNOR_TEST_CASE(overflows_the_stack_it_was_given),
    CNOR_TEST_CASE(joins_two_strings_and_measures_them),
    CNOR_TEST_CASE(calls_a_native_and_keeps_its_result),
    CNOR_TEST_CASE(refuses_a_native_it_was_not_given_at_load),
    CNOR_TEST_CASE(refuses_a_call_with_the_wrong_arity),
    CNOR_TEST_CASE(keeps_globals_across_runs_until_a_reset),
    CNOR_TEST_CASE(maps_a_failure_back_to_the_source_line),
    CNOR_TEST_CASE(maps_a_failure_back_to_its_file),
    CNOR_TEST_CASE(rejects_a_function_of_a_file_the_image_does_not_have),
    CNOR_TEST_CASE(answers_no_line_for_a_program_built_without_them),
    CNOR_TEST_CASE(refuses_to_run_before_anything_is_loaded),
    CNOR_TEST_CASE(reuses_one_machine_for_two_programs),
    CNOR_TEST_CASE(stops_a_jump_to_itself_and_unwinds),
    CNOR_TEST_CASE(runs_unlimited_with_a_zero_budget),
    CNOR_TEST_CASE(rejects_a_jump_into_the_middle_of_an_instruction),
    CNOR_TEST_CASE(rejects_a_jump_out_of_its_function),
    CNOR_TEST_CASE(rejects_paths_that_disagree_on_the_stack),
    CNOR_TEST_CASE(rejects_an_operand_stack_underflow),
    CNOR_TEST_CASE(rejects_code_that_falls_off_its_function),
    CNOR_TEST_CASE(rejects_operands_outside_their_tables),
    CNOR_TEST_CASE(rejects_a_call_with_the_wrong_argument_count),
    CNOR_TEST_CASE(rejects_functions_that_do_not_tile_the_code),
    CNOR_TEST_CASE(describes_every_operand_form_in_the_op_table),
    CNOR_TEST_CASE(runs_a_counted_loop_one_step_per_pass),
    CNOR_TEST_CASE(stops_on_both_sides_of_a_for_loop),
    CNOR_TEST_CASE(counts_unlimited_steps_across_the_wrap),
    CNOR_TEST_CASE(decodes_signed_immediates_at_both_ends),
    CNOR_TEST_CASE(fails_a_for_loop_over_text_like_its_add),
    CNOR_TEST_CASE(runs_operand_forms_on_other_kinds),
    CNOR_TEST_CASE(rejects_slot_operands_outside_the_frame),
    CNOR_TEST_CASE(rejects_a_for_loop_into_the_middle_of_an_instruction),
    CNOR_TEST_CASE(rejects_a_stack_form_without_its_operand),
    CNOR_TEST_CASE(switches_on_an_integer_by_its_value),
    CNOR_TEST_CASE(rejects_a_switch_without_its_table),
    CNOR_TEST_CASE(swaps_and_duplicates_whole_values),
};

int
main(int argc, char **argv)
{
    return cnor_test_main("image_vm", m_cases, CNOR_TEST_COUNT(m_cases), argc, argv);
}
