#include <cnor/image/view.h>
#include <cnor/vm/internal.h>
#include <lh/memory.h>
#include <lh/runtime/allocator.h>

/* -- release --------------------------------------------------------------- */

static void
cnor_vm_free(lh_ptr block)
{
    if (block != lh_null)
    {
        lh_runtime_allocator_free(block);
    }
}

/* Forget the loaded program, keep the configuration and the storage: the
 * stack and the string pool are sized by the configuration, not by the
 * program, so a host that loads another script keeps them. */
static void
cnor_vm_unload(cnor_vm_t *self)
{
    cnor_vm_free(self->image);
    cnor_vm_free(self->functions);
    cnor_vm_free(self->constants);
    cnor_vm_free((lh_ptr) self->natives);
    cnor_vm_free(self->frames);
    cnor_vm_free(self->tries);
    cnor_vm_free(self->trace_nodes);
    self->trace_nodes = lh_null;
    self->trace_head = CNOR_VM_TRACE_NONE;
    self->trace_valid = lh_bool_false;
    self->trace_truncated = lh_bool_false;
    self->trace_at = 0U;
    self->image = lh_null;
    self->code = lh_null;
    self->code_size = 0U;
    self->functions = lh_null;
    self->function_count = 0U;
    self->constants = lh_null;
    self->const_count = 0U;
    self->natives = lh_null;
    self->native_count = 0U;
    self->line_count = 0U;
    self->file_count = 0U;
    self->frames = lh_null;
    self->tries = lh_null;
    self->try_count = 0U;
    self->depth = 0U;
    self->fp = 0U;
    self->function = 0U;
    self->ip = 0U;
    self->running = lh_bool_false;
    self->steps = 0U;
    self->result = lh_value_make_none();
}

/* -- lifetime -------------------------------------------------------------- */

void
cnor_vm_init(cnor_vm_t *self, const cnor_vm_config_t *config)
{
    const cnor_vm_config_t defaults = cnor_vm_config_initializer();

    self->config = config != lh_null ? *config : defaults;
    self->image = lh_null;
    self->functions = lh_null;
    self->constants = lh_null;
    self->natives = lh_null;
    self->frames = lh_null;
    self->tries = lh_null;
    self->trace_nodes = lh_null;
    self->try_count = 0U;
    cnor_vm_stack_init(&self->stack);
    cnor_str_pool_init(&self->str_pool);
    cnor_vm_unload(self);
    cnor_diag_init(&self->diag);
}

void
cnor_vm_deinit(cnor_vm_t *self)
{
    cnor_vm_unload(self);
    cnor_vm_stack_deinit(&self->stack);
    cnor_str_pool_deinit(&self->str_pool);
}

void
cnor_vm_reset(cnor_vm_t *self)
{
    cnor_vm_trace_clear(self);
    self->stack.sp = 0U;
    cnor_vm_stack_push_none(&self->stack, self->image != lh_null ? self->header.globals_count : 0U);
    cnor_str_pool_rewind(&self->str_pool);
    self->depth = 0U;
    self->try_count = 0U;
    self->running = lh_bool_false;
    self->result = lh_value_make_none();
}

lh_bool_t
cnor_vm_load(cnor_vm_t *self, const lh_byte_t *data, lh_u32_t size)
{
    cnor_vm_unload(self);
    cnor_diag_init(&self->diag);
    if (data == lh_null || size == 0U)
    {
        return cnor_diag_set(&self->diag, cnor_error_too_small, 0U, 0U);
    }
    /* The machine keeps its own copy: strings and names point into it. */
    self->image = (lh_byte_t *) lh_runtime_allocator_alloc(size);
    if (self->image == lh_null)
    {
        return cnor_diag_set(&self->diag, cnor_error_no_memory, 0U, 0U);
    }
    lh_memory_copy(self->image, size, data, size);
    if (!cnor_vm_load_image(self, size))
    {
        cnor_vm_unload(self);
        return lh_bool_false;
    }
    cnor_vm_reset(self);
    return lh_bool_true;
}

/* -- running --------------------------------------------------------------- */

