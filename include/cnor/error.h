/**
 * @file error.h
 * @brief Every reason a cnor stage can fail (::cnor_error_t).
 *
 * One flat list for all stages, grouped by the stage that reports them, so a
 * host switches on a single type whatever went wrong. The value travels inside
 * a ::cnor_diag_t together with the place it happened.
 */

#ifndef CNOR_ERROR_H
#define CNOR_ERROR_H

#include <lh/compiler/extern/c.h>
#include <lh/str/ptr.h>

/**
 * @enum cnor_error
 * @brief Why a stage stopped. ::cnor_error_none means it did not.
 */
enum cnor_error
{
    cnor_error_none = 0, /**< Nothing went wrong. */

    /* -- shared ---------------------------------------------------------- */
    cnor_error_no_memory, /**< An arena, pool or table ran out of room. */

    /* -- lexer ----------------------------------------------------------- */
    cnor_error_unterminated_string, /**< A string literal ran into the end of its line. */
    cnor_error_bad_character,       /**< A character that cannot begin any token. */
    cnor_error_bad_indent,          /**< A dedent to a column no block opened at. */
    cnor_error_too_deep,            /**< Blocks nested past ::CNOR_LEXER_MAX_INDENTS. */

    /* -- parser ---------------------------------------------------------- */
    cnor_error_unexpected_token,  /**< A token that cannot appear where it did. */
    cnor_error_empty_block,       /**< A block header with no body. */
    cnor_error_unknown_type,      /**< A type name that is not a built-in type. */
    cnor_error_bad_number,        /**< A malformed or out-of-range number literal. */
    cnor_error_bad_escape,        /**< An unknown escape sequence in a string. */
    cnor_error_not_an_expression, /**< A value was required and none was there. */
    cnor_error_no_reals,          /**< A real literal in a build without ::LH_REAL_ENABLED. */
    cnor_error_misplaced_import,  /**< An `import` after other items, or inside a block. */

    /* -- compiler -------------------------------------------------------- */
    cnor_error_unknown_name,       /**< A name that is not bound anywhere. */
    cnor_error_duplicate_name,     /**< A name declared twice in one scope. */
    cnor_error_not_assignable,     /**< Assignment to something that is not a variable. */
    cnor_error_array_returned,     /**< A function that returns an array. */
    cnor_error_immutable,          /**< Assignment to a binding declared without `mut`. */
    cnor_error_outside_loop,       /**< `break` or `continue` outside of a loop. */
    cnor_error_too_many_locals,    /**< A function needs more than ::CNOR_SLOT_MAX slots. */
    cnor_error_too_many_globals,   /**< More globals than a 16-bit index can name. */
    cnor_error_too_many_constants, /**< More constants than a 16-bit index can name. */
    cnor_error_too_many_functions, /**< More functions than a 16-bit index can name. */
    cnor_error_too_many_args,      /**< A call with more than ::CNOR_ARGC_MAX arguments. */
    cnor_error_wrong_arg_count,    /**< A call whose argument count the callee rejects. */
    cnor_error_unknown_construct,  /**< A node kind no compiler module handles. */
    cnor_error_type_mismatch,      /**< A value whose type is not the one required there. */
    cnor_error_operand_type,       /**< An operator given types it always fails on. */
    cnor_error_missing_return,     /**< A function with a return type can end without one. */
    cnor_error_not_initialized,    /**< A global used before its `let` has run. */
    cnor_error_ambiguous_name,     /**< A name two imported files both define. */
    cnor_error_no_such_module,     /**< An imported file the loader cannot find. */
    cnor_error_bad_module,         /**< An imported file the loader found but cannot read. */
    cnor_error_import_cycle,       /**< Files that import each other. */
    cnor_error_imports_too_deep,   /**< Imports nested past ::CNOR_COMPILER_MAX_IMPORT_DEPTH. */
    cnor_error_too_many_files,     /**< More files than a 16-bit index can name. */
    cnor_error_not_a_declaration,   /**< An array literal outside a `let`. */

    /* -- image ----------------------------------------------------------- */
    cnor_error_too_small,     /**< The buffer is shorter than a header. */
    cnor_error_bad_magic,     /**< The buffer does not start with the signature. */
    cnor_error_bad_version,   /**< The format version is not the one this build reads. */
    cnor_error_bad_header,    /**< Header fields contradict each other or the buffer. */
    cnor_error_bad_section,   /**< A section's content does not parse. */
    cnor_error_stack_too_big, /**< The program needs more stack than the host allows. */

    /* -- verifier -------------------------------------------------------- */
    cnor_error_code_truncated,   /**< An instruction runs past its function. */
    cnor_error_bad_opcode,       /**< A byte that is not an opcode. */
    cnor_error_bad_operand,      /**< An operand indexes outside its table. */
    cnor_error_bad_jump,         /**< A jump lands outside its function or mid-instruction. */
    cnor_error_bad_stack,        /**< Stack depth underflows, disagrees, or exceeds its bound. */
    cnor_error_no_terminator,    /**< A function can run off its end. */

    /* -- vm -------------------------------------------------------------- */
    cnor_error_not_loaded,       /**< Run before a program was loaded. */
    cnor_error_stack_overflow,   /**< A call frame does not fit the stack. */
    cnor_error_type,             /**< An operator got a value of the wrong kind. */
    cnor_error_div_by_zero,      /**< `/` or `%` by zero. */
    cnor_error_shift_out_of_range, /**< A shift of more than 63 bits, or a negative one. */
    cnor_error_index,            /**< Indexing past the end of a string. */
    cnor_error_no_such_native,   /**< A native the host did not provide. */
    cnor_error_bad_arity,        /**< A native called with an argument count it rejects. */
    cnor_error_native_fail,      /**< A native reported a failure of its own. */
    cnor_error_call_depth,       /**< Calls nested past the configured depth. */
    cnor_error_str_pool_full,    /**< The string pool has no room for a new string. */
    cnor_error_step_limit,       /**< A run executed more instructions than allowed. */

    cnor_error_count /**< Number of codes; not a code itself. */
};
/** @copybrief cnor_error */
typedef enum cnor_error cnor_error_t;

LH_COMPILER_EXTERN_C_BEGIN

/**
 * @brief Describe @p self in English, for a log line.
 *
 * Never returns ::lh_null: an unknown code describes itself as unknown.
 */
const lh_str_ptr
cnor_error_get_message(cnor_error_t self);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_ERROR_H */
