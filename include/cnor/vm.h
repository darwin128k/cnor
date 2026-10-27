/**
 * @file vm.h
 * @brief The interpreter (::cnor_vm_t): load a v4 image, run it, read the result.
 *
 * Loading checks the image (header, sections, verifier), binds every native
 * import by name against the host table, decodes the constants and allocates
 * the stack, the call frames and the string pool. Nothing is allocated while a
 * program runs.
 *
 * ## Globals and resets
 *
 * Globals live at the bottom of the stack. ::cnor_vm_run starts the entry
 * function with globals as the previous run left them; ::cnor_vm_reset sets
 * every global to none and rewinds the string pool. A host that wants each event
 * handled from a clean state resets before each run.
 */

#ifndef CNOR_VM_H
#define CNOR_VM_H

#include <cnor/diag.h>
#include <cnor/image.h>
#include <cnor/native.h>
#include <cnor/op.h>
#include <cnor/types.h>
#include <cnor/vm/stack.h>
#include <cnor/vm/str_pool.h>
#include <lh/bool.h>
#include <lh/compiler/extern/c.h>
#include <lh/initializer.h>
#include <lh/null.h>
#include <lh/ptr.h>
#include <lh/value.h>

/** @def CNOR_VM_DEFAULT_STACK_SLOTS @brief Value stack size, in values. */
#define CNOR_VM_DEFAULT_STACK_SLOTS 256U

/** @def CNOR_VM_DEFAULT_MAX_DEPTH @brief Nested script calls allowed. */
#define CNOR_VM_DEFAULT_MAX_DEPTH 64U

/** @def CNOR_VM_DEFAULT_STR_POOL_SIZE @brief String pool size, in bytes. */
#define CNOR_VM_DEFAULT_STR_POOL_SIZE 512U

/**
 * @def CNOR_VM_DEFAULT_MAX_STEPS
 * @brief Instructions one run may execute: one million.
 *
 * Enough for any event handler a script is meant for, and a bound on how long
 * a runaway loop can hold the host: a few milliseconds on a desktop, about a
 * second on a small microcontroller. 0 means unlimited.
 */
#define CNOR_VM_DEFAULT_MAX_STEPS 1000000U

/**
 * @struct cnor_vm_config
 * @brief What a host decides about a machine.
 */
struct cnor_vm_config
{
    const cnor_native_table_t *natives; /**< Host natives, or ::lh_null. */
    lh_ptr ctx;                         /**< Passed to every native. */
    cnor_stack_slots_t stack_slots;     /**< Value stack, in values. */
    cnor_call_depth_t max_depth;        /**< Nested calls allowed. */
    lh_u32_t str_pool_size;             /**< String pool, in bytes. */
    cnor_step_count_t max_steps;        /**< Instructions per run; 0 is unlimited. */
};
/** @copybrief cnor_vm_config */
typedef struct cnor_vm_config cnor_vm_config_t;

/**
 * @def cnor_vm_config_initializer()
 * @brief No natives, default sizes and budget.
 */
#define cnor_vm_config_initializer()                                                               \
    lh_initializer(lh_null, lh_null, CNOR_VM_DEFAULT_STACK_SLOTS, CNOR_VM_DEFAULT_MAX_DEPTH,       \
                   CNOR_VM_DEFAULT_STR_POOL_SIZE, CNOR_VM_DEFAULT_MAX_STEPS)

/**
 * @struct cnor_vm_frame
 * @brief One active call: where to resume and the caller's frame.
 */
struct cnor_vm_frame
{
    cnor_code_addr_t return_addr;   /**< Caller's next instruction. */
    cnor_stack_slots_t fp;          /**< Caller's first slot. */
    cnor_function_index_t function; /**< Caller. */
};
/** @copybrief cnor_vm_frame */
typedef struct cnor_vm_frame cnor_vm_frame_t;

