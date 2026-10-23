/**
 * @file view.h
 * @brief Library-private: a checked, non-owning view of an image's sections
 *        (::cnor_image_view_t), plus record readers for each table.
 *
 * Opening a view checks the header and that every table section parses to its
 * exact end. What the code does is the verifier's business (verify.h).
 */

#ifndef CNOR_SRC_IMAGE_VIEW_H
#define CNOR_SRC_IMAGE_VIEW_H

#include <cnor/byte/reader.h>
#include <cnor/image.h>
#include <lh/str/view.h>

/**
 * @struct cnor_image_view
 * @brief A checked image.
 */
struct cnor_image_view
{
    cnor_image_header_t header;  /**< Decoded header. */
    const lh_byte_t *data;       /**< Image bytes; borrowed. */
    const lh_byte_t *code;       /**< Code section. */
    cnor_code_addr_t code_size;  /**< Its length. */
    cnor_count_t const_count;    /**< Constant pool entries. */
    cnor_count_t function_count; /**< Function table entries. */
    cnor_count_t native_count;   /**< Native imports. */
    cnor_count_t line_count;     /**< Line table entries. */
    cnor_count_t file_count;     /**< File table entries. */
};
/** @copybrief cnor_image_view */
typedef struct cnor_image_view cnor_image_view_t;

/** @brief Check @p data and describe it in @p self. */
cnor_error_t
cnor_image_view_open(cnor_image_view_t *self, const lh_byte_t *data, lh_u32_t size);

/**
 * @brief A reader over the records of a table section, positioned after its
 *        count.
 */
void
cnor_image_view_records(const cnor_image_view_t *self, cnor_image_section_t section,
                        cnor_byte_reader_t *out);

/** @brief Function table entry @p index (< `function_count`). */
cnor_image_function_t
cnor_image_view_function_at(const cnor_image_view_t *self, cnor_function_index_t index);

/** @brief Read one constant record. */
lh_bool_t
cnor_image_const_read(cnor_byte_reader_t *reader, cnor_const_kind_t *kind, lh_str_view_t *bytes);

/** @brief Read one native import record. */
lh_bool_t
cnor_image_native_read(cnor_byte_reader_t *reader, lh_str_view_t *name);

/** @brief Read one file table record (the name may be empty). */
lh_bool_t
cnor_image_file_read(cnor_byte_reader_t *reader, lh_str_view_t *name);

/** @brief Source line of the instruction at @p addr, or 0 without a line table. */
cnor_line_t
cnor_image_view_line_at(const cnor_image_view_t *self, cnor_code_addr_t addr);

/** @brief Name of file @p index, or an empty view without a file table. */
lh_str_view_t
cnor_image_view_file(const cnor_image_view_t *self, cnor_file_index_t index);

/** @brief Function whose code holds @p addr (< `code_size`); functions tile the code. */
cnor_function_index_t
cnor_image_view_function_of(const cnor_image_view_t *self, cnor_code_addr_t addr);

#endif /* CNOR_SRC_IMAGE_VIEW_H */
