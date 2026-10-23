/**
 * @file loader.h
 * @brief Imported files read from the file system (::cnor_cli_loader_t).
 *
 * The compiler resolves every `import` to a path and asks its host for the
 * text; the command line answers from the file system, with lh's file API.
 * `-` is a file named `-` here, never the standard input.
 */

#ifndef CNOR_CLI_LOADER_H
#define CNOR_CLI_LOADER_H

#include "file.h"
#include "options.h"

#include <cnor/compiler.h>
#include <lh/str/view.h>

/**
 * @struct cnor_cli_loader
 * @brief The search paths of a compilation, as the compiler takes them.
 */
struct cnor_cli_loader
{
    lh_str_view_t search_paths[CNOR_CLI_MAX_IMPORT_PATHS]; /**< `-I` directories, in order. */
};
/** @copybrief cnor_cli_loader */
typedef struct cnor_cli_loader cnor_cli_loader_t;

/** @brief The text of @p buffer, without a UTF-8 byte order mark. */
lh_str_view_t
cnor_cli_loader_text(const cnor_cli_buffer_t *buffer);

/**
 * @brief Point @p config's loader at the file system and its search paths at
 *        the `-I` directories of @p options; @p self must outlive the
 *        compilation.
 */
void
cnor_cli_loader_configure(cnor_cli_loader_t *self, const cnor_cli_options_t *options,
                          cnor_compiler_config_t *config);

#endif /* CNOR_CLI_LOADER_H */
