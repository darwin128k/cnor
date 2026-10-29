/**
 * @file op.h
 * @brief The instruction set (::cnor_op_t) and its description table
 *        (::cnor_op_info_t).
 *
 * One opcode is one byte, followed by at most three little-endian operands.
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
    cnor_op_band,         /**< left & right, bitwise. */
    cnor_op_bor,          /**< left | right, bitwise. */
    cnor_op_bxor,         /**< left ^ right, bitwise. */
    cnor_op_shl,          /**< left << right. */
    cnor_op_shr,          /**< left >> right. */
    cnor_op_not,          /**< Boolean negation of truthiness. */
    cnor_op_bnot,         /**< ~value, bitwise not. */
    cnor_op_jmp,          /**< Jump. Operand: s32 relative to the next instruction. */
    cnor_op_jmp_if_false, /**< Pop; jump when falsy. Operand: s32. */
    cnor_op_jmp_if_true,  /**< Pop; jump when truthy. Operand: s32. */
    cnor_op_call,         /**< Call a function. Operands: u16 index, u8 argc. */
    cnor_op_ret,          /**< Pop the result, drop the frame, push the result. */
    cnor_op_native,       /**< Call a native import. Operands: u16 index, u8 argc. */
    cnor_op_len,          /**< Length of a string in bytes. */
    cnor_op_index,        /**< One-byte string at an index of a string. */

    /* Operand forms of the binary operators (format v2): the same operation
     * as the plain opcode, with operands taken from local slots or from the
     * code instead of the stack. `_ll`: slot, slot; `_li`: slot, s32; `_i`:
     * the top of the stack and an s32, which replaces the top. The `_ll` and
     * `_li` forms push the result. Each family follows the order of
     * ::cnor_op_get_operand_form. */
    cnor_op_add_ll, /**< fp[a] + fp[b]. Operands: slot, slot. */
    cnor_op_sub_ll, /**< fp[a] - fp[b]. */
    cnor_op_mul_ll, /**< fp[a] * fp[b]. */
    cnor_op_div_ll, /**< fp[a] / fp[b]. */
    cnor_op_mod_ll, /**< fp[a] % fp[b]. */
    cnor_op_eq_ll,  /**< fp[a] == fp[b]. */
    cnor_op_ne_ll,  /**< fp[a] != fp[b]. */
    cnor_op_lt_ll,  /**< fp[a] < fp[b]. */
    cnor_op_le_ll,  /**< fp[a] <= fp[b]. */
    cnor_op_gt_ll,  /**< fp[a] > fp[b]. */
    cnor_op_ge_ll,  /**< fp[a] >= fp[b]. */
    cnor_op_band_ll, /**< fp[a] & fp[b]. Operands: slot, slot. */
    cnor_op_bor_ll,  /**< fp[a] | fp[b]. */
    cnor_op_bxor_ll, /**< fp[a] ^ fp[b]. */
    cnor_op_shl_ll,  /**< fp[a] << fp[b]. */
    cnor_op_shr_ll,  /**< fp[a] >> fp[b]. */
    cnor_op_add_li, /**< fp[a] + k. Operands: slot, s32. */
    cnor_op_sub_li, /**< fp[a] - k. */
    cnor_op_mul_li, /**< fp[a] * k. */
    cnor_op_div_li, /**< fp[a] / k. */
    cnor_op_mod_li, /**< fp[a] % k. */
    cnor_op_eq_li,  /**< fp[a] == k. */
    cnor_op_ne_li,  /**< fp[a] != k. */
    cnor_op_lt_li,  /**< fp[a] < k. */
    cnor_op_le_li,  /**< fp[a] <= k. */
    cnor_op_gt_li,  /**< fp[a] > k. */
    cnor_op_ge_li,  /**< fp[a] >= k. */
    cnor_op_band_li, /**< fp[a] & k. Operands: slot, s32. */
    cnor_op_bor_li,  /**< fp[a] | k. */
    cnor_op_bxor_li, /**< fp[a] ^ k. */
    cnor_op_shl_li,  /**< fp[a] << k. */
    cnor_op_shr_li,  /**< fp[a] >> k. */
    cnor_op_add_i,  /**< top + k, in place. Operand: s32. */
    cnor_op_sub_i,  /**< top - k. */
    cnor_op_mul_i,  /**< top * k. */
    cnor_op_div_i,  /**< top / k. */
    cnor_op_mod_i,  /**< top % k. */
    cnor_op_eq_i,   /**< top == k. */
    cnor_op_ne_i,   /**< top != k. */
    cnor_op_lt_i,   /**< top < k. */
    cnor_op_le_i,   /**< top <= k. */
    cnor_op_gt_i,   /**< top > k. */
    cnor_op_ge_i,   /**< top >= k. */
    cnor_op_band_i,  /**< top & k. Operand: s32. */
    cnor_op_bor_i,   /**< top | k. */
    cnor_op_bxor_i,  /**< top ^ k. */
    cnor_op_shl_i,   /**< top << k. */
    cnor_op_shr_i,   /**< top >> k. */
    /**
     * One step of `for v in a..b`: fp[v] = fp[v] + 1, then jump when
     * fp[v] < fp[b]; both by the rules of `add` and `lt`. Operands: s32 jump,
     * slot v, slot b.
     */
    cnor_op_for_loop,
    /**
     * Dispatch on an integer (format v3): `count` `jmp` instructions follow,
     * the table. When fp[v] == k + i by the rule of `==` for some i below
     * `count`, continue at the i-th of them; otherwise after the table.
     * Operands: slot v, s32 k, u8 count (at least 1).
     */
    cnor_op_switch,
    /**
     * The index operation with its operands in the frame or in the code
     * (format v5): `index_ll` takes a local and a local, `index_li` a local
     * and an s32, `index_i` the top of the stack and an s32, the same shapes
     * as the forms of the binary operators. The first two push the one-byte
     * string; the last one replaces the text on the stack with it.
     */
    cnor_op_index_ll, /**< fp[a][fp[b]]. Operands: slot, slot. */
    cnor_op_index_li, /**< fp[a][k]. Operands: slot, s32. */
    cnor_op_index_i,  /**< top[k]. Operand: s32. */
    /**
     * An array (format v6; the header of v18): the elements are slots of the
     * frame, the header that counts them is in the slot before them, and the
     * array's value (where the header is) in the slot before that.
     * `array_new` fills the elements and writes the header and the value;
     * `array_get_l` reads an element of the array in fp[a], `array_set_l`
     * writes one. The bounds are checked at run time.
     */
    cnor_op_array_new, /**< Make an array of fp[a] and its `count` elements. Operands: slot, u16. */
    /**
     * An empty array with room for `room` elements (format v18): the same
     * layout, but the count of the header is zero, so the array grows as
     * things are pushed into it.
     */
    cnor_op_array_room, /**< An empty array in fp[a] with room for `room` elements. Operands: slot, u16. */
    cnor_op_array_new_global, /**< The same, in the globals. Operands: u16 index, u16. */
    cnor_op_array_room_global, /**< The same, in the globals. Operands: u16 index, u16. */
    cnor_op_array_get_l, /**< An element of fp[a]. Operand: slot. */
    cnor_op_array_set_l, /**< Write an element of fp[a]. Operand: slot. */
    /**
     * An array that is a value (format v8): `array_get` reads an element of
     * the array on the stack, `array_set` writes one. They are how an array
     * that is not a local is read: a parameter, or a global.
     */
    cnor_op_array_get, /**< An element of the array below the index. Operands: none. */
    cnor_op_array_set, /**< Write an element of the array below the index. Operands: none. */
    /**
     * A dictionary (format v10): the pairs are slots of the frame, two per
     * pair (the key and the value), and the dictionary's value (where they are
     * and how many) is in the slot before them. The room the dictionary has is
     * in the slot before that, so a dictionary carries its room wherever it
     * goes. `dict_new` makes one, `dict_get_l` reads a value, `dict_set_l`
     * writes one, `dict_key_l` reads a key. The key is found by comparing the
     * texts.
     */
    cnor_op_dict_new, /**< A dictionary in fp[a] with room for `count` pairs. Operands: slot, u16. */
    cnor_op_dict_new_global, /**< The same, in the globals. Operands: u16 index, u16. */
    cnor_op_dict_get_l, /**< The value of a key of the dictionary in fp[a]. Operand: slot. */
    cnor_op_dict_set_l, /**< Write the value of a key of the dictionary in fp[a]. Operand: slot. */
    /**
     * A dictionary that is a value (format v10): `dict_get` reads a value of
     * the dictionary on the stack, `dict_set` writes one. They are how a
     * dictionary that is not a local is read: a global.
     */
    cnor_op_dict_get, /**< The value of a key of the dictionary below the key. Operands: none. */
    cnor_op_dict_set, /**< Write the value of a key of the dictionary below the key. Operands: none. */
    cnor_op_dict_key_l, /**< The key at an index of the dictionary in fp[a]. Operand: slot. */
    /**
     * A collection whose kind is not known when the code is written (format
     * v10): `collection_get_l` reads an element of an array or a key of a
     * dictionary, and the machine tells them apart.
     */
    cnor_op_collection_get_l, /**< An element of the array or key of the dictionary in fp[a]. Operand: slot. */
    /**
     * Error handling (format v12): `try_push` marks a catch block, and the
     * machine unwinds to it when an error happens inside the `try`. The
     * operand is the address of the catch block, relative to the next
     * instruction, and the slot the error's dictionary goes into. An address
     * of zero means the catch block is not there.
     */
    cnor_op_try_push, /**< Mark the catch block. Operands: s32, u8. */
    cnor_op_try_pop,  /**< The `try` ended without an error. Operands: none. */
    /**
     * The finally block of a `try` (format v12): the machine runs it when the
     * `try` is left, however it is left. The operand is the address of the
     * finally block, relative to the next instruction.
     */
    cnor_op_try_finally, /**< Mark the finally block. Operands: s32. */
    /**
     * The end of a `finally` block (format v12): the handler is gone, and the
     * error it was holding, if any, goes on outward. Operands: none.
     */
    cnor_op_finally_end,
    /**
     * Raise the error that is being handled (format v12): the machine gives it
     * to the next handler outward, as if it had just happened. Operands: none.
     */
    cnor_op_raise,
    /**
     * Leave a `try` that has a `finally` block (format v12): the finally block
     * runs, and the machine goes where the operand says afterwards. The
     * operand is that address, relative to the next instruction.
     */
    cnor_op_try_exit, /**< Leave the `try`. Operands: s32. */
    /**
     * Call a function of the language itself (format v7): the operand is the
     * ordinal of the function (`include/cnor/builtin.h`). The arguments are on
     * the stack, as for `native`, and the result takes their place.
     */
    cnor_op_call_builtin, /**< Call the builtin with that ordinal. Operand: u8. */
    /**
     * A function as a value (format v14): the operand is the index of the
     * function in the image's function table, and the function goes on the
     * stack as a value. A function takes no closure over the place it was
     * made in.
     */
    cnor_op_push_function, /**< Push the function with that index. Operand: u16. */
    /**
     * Call the function on the stack (format v14): the arguments follow it,
     * and the result takes their place. The function takes no closure over the
     * place it was made in.
     */
    cnor_op_call_value, /**< Call the function on the stack. Operand: u8. */
    /**
     * A key of the dictionary in a local that is a constant (format v15):
     * `settings["hp"]` reads the value without pushing the key first, as
     * `dict_get_l` does. A key that is not there gives `none`.
     */
    cnor_op_dict_get_lk, /**< The value of constant k in the dictionary in fp[a]. Operands: slot, u16. */
    /**
     * A local function (format v16): `call_local` jumps into the code of a
     * function declared inside the one being run. That code is a part of the
     * same function's, so it shares the frame: its parameters and the
     * variables it closes over are slots of that frame, and no new frame is
     * made. The operand is the address of that code, relative to the next
     * instruction; the second is the slot where the address to come back to
     * is kept.
     */
    cnor_op_call_local, /**< Call a local function. Operands: s32, slot. */
    /**
     * The end of a local function (format v16): the machine goes back to
     * where the call was made, which is the address in the slot. The result of
     * the call waits in a slot of its own, so nothing is left on the stack.
     */
    cnor_op_ret_local, /**< Return from a local function. Operand: slot. */
    cnor_op_count /**< Number of opcodes; any byte at or above is invalid. */
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
    cnor_operand_s32,      /**< 4 bytes, two's complement, little-endian. */
    cnor_operand_slot      /**< 1 byte, a local slot of the current frame. */
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
#define CNOR_OP_FLAG_POPS_ARGC0 0x10U /**< Also pops as many values as operand 0. */