/**
 * @struct cnor_vm_try
 * @brief One `try`: the catch block, the finally block, and the frame it was
 *        made in.
 *
 * The `try` blocks are a stack of their own: a `try` inside a `try` pushes
 * two, and the machine unwinds them as it leaves the frames they were made in.
 *
 * The handler holds what the machine needs to know about the error it is
 * dealing with: the error itself, where it happened, and what the `try` block
 * has left to do. The flags say how far the block has come: ::CNOR_VM_TRY_CLOSED
 * means the code that may fail is over and the catch block will not take
 * another error, and ::CNOR_VM_TRY_CLEANED means the finally block has run and
 * must not run again.
 */
struct cnor_vm_try
{
    cnor_code_addr_t catch_addr;   /**< Where the catch block starts, 0 when there is none. */
    cnor_code_addr_t finally_addr; /**< Where the finally block starts, 0 when there is none. */
    cnor_code_addr_t action_addr;  /**< Where to go after the finally block, 0 when nowhere. */
    cnor_code_addr_t error_at;     /**< Where the error happened. */
    cnor_call_depth_t depth;       /**< The call depth the `try` was made at. */
    lh_u8_t slot;                  /**< Where the error's dictionary goes. */
    lh_u8_t flags;                 /**< ::CNOR_VM_TRY_CLOSED, ::CNOR_VM_TRY_CLEANED, ::CNOR_VM_TRY_ACTION. */
    cnor_error_t error;            /**< The error being handled, ::cnor_error_none when there is none. */
};
/** @copybrief cnor_vm_try */
typedef struct cnor_vm_try cnor_vm_try_t;

/**
 * @def CNOR_VM_TRY_CLOSED @brief The code that may fail is over; the catch
 *        block will not take another error.
 */
#define CNOR_VM_TRY_CLOSED 0x01U

/**
 * @def CNOR_VM_TRY_CLEANED @brief The finally block has run; it must not run
 *        again.
 */
#define CNOR_VM_TRY_CLEANED 0x02U

/**
 * @def CNOR_VM_TRY_ACTION @brief The finally block is asked to go somewhere
 *        when it ends; the address is in the handler.
 */
#define CNOR_VM_TRY_ACTION 0x04U

/**
 * @struct cnor_vm
 * @brief One interpreter. Every field is written by ::cnor_vm_init; read it
 *        through the functions below.
 */
struct cnor_vm
{
    cnor_vm_config_t config;           /**< Host decisions. */
    lh_byte_t *image;                  /**< Private copy of the loaded image. */
    cnor_image_header_t header;        /**< Its header. */
    const lh_byte_t *code;             /**< Code section. */
    cnor_code_addr_t code_size;        /**< Its length. */
    cnor_image_function_t *functions;  /**< Decoded function table. */
    cnor_count_t function_count;       /**< Its length. */
    lh_value_t *constants;             /**< Decoded constants. */
    cnor_count_t const_count;          /**< Their number. */
    const cnor_native_t **natives;     /**< Imports bound to host entries. */
    cnor_count_t native_count;         /**< Their number. */
    cnor_count_t line_count;           /**< Line table entries. */
    cnor_vm_stack_t stack;             /**< Globals, then frames. */
    cnor_vm_frame_t *frames;           /**< Call stack, `max_depth` entries. */
    cnor_call_depth_t depth;           /**< Frames in use. */
    cnor_vm_try_t *tries;              /**< The `try` blocks, `max_depth` entries. */
    cnor_count_t try_count;            /**< How many are open. */
    cnor_str_pool_t str_pool;          /**< Strings built while running. */
    cnor_stack_slots_t fp;             /**< First slot of the current frame. */
    cnor_function_index_t function;    /**< Function running. */
    cnor_code_addr_t ip;               /**< Next instruction. */
    lh_bool_t running;                 /**< Cleared by `ret` from the entry or `halt`. */
    cnor_step_count_t steps;           /**< Instructions executed by the current run. */
    lh_value_t result;                 /**< What the last run produced. */
    cnor_diag_t diag;                  /**< Why the last load or run failed. */
    cnor_count_t file_count;           /**< File table entries; last, it is never hot. */
    cnor_count_t function_name_count;  /**< Function name entries; a host looks them up by name. */
};
/** @copybrief cnor_vm */
typedef struct cnor_vm cnor_vm_t;

