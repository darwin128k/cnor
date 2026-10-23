/**
 * @file writer.h
 * @brief Library-private: a growable little-endian byte writer
 *        (::cnor_byte_writer_t) over an ::lh_array_t of bytes.
 *
 * Used for the code section, the constant pool and the final image. Fields are
 * written byte by byte with lh_bit_pack_le*, never by copying a struct, so the
 * result does not depend on the host's layout or endianness.
 *
 * @note Candidate lh brick (`lh_byte_writer_t`).
 */

#ifndef CNOR_SRC_BYTE_WRITER_H
#define CNOR_SRC_BYTE_WRITER_H

#include <lh/array.h>
#include <lh/bool.h>
#include <lh/byte.h>
#include <lh/numeric/fixed/types.h>

/**
 * @struct cnor_byte_writer
 * @brief Bytes written so far.
 */
struct cnor_byte_writer
{
    lh_array_t bytes; /**< Element size 1. */
};
/** @copybrief cnor_byte_writer */
typedef struct cnor_byte_writer cnor_byte_writer_t;

/** @brief Start empty. */
void
cnor_byte_writer_init(cnor_byte_writer_t *self);

/** @brief Release the buffer. */
void
cnor_byte_writer_deinit(cnor_byte_writer_t *self);

/** @brief Bytes written. */
lh_u32_t
cnor_byte_writer_get_size(const cnor_byte_writer_t *self);

/** @brief First byte, or ::lh_null when empty. */
lh_byte_t *
cnor_byte_writer_get_data(const cnor_byte_writer_t *self);

/** @brief Append @p size bytes from @p data. */
void
cnor_byte_writer_put_bytes(cnor_byte_writer_t *self, const lh_byte_t *data, lh_u32_t size);

/** @brief Append one byte. */
void
cnor_byte_writer_put_u8(cnor_byte_writer_t *self, lh_u8_t value);

/** @brief Append a little-endian 16-bit value. */
void
cnor_byte_writer_put_u16(cnor_byte_writer_t *self, lh_u16_t value);

/** @brief Append a little-endian 32-bit value. */
void
cnor_byte_writer_put_u32(cnor_byte_writer_t *self, lh_u32_t value);

/** @brief Append zero bytes until the size is a multiple of @p alignment. */
void
cnor_byte_writer_align(cnor_byte_writer_t *self, lh_u32_t alignment);

/** @brief Overwrite 4 bytes at @p at with a little-endian @p value. */
void
cnor_byte_writer_patch_u32(cnor_byte_writer_t *self, lh_u32_t at, lh_u32_t value);

/**
 * @brief Move the bytes into a fresh runtime-allocator block owned by the caller.
 *
 * The writer is empty afterwards.
 *
 * @param[out] size Receives the length.
 * @return The block, or ::lh_null when empty.
 */
lh_byte_t *
cnor_byte_writer_release(cnor_byte_writer_t *self, lh_u32_t *size);

#endif /* CNOR_SRC_BYTE_WRITER_H */