/** @def CNOR_OP_MAX_OPERANDS @brief Operands an instruction can have. */
#define CNOR_OP_MAX_OPERANDS 3U

/** @def CNOR_OP_MAX_SIZE @brief Largest encoded instruction, in bytes. */
#define CNOR_OP_MAX_SIZE 10U

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

/**
 * @enum cnor_operand_form
 * @brief Where a binary operator finds its operands.
 */
enum cnor_operand_form
{
    cnor_operand_form_stack = 0,   /**< Both on the stack: the plain opcode. */
    cnor_operand_form_local_local, /**< `_ll`: two local slots. */
    cnor_operand_form_local_int,   /**< `_li`: a local slot and an s32. */
    cnor_operand_form_stack_int,   /**< `_i`: the top of the stack and an s32. */
    cnor_operand_form_count
};
/** @copybrief cnor_operand_form */
typedef enum cnor_operand_form cnor_operand_form_t;

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

/** @def CNOR_OP_SWITCH_ENTRY_SIZE @brief Bytes of one `switch` table entry, a `jmp`. */
#define CNOR_OP_SWITCH_ENTRY_SIZE 5U

/** @brief Target of a jump instruction; meaningful with ::CNOR_OP_FLAG_JUMP. */
lh_s64_t
cnor_insn_get_jump_target(const cnor_insn_t *insn);

/**
 * @brief The opcode for binary operator @p op (`add` .. `mod`, `eq` .. `ge`)
 *        with its operands in @p form.
 * @return ::cnor_op_count when @p op is not a binary operator.
 */
cnor_op_t
cnor_op_get_operand_form(cnor_op_t op, cnor_operand_form_t form);

/**
 * @brief The plain binary operator of an operand-form opcode, and the form
 *        through @p form; any other opcode is its own operator, on the stack.
 */
cnor_op_t
cnor_op_get_binary_base(cnor_op_t op, cnor_operand_form_t *form);

/**
 * @brief The opcode for the index operation with its operands in @p form.
 * @return ::cnor_op_count when @p form is not one of the three.
 */
cnor_op_t
cnor_op_get_index_form(cnor_operand_form_t form);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_OP_H */
