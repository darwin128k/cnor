/**
 * @file initializer.h
 * @brief Brace initializers for ::cnor_native_t and ::cnor_native_table_t.
 */

#ifndef CNOR_NATIVE_INITIALIZER_H
#define CNOR_NATIVE_INITIALIZER_H

#include <lh/initializer.h>
#include <lh/null.h>
#include <lh/str/view/initializer.h>

/**
 * @def cnor_native_initializer(name_lit, fn, arity_min, arity_max)
 * @brief A native named by a string literal.
 */
#define cnor_native_initializer(name_lit, fn, arity_min, arity_max)                                \
    lh_initializer(lh_str_view_initializer_lit(name_lit), fn, arity_min, arity_max)

/**
 * @def cnor_native_table_initializer(entries, count)
 * @brief A table over @p count entries.
 */
#define cnor_native_table_initializer(entries, count) lh_initializer(entries, count)

/**
 * @def cnor_native_table_empty_initializer()
 * @brief A table with no natives.
 */
#define cnor_native_table_empty_initializer() cnor_native_table_initializer(lh_null, 0U)

#endif /* CNOR_NATIVE_INITIALIZER_H */