/* Main's frame sits right above the globals. */
static lh_bool_t
cnor_vm_enter_main(cnor_vm_t *self)
{
    const cnor_image_function_t *main = &self->functions[0];

    self->stack.sp = self->header.globals_count;
    self->depth = 0U;
    self->fp = self->stack.sp;
    self->function = 0U;
    self->ip = main->entry;
    if (!cnor_vm_stack_fits(&self->stack, (cnor_stack_slots_t) main->slot_count + main->max_stack))
    {
        return cnor_diag_set(&self->diag, cnor_error_stack_overflow, 0U, main->entry);
    }
    cnor_vm_stack_push_none(&self->stack, main->slot_count);
    return lh_bool_true;
}

lh_bool_t
cnor_vm_run(cnor_vm_t *self)
{
    lh_bool_t ok;

    cnor_diag_init(&self->diag);
    cnor_vm_trace_clear(self);
    self->result = lh_value_make_none();
    self->steps = 0U;
    if (self->image == lh_null)
    {
        return cnor_diag_set(&self->diag, cnor_error_not_loaded, 0U, 0U);
    }
    if (!cnor_vm_enter_main(self))
    {
        return lh_bool_false;
    }
    self->running = lh_bool_true;
    ok = cnor_vm_dispatch(self);
    /* Preserve the saved trace while releasing handlers left by a failure. */
    cnor_vm_trace_finish(self, ok);
    /* Unwind whatever a failure left so globals are all that remains. */
    self->stack.sp = self->header.globals_count;
    self->depth = 0U;
    self->try_count = 0U;
    return ok;
}

/* -- queries --------------------------------------------------------------- */

/* The loaded image was fully checked, so a view can be rebuilt from its parts. */
static cnor_image_view_t cnor_vm_view(const cnor_vm_t *self);

/* The function of that name, or ::CNOR_INDEX_NONE when the image has none. */
static cnor_function_index_t
cnor_vm_function_of_name(const cnor_vm_t *self, lh_str_view_t name)
{
    cnor_image_view_t view;
    cnor_count_t i;

    if (self->image == lh_null)
    {
        return CNOR_INDEX_NONE;
    }
    view = cnor_vm_view(self);
    for (i = 0U; i < view.function_name_count; ++i)
    {
        const lh_str_view_t candidate =
            cnor_image_view_function_name(&view, (cnor_function_index_t) i);

        if (lh_str_view_equals(&candidate, &name, lh_bool_false))
        {
            return (cnor_function_index_t) i;
        }
    }
    return CNOR_INDEX_NONE;
}

/* The frame of the function: its arguments are its parameters, and the rest
 * of its slots are reserved as none. */
static lh_bool_t
cnor_vm_enter_function(cnor_vm_t *self, cnor_function_index_t index, const lh_value_t *args,
                       cnor_argc_t argc)
{
    const cnor_image_function_t *const callee = &self->functions[index];
    const cnor_stack_slots_t reserve =
        (cnor_stack_slots_t) (callee->slot_count - callee->param_count);
    cnor_argc_t i;

    if (argc != callee->param_count)
    {
        return cnor_diag_set(&self->diag, cnor_error_wrong_arg_count, 0U, callee->entry);
    }
    if (!cnor_vm_stack_fits(&self->stack,
                            (cnor_stack_slots_t) callee->slot_count + callee->max_stack))
    {
        return cnor_diag_set(&self->diag, cnor_error_stack_overflow, 0U, callee->entry);
    }
    self->stack.sp = self->header.globals_count;
    for (i = 0U; i < argc; ++i)
    {
        cnor_vm_stack_push(&self->stack, args[i]);
    }
    self->fp = self->stack.sp - callee->param_count;
    self->depth = 0U;
    self->function = index;
    self->ip = callee->entry;
    cnor_vm_stack_push_none(&self->stack, reserve);
    return lh_bool_true;
}

lh_bool_t
cnor_vm_call(cnor_vm_t *self, lh_str_view_t name, const lh_value_t *args, cnor_argc_t argc,
             lh_value_t *out)
{
    const cnor_function_index_t index = cnor_vm_function_of_name(self, name);
    lh_bool_t ok;

    cnor_diag_init(&self->diag);
    cnor_vm_trace_clear(self);
    self->result = lh_value_make_none();
    self->steps = 0U;
    if (self->image == lh_null)
    {
        return cnor_diag_set(&self->diag, cnor_error_not_loaded, 0U, 0U);
    }
    if (index == CNOR_INDEX_NONE)
    {
        return cnor_diag_set(&self->diag, cnor_error_unknown_function, 0U, 0U);
    }
    if (!cnor_vm_enter_function(self, index, args, argc))
    {
        return lh_bool_false;
    }
    self->running = lh_bool_true;
    ok = cnor_vm_dispatch(self);
    /* Preserve the saved trace while releasing handlers left by a failure. */
    cnor_vm_trace_finish(self, ok);
    /* Unwind whatever a failure left so globals are all that remains. */
    self->stack.sp = self->header.globals_count;
    self->depth = 0U;
    self->try_count = 0U;
    if (out != lh_null)
    {
        *out = self->result;
    }
    return ok;
}

