#include <cnor/compiler/internal.h>
#include <lh/hash/fnv.h>
#include <lh/null.h>
#include <lh/runtime/allocator.h>

/* -- the top-level index --------------------------------------------------- */

#define CNOR_NAME_INDEX_MIN_CAPACITY 64U

static void
cnor_name_index_init(struct cnor_name_index *self)
{
    self->entries = lh_null;
    self->capacity = 0U;
    self->count = 0U;
    self->usable = lh_bool_true;
}

static void
cnor_name_index_deinit(struct cnor_name_index *self)
{
    if (self->entries != lh_null)
    {
        lh_runtime_allocator_free(self->entries);
    }
    cnor_name_index_init(self);
}

/* The hash of a name alone; a lookup in several files computes it once. */
static lh_u32_t
cnor_name_index_hash_name(lh_str_view_t name)
{
    return lh_hash_fnv1a_32(lh_str_view_get_data(&name), lh_str_view_get_size(&name));
}

/* A name's hash (::cnor_name_index_hash_name) mixed with its file: one name
 * may be declared once per file. */
static lh_u32_t
cnor_name_index_mix(lh_u32_t name_hash, cnor_module_index_t module)
{
    return name_hash ^ ((lh_u32_t) module * 0x9E3779B1U);
}

static lh_u32_t
cnor_name_index_hash(lh_str_view_t name, cnor_module_index_t module)
{
    return cnor_name_index_mix(cnor_name_index_hash_name(name), module);
}

/* The entry holding @p name of @p module, or the free entry where it would go. */
static struct cnor_name_entry *
cnor_name_index_slot(const struct cnor_name_index *self, lh_str_view_t name,
                     cnor_module_index_t module, lh_u32_t hash)
{
    const lh_u32_t mask = self->capacity - 1U;
    lh_u32_t at = hash & mask;

    for (;;)
    {
        struct cnor_name_entry *entry = &self->entries[at];

        if (lh_str_view_get_size(&entry->name) == 0U
            || (entry->hash == hash && entry->module == module
                && lh_str_view_equals(&entry->name, &name, lh_bool_false)))
        {
            return entry;
        }
        at = (at + 1U) & mask;
    }
}

/* Double the capacity and re-insert; on failure the index is dropped. */
static void
cnor_name_index_grow(struct cnor_name_index *self)
{
    const lh_u32_t capacity =
        self->capacity == 0U ? CNOR_NAME_INDEX_MIN_CAPACITY : self->capacity * 2U;
    struct cnor_name_entry *old = self->entries;
    const lh_u32_t old_capacity = self->capacity;
    lh_u32_t i;

    self->entries = (struct cnor_name_entry *) lh_runtime_allocator_alloc(
        (lh_usize_t) capacity * sizeof(struct cnor_name_entry));
    if (self->entries == lh_null)
    {
        self->entries = old;
        cnor_name_index_deinit(self);
        self->usable = lh_bool_false;
        return;
    }
    self->capacity = capacity;
    for (i = 0U; i < capacity; ++i)
    {
        self->entries[i].name = lh_str_view_empty();
    }
    for (i = 0U; i < old_capacity; ++i)
    {
        if (lh_str_view_get_size(&old[i].name) != 0U)
        {
            *cnor_name_index_slot(self, old[i].name, old[i].module, old[i].hash) = old[i];
        }
    }
    if (old != lh_null)
    {
        lh_runtime_allocator_free(old);
    }
}

/* Names are unique per file across the three tables, so a key is added once. */
static void
cnor_name_index_add(struct cnor_name_index *self, struct cnor_decl_key key,
                    cnor_binding_kind_t kind, lh_u16_t index)
{
    const lh_u32_t hash = cnor_name_index_hash(key.name, key.module);
    struct cnor_name_entry *entry;

    if (!self->usable || lh_str_view_get_size(&key.name) == 0U)
    {
        return;
    }
    if ((self->count + 1U) * 2U > self->capacity)
    {
        cnor_name_index_grow(self);
        if (!self->usable)
        {
            return;
        }
    }
    entry = cnor_name_index_slot(self, key.name, key.module, hash);
    entry->name = key.name;
    entry->hash = hash;
    entry->module = key.module;
    entry->kind = kind;
    entry->index = index;
    self->count++;
}

