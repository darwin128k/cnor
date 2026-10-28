/**
 * @file image.h
 * @brief The `.cnb` image, format v4: header, sections and the owning buffer
 *        (::cnor_image_t).
 *
 * Every field is a fixed-width little-endian integer written and read byte by
 * byte, so an image is the same on every host. Layout:
 *
 * | Offset | Field           | Type  | Meaning                                    |
 * |-------:|-----------------|-------|--------------------------------------------|
 * | 0      | magic           | 4 B   | `c` `n` `o` `r`                            |
 * | 4      | version         | u16   | ::CNOR_IMAGE_VERSION                       |
 * | 6      | flags           | u16   | `CNOR_IMAGE_FLAG_*`                        |
 * | 8      | image_size      | u32   | total bytes                                |
 * | 12     | stack_slots     | u32   | largest frame: slots + operand depth       |
 * | 16     | globals_count   | u32   | global variables                           |
 * | 20     | sections[6]     | 2 u32 | offset, size of each ::cnor_image_section_t |
 *
 * Sections, each starting on a multiple of ::CNOR_IMAGE_ALIGN:
 *
 * - **code**: instructions (see op.h). Function bodies are contiguous.
 * - **consts**: u32 count, then per entry u8 ::cnor_const_kind_t, u16 length,
 *   bytes. Strings are the final bytes; reals are their decimal text, parsed at
 *   load, so float and double builds read the same file.
 * - **functions**: u32 count, then per entry u32 entry address, u32 code size,
 *   u8 parameters, u8 slots (parameters included), u16 operand stack depth,
 *   u16 file (index into the file table, 0 without one). Function 0 is the
 *   entry point.
 * - **natives**: u32 count, then per entry u16 length and the name. Bound by
 *   name against the host table when the image is loaded.
 * - **lines**: optional (::CNOR_IMAGE_FLAG_LINES): u32 count, then ascending
 *   (u32 code address, u32 line) pairs.
 * - **files**: optional: u32 count, then per entry u16 length and the name
 *   (which may be empty). File 0 is the main file; the others are the files it
 *   imports, so every function names the file its code came from.
 */

#ifndef CNOR_IMAGE_H
#define CNOR_IMAGE_H

#include <cnor/error.h>
#include <cnor/types.h>
#include <lh/byte.h>
#include <lh/compiler/extern/c.h>
#include <lh/numeric/fixed/types.h>

/** @def CNOR_IMAGE_VERSION @brief Format version this build reads and writes. */
#define CNOR_IMAGE_VERSION 16U

/** @def CNOR_IMAGE_HEADER_SIZE @brief Bytes of the fixed header. */
#define CNOR_IMAGE_HEADER_SIZE 68U

/** @def CNOR_IMAGE_ALIGN @brief Section alignment in bytes. */
#define CNOR_IMAGE_ALIGN 4U

/** @def CNOR_IMAGE_FLAG_LINES @brief The image carries a line table. */
#define CNOR_IMAGE_FLAG_LINES 0x0001U

/** @def CNOR_IMAGE_FUNCTION_SIZE @brief Bytes of one function table entry. */
#define CNOR_IMAGE_FUNCTION_SIZE 14U

/**
 * @enum cnor_image_section
 * @brief The sections, in header order.
 */
enum cnor_image_section
{
    cnor_image_section_code = 0,  /**< Instructions. */
    cnor_image_section_consts,    /**< Constant pool. */
    cnor_image_section_functions, /**< Function table. */
    cnor_image_section_natives,   /**< Native imports. */
    cnor_image_section_lines,     /**< Line table (optional). */
    cnor_image_section_files,     /**< Source file names (optional). */
    cnor_image_section_names,     /**< Function names (optional). */
    cnor_image_section_count
};
/** @copybrief cnor_image_section */
typedef enum cnor_image_section cnor_image_section_t;

/**
 * @enum cnor_const_kind
 * @brief What a constant pool entry holds.
 */
enum cnor_const_kind
{
    cnor_const_str = 0, /**< Bytes of a string. */
    cnor_const_real,    /**< Decimal text of a real. */
    cnor_const_kind_count
};
/** @copybrief cnor_const_kind */
typedef enum cnor_const_kind cnor_const_kind_t;

/**
 * @struct cnor_image_range
 * @brief Where a section lies inside the image.
 */
struct cnor_image_range
{
    lh_u32_t offset; /**< First byte, from the start of the image. */
    lh_u32_t size;   /**< Length in bytes; 0 for an absent section. */
};
/** @copybrief cnor_image_range */
typedef struct cnor_image_range cnor_image_range_t;

/**
 * @struct cnor_image_header
 * @brief The decoded header.
 */
struct cnor_image_header
{
    lh_u16_t version;                                    /**< Format version. */
    lh_u16_t flags;                                      /**< `CNOR_IMAGE_FLAG_*`. */
    lh_u32_t image_size;                                 /**< Total bytes. */
    cnor_stack_slots_t stack_slots;                      /**< Largest frame. */
    lh_u32_t globals_count;                              /**< Global variables. */
    cnor_image_range_t sections[cnor_image_section_count]; /**< Section table. */
};
/** @copybrief cnor_image_header */
typedef struct cnor_image_header cnor_image_header_t;

/**
 * @struct cnor_image_function
 * @brief One decoded function table entry.
 */
struct cnor_image_function
{
    cnor_code_addr_t entry;    /**< First instruction. */
    cnor_code_addr_t size;     /**< Code bytes. */
    cnor_argc_t param_count;   /**< Parameters, the first slots. */
    lh_u8_t slot_count;        /**< Frame slots, parameters included. */
    lh_u16_t max_stack;        /**< Operand stack depth above the slots. */
    cnor_file_index_t file;    /**< Source file of its code. */
};
/** @copybrief cnor_image_function */
typedef struct cnor_image_function cnor_image_function_t;

/**
 * @struct cnor_image
 * @brief An image in memory, owned. Produced by the compiler.
 */
struct cnor_image
{
    lh_byte_t *data; /**< The bytes; runtime-allocator owned. */
    lh_u32_t size;   /**< Their length. */
};
/** @copybrief cnor_image */
typedef struct cnor_image cnor_image_t;

LH_COMPILER_EXTERN_C_BEGIN

/** @brief Empty image. */
void
cnor_image_init(cnor_image_t *self);

/** @brief Release the bytes; safe on an empty image and twice. */
void
cnor_image_deinit(cnor_image_t *self);

/**
 * @brief Decode and check the header of @p data.
 *
 * Checks the signature, the version, that `image_size` equals @p size and that
 * every section lies inside the image, aligned. Section contents are checked
 * when the image is loaded.
 *
 * @param[out] out Written on success only.
 */
cnor_error_t
cnor_image_header_read(const lh_byte_t *data, lh_u32_t size, cnor_image_header_t *out);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_IMAGE_H */
