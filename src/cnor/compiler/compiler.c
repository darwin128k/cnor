#include <cnor/arena.h>
#include <cnor/compiler/internal.h>
#include <cnor/image/writer.h>
#include <cnor/parser.h>
#include <lh/bit/endian.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>
#include <lh/util/return.h>

/* -- diagnostics ----------------------------------------------------------- */

lh_bool_t
cnor_compile_fail(cnor_compiler_t *self, const cnor_node_t *node, cnor_error_t code)
{
    return cnor_diag_set(self->diag, code, node != lh_null ? node->line : self->emit.line, 0U);
}

lh_bool_t
cnor_compile_fail_name(cnor_compiler_t *self, const cnor_node_t *node, cnor_error_t code)
{
    return cnor_diag_set_name(self->diag, code, node->line, node->name);
}

lh_bool_t
cnor_compile_fail_text(cnor_compiler_t *self, cnor_line_t line, cnor_error_t code,
                       lh_str_view_t name)
{
    return cnor_diag_set_name(self->diag, code, line, name);
}

lh_bool_t
cnor_compile_fail_binding(cnor_compiler_t *self, const cnor_node_t *node,
                          const cnor_binding_t *binding)
{
    return cnor_compile_fail_name(self, node,
                                  binding->kind == cnor_binding_ambiguous
                                      ? cnor_error_ambiguous_name
                                      : cnor_error_unknown_name);
}

/* -- state ----------------------------------------------------------------- */

static void
cnor_compiler_init(cnor_compiler_t *self, const cnor_compiler_config_t *config, cnor_diag_t *diag)
{
    self->config = config;
    self->diag = diag;
    cnor_emit_init(&self->emit, config->emit_lines);
    cnor_scope_init(&self->scope);
    cnor_constant_pool_init(&self->constants);
    cnor_modules_init(self);
    cnor_order_init(self);
    self->loop_depth = 0U;
    self->function = 0U;
    self->result_slot = 0U;
    self->block_depth = 0U;
    self->type = cnor_static_any;
    self->returns = cnor_static_any;
}

static void
cnor_compiler_deinit(cnor_compiler_t *self)
{
    cnor_order_deinit(self);
    cnor_modules_deinit(self);
    cnor_emit_deinit(&self->emit);
    cnor_scope_deinit(&self->scope);
    cnor_constant_pool_deinit(&self->constants);
}

/* -- image ----------------------------------------------------------------- */

/* Largest frame of any function: its slots plus its operand stack. */
static cnor_stack_slots_t
cnor_compiler_stack_slots(cnor_compiler_t *self)
{
    const cnor_count_t count = cnor_scope_function_count(&self->scope);
    cnor_stack_slots_t largest = 0U;
    cnor_count_t i;

    for (i = 0U; i < count; ++i)
    {
        const cnor_image_function_t *image =
            &cnor_scope_function_at(&self->scope, (cnor_function_index_t) i)->image;
        const cnor_stack_slots_t frame = (cnor_stack_slots_t) image->slot_count + image->max_stack;

        largest = frame > largest ? frame : largest;
    }
    return largest;
}

/*
 * The main file's code (function 0) is compiled after the files it imports
 * (see function.c), but the image keeps functions in index order: move its
 * code, and its line records, to the front. Jumps are relative and calls name
 * function indices, so the code itself needs no patching.
 */
static lh_bool_t
cnor_compiler_relocate(cnor_compiler_t *self)
{
    cnor_image_function_t *entry = &cnor_scope_function_at(&self->scope, 0U)->image;
    const cnor_code_addr_t start = entry->entry;
    const cnor_code_addr_t end = start + entry->size;
    const lh_byte_t *code = cnor_byte_writer_get_data(&self->emit.code);
    const lh_byte_t *lines = cnor_byte_writer_get_data(&self->emit.lines);
    const cnor_count_t count = cnor_scope_function_count(&self->scope);
    cnor_byte_writer_t moved_code;
    cnor_byte_writer_t moved_lines;
    lh_u32_t pass;
    cnor_count_t i;

    if (start == 0U)
    {
        return lh_bool_true;
    }
    cnor_byte_writer_init(&moved_code);
    cnor_byte_writer_put_bytes(&moved_code, code + start, entry->size);
    cnor_byte_writer_put_bytes(&moved_code, code, start);
    cnor_byte_writer_put_bytes(&moved_code, code + end,
                               cnor_byte_writer_get_size(&self->emit.code) - end);
    /* Records are in address order: the entry's first, then the ones before
     * it, then the rest. */
    cnor_byte_writer_init(&moved_lines);
    for (pass = 0U; pass < 3U; ++pass)
    {
        for (i = 0U; i < self->emit.line_count; ++i)
        {
            const lh_u32_t addr = lh_bit_unpack_le32(lines + i * 8U);
            const lh_u32_t which = addr < start ? 1U : (addr < end ? 0U : 2U);

            if (which == pass)
            {
                cnor_byte_writer_put_u32(&moved_lines,
                                         which == 0U ? addr - start
                                                     : (which == 1U ? addr + entry->size : addr));
                cnor_byte_writer_put_u32(&moved_lines, lh_bit_unpack_le32(lines + i * 8U + 4U));
            }
        }
    }
    for (i = 1U; i < count; ++i)
    {
        cnor_image_function_t *image =
            &cnor_scope_function_at(&self->scope, (cnor_function_index_t) i)->image;

        if (image->entry < start)
        {
            image->entry += entry->size;
        }
    }
    entry->entry = 0U;
    cnor_byte_writer_deinit(&self->emit.code);
    cnor_byte_writer_deinit(&self->emit.lines);
    self->emit.code = moved_code;
    self->emit.lines = moved_lines;
    return lh_bool_true;
}

