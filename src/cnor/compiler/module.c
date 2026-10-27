#include <cnor/compiler/internal.h>
#include <cnor/parser.h>
#include <cnor/name.h>
#include <lh/null.h>

/*
 * The files of a compilation. The main file is 0; each `import` is resolved,
 * loaded through the host's loader and parsed once, depth first. A file met
 * again while its own imports are still being followed closes a cycle. When a
 * file's imports are all done it joins `order`, so every file comes after the
 * files it imports: its globals are declared, and its top-level code runs, in
 * that order. The main file always finishes last, so it is left out of
 * `order` (a single file needs no allocation for it).
 */

/* -- table ----------------------------------------------------------------- */

void
cnor_modules_init(cnor_compiler_t *self)
{
    cnor_arena_init(&self->arena, 0U);
    lh_array_init(&self->modules, sizeof(struct cnor_module));
    lh_array_init(&self->order, sizeof(cnor_module_index_t));
    self->module = 0U;
}

void
cnor_modules_deinit(cnor_compiler_t *self)
{
    const cnor_count_t count = cnor_module_count(self);
    cnor_count_t i;

    for (i = 0U; i < count; ++i)
    {
        lh_array_deinit(&cnor_module_at(self, (cnor_module_index_t) i)->imports);
    }
    lh_array_deinit(&self->modules);
    lh_array_deinit(&self->order);
    cnor_arena_deinit(&self->arena);
}

struct cnor_module *
cnor_module_at(cnor_compiler_t *self, cnor_module_index_t index)
{
    return (struct cnor_module *) lh_array_get_ptr(&self->modules, index);
}

cnor_count_t
cnor_module_count(const cnor_compiler_t *self)
{
    return (cnor_count_t) lh_array_get_size(&self->modules);
}

/* The file whose resolved path is @p path, or ::CNOR_INDEX_NONE. */
static cnor_module_index_t
cnor_module_find(cnor_compiler_t *self, lh_str_view_t path)
{
    const cnor_count_t count = cnor_module_count(self);
    cnor_count_t i;

    for (i = 0U; i < count; ++i)
    {
        if (lh_str_view_equals(&cnor_module_at(self, (cnor_module_index_t) i)->path, &path,
                               lh_bool_false))
        {
            return (cnor_module_index_t) i;
        }
    }
    return CNOR_INDEX_NONE;
}

static lh_bool_t
cnor_module_add(cnor_compiler_t *self, lh_str_view_t path, lh_str_view_t name,
                const cnor_node_t *program, cnor_module_index_t *index)
{
    struct cnor_module module;

    if (cnor_module_count(self) > CNOR_INDEX_MAX)
    {
        return cnor_diag_set(self->diag, cnor_error_too_many_files, 0U, 0U);
    }
    module.path = path;
    module.name = name;
    module.program = program;
    lh_array_init(&module.imports, sizeof(struct cnor_module_import));
    module.function = 0U;
    module.first_global = 0U;
    module.state = cnor_module_loading;
    *index = (cnor_module_index_t) lh_array_push_back(&self->modules, &module);
    return lh_bool_true;
}

/* -- loading --------------------------------------------------------------- */

/* Parse a loaded file into the compiler's arena; the text is given back as
 * soon as it is parsed, since the tree keeps copies of what it needs. */
static const cnor_node_t *
cnor_module_parse(cnor_compiler_t *self, lh_str_view_t path, cnor_source_t *source)
{
    const cnor_compiler_config_t *config = self->config;
    cnor_parser_t parser;
    const cnor_node_t *program;

    cnor_parser_init(&parser, source->text, &self->arena);
    program = cnor_parser_parse(&parser);
    if (program == lh_null)
    {
        *self->diag = *cnor_parser_get_diag(&parser);
        cnor_diag_set_file(self->diag, path);
    }
    if (config->release != lh_null)
    {
        config->release(config->load_ctx, source);
    }
    return program;
}

/*
 * Load @p path as a new file. @return ::cnor_error_no_such_module when the
 * loader has nothing there, ::cnor_error_none with @p index set when it is
 * loaded and parsed, any other code when the compilation must stop (the
 * diagnostic is set, except for the loader's own codes).
 */