/* -- lifetime -------------------------------------------------------------- */

void
cnor_scope_init(cnor_scope_t *self)
{
    cnor_name_index_init(&self->top);
    lh_array_init(&self->functions, sizeof(struct cnor_function_decl));
    lh_array_init(&self->natives, sizeof(struct cnor_native_decl));
    lh_array_init(&self->imports, sizeof(lh_str_view_t));
    lh_array_init(&self->globals, sizeof(struct cnor_global_decl));
    lh_array_init(&self->locals, sizeof(struct cnor_local_decl));
    self->next_slot = 0U;
    self->slot_count = 0U;
}

void
cnor_scope_deinit(cnor_scope_t *self)
{
    cnor_name_index_deinit(&self->top);
    lh_array_deinit(&self->functions);
    lh_array_deinit(&self->natives);
    lh_array_deinit(&self->imports);
    lh_array_deinit(&self->globals);
    lh_array_deinit(&self->locals);
}

/* -- lookups --------------------------------------------------------------- */

/* Sizes first: most names that differ differ in length, and that test needs
 * no call. */
static lh_bool_t
cnor_scope_name_is(const lh_str_view_t *a, const lh_str_view_t *b)
{
    return (a->size > 0U && a->size == b->size && lh_str_view_equals(a, b, lh_bool_false))
               ? lh_bool_true
               : lh_bool_false;
}

/* The local named @p name, from the innermost, or ::lh_null. */
static const struct cnor_local_decl *
cnor_scope_find_local(const lh_array_t *locals, lh_str_view_t name)
{
    const struct cnor_local_decl *first =
        (const struct cnor_local_decl *) lh_array_get_begin(locals);
    const struct cnor_local_decl *local = first + lh_array_get_size(locals);

    while (local != first)
    {
        if (cnor_scope_name_is(&(--local)->name, &name))
        {
            return local;
        }
    }
    return lh_null;
}

/* Index of the declaration keyed (@p name, @p module) in a table whose entries
 * start with a struct cnor_decl_key. */
static lh_u16_t
cnor_scope_find_key(const lh_array_t *table, lh_str_view_t name, cnor_module_index_t module)
{
    lh_usize_t i = lh_array_get_size(table);

    while (i > 0U)
    {
        const struct cnor_decl_key *key =
            (const struct cnor_decl_key *) lh_array_get_ptr(table, --i);

        if (key->module == module && cnor_scope_name_is(&key->name, &name))
        {
            return (lh_u16_t) i;
        }
    }
    return CNOR_INDEX_NONE;
}

struct cnor_function_decl *
cnor_scope_function_at(cnor_scope_t *self, cnor_function_index_t index)
{
    return (struct cnor_function_decl *) lh_array_get_ptr(&self->functions, index);
}

cnor_count_t
cnor_scope_function_count(const cnor_scope_t *self)
{
    return (cnor_count_t) lh_array_get_size(&self->functions);
}

const struct cnor_native_decl *
cnor_scope_native_at(const cnor_scope_t *self, lh_u16_t index)
{
    return (const struct cnor_native_decl *) lh_array_get_ptr(&self->natives, index);
}

struct cnor_global_decl *
cnor_scope_global_at(cnor_scope_t *self, lh_u32_t index)
{
    return (struct cnor_global_decl *) lh_array_get_ptr(&self->globals, index);
}

cnor_count_t
cnor_scope_global_count(const cnor_scope_t *self)
{
    return (cnor_count_t) lh_array_get_size(&self->globals);
}

/*
 * The function, native or global named @p name in file @p module: through the
 * index, or by scanning the tables when the index ran out of memory.
 * @p name_hash is ::cnor_name_index_hash_name of @p name.
 * @return ::cnor_binding_none when there is none.
 */
