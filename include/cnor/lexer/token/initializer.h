/**
 * @file initializer.h
 * @brief Brace initializers for ::cnor_token_t.
 */

#ifndef CNOR_LEXER_TOKEN_INITIALIZER_H
#define CNOR_LEXER_TOKEN_INITIALIZER_H

#include <lh/initializer.h>

/**
 * @def cnor_token_initializer(kind, punct, keyword, start, end, line)
 * @brief Initializer with every field given.
 */
#define cnor_token_initializer(kind, punct, keyword, start, end, line)                             \
    lh_initializer(kind, punct, keyword, start, end, line)

/**
 * @def cnor_token_eof_initializer()
 * @brief An end-of-input token at the very start of the source.
 */
#define cnor_token_eof_initializer()                                                               \
    cnor_token_initializer(cnor_token_eof, cnor_punct_none, cnor_keyword_none, 0U, 0U, 1U)

#endif /* CNOR_LEXER_TOKEN_INITIALIZER_H */