static cnor_error_t
cnor_module_try(cnor_compiler_t *self, lh_str_view_t path, cnor_module_index_t *index)
{
    const cnor_compiler_config_t *config = self->config;
    cnor_source_t source;
    const cnor_node_t *program;
    cnor_error_t error;

    if (config->load == lh_null)
    {
        return cnor_error_no_such_module;
    }
    source.text = lh_str_view_empty();
    source.handle = lh_null;
    error = config->load(config->load_ctx, path, &source);
    if (error != cnor_error_none)
    {
        return error;
    }
    program = cnor_module_parse(self, path, &source);
    if (program == lh_null)
    {
        return cnor_error_unexpected_token;
    }
    return cnor_module_add(self, path, path, program, index) ? cnor_error_none
                                                              : cnor_error_too_many_files;
}

/* The candidate @p at of @p node's path in file @p from: its directory, then
 * each search path. @return ::lh_bool_false past the last candidate. */
static lh_bool_t
cnor_module_candidate(cnor_compiler_t *self, const struct cnor_module *from,
                      const cnor_node_t *node, lh_u32_t at, lh_str_view_t *path)
{
    lh_str_view_t dir;

    if (at == 0U)
    {
        dir = cnor_path_dir(from->path);
    }
    else if (cnor_path_is_absolute(node->name) || at > self->config->search_path_count)
    {
        return lh_bool_false;
    }
    else
    {
        dir = self->config->search_paths[at - 1U];
    }
    if (!cnor_path_join(&self->arena, dir, node->name, path))
    {
        *path = lh_str_view_empty();
    }
    return lh_bool_true;
}

/* The file @p node in @p from names, already known or loaded now. */
static lh_bool_t
cnor_module_resolve(cnor_compiler_t *self, cnor_module_index_t from, const cnor_node_t *node,
                    cnor_module_index_t *index, lh_bool_t *is_new)
{
    lh_str_view_t path;
    lh_u32_t at;

    for (at = 0U; cnor_module_candidate(self, cnor_module_at(self, from), node, at, &path); ++at)
    {
        cnor_error_t error;

        if (lh_str_view_get_size(&path) == 0U)
        {
            return cnor_compile_fail(self, node, cnor_error_no_memory);
        }
        *index = cnor_module_find(self, path);
        *is_new = *index == CNOR_INDEX_NONE ? lh_bool_true : lh_bool_false;
        if (!*is_new)
        {
            return lh_bool_true;
        }
        error = cnor_module_try(self, path, index);
        if (error == cnor_error_none)
        {
            return lh_bool_true;
        }
        if (error != cnor_error_no_such_module)
        {
            /* A parse failure set the diagnostic in the imported file already. */
            return !cnor_diag_is_ok(self->diag)
                       ? lh_bool_false
                       : cnor_compile_fail_text(self, node->line, error, node->name);
        }
    }
    return cnor_compile_fail_text(self, node->line, cnor_error_no_such_module, node->name);
}

/* -- walking --------------------------------------------------------------- */

