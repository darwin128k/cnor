/**
 * @file reader.h
 * @brief Library-private: a bounds-checked little-endian byte reader
 *        (::cnor_byte_reader_t).
 *
 * Every read checks the remaining length. A read past the end does not fault:
 * it returns 0 and marks the reader failed, so a section parser can read a
 * whole record and check once.
 *
 * @note Candidate lh brick (`lh_byte_reader_t`).
 */

#ifndef CNOR_SRC_BYTE_READER_H
#define CNOR_SRC_BYTE_READER_H

#include <lh/bool.h>
#include <lh/byte.h>
#include <lh/numeric/fixed/types.h>
#include <lh/str/view.h>

/**
 * @struct cnor_byte_reader
 * @brief A cursor over a byte range.
 */
struct cnor_byte_reader
{
    const lh_byte_t *data; /**< First byte. */
    lh_u32_t size;         /**< Length of the range. */
    lh_u32_t pos;          /**< Next byte to read. */
    lh_bool_t ok;          /**< Cleared by the first out-of-range read. */
};
/** @copybrief cnor_byte_reader */
typedef struct cnor_byte_reader cnor_byte_reader_t;

/** @brief Read [data, data + size). */
void
cnor_byte_reader_init(cnor_byte_reader_t *self, const lh_byte_t *data, lh_u32_t size);

/** @brief One byte, or 0 past the end. */
lh_u8_t
cnor_byte_reader_u8(cnor_byte_reader_t *self);

/** @brief Little-endian 16 bits, or 0 past the end. */
lh_u16_t
cnor_byte_reader_u16(cnor_byte_reader_t *self);

/** @brief Little-endian 32 bits, or 0 past the end. */
lh_u32_t
cnor_byte_reader_u32(cnor_byte_reader_t *self);

/** @brief A view of the next @p size bytes, or an empty view past the end. */
lh_str_view_t
cnor_byte_reader_view(cnor_byte_reader_t *self, lh_u32_t size);

/** @brief Whether every read so far stayed in range. */
lh_bool_t
cnor_byte_reader_is_ok(const cnor_byte_reader_t *self);

/** @brief Whether every byte has been read. */
lh_bool_t
cnor_byte_reader_at_end(const cnor_byte_reader_t *self);

#endif /* CNOR_SRC_BYTE_READER_H */
