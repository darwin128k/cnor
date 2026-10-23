/**
 * @file initializer.h
 * @brief Brace initializers for ::cnor_diag_t.
 */

#ifndef CNOR_DIAG_INITIALIZER_H
#define CNOR_DIAG_INITIALIZER_H

#include <lh/initializer.h>

/**
 * @def cnor_diag_initializer(code, line, offset)
 * @brief Initializer for a failure without a name.
 */
#define cnor_diag_initializer(code, line, offset) lh_initializer(code, line, offset, 0U, {0})

/**
 * @def cnor_diag_ok_initializer()
 * @brief Initializer for "no error, nowhere".
 */
#define cnor_diag_ok_initializer()                                                                 \
    cnor_diag_initializer(cnor_error_none, 0U, 0U)

#endif /* CNOR_DIAG_INITIALIZER_H */
