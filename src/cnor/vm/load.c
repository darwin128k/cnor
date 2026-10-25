#include <cnor/image/verify.h>
#include <cnor/image/view.h>
#include <cnor/vm/internal.h>
#include <lh/runtime/allocator.h>
#if LH_REAL_ENABLED
#include <lh/str/parse/real.h>
#endif

/* -- tables ---------------------------------------------------------------- */

static lh_ptr
cnor_vm_alloc_table(cnor_count_t count, lh_usize_t size)
{
    /* Never ask the allocator for zero bytes. */
    return lh_runtime_allocator_alloc((count > 0U ? count : 1U) * size);
}

static lh_bool_t
cnor_vm_load_functions(cnor_vm_t *vm, const cnor_image_view_t *view)
{
    cnor_count_t i;

    vm->functions = (cnor_image_function_t *) cnor_vm_alloc_table(view->function_count,
                                                                  sizeof(cnor_image_function_t));
    if (vm->functions == lh_null)
    {
        return cnor_diag_set(&vm->diag, cnor_error_no_memory, 0U, 0U);
    }
    for (i = 0U; i < view->function_count; ++i)
    {
        vm->functions[i] = cnor_image_view_function_at(view, (cnor_function_index_t) i);
    }
    vm->function_count = view->function_count;
    return lh_bool_true;
}

static lh_bool_t
cnor_vm_decode_const(cnor_vm_t *vm, cnor_const_kind_t kind, lh_str_view_t bytes,
                     lh_value_t *out)
{
    if (kind == cnor_const_str)
    {
        *out = lh_value_make_str(bytes);
        return lh_bool_true;
    }
#if LH_REAL_ENABLED
    {
        lh_real_t real;

        if (lh_str_ptr_parse_real(lh_str_view_get_data(&bytes), lh_str_view_get_size(&bytes),
                                  &real))
        {
            *out = lh_value_make_real(real);
            return lh_bool_true;
        }
        return cnor_diag_set(&vm->diag, cnor_error_bad_section, 0U, 0U);
    }
#else
    (void) bytes;
    (void) out;
    return cnor_diag_set(&vm->diag, cnor_error_no_reals, 0U, 0U);
#endif
}

/* Constants are decoded once; a real is stored as its decimal text. */
static lh_bool_t
cnor_vm_load_constants(cnor_vm_t *vm, const cnor_image_view_t *view)
{
    cnor_byte_reader_t reader;
    cnor_count_t i;

    vm->constants =
        (lh_value_t *) cnor_vm_alloc_table(view->const_count, sizeof(lh_value_t));
    if (vm->constants == lh_null)
    {
        return cnor_diag_set(&vm->diag, cnor_error_no_memory, 0U, 0U);
    }
    cnor_image_view_records(view, cnor_image_section_consts, &reader);
    for (i = 0U; i < view->const_count; ++i)
    {
        cnor_const_kind_t kind;
        lh_str_view_t bytes;

        cnor_image_const_read(&reader, &kind, &bytes);
        if (!cnor_vm_decode_const(vm, kind, bytes, &vm->constants[i]))
        {
            return lh_bool_false;
        }
    }
    vm->const_count = view->const_count;
    return lh_bool_true;
}

/* Every import is bound by name now, so a missing native fails the load, not
 * the call that happens to reach it first. */
static lh_bool_t
cnor_vm_bind_natives(cnor_vm_t *vm, const cnor_image_view_t *view)
{
    cnor_byte_reader_t reader;
    cnor_count_t i;

    vm->natives = (const cnor_native_t **) cnor_vm_alloc_table(view->native_count,
                                                               sizeof(const cnor_native_t *));
    if (vm->natives == lh_null)
    {
        return cnor_diag_set(&vm->diag, cnor_error_no_memory, 0U, 0U);
    }
    cnor_image_view_records(view, cnor_image_section_natives, &reader);
    for (i = 0U; i < view->native_count; ++i)
    {
        lh_str_view_t name;

        cnor_image_native_read(&reader, &name);
        vm->natives[i] = cnor_native_table_find(vm->config.natives, name);
        if (vm->natives[i] == lh_null)
        {
            return cnor_diag_set_name(&vm->diag, cnor_error_no_such_native, 0U, name);
        }
    }
    vm->native_count = view->native_count;
    return lh_bool_true;
}

/* -- storage --------------------------------------------------------------- */

static lh_bool_t
cnor_vm_allocate(cnor_vm_t *vm)
{
    const lh_ullong_t needed =
        (lh_ullong_t) vm->header.globals_count + (lh_ullong_t) vm->header.stack_slots;

    if (needed > vm->config.stack_slots)
    {
        return cnor_diag_set(&vm->diag, cnor_error_stack_too_big, 0U, 0U);
    }
    /* The stack and the string pool are sized by the configuration, not by the
     * program, and a host that loads another script keeps them: freeing and
     * reserving megabytes again costs more than the whole load. */
    if (vm->stack.capacity < vm->config.stack_slots
        && !cnor_vm_stack_reserve(&vm->stack, vm->config.stack_slots))
    {
        return cnor_diag_set(&vm->diag, cnor_error_no_memory, 0U, 0U);
    }
    if (vm->str_pool.size < vm->config.str_pool_size
        && !cnor_str_pool_reserve(&vm->str_pool, vm->config.str_pool_size))
    {
        return cnor_diag_set(&vm->diag, cnor_error_no_memory, 0U, 0U);
    }
    vm->frames = (cnor_vm_frame_t *) cnor_vm_alloc_table(vm->config.max_depth,
                                                         sizeof(cnor_vm_frame_t));
    if (vm->frames == lh_null)
    {
        return cnor_diag_set(&vm->diag, cnor_error_no_memory, 0U, 0U);
    }
    return lh_bool_true;
}

/* -- load ------------------------------------------------------------------ */

lh_bool_t
cnor_vm_load_image(cnor_vm_t *vm, lh_u32_t size)
{
    cnor_image_view_t view;
    const cnor_error_t error = cnor_image_view_open(&view, vm->image, size);

    if (error != cnor_error_none)
    {
        return cnor_diag_set(&vm->diag, error, 0U, 0U);
    }
    if (!cnor_image_verify(&view, &vm->diag))
    {
        return lh_bool_false;
    }
    vm->header = view.header;
    vm->code = view.code;
    vm->code_size = view.code_size;
    vm->line_count = view.line_count;
    vm->file_count = view.file_count;
    return cnor_vm_load_functions(vm, &view) && cnor_vm_load_constants(vm, &view)
           && cnor_vm_bind_natives(vm, &view) && cnor_vm_allocate(vm);
}
