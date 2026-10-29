/**
 * @file builtin.h
 * @brief The functions the language itself provides (::cnor_builtin_t).
 *
 * A builtin is a function of the language, not of the host: it is called like
 * any other function, but its body lives in the machine, which is why it can
 * put a string into the machine's string pool. The host may still provide a
 * native of the same name, and then the host's wins.
 *
 * The compiler knows the names, the argument types and the result type
 * (`src/cnor/compiler/builtin.c`); the machine knows the bodies
 * (`src/cnor/vm/op/builtin.c`).
 */

#ifndef CNOR_BUILTIN_H
#define CNOR_BUILTIN_H

#include <lh/compiler/extern/c.h>
#include <lh/numeric/fixed/types.h>

/**
 * @enum cnor_builtin
 * @brief Which function a `call_builtin` instruction calls.
 *
 * The numbers are part of the image format: a new function is appended, and
 * the format version grows.
 */
typedef enum cnor_builtin
{
    cnor_builtin_find = 0,    /**< `find(text, needle)`: the index of the first occurrence, or -1. */
    cnor_builtin_contains,    /**< `contains(text, needle)`: whether the needle is in the text. */
    cnor_builtin_replace,     /**< `replace(text, from, to)`: every occurrence of `from` replaced. */
    cnor_builtin_trim,        /**< `trim(text)`: without the spaces and tabs around it. */
    cnor_builtin_upper,       /**< `upper(text)`: in upper case (ASCII). */
    cnor_builtin_lower,       /**< `lower(text)`: in lower case (ASCII). */
    cnor_builtin_starts_with, /**< `starts_with(text, prefix)`. */
    cnor_builtin_ends_with,   /**< `ends_with(text, suffix)`. */
    cnor_builtin_substr,      /**< `substr(text, start, length)`: `length` bytes from `start`. */
    cnor_builtin_ord,         /**< `ord(text)`: the code of the first byte, or -1. */
    cnor_builtin_char,        /**< `char(code)`: the one-byte string of that code. */
    cnor_builtin_str,         /**< `str(value)`: the value as text. */
    cnor_builtin_parse_int,   /**< `parse_int(text)`: the number in the text, or 0. */
    /**
     * `split_into(text, separator, out)`: the pieces of the text, written into
     * the array, and how many there are. A collection cannot be made while the
     * program runs, so the array is made by the program and given here.
     */
    cnor_builtin_split_into,
    cnor_builtin_min,   /**< `min(a, b)`: the smaller of two numbers. */
    cnor_builtin_max,   /**< `max(a, b)`: the larger of two numbers. */
    cnor_builtin_abs,   /**< `abs(x)`: the number without its sign. */
    cnor_builtin_clamp, /**< `clamp(x, low, high)`: the number inside the bounds. */
    cnor_builtin_floor, /**< `floor(x)`: the whole number below the real one. */
    cnor_builtin_ceil,  /**< `ceil(x)`: the whole number above the real one. */
    cnor_builtin_round, /**< `round(x)`: the whole number nearest to the real one. */
    cnor_builtin_int,   /**< `int(x)`: the whole number the real one is cut to. */
    /**
     * `fail(text)`: the script fails with its own text. The call never comes
     * back; a `catch` above takes the text as the error's message.
     */
    cnor_builtin_fail,
    cnor_builtin_count
} cnor_builtin_t;

/**
 * @brief The name of the builtin, as the script writes it.
 * @return The name; ::lh_null for an ordinal out of range.
 */
const lh_char_t *
cnor_builtin_name(cnor_builtin_t builtin);

/**
 * @brief How many arguments the builtin takes.
 * @return The count; 0 for an ordinal out of range.
 */
lh_u8_t
cnor_builtin_argc(cnor_builtin_t builtin);

#endif /* CNOR_BUILTIN_H */