const cnor_diag_t *
cnor_vm_get_diag(const cnor_vm_t *self)
{
    return &self->diag;
}

cnor_step_count_t
cnor_vm_get_steps(const cnor_vm_t *self)
{
    return self->steps;
}

lh_value_t
cnor_vm_get_result(const cnor_vm_t *self)
{
    return self->result;
}

const lh_value_t *
cnor_vm_get_global(const cnor_vm_t *self, cnor_global_index_t index)
{
    if (self->image == lh_null || index >= self->header.globals_count)
    {
        return lh_null;
    }
    return &self->stack.values[index];
}

/* The loaded image was fully checked, so a view can be rebuilt from its parts. */
static cnor_image_view_t
cnor_vm_view(const cnor_vm_t *self)
{
    cnor_image_view_t view;

    view.header = self->header;
    view.data = self->image;
    view.code = self->code;
    view.code_size = self->code_size;
    view.const_count = self->const_count;
    view.function_count = self->function_count;
    view.native_count = self->native_count;
    view.line_count = self->line_count;
    view.file_count = self->file_count;
    view.function_name_count = self->function_name_count;
    return view;
}

lh_str_view_t
cnor_vm_get_file(const cnor_vm_t *self)
{
    cnor_image_view_t view;

    if (self->image == lh_null)
    {
        return lh_str_view_empty();
    }
    view = cnor_vm_view(self);
    return cnor_image_view_file(&view, 0U);
}

lh_str_view_t
cnor_vm_get_file_at(const cnor_vm_t *self, cnor_code_addr_t addr)
{
    cnor_image_view_t view;

    if (self->image == lh_null || addr >= self->code_size)
    {
        return lh_str_view_empty();
    }
    view = cnor_vm_view(self);
    return cnor_image_view_file(
        &view, self->functions[cnor_image_view_function_of(&view, addr)].file);
}

cnor_line_t
cnor_vm_get_line_at(const cnor_vm_t *self, cnor_code_addr_t addr)
{
    cnor_image_view_t view;

    if (self->image == lh_null)
    {
        return 0U;
    }
    view = cnor_vm_view(self);
    return cnor_image_view_line_at(&view, addr);
}

/* -- failures -------------------------------------------------------------- */

lh_bool_t
cnor_vm_fail(cnor_vm_t *vm, const cnor_insn_t *insn, cnor_error_t code)
{
    cnor_vm_trace_capture(vm, insn->at);
    vm->running = lh_bool_false;
    (void) cnor_diag_set(&vm->diag, code, cnor_vm_get_line_at(vm, insn->at), insn->at);
    cnor_diag_set_file(&vm->diag, cnor_vm_get_file_at(vm, insn->at));
    return lh_bool_false;
}

lh_bool_t
cnor_vm_get_trace_frame(const cnor_vm_t *self, cnor_count_t index, cnor_vm_trace_frame_t *out)
{
    cnor_count_t head;
    cnor_code_addr_t at;
    cnor_image_view_t view;
    if (out == lh_null || index >= cnor_vm_get_trace_count(self)) return lh_bool_false;
    head = self->trace_head;
    if (head == CNOR_VM_TRACE_NONE) at = self->trace_at;
    else
    {
        while (index-- > 0U) head = self->trace_nodes[head].next;
        at = self->trace_nodes[head].at;
    }
    view = cnor_vm_view(self);
    out->offset = at;
    out->function = cnor_image_view_function_of(&view, at);
    out->name = cnor_image_view_function_name(&view, out->function);
    out->file = cnor_vm_get_file_at(self, at);
    out->line = cnor_vm_get_line_at(self, at);
    return lh_bool_true;
}