static cnor_binding_kind_t
cnor_scope_find_top_hashed(const cnor_scope_t *self, lh_str_view_t name, lh_u32_t name_hash,
                           cnor_module_index_t module, lh_u16_t *index)
{
    if (self->top.usable)
    {
        const struct cnor_name_entry *entry;

        if (self->top.capacity == 0U || lh_str_view_get_size(&name) == 0U)
        {
            return cnor_binding_none;
        }
        entry = cnor_name_index_slot(&self->top, name, module,
                                     cnor_name_index_mix(name_hash, module));
        if (lh_str_view_get_size(&entry->name) == 0U)
        {
            return cnor_binding_none;
        }
        *index = entry->index;
        return entry->kind;
    }
    if ((*index = cnor_scope_find_key(&self->globals, name, module)) != CNOR_INDEX_NONE)
    {
        return cnor_binding_global;
    }
    if ((*index = cnor_scope_find_key(&self->functions, name, module)) != CNOR_INDEX_NONE)
    {
        return cnor_binding_function;
    }
    if ((*index = cnor_scope_find_key(&self->natives, name, module)) != CNOR_INDEX_NONE)
    {
        return cnor_binding_native;
    }
    return cnor_binding_none;
}

static cnor_binding_kind_t
cnor_scope_find_top(const cnor_scope_t *self, lh_str_view_t name, cnor_module_index_t module,
                    lh_u16_t *index)
{
    return cnor_scope_find_top_hashed(self, name, cnor_name_index_hash_name(name), module, index);
}

static lh_bool_t
cnor_scope_is_top_level_name(cnor_compiler_t *self, lh_str_view_t name)
{
    lh_u16_t index = CNOR_INDEX_NONE;

    return cnor_scope_find_top(&self->scope, name, self->module, &index) != cnor_binding_none
               ? lh_bool_true
               : lh_bool_false;
}

static struct cnor_decl_key
cnor_scope_key(lh_str_view_t name, cnor_module_index_t module)
{
    struct cnor_decl_key key;

    key.name = name;
    key.module = module;
    return key;
}

/* -- declarations ---------------------------------------------------------- */

static lh_bool_t
cnor_scope_push_function(cnor_compiler_t *self, const cnor_node_t *node,
                         struct cnor_decl_key key)
{
    struct cnor_function_decl decl;

    if (cnor_scope_function_count(&self->scope) > CNOR_INDEX_MAX)
    {
        return cnor_compile_fail(self, node, cnor_error_too_many_functions);
    }
    decl.key = key;
    decl.node = node;
    decl.image.entry = 0U;
    decl.image.size = 0U;
    decl.image.param_count = node != lh_null ? node->data.fn.param_count : 0U;
    decl.image.slot_count = 0U;
    decl.image.max_stack = 0U;
    decl.image.file = key.module;
    decl.globals_needed = 0U;
    cnor_name_index_add(&self->scope.top, key, cnor_binding_function,
                        (lh_u16_t) lh_array_push_back(&self->scope.functions, &decl));
    return lh_bool_true;
}

lh_bool_t
cnor_scope_add_module_code(cnor_compiler_t *self, cnor_module_index_t module)
{
    return cnor_scope_push_function(self, lh_null, cnor_scope_key(lh_str_view_empty(), module));
}

lh_bool_t
cnor_scope_add_function(cnor_compiler_t *self, const cnor_node_t *node)
{
    if (cnor_scope_is_top_level_name(self, node->name))
    {
        return cnor_compile_fail_name(self, node, cnor_error_duplicate_name);
    }
    return cnor_scope_push_function(self, node, cnor_scope_key(node->name, self->module));
}

lh_bool_t
cnor_scope_add_native(cnor_compiler_t *self, const cnor_node_t *node)
{
    struct cnor_native_decl decl;

    if (cnor_scope_is_top_level_name(self, node->name))
    {
        return cnor_compile_fail_name(self, node, cnor_error_duplicate_name);
    }
    if (lh_array_get_size(&self->scope.natives) > CNOR_INDEX_MAX)
    {
        return cnor_compile_fail(self, node, cnor_error_too_many_functions);
    }
    decl.key = cnor_scope_key(node->name, self->module);
    decl.node = node;
    decl.arity_min = node->data.fn.param_count;
    decl.arity_max = node->data.fn.param_count;
    decl.import = CNOR_INDEX_NONE;
    cnor_name_index_add(&self->scope.top, decl.key, cnor_binding_native,
                        (lh_u16_t) lh_array_push_back(&self->scope.natives, &decl));
    return lh_bool_true;
}

