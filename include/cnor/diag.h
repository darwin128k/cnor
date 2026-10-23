/**
 * @file diag.h
 * @brief The one diagnostic type every stage reports through (::cnor_diag_t).
 *
 * The lexer, parser, compiler, verifier and machine all describe a failure the
 * same way: an ::cnor_error_t, a line, an offset and, when the failure is about
 * a name, that name. A host prints all of them with one function.
 */

#ifndef CNOR_DIAG_H
#define CNOR_DIAG_H

#include <cnor/diag/fields.h>
#include <cnor/diag/initializer.h>
#include <cnor/error.h>
#include <cnor/types.h>
#include <lh/bool.h>
#include <lh/char.h>
#include <lh/compiler/extern/c.h>
#include <lh/str/view.h>

/**
 * @def CNOR_DIAG_NAME_CAPACITY
 * @brief Bytes of a name a diagnostic keeps; longer names are cut.
 */
#define CNOR_DIAG_NAME_CAPACITY 32U

/**
 * @struct cnor_diag
 * @brief A failure and its place. Fields via ::cnor_diag_fields.
 */
struct cnor_diag
{
    /** Members: ::cnor_diag_fields. */
    cnor_diag_fields(cnor_error_t, cnor_line_t, lh_u32_t, lh_u8_t, lh_char_t,
                     CNOR_DIAG_NAME_CAPACITY);
};
/** @copybrief cnor_diag */
typedef struct cnor_diag cnor_diag_t;

LH_COMPILER_EXTERN_C_BEGIN

/** @brief Reset @p self to "no error". */
void
cnor_diag_init(cnor_diag_t *self);

/**
 * @brief Record a failure, unless one is recorded already.
 *
 * The first failure wins: later ones are usually consequences of it.
 *
 * @return Always ::lh_bool_false, so a caller can `return` it.
 */
lh_bool_t
cnor_diag_set(cnor_diag_t *self, cnor_error_t code, cnor_line_t line, lh_u32_t offset);

/**
 * @brief Like ::cnor_diag_set, also naming the identifier at fault.
 * @return Always ::lh_bool_false.
 */
lh_bool_t
cnor_diag_set_name(cnor_diag_t *self, cnor_error_t code, cnor_line_t line, lh_str_view_t name);

/** @brief True when nothing has been recorded. */
lh_bool_t
cnor_diag_is_ok(const cnor_diag_t *self);

/** @brief The recorded code, ::cnor_error_none when none. */
cnor_error_t
cnor_diag_get_code(const cnor_diag_t *self);

/** @brief The recorded line, 0 when unknown. */
cnor_line_t
cnor_diag_get_line(const cnor_diag_t *self);

/** @brief The recorded offset: source byte or code address, by stage. */
lh_u32_t
cnor_diag_get_offset(const cnor_diag_t *self);

/** @brief The name the failure is about (a view into @p self), or an empty view. */
lh_str_view_t
cnor_diag_get_name(const cnor_diag_t *self);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_DIAG_H */
