/**
 * @file writer.h
 * @brief Library-private: assemble an image from its parts.
 */

#ifndef CNOR_SRC_IMAGE_WRITER_H
#define CNOR_SRC_IMAGE_WRITER_H

#include <cnor/byte/writer.h>
#include <cnor/image.h>
#include <lh/str/view.h>

/**
 * @struct cnor_image_parts
 * @brief Everything an image is made of. Table sections are given as their
 *        records, already encoded, plus a count.
 */
struct cnor_image_parts
{
    const cnor_byte_writer_t *code;         /**< Instructions. */
    const cnor_byte_writer_t *consts;       /**< Constant records. */
    cnor_count_t const_count;               /**< How many. */
    const cnor_image_function_t *functions; /**< Function table. */
    cnor_count_t function_count;            /**< How many; at least 1. */
    const lh_str_view_t *natives;           /**< Native import names. */
    cnor_count_t native_count;              /**< How many. */
    const cnor_byte_writer_t *lines;        /**< Line records, or ::lh_null. */
    cnor_count_t line_count;                /**< How many. */
    lh_str_view_t file;                     /**< Source file name, may be empty. */
    lh_u32_t globals_count;                 /**< Global variables. */
    cnor_stack_slots_t stack_slots;         /**< Largest frame. */
};
/** @copybrief cnor_image_parts */
typedef struct cnor_image_parts cnor_image_parts_t;

/** @brief Write @p parts into @p out. @return ::lh_bool_false without memory. */
lh_bool_t
cnor_image_write(const cnor_image_parts_t *parts, cnor_image_t *out);

#endif /* CNOR_SRC_IMAGE_WRITER_H */
