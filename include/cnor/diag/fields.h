/**
 * @file fields.h
 * @brief Member fields of ::cnor_diag_t.
 */

#ifndef CNOR_DIAG_FIELDS_H
#define CNOR_DIAG_FIELDS_H

/**
 * @def cnor_diag_fields(error_type, line_type, offset_type, size_type, char_type, capacity)
 * @brief What went wrong and where.
 *
 * `offset` is a byte offset into the source for the lexer, parser and
 * compiler, and a code address for the verifier and the machine. `name` holds
 * a copy of the identifier the failure is about (`name_size` bytes, at most
 * `capacity`): a copy, because the tree or image it came from is usually gone
 * by the time a host prints the diagnostic.
 */
#define cnor_diag_fields(error_type, line_type, offset_type, size_type, char_type, capacity)       \
    error_type code;                                                                               \
    line_type line;                                                                                \
    offset_type offset;                                                                            \
    size_type name_size;                                                                           \
    char_type name[capacity]

#endif /* CNOR_DIAG_FIELDS_H */
