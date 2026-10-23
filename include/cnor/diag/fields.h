/**
 * @file fields.h
 * @brief Member fields of ::cnor_diag_t.
 */

#ifndef CNOR_DIAG_FIELDS_H
#define CNOR_DIAG_FIELDS_H

/**
 * @def cnor_diag_fields(error_type, line_type, offset_type, size_type, char_type, capacity,
 *                       file_capacity)
 * @brief What went wrong and where.
 *
 * `offset` is a byte offset into the source for the lexer, parser and
 * compiler, and a code address for the verifier and the machine. `name` holds
 * a copy of the identifier the failure is about (`name_size` bytes, at most
 * `capacity`), or for a type error the types involved: a copy, because the
 * tree or image it came from is usually gone by the time a host prints the
 * diagnostic. `file` holds a copy of the source file the line belongs to, when
 * the stage knows it (`file_size` bytes, at most `file_capacity`, the end of a
 * longer path).
 */
#define cnor_diag_fields(error_type, line_type, offset_type, size_type, char_type, capacity,       \
                         file_capacity)                                                            \
    error_type code;                                                                               \
    line_type line;                                                                                \
    offset_type offset;                                                                            \
    size_type name_size;                                                                           \
    size_type file_size;                                                                           \
    char_type name[capacity];                                                                      \
    char_type file[file_capacity]

#endif /* CNOR_DIAG_FIELDS_H */
