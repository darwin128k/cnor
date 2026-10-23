/**
 * @file native.h
 * @brief Host functions a script can call (::cnor_native_t) and the table a
 *        host hands over (::cnor_native_table_t).
 *
 * A native is an ordinary C function. The image only records the *names* of
 * the natives it calls; the machine binds each name to an entry of the host
 * table once, at load time, so a call is one indexed jump and a missing native
 * is a load error, not a run-time surprise.
 */

#ifndef CNOR_NATIVE_H
#define CNOR_NATIVE_H

#include <cnor/error.h>
#include <cnor/native/fields.h>
#include <cnor/native/initializer.h>
#include <cnor/types.h>
#include <lh/compiler/extern/c.h>
#include <lh/ptr.h>
#include <lh/size.h>
#include <lh/str/view.h>
#include <lh/value.h>

/**
 * @typedef cnor_native_fn_t
 * @brief A native's C side.
 *
 * @param args  `argc` evaluated arguments. Strings view the machine's memory and
 *              stay valid until the machine is reset.
 * @param argc  How many.
 * @param ctx   The host context given in the machine's configuration.
 * @param[out] error Set to something other than ::cnor_error_none to fail the run.
 * @return The result; ::lh_value_make_none for "nothing".
 */
typedef lh_value_t (*cnor_native_fn_t)(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx,
                                       cnor_error_t *error);

/**
 * @struct cnor_native
 * @brief One native. Fields via ::cnor_native_fields.
 */
struct cnor_native
{
    /** Members: ::cnor_native_fields. */
    cnor_native_fields(lh_str_view_t, cnor_native_fn_t, cnor_argc_t);
};
/** @copybrief cnor_native */
typedef struct cnor_native cnor_native_t;

/**
 * @struct cnor_native_table
 * @brief Host-owned natives. Fields via ::cnor_native_table_fields.
 */
struct cnor_native_table
{
    /** Members: ::cnor_native_table_fields. */
    cnor_native_table_fields(cnor_native_t, lh_usize_t);
};
/** @copybrief cnor_native_table */
typedef struct cnor_native_table cnor_native_table_t;

LH_COMPILER_EXTERN_C_BEGIN

/** @brief The entry named @p name, or ::lh_null. */
const cnor_native_t *
cnor_native_table_find(const cnor_native_table_t *self, lh_str_view_t name);

/** @brief Whether @p self accepts @p argc arguments. */
lh_bool_t
cnor_native_accepts(const cnor_native_t *self, cnor_argc_t argc);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_NATIVE_H */