lh_bool_t
cnor_scope_add_global(cnor_compiler_t *self, const cnor_node_t *node)
{
    struct cnor_global_decl decl;

    if (cnor_scope_is_top_level_name(self, node->name))
    {
        return cnor_compile_fail_name(self, node, cnor_error_duplicate_name);
    }
    if (lh_array_get_size(&self->scope.globals) > CNOR_INDEX_MAX)
    {
        return cnor_compile_fail(self, node, cnor_error_too_many_globals);
    }
    decl.key = cnor_scope_key(node->name, self->module);
    decl.is_mutable = node->data.let.is_mutable;
    decl.type = cnor_type_of_annotation(node->data.let.type);
    cnor_name_index_add(&self->scope.top, decl.key, cnor_binding_global,
                        (lh_u16_t) lh_array_push_back(&self->scope.globals, &decl));
    return lh_bool_true;
}

/* -- locals ---------------------------------------------------------------- */

void
cnor_scope_begin_function(cnor_scope_t *self)
{
    lh_array_clear(&self->locals);
    self->next_slot = 0U;
    self->slot_count = 0U;
}

cnor_scope_mark_t
cnor_scope_enter_block(const cnor_scope_t *self)
{
    cnor_scope_mark_t mark;

    mark.locals = lh_array_get_size(&self->locals);
    mark.next_slot = self->next_slot;
    return mark;
}

void
cnor_scope_leave_block(cnor_scope_t *self, cnor_scope_mark_t mark)
{
    /* Slots of a closed block are reused by the next one; the frame keeps the
     * largest count any point of the function needed. */
    lh_array_resize(&self->locals, mark.locals);
    self->next_slot = mark.next_slot;
}

lh_bool_t
cnor_scope_add_local(cnor_compiler_t *self, const cnor_node_t *node, lh_str_view_t name,
                     lh_bool_t is_mutable, cnor_static_type_t type, cnor_slot_t *slot)
{
    cnor_scope_t *scope = &self->scope;
    struct cnor_local_decl decl;

    if (scope->next_slot >= CNOR_SLOT_MAX)
    {
        return cnor_compile_fail(self, node, cnor_error_too_many_locals);
    }
    decl.name = name;
    decl.slot = scope->next_slot++;
    decl.is_mutable = is_mutable;
    decl.type = type;
    lh_array_push_back(&scope->locals, &decl);
    if (scope->next_slot > scope->slot_count)
    {
        scope->slot_count = scope->next_slot;
    }
    *slot = decl.slot;
    return lh_bool_true;
}

/* -- resolution ------------------------------------------------------------ */

/* The host's native @p name (hashed: @p name_hash), declared on first use
 * under ::CNOR_MODULE_HOST. */
static lh_u16_t
cnor_scope_find_host_native(cnor_compiler_t *self, lh_str_view_t name, lh_u32_t name_hash)
{
    const cnor_native_t *host;
    struct cnor_native_decl decl;
    lh_u16_t index = CNOR_INDEX_NONE;

    if (cnor_scope_find_top_hashed(&self->scope, name, name_hash, CNOR_MODULE_HOST, &index)
        == cnor_binding_native)
    {
        return index;
    }
    host = cnor_native_table_find(self->config->natives, name);
    if (host == lh_null || lh_array_get_size(&self->scope.natives) > CNOR_INDEX_MAX)
    {
        return CNOR_INDEX_NONE;
    }
    decl.key = cnor_scope_key(host->name, CNOR_MODULE_HOST);
    decl.node = lh_null;
    decl.arity_min = host->arity_min;
    decl.arity_max = host->arity_max;
    decl.import = CNOR_INDEX_NONE;
    index = (lh_u16_t) lh_array_push_back(&self->scope.natives, &decl);
    cnor_name_index_add(&self->scope.top, decl.key, cnor_binding_native, index);
    return index;
}