static lh_bool_t
cnor_compiler_write_image(cnor_compiler_t *self, cnor_image_t *out)
{
    const cnor_count_t function_count = cnor_scope_function_count(&self->scope);
    const cnor_count_t module_count = cnor_module_count(self);
    cnor_image_function_t *functions = (cnor_image_function_t *) lh_runtime_allocator_alloc(
        function_count * sizeof(cnor_image_function_t));
    lh_str_view_t *files =
        (lh_str_view_t *) lh_runtime_allocator_alloc(module_count * sizeof(lh_str_view_t));
    cnor_count_t file_count = module_count;
    cnor_image_parts_t parts;
    cnor_count_t i;
    lh_bool_t ok;

    if (functions == lh_null || files == lh_null)
    {
        lh_runtime_allocator_free(functions);
        lh_runtime_allocator_free(files);
        return cnor_diag_set(self->diag, cnor_error_no_memory, 0U, 0U);
    }
    for (i = 0U; i < function_count; ++i)
    {
        functions[i] = cnor_scope_function_at(&self->scope, (cnor_function_index_t) i)->image;
    }
    for (i = 0U; i < module_count; ++i)
    {
        files[i] = cnor_module_at(self, (cnor_module_index_t) i)->name;
    }
    if (module_count == 1U && lh_str_view_get_size(&files[0]) == 0U)
    {
        /* A single unnamed file: no table, as before imports existed. */
        file_count = 0U;
    }
    parts.code = &self->emit.code;
    parts.consts = &self->constants.records;
    parts.const_count = cnor_constant_pool_count(&self->constants);
    parts.functions = functions;
    parts.function_count = function_count;
    parts.natives = (const lh_str_view_t *) lh_array_get_begin(&self->scope.imports);
    parts.native_count = (cnor_count_t) lh_array_get_size(&self->scope.imports);
    parts.lines = &self->emit.lines;
    parts.line_count = self->emit.line_count;
    parts.files = files;
    parts.file_count = file_count;
    parts.globals_count = (lh_u32_t) lh_array_get_size(&self->scope.globals);
    parts.stack_slots = cnor_compiler_stack_slots(self);
    ok = cnor_image_write(&parts, out);
    lh_runtime_allocator_free(functions);
    lh_runtime_allocator_free(files);
    return ok ? lh_bool_true : cnor_diag_set(self->diag, cnor_error_no_memory, 0U, 0U);
}

/* -- public ---------------------------------------------------------------- */

lh_bool_t
cnor_compiler_compile(const cnor_node_t *program, const cnor_compiler_config_t *config,
                      cnor_image_t *out, cnor_diag_t *diag)
{
    const cnor_compiler_config_t defaults = cnor_compiler_config_initializer();
    cnor_diag_t local_diag;
    cnor_compiler_t compiler;
    lh_bool_t ok;

    lh_return_ifn(out, lh_bool_false);
    cnor_image_init(out);
    if (diag == lh_null)
    {
        diag = &local_diag;
    }
    cnor_diag_init(diag);
    if (program == lh_null)
    {
        return cnor_diag_set(diag, cnor_error_empty_block, 0U, 0U);
    }
    cnor_compiler_init(&compiler, config != lh_null ? config : &defaults, diag);
    ok = cnor_modules_load(&compiler, program) && cnor_compile_declare(&compiler)
         && cnor_compile_bodies(&compiler) && cnor_compiler_relocate(&compiler)
         && cnor_compiler_write_image(&compiler, out);
    if (!ok && cnor_module_count(&compiler) > 0U)
    {
        const lh_str_view_t file = cnor_diag_get_file(diag);

        if (lh_str_view_get_size(&file) == 0U)
        {
            /* The file being compiled when it failed. */
            cnor_diag_set_file(diag, cnor_module_at(&compiler, compiler.module)->name);
        }
    }
    cnor_compiler_deinit(&compiler);
    if (!ok)
    {
        cnor_image_deinit(out);
    }
    return ok;
}

lh_bool_t
cnor_compiler_compile_source(lh_str_view_t src, const cnor_compiler_config_t *config,
                             cnor_image_t *out, cnor_diag_t *diag)
{
    cnor_arena_t arena;
    cnor_parser_t parser;
    const cnor_node_t *program;
    lh_bool_t ok;

    lh_return_ifn(out, lh_bool_false);
    cnor_arena_init(&arena, 0U);
    cnor_parser_init(&parser, src, &arena);
    program = cnor_parser_parse(&parser);
    if (program == lh_null)
    {
        cnor_image_init(out);
        if (diag != lh_null)
        {
            *diag = *cnor_parser_get_diag(&parser);
            cnor_diag_set_file(diag, config != lh_null ? config->file : lh_str_view_empty());
        }
        cnor_arena_deinit(&arena);
        return lh_bool_false;
    }
    ok = cnor_compiler_compile(program, config, out, diag);
    cnor_arena_deinit(&arena);
    return ok;
}
