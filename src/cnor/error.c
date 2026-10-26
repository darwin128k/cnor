#include <cnor/error.h>

/* Indexed by ::cnor_error_t; the order must follow the enum. */
static const lh_char_t *const m_messages[cnor_error_count] = {
    "no error",
    "out of memory",
    "unterminated string",
    "bad character",
    "dedent to a column no block opened at",
    "blocks nested too deep",
    "unexpected token",
    "empty block",
    "unknown type",
    "bad number literal",
    "bad escape sequence",
    "expected an expression",
    "real numbers are not enabled in this build",
    "`import` must come first in a file, at the top level",
    "unknown name",
    "name declared twice",
    "cannot assign to this",
    "a function cannot return an array",
    "assignment to a binding without `mut`",
    "`break` or `continue` outside of a loop",
    "too many locals in one function",
    "too many globals",
    "too many constants",
    "too many functions",
    "too many arguments",
    "wrong number of arguments",
    "construct has no compiler module",
    "type mismatch",
    "operator cannot take these types",
    "function can end without returning a value",
    "global used before its `let`",
    "name defined by two imported files",
    "imported file not found",
    "imported file cannot be read",
    "files import each other",
    "imports nested too deep",
    "too many files",
    "an array is only a declaration",
    "image shorter than its header",
    "bad image signature",
    "unsupported image version",
    "inconsistent image header",
    "malformed image section",
    "program needs more stack than allowed",
    "instruction runs past its function",
    "bad opcode",
    "operand out of range",
    "bad jump target",
    "inconsistent stack depth",
    "function can run off its end",
    "no program loaded",
    "stack overflow",
    "wrong value kind",
    "division by zero",
    "shift out of range",
    "index out of range",
    "native not provided by the host",
    "native called with a wrong argument count",
    "native failed",
    "calls nested too deep",
    "string pool is full",
    "instruction budget exhausted",
};

const lh_str_ptr
cnor_error_get_message(cnor_error_t self)
{
    if ((lh_uint_t) self >= (lh_uint_t) cnor_error_count)
    {
        return "unknown error";
    }
    return m_messages[self];
}
