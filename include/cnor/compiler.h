/**
 * @file compiler.h
 * @brief Syntax tree to image (::cnor_compiler_compile).
 *
 * The compiler resolves every name (local slot, global index, function index,
 * native import), writes the code section one function after another, builds
 * the constant pool, the function table and the optional line table, computes
 * every function's frame size and operand stack depth, and assembles a v1
 * image (see image.h).
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
#include <lh/str/view.h>

/**
 * @struct cnor_compiler_config
 * @brief What the compiler is told besides the tree.
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
};
/** @copybrief cnor_compiler_config */
typedef struct cnor_compiler_config cnor_compiler_config_t;

/**
 * @def cnor_compiler_config_initializer()
 * @brief No natives, no file name, line table on.
 */
#define cnor_compiler_config_initializer()                                                         \
    lh_initializer(lh_null, lh_str_view_empty_initializer(), lh_bool_true)

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