static cnor_binding_t
cnor_binding_make(cnor_binding_kind_t kind, lh_u16_t index, lh_bool_t is_mutable,
                  cnor_static_type_t type)
{
    cnor_binding_t binding;

    binding.kind = kind;
    binding.index = index;
    binding.is_mutable = is_mutable;
    binding.type = type;
    return binding;
}

/* A top-level name of the current file, else of exactly one file it imports:
 * the file's own names shadow imported ones, two imported files defining it
 * make it ambiguous. Imports are not passed on: what a file imports is not
 * visible to the files importing it. */
static cnor_binding_kind_t
cnor_scope_find_visible(cnor_compiler_t *self, lh_str_view_t name, lh_u32_t name_hash,
                        lh_u16_t *index)
{
    const cnor_binding_kind_t own =
        cnor_scope_find_top_hashed(&self->scope, name, name_hash, self->module, index);
    const struct cnor_module *module;
    const cnor_module_index_t *imports;
    cnor_binding_kind_t found = cnor_binding_none;
    lh_usize_t count;
    lh_usize_t i;

    if (own != cnor_binding_none)
    {
        return own;
    }
    module = cnor_module_at(self, self->module);
    imports = (const cnor_module_index_t *) lh_array_get_begin(&module->imports);
    count = lh_array_get_size(&module->imports);
    for (i = 0U; found == cnor_binding_none && i < count; ++i)
    {
        lh_usize_t j;

        found = cnor_scope_find_top_hashed(&self->scope, name, name_hash, imports[i], index);
        for (j = i + 1U; found != cnor_binding_none && j < count; ++j)
        {
            lh_u16_t other = CNOR_INDEX_NONE;

            if (cnor_scope_find_top_hashed(&self->scope, name, name_hash, imports[j], &other)
                != cnor_binding_none)
            {
                return cnor_binding_ambiguous;
            }
        }
    }
    return found;
}

cnor_binding_t
cnor_scope_resolve(cnor_compiler_t *self, lh_str_view_t name)
{
    cnor_scope_t *scope = &self->scope;
    const struct cnor_local_decl *local = cnor_scope_find_local(&scope->locals, name);
    lh_u16_t index = CNOR_INDEX_NONE;
    lh_u32_t name_hash;
    cnor_binding_kind_t kind;

    if (local != lh_null)
    {
        return cnor_binding_make(cnor_binding_local, local->slot, local->is_mutable, local->type);
    }
    name_hash = cnor_name_index_hash_name(name);
    kind = cnor_scope_find_visible(self, name, name_hash, &index);
    if (kind == cnor_binding_global)
    {
        const struct cnor_global_decl *global = cnor_scope_global_at(scope, index);

        return cnor_binding_make(cnor_binding_global, index, global->is_mutable, global->type);
    }
    if (kind == cnor_binding_none)
    {
        index = cnor_scope_find_host_native(self, name, name_hash);
        kind = index != CNOR_INDEX_NONE ? cnor_binding_native : cnor_binding_none;
    }
    return cnor_binding_make(kind, index, lh_bool_false, cnor_static_any);
}

/* Natives are bound by name, so two files declaring one share its import. */
lh_u16_t
cnor_scope_import_native(cnor_compiler_t *self, lh_u16_t native)
{
    struct cnor_native_decl *decl =
        (struct cnor_native_decl *) lh_array_get_ptr(&self->scope.natives, native);
    const lh_usize_t count = lh_array_get_size(&self->scope.imports);
    lh_usize_t i;

    for (i = 0U; decl->import == CNOR_INDEX_NONE && i < count; ++i)
    {
        const lh_str_view_t *name =
            (const lh_str_view_t *) lh_array_get_ptr(&self->scope.imports, i);

        if (lh_str_view_equals(name, &decl->key.name, lh_bool_false))
        {
            decl->import = (lh_u16_t) i;
        }
    }
    if (decl->import == CNOR_INDEX_NONE)
    {
        decl->import = (lh_u16_t) lh_array_push_back(&self->scope.imports, &decl->key.name);
    }
    return decl->import;
}