/* Follow the imports of @p index, depth first, then append it to the order. */
static lh_bool_t
cnor_module_visit(cnor_compiler_t *self, cnor_module_index_t index, lh_u32_t depth)
{
    const cnor_node_t *node;

    for (node = cnor_module_at(self, index)->program;
         node != lh_null && node->kind == cnor_node_import; node = node->next)
    {
        cnor_module_index_t imported = CNOR_INDEX_NONE;
        lh_bool_t is_new = lh_bool_false;

        self->module = index;
        if (node->data.import_.alias.size != 0U)
        {
            const lh_str_view_t alias = node->data.import_.alias;
            const cnor_node_t *decl;
            if (cnor_namespace_module(self, alias) != CNOR_INDEX_NONE
                || cnor_parser_type_of(alias) != cnor_type_none)
                return cnor_compile_fail_text(self, node->line, cnor_error_duplicate_name, alias);
            for (decl = cnor_module_at(self, index)->program; decl != lh_null; decl = decl->next)
                if ((decl->kind == cnor_node_fn || decl->kind == cnor_node_native_fn
                     || decl->kind == cnor_node_let || decl->kind == cnor_node_struct
                     || decl->kind == cnor_node_enum)
                    && lh_str_view_equals(&decl->name, &alias, lh_bool_false))
                    return cnor_compile_fail_text(self, node->line, cnor_error_duplicate_name, alias);
        }
        if (!cnor_module_resolve(self, index, node, &imported, &is_new))
        {
            return lh_bool_false;
        }
        if (!is_new && cnor_module_at(self, imported)->state == cnor_module_loading)
        {
            return cnor_compile_fail_text(self, node->line, cnor_error_import_cycle, node->name);
        }
        {
            struct cnor_module_import edge;
            edge.module = imported;
            edge.alias = node->data.import_.alias;
            lh_array_push_back(&cnor_module_at(self, index)->imports, &edge);
        }
        if (is_new && depth == CNOR_COMPILER_MAX_IMPORT_DEPTH)
        {
            return cnor_compile_fail_text(self, node->line, cnor_error_imports_too_deep,
                                          node->name);
        }
        if (is_new && !cnor_module_visit(self, imported, depth + 1U))
        {
            return lh_bool_false;
        }
    }
    cnor_module_at(self, index)->state = cnor_module_loaded;
    if (index != 0U)
    {
        lh_array_push_back(&self->order, &index);
    }
    return lh_bool_true;
}

lh_bool_t
cnor_modules_load(cnor_compiler_t *self, const cnor_node_t *program)
{
    const lh_str_view_t name = self->config->file;
    lh_str_view_t path = lh_str_view_empty();
    cnor_module_index_t main = 0U;

    if (lh_str_view_get_size(&name) > 0U
        && !cnor_path_join(&self->arena, lh_str_view_empty(), name, &path))
    {
        return cnor_diag_set(self->diag, cnor_error_no_memory, 0U, 0U);
    }
    return cnor_module_add(self, path, name, program, &main) && cnor_module_visit(self, main, 0U);
}

/* Namespace edges retain source-level aliases; the image only needs resolved slots. */
cnor_module_index_t
cnor_namespace_module(cnor_compiler_t *self, lh_str_view_t alias)
{
    const struct cnor_module *module = cnor_module_at(self, self->module);
    lh_usize_t i;
    if (alias.size == 0U) return CNOR_INDEX_NONE;
    for (i = 0U; i < lh_array_get_size(&module->imports); ++i)
    {
        const struct cnor_module_import *edge = lh_array_get_ptr(&module->imports, i);
        if (edge->alias.size != 0U && lh_str_view_equals(&edge->alias, &alias, lh_bool_false))
            return edge->module;
    }
    return CNOR_INDEX_NONE;
}

lh_bool_t
cnor_namespace_split(cnor_compiler_t *self, lh_str_view_t name,
                       cnor_module_index_t *module, lh_str_view_t *member)
{
    const lh_char_t *text = name.begin;
    lh_usize_t i;
    for (i = 0U; i < name.size; ++i)
    {
        lh_str_view_t alias;
        if (text[i] != '.') continue;
        lh_str_init_by_size(&alias, text, i);
        lh_str_init_by_size(member, text + i + 1U, name.size - i - 1U);
        *module = cnor_namespace_module(self, alias);
        return lh_bool_true;
    }
    return lh_bool_false;
}

lh_bool_t
cnor_namespace_node(cnor_compiler_t *self, const cnor_node_t *node, cnor_node_t *out)
{
    const cnor_node_t *base;
    if (node->kind != cnor_node_member) return lh_bool_false;
    base = node->data.index.target;
    if (base->kind != cnor_node_ident
        || cnor_namespace_module(self, base->name) == CNOR_INDEX_NONE) return lh_bool_false;
    *out = *node;
    out->kind = cnor_node_ident;
    if (!cnor_name_join(&self->arena, base->name, node->name, &out->name))
        return cnor_compile_fail(self, node, cnor_error_no_memory);
    return lh_bool_true;
}
