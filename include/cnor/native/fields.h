/**
 * @file fields.h
 * @brief Member fields of ::cnor_native_t and ::cnor_native_table_t.
 */

#ifndef CNOR_NATIVE_FIELDS_H
#define CNOR_NATIVE_FIELDS_H

/**
 * @def cnor_native_fields(name_type, fn_type, arity_type)
 * @brief A native's script name, its C function and the argument counts it takes.
 */
#define cnor_native_fields(name_type, fn_type, arity_type)                                         \
    name_type name;                                                                                \
    fn_type fn;                                                                                    \
    arity_type arity_min;                                                                          \
    arity_type arity_max

/**
 * @def cnor_native_table_fields(entry_type, count_type)
 * @brief A host-owned array of natives and its length.
 */
#define cnor_native_table_fields(entry_type, count_type)                                           \
    const entry_type *entries;                                                                     \
    count_type count

#endif /* CNOR_NATIVE_FIELDS_H */
