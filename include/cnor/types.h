/**
 * @file types.h
 * @brief Semantic integer types shared by every stage of cnor.
 *
 * Each value that means something gets a name of its own, so a signature says
 * what it takes: a ::cnor_slot_t is a local slot, a ::cnor_code_addr_t is an
 * offset into the code section. The widths are the widths the image format
 * stores, and every "no such index" sentinel is spelled with the same width as
 * the type it marks, so a sentinel can never be truncated into a valid index.
 */

#ifndef CNOR_TYPES_H
#define CNOR_TYPES_H

#include <lh/numeric/fixed/types.h>

/**
 * @typedef cnor_line_t
 * @brief 1-based source line. 0 means "unknown".
 */
typedef lh_u32_t cnor_line_t;

/**
 * @typedef cnor_source_offset_t
 * @brief Byte offset into the source text.
 */
typedef lh_u32_t cnor_source_offset_t;

/**
 * @typedef cnor_code_addr_t
 * @brief Byte offset into the code section of an image.
 */
typedef lh_u32_t cnor_code_addr_t;

/**
 * @typedef cnor_jump_offset_t
 * @brief Signed jump distance, relative to the instruction after the jump.
 */
typedef lh_s32_t cnor_jump_offset_t;

/**
 * @typedef cnor_slot_t
 * @brief Local slot inside a call frame. Encoded as one byte in the code.
 */
typedef lh_u16_t cnor_slot_t;

/**
 * @typedef cnor_global_index_t
 * @brief Index of a global variable.
 */
typedef lh_u16_t cnor_global_index_t;

/**
 * @typedef cnor_const_index_t
 * @brief Ordinal of an entry in the constant section (not a byte offset).
 */
typedef lh_u16_t cnor_const_index_t;

/**
 * @typedef cnor_function_index_t
 * @brief Index into the function table. Function 0 is the entry point.
 */
typedef lh_u16_t cnor_function_index_t;

/**
 * @typedef cnor_file_index_t
 * @brief Index into the image's file table. File 0 is the main file.
 */
typedef lh_u16_t cnor_file_index_t;

/**
 * @typedef cnor_native_index_t
 * @brief Index into the native import section of an image.
 */
typedef lh_u16_t cnor_native_index_t;

/**
 * @typedef cnor_argc_t
 * @brief Number of arguments passed to a call.
 */
typedef lh_u8_t cnor_argc_t;

/**
 * @typedef cnor_stack_slots_t
 * @brief A count of value-stack slots (not bytes).
 */
typedef lh_u32_t cnor_stack_slots_t;

/**
 * @typedef cnor_call_depth_t
 * @brief How many script calls are nested.
 */
typedef lh_u32_t cnor_call_depth_t;

/**
 * @typedef cnor_step_count_t
 * @brief How many instructions a run executes.
 */
typedef lh_u32_t cnor_step_count_t;

/**
 * @typedef cnor_count_t
 * @brief Number of entries in one of the compiler's or image's tables.
 */
typedef lh_u32_t cnor_count_t;

/**
 * @def CNOR_INDEX_NONE
 * @brief "No such entry" for every 16-bit index type above.
 */
#define CNOR_INDEX_NONE 0xFFFFU

/**
 * @def CNOR_SLOT_MAX
 * @brief Largest number of slots a frame may have: slots are one byte in code.
 */
#define CNOR_SLOT_MAX 255U

/**
 * @def CNOR_ARGC_MAX
 * @brief Largest number of arguments a call may pass.
 */
#define CNOR_ARGC_MAX 255U

/**
 * @def CNOR_INDEX_MAX
 * @brief Largest valid 16-bit index; ::CNOR_INDEX_NONE is one above it.
 */
#define CNOR_INDEX_MAX 0xFFFEU

#endif /* CNOR_TYPES_H */
