/**
 * @file writer.h
 * @brief Library-private: a growable little-endian byte writer
 *        (::cnor_byte_writer_t) over a runtime-allocator block.
 *
 * Used for the code section, the constant pool and the final image. Fields are
 * written byte by byte with lh_bit_pack_le*, never by copying a struct, so the
 * result does not depend on the host's layout or endianness. Every write is
 * one capacity test and a few stores; the block grows by doubling, from
 * ::CNOR_BYTE_WRITER_MIN_CAPACITY, so the compiler does not reallocate for
 * each of the first few dozen bytes of a section.
 *
 * @note Candidate lh brick (`lh_byte_writer_t`).
 */

#ifndef CNOR_SRC_BYTE_WRITER_H
#define CNOR_SRC_BYTE_WRITER_H

#include <lh/bool.h>
#include <lh/byte.h>
#include <lh/numeric/fixed/types.h>

/** @brief Bytes the block gets when the first byte is written. */
#define CNOR_BYTE_WRITER_MIN_CAPACITY 64U

/**
 * @struct cnor_byte_writer
 * @brief Bytes written so far.
 */
struct cnor_byte_writer
{
    lh_byte_t *data;   /**< `capacity` bytes, or ::lh_null before the first write. */
    lh_u32_t size;     /**< Bytes written. */
    lh_u32_t capacity; /**< Bytes the block holds. */
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

/** @brief Grow the block to hold at least @p capacity bytes (a hint: nothing
 *         changes when it cannot). */
void
cnor_byte_writer_reserve(cnor_byte_writer_t *self, lh_u32_t capacity);

/** @brief Append @p size bytes from @p data; nothing when the block cannot grow. */
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
 * @brief Hand the bytes over in a runtime-allocator block, cut to their size,
 *        owned by the caller.
 *
 * The writer is empty afterwards.
 *
 * @param[out] size Receives the length.
 * @return The block, or ::lh_null when empty.
 */
lh_byte_t *
cnor_byte_writer_release(cnor_byte_writer_t *self, lh_u32_t *size);

#endif /* CNOR_SRC_BYTE_WRITER_H */
