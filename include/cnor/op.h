/**
 * @file op.h
 * @brief The instruction set (::cnor_op_t) and its description table
 *        (::cnor_op_info_t).
 *
 * One opcode is one byte, followed by at most two little-endian operands.
 * Everything the compiler, the verifier and the machine need to know about an
 * opcode besides what it does lives in its ::cnor_op_info_t row: operand
 * layout, encoded size, stack effect and control-flow flags. The compiler
 * tracks stack depth from that row, and the verifier checks the same row, so the
 * two cannot disagree.
 *
 * The numbers are the image format: a change bumps ::CNOR_IMAGE_VERSION.
 */

#ifndef CNOR_OP_H
#define CNOR_OP_H

#include <cnor/error.h>
#include <cnor/types.h>
#include <lh/bool.h>
#include <lh/byte.h>
#include <lh/char.h>
#include <lh/compiler/extern/c.h>

/**
 * @enum cnor_op
 * @brief One instruction.
 */
enum cnor_op
{
    cnor_op_halt = 0,     /**< Stop; the top of the stack (or none) is the result. */
    cnor_op_push_none,    /**< Push none. */
    cnor_op_push_true,    /**< Push true. */
    cnor_op_push_false,   /**< Push false. */
    cnor_op_push_i32,     /**< Push a signed integer. Operand: s32. */
    cnor_op_push_u32,     /**< Push an unsigned integer. Operand: u32. */
    cnor_op_push_const,   /**< Push constant #n (ordinal). Operand: u16. */
    cnor_op_load_local,   /**< Push local slot. Operand: u8. */
    cnor_op_store_local,  /**< Pop into local slot. Operand: u8. */
    cnor_op_load_global,  /**< Push global. Operand: u16. */
    cnor_op_store_global, /**< Pop into global. Operand: u16. */
    cnor_op_dup,          /**< Duplicate the top value. */
    cnor_op_swap,         /**< Exchange the top two values. */
    cnor_op_pop,          /**< Drop the top value. */
    cnor_op_add,          /**< left + right; strings concatenate. */
    cnor_op_sub,          /**< left - right. */
    cnor_op_mul,          /**< left * right. */
    cnor_op_div,          /**< left / right; zero is ::cnor_error_div_by_zero. */
    cnor_op_mod,          /**< left % right; zero is ::cnor_error_div_by_zero. */
    cnor_op_neg,          /**< Arithmetic negation. */
    cnor_op_eq,           /**< left == right, any kinds. */
    cnor_op_ne,           /**< left != right, any kinds. */
    cnor_op_lt,           /**< left < right, numbers. */
    cnor_op_le,           /**< left <= right, numbers. */
    cnor_op_gt,           /**< left > right, numbers. */
    cnor_op_ge,           /**< left >= right, numbers. */
    cnor_op_not,          /**< Boolean negation of truthiness. */
    cnor_op_jmp,          /**< Jump. Operand: s32 relative to the next instruction. */
    cnor_op_jmp_if_false, /**< Pop; jump when falsy. Operand: s32. */
    cnor_op_jmp_if_true,  /**< Pop; jump when truthy. Operand: s32. */
    cnor_op_call,         /**< Call a function. Operands: u16 index, u8 argc. */
    cnor_op_ret,          /**< Pop the result, drop the frame, push the result. */
    cnor_op_native,       /**< Call a native import. Operands: u16 index, u8 argc. */
    cnor_op_len,          /**< Length of a string in bytes. */
    cnor_op_index,        /**< One-byte string at an index of a string. */
    cnor_op_count         /**< Number of opcodes; any byte at or above is invalid. */
};
/** @copybrief cnor_op */
typedef enum cnor_op cnor_op_t;

/**
 * @enum cnor_operand_kind
 * @brief Encoding of one operand.
 */
enum cnor_operand_kind
{
    cnor_operand_none = 0, /**< No operand. */
    cnor_operand_u8,       /**< 1 byte, unsigned. */
    cnor_operand_u16,      /**< 2 bytes, unsigned, little-endian. */
    cnor_operand_u32,      /**< 4 bytes, unsigned, little-endian. */
    cnor_operand_s32       /**< 4 bytes, two's complement, little-endian. */
};
/** @copybrief cnor_operand_kind */
typedef enum cnor_operand_kind cnor_operand_kind_t;

/**
 * @typedef cnor_op_flags_t
 * @brief Bit set of `CNOR_OP_FLAG_*`.
 */
typedef lh_u8_t cnor_op_flags_t;

#define CNOR_OP_FLAG_JUMP 0x01U       /**< First operand is a relative jump. */
#define CNOR_OP_FLAG_TERMINATOR 0x02U /**< Control never falls through. */
#define CNOR_OP_FLAG_POPS_ARGC 0x04U  /**< Also pops as many values as operand 1. */

/** @def CNOR_OP_MAX_OPERANDS @brief Operands an instruction can have. */
#define CNOR_OP_MAX_OPERANDS 2U

/** @def CNOR_OP_MAX_SIZE @brief Largest encoded instruction, in bytes. */
#define CNOR_OP_MAX_SIZE 5U

/**
 * @struct cnor_op_info
 * @brief Static description of one opcode.
 */
struct cnor_op_info
{
    const lh_char_t *name;                               /**< Mnemonic. */
    cnor_operand_kind_t operands[CNOR_OP_MAX_OPERANDS]; /**< Operand layout. */
    lh_u8_t size;                                        /**< Encoded size, opcode included. */
    lh_u8_t pops;                                        /**< Fixed values popped. */
    lh_u8_t pushes;                                      /**< Values pushed. */
    cnor_op_flags_t flags;                               /**< `CNOR_OP_FLAG_*`. */
};
/** @copybrief cnor_op_info */
typedef struct cnor_op_info cnor_op_info_t;

/**
 * @struct cnor_insn
 * @brief One decoded instruction.
 */
struct cnor_insn
{
    cnor_op_t op;                            /**< Opcode. */
    cnor_code_addr_t at;                     /**< Address of the opcode byte. */
    cnor_code_addr_t next;                   /**< Address of the next instruction. */
    lh_u32_t operands[CNOR_OP_MAX_OPERANDS]; /**< Raw operand values (s32 as bits). */
};
/** @copybrief cnor_insn */
typedef struct cnor_insn cnor_insn_t;

LH_COMPILER_EXTERN_C_BEGIN

/** @brief Whether @p byte is an opcode. */
lh_bool_t
cnor_op_is_valid(lh_u32_t byte);

/** @brief Row of @p op; @p op must be valid. */
const cnor_op_info_t *
cnor_op_get_info(cnor_op_t op);

/** @brief Encoded size of @p kind in bytes. */
lh_u8_t
cnor_operand_get_size(cnor_operand_kind_t kind);

/**
 * @brief Values @p insn pops, the argument count of a call included.
 */
lh_u32_t
cnor_insn_get_pops(const cnor_insn_t *insn);

/**
 * @brief Decode the instruction at @p at in @p code of @p size bytes.
 *
 * @return ::cnor_error_none, ::cnor_error_bad_opcode or
 *         ::cnor_error_code_truncated when the operands run past @p size.
 */
cnor_error_t
cnor_insn_decode(const lh_byte_t *code, cnor_code_addr_t size, cnor_code_addr_t at,
                 cnor_insn_t *out);

/** @brief Target of a jump instruction; meaningful with ::CNOR_OP_FLAG_JUMP. */
lh_s64_t
cnor_insn_get_jump_target(const cnor_insn_t *insn);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_OP_H */