LH_COMPILER_EXTERN_C_BEGIN

/**
 * @brief Prepare @p self; nothing is allocated.
 * @param config ::lh_null for ::cnor_vm_config_initializer. Copied.
 */
void
cnor_vm_init(cnor_vm_t *self, const cnor_vm_config_t *config);

/** @brief Release everything a load took. Safe twice. */
void
cnor_vm_deinit(cnor_vm_t *self);

/**
 * @brief Check, copy and bind the image in @p data; the caller may free @p data
 *        afterwards. A previously loaded program is released first.
 * @return ::lh_bool_false with ::cnor_vm_get_diag set.
 */
lh_bool_t
cnor_vm_load(cnor_vm_t *self, const lh_byte_t *data, lh_u32_t size);

/** @brief Set every global to none, rewind the string pool, clear the result. */
void
cnor_vm_reset(cnor_vm_t *self);

/**
 * @brief Run the entry function to its end.
 *
 * A run that would execute more than `max_steps` instructions (unless that is
 * 0) stops before the next one with ::cnor_error_step_limit, at that
 * instruction's address. Like every failure, it unwinds the stack to the
 * globals; globals keep what the run had written so far.
 *
 * @return ::lh_bool_false with ::cnor_vm_get_diag set: its offset is the code
 *         address at fault, its line comes from the line table (0 without one).
 */
lh_bool_t
cnor_vm_run(cnor_vm_t *self);

/**
 * @brief Call the script's function @p name with @p args.
 *
 * The function runs to its end, as the entry function does in
 * ::cnor_vm_run; the globals keep what it wrote. The arguments are the
 * function's parameters, in order; a string among them is a view of the
 * caller's text, so it stays valid as long as the machine's string pool does.
 *
 * The image has to carry the names of its functions, which the compiler
 * writes.
 *
 * @param name  Name of the function; the image has none of that name, the call
 *              fails with ::cnor_error_unknown_function.
 * @param args  The arguments; @p argc of them.
 * @param argc  How many; it has to be the count of the function's parameters.
 * @param out   Where the result goes; may be ::lh_null.
 *
 * @return ::lh_bool_false with ::cnor_vm_get_diag set: its offset is the code
 *         address at fault, its line comes from the line table (0 without one).
 */
lh_bool_t
cnor_vm_call(cnor_vm_t *self, lh_str_view_t name, const lh_value_t *args, cnor_argc_t argc,
             lh_value_t *out);

/** @brief Why the last load or run failed. */
const cnor_diag_t *
cnor_vm_get_diag(const cnor_vm_t *self);

/** @brief Instructions the last run executed. */
cnor_step_count_t
cnor_vm_get_steps(const cnor_vm_t *self);

/** @brief What the last run produced, or none. */
lh_value_t
cnor_vm_get_result(const cnor_vm_t *self);

/** @brief Global @p index, or ::lh_null outside the program's globals. */
const lh_value_t *
cnor_vm_get_global(const cnor_vm_t *self, cnor_global_index_t index);

/** @brief Main source file the loaded image names, or an empty view. */
lh_str_view_t
cnor_vm_get_file(const cnor_vm_t *self);

/**
 * @brief Source file of the code at address @p addr: the main file or one it
 *        imports, or an empty view without a file table. A run failure's
 *        address is ::cnor_diag_get_offset; its diagnostic also keeps a copy
 *        (::cnor_diag_get_file), cut to ::CNOR_DIAG_FILE_CAPACITY bytes.
 */
lh_str_view_t
cnor_vm_get_file_at(const cnor_vm_t *self, cnor_code_addr_t addr);

/** @brief Source line of code address @p addr, or 0 without a line table. */
cnor_line_t
cnor_vm_get_line_at(const cnor_vm_t *self, cnor_code_addr_t addr);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_VM_H */
