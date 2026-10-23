/**
 * @file fields.h
 * @brief Member fields of ::cnor_token_t.
 */

#ifndef CNOR_LEXER_TOKEN_FIELDS_H
#define CNOR_LEXER_TOKEN_FIELDS_H

/**
 * @def cnor_token_fields(kind_type, punct_type, keyword_type, offset_type, line_type)
 * @brief What a token is and where it sits in the source.
 *
 * `punct` is meaningful for ::cnor_token_punct only, `keyword` for
 * ::cnor_token_keyword only. `start` and `end` are byte offsets, `end` one
 * past the last byte.
 */
#define cnor_token_fields(kind_type, punct_type, keyword_type, offset_type, line_type)             \
    kind_type kind;                                                                                \
    punct_type punct;                                                                              \
    keyword_type keyword;                                                                          \
    offset_type start;                                                                             \
    offset_type end;                                                                               \
    line_type line

#endif /* CNOR_LEXER_TOKEN_FIELDS_H */
