/**
 * @file token.h
 * @brief What the lexer hands the parser (::cnor_token_t) and the closed sets
 *        a token draws from: kinds, keywords and punctuators.
 *
 * A token points into the source and owns nothing. Indentation is resolved by
 * the lexer, so the parser only sees ::cnor_token_newline, ::cnor_token_indent
 * and ::cnor_token_dedent as ordinary tokens.
 */

#ifndef CNOR_LEXER_TOKEN_H
#define CNOR_LEXER_TOKEN_H

#include <cnor/lexer/token/fields.h>
#include <cnor/lexer/token/initializer.h>
#include <cnor/types.h>

/**
 * @enum cnor_token_kind
 * @brief Which sort of thing a token is.
 */
enum cnor_token_kind
{
    cnor_token_eof = 0, /**< End of input; repeated forever once reached. */
    cnor_token_newline, /**< End of a logical line that carried tokens. */
    cnor_token_indent,  /**< A deeper block starts on this line. */
    cnor_token_dedent,  /**< One block closed; one token per level. */
    cnor_token_ident,   /**< A name that is not a keyword. */
    cnor_token_int,     /**< Whole number: decimal, `0x` hex or `0b` binary. */
    cnor_token_real,    /**< Number with a fraction: `1.5`. */
    cnor_token_string,  /**< Double-quoted text; spans the quotes, escapes raw. */
    cnor_token_keyword, /**< A reserved word; which one is in `keyword`. */
    cnor_token_punct,   /**< Punctuation or operator; which one is in `punct`. */
    cnor_token_kind_count
};
/** @copybrief cnor_token_kind */
typedef enum cnor_token_kind cnor_token_kind_t;

/**
 * @enum cnor_keyword
 * @brief Which reserved word a keyword token carries. 0 is "not a keyword".
 */
enum cnor_keyword
{
    cnor_keyword_none = 0, /**< Not a reserved word. */
    cnor_keyword_fn,       /**< `fn` */
    cnor_keyword_let,      /**< `let` */
    cnor_keyword_mut,      /**< `mut` */
    cnor_keyword_if,       /**< `if` */
    cnor_keyword_else,     /**< `else` */
    cnor_keyword_match,    /**< `match` */
    cnor_keyword_for,      /**< `for` */
    cnor_keyword_in,       /**< `in` (not in the spec list, needed by `for`) */
    cnor_keyword_while,    /**< `while` */
    cnor_keyword_return,   /**< `return` */
    cnor_keyword_break,    /**< `break` */
    cnor_keyword_continue, /**< `continue` */
    cnor_keyword_true,     /**< `true` */
    cnor_keyword_false,    /**< `false` */
    cnor_keyword_absent,   /**< `none` (not in the spec list, the absent value) */
    cnor_keyword_and,      /**< `and` */
    cnor_keyword_or,       /**< `or` */
    cnor_keyword_not,      /**< `not` */
    cnor_keyword_import,   /**< `import` */
    cnor_keyword_native,   /**< `native` */
    cnor_keyword_count
};
/** @copybrief cnor_keyword */
typedef enum cnor_keyword cnor_keyword_t;

/**
 * @enum cnor_punct
 * @brief Which punctuator a punct token carries. 0 is "not punctuation".
 */
enum cnor_punct
{
    cnor_punct_none = 0,  /**< Not punctuation. */
    cnor_punct_lparen,    /**< `(` */
    cnor_punct_rparen,    /**< `)` */
    cnor_punct_lbracket,  /**< `[` */
    cnor_punct_rbracket,  /**< `]` */
    cnor_punct_colon,     /**< `:` */
    cnor_punct_semicolon, /**< `;` */
    cnor_punct_comma,     /**< `,` */
    cnor_punct_dot,       /**< `.` */
    cnor_punct_range,     /**< `..` */
    cnor_punct_plus,      /**< `+` */
    cnor_punct_minus,     /**< `-` */
    cnor_punct_star,      /**< `*` */
    cnor_punct_slash,     /**< `/` */
    cnor_punct_percent,   /**< `%` */
    cnor_punct_assign,    /**< `=` */
    cnor_punct_eq,        /**< `==` */
    cnor_punct_ne,        /**< `!=` */
    cnor_punct_lt,        /**< `<` */
    cnor_punct_le,        /**< `<=` */
    cnor_punct_gt,        /**< `>` */
    cnor_punct_ge,        /**< `>=` */
    cnor_punct_arrow,     /**< `->` */
    cnor_punct_fat_arrow, /**< `=>` */
    cnor_punct_amp,       /**< `&` */
    cnor_punct_pipe,      /**< `|` */
    cnor_punct_caret,     /**< `^` */
    cnor_punct_tilde,     /**< `~` */
    cnor_punct_shift_left,  /**< `<<` */
    cnor_punct_shift_right, /**< `>>` */
    cnor_punct_count
};
/** @copybrief cnor_punct */
typedef enum cnor_punct cnor_punct_t;

/**
 * @struct cnor_token
 * @brief One token. Fields via ::cnor_token_fields.
 */
struct cnor_token
{
    /** Members: ::cnor_token_fields. */
    cnor_token_fields(cnor_token_kind_t, cnor_punct_t, cnor_keyword_t, cnor_source_offset_t,
                      cnor_line_t);
};
/** @copybrief cnor_token */
typedef struct cnor_token cnor_token_t;

#endif /* CNOR_LEXER_TOKEN_H */
