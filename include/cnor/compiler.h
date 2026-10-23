/**
 * @file compiler.h
 * @brief Syntax tree to image (::cnor_compiler_compile).
 *
 * The compiler resolves every name (local slot, global index, function index,
 * native import), writes the code section one function after another, builds
 * the constant pool, the function table and the optional line table, computes
 * every function's frame size and operand stack depth, checks the types the
 * program states or implies, and assembles a v4 image (see image.h).
 *
 * Statements and expressions are compiled through a table keyed by node kind,
 * one module per construct; see docs/architecture.md.
 *
 * ## Program shape
 *
 * Function 0 is the entry point: the top-level statements in source order. A
 * top-level `let` declares a global, visible to every function wherever it is
 * declared in the file; a `let` in any block declares a local of the enclosing
 * function. The value of the last top-level expression statement that ran is
 * the program's result, unless a top-level `return` gives one explicitly.
 *
 * ## Imports
 *
 * `import "path"` at the top of a file compiles another file into the same
 * image. The compiler resolves the path (relative to the importing file, then
 * each search path) and asks the host's loader for the text, so it never
 * touches a file system itself; without a loader every import fails with
 * ::cnor_error_no_such_module. Each file is compiled once; its top-level code
 * runs once per run, before the code of the files that import it.
 *
 * ## Diagnostics
 *
 * A failure records its line and, through ::cnor_diag_get_file, the file it is
 * in: the configured file name, or the resolved path of an imported file.
 */

#ifndef CNOR_COMPILER_H
#define CNOR_COMPILER_H

#include <cnor/ast.h>
#include <cnor/diag.h>
#include <cnor/image.h>
#include <cnor/native.h>
#include <lh/initializer.h>
#include <lh/null.h>
#include <lh/bool.h>
#include <lh/compiler/extern/c.h>
#include <lh/ptr.h>
#include <lh/str/view.h>

/**
 * @def CNOR_COMPILER_MAX_IMPORT_DEPTH
 * @brief Longest chain of files each importing the next, the main file not
 *        counted.
 */
#define CNOR_COMPILER_MAX_IMPORT_DEPTH 64U

/**
 * @struct cnor_source
 * @brief The text of one imported file, as a loader hands it over.
 */
struct cnor_source
{
    lh_str_view_t text; /**< The source; valid until the loader's release is called. */
    lh_ptr handle;      /**< Whatever the loader needs to release it. */
};
/** @copybrief cnor_source */
typedef struct cnor_source cnor_source_t;

/**
 * @brief Find the file at @p path and hand over its text in @p out.
 *
 * @p path is resolved already (see ::cnor_compiler_config). Return
 * ::cnor_error_none on success; ::cnor_error_no_such_module when nothing is
 * there, and the compiler tries the next search path; anything else (usually
 * ::cnor_error_bad_module or ::cnor_error_no_memory) stops the compilation
 * with that code.
 */
typedef cnor_error_t (*cnor_compiler_load_fn_t)(lh_ptr ctx, lh_str_view_t path,
                                                cnor_source_t *out);

/**
 * @brief Give back what a successful load handed over. Called once per load,
 *        as soon as the text is parsed (the tree keeps copies of what it needs).
 */
typedef void (*cnor_compiler_release_fn_t)(lh_ptr ctx, cnor_source_t *source);

/**
 * @struct cnor_compiler_config
 * @brief What the compiler is told besides the tree.
 *
 * An import path that starts with `/`, `\` or a drive letter (`C:`) is used as
 * it is. Any other is tried relative to the directory of the importing file
 * (for the main file: of `file`), then relative to each of `search_paths` in
 * order. `.` and `..` segments are folded and `\` becomes `/`, so a file has
 * one spelling; the first path the loader accepts wins.
 */
struct cnor_compiler_config
{
    /**
     * Host natives, or ::lh_null. When given, a call to a name that is neither
     * a script function nor a `native fn` declaration is checked against it
     * (existence and arity) at compile time. Binding still happens at load.
     */
    const cnor_native_table_t *natives;
    lh_str_view_t file;   /**< Source file name, recorded in the image. */
    lh_bool_t emit_lines; /**< Write a line table for diagnostics. */
    cnor_compiler_load_fn_t load;       /**< Loader for imports, or ::lh_null. */
    cnor_compiler_release_fn_t release; /**< Its release, or ::lh_null. */
    lh_ptr load_ctx;                    /**< Passed to both. */
    const lh_str_view_t *search_paths;  /**< Directories tried after the importer's. */
    lh_u32_t search_path_count;         /**< How many. */
};
/** @copybrief cnor_compiler_config */
typedef struct cnor_compiler_config cnor_compiler_config_t;

/**
 * @def cnor_compiler_config_initializer()
 * @brief No natives, no file name, line table on, no loader.
 */
#define cnor_compiler_config_initializer()                                                         \
    lh_initializer(lh_null, lh_str_view_empty_initializer(), lh_bool_true, lh_null, lh_null,     \
                   lh_null, lh_null, 0U)

LH_COMPILER_EXTERN_C_BEGIN

/**
 * @brief Compile @p program (as returned by ::cnor_parser_parse) into @p out.
 *
 * @param config ::lh_null for ::cnor_compiler_config_initializer.
 * @param[out] out  Receives an image to release with ::cnor_image_deinit.
 * @param[out] diag Why it failed, with the line and, for a name, the name.
 * @return ::lh_bool_true when an image was produced.
 */
lh_bool_t
cnor_compiler_compile(const cnor_node_t *program, const cnor_compiler_config_t *config,
                      cnor_image_t *out, cnor_diag_t *diag);

/**
 * @brief Parse and compile @p src in one call, with a private arena.
 */
lh_bool_t
cnor_compiler_compile_source(lh_str_view_t src, const cnor_compiler_config_t *config,
                             cnor_image_t *out, cnor_diag_t *diag);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_COMPILER_H */
