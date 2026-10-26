/**
 * @file tree_run.h
 * @brief The tree-walker: the same language, run by walking the tree.
 *
 * A measurement prototype, not shipped code: it exists to answer "how much
 * slower is a tree-walker than the bytecode machine?" with a number instead of
 * an opinion. It runs the same programs, with the same values and the same
 * rules, and only the way the work is dispatched differs.
 *
 * The names are resolved once, before the run, and the slots are written into
 * the tree: the tree-walker is measured at its best, with the one advantage
 * the bytecode machine has (a name resolved at compile time) taken away.
 */

#ifndef CNOR_TREE_RUN_H
#define CNOR_TREE_RUN_H

#include <cnor/ast.h>
#include <cnor/diag.h>
#include <lh/bool.h>
#include <lh/value.h>

struct tree_program;

/**
 * @brief Read @p source into a program; the reason in @p diag on failure.
 * @return The program, or ::lh_null.
 */
struct tree_program *
tree_program_compile(const char *source, cnor_diag_t *diag);

/** @brief Release @p program and everything it holds. */
void
tree_program_free(struct tree_program *program);

/**
 * @brief Run the program: its top-level code, as the machine runs it.
 *
 * @param[out] out The result of the last statement.
 * @param[out] nodes How many tree nodes the walk visited.
 * @return Whether the run finished.
 */
lh_bool_t
tree_program_run(const struct tree_program *program, lh_value_t *out, lh_u64_t *nodes);

#endif /* CNOR_TREE_RUN_H */
