/**
 * @file ast.h
 * @brief The syntax tree (::cnor_node_t) the parser builds and the compiler walks.
 *
 * Every node is one struct: a kind, the line it starts on, an optional name, a
 * sibling link and a union of per-kind members. Lists (statements, parameters,
 * arguments, match arms) are chained through `next`. Nodes live in a
 * ::cnor_arena_t and are released with it.
 *
 * Adding a construct starts here: a new ::cnor_node_kind_t value and, when it
 * carries data, a member of ::cnor_node_data. See docs/architecture.md.
 */

#ifndef CNOR_AST_H
#define CNOR_AST_H

#include <cnor/types.h>
#include <lh/bool.h>
#include <lh/compiler/extern/c.h>
#include <lh/str/view.h>

/**
 * @enum cnor_node_kind
 * @brief Which construct a node is. Also the key of the compiler's rule table.
 */
enum cnor_node_kind
{
    /* declarations */
    cnor_node_fn = 0,     /**< `fn name(params) -> type:` + body. */
    cnor_node_native_fn,  /**< `native fn name(params) -> type`. */
    cnor_node_import,     /**< `import "path"`; the unescaped path is in `name`. */
    cnor_node_param,      /**< One parameter: `name: type`. */
    /* statements */
    cnor_node_let,        /**< `let [mut] name [: type] [= init]`. */
    cnor_node_assign,     /**< `target = value`. */
    cnor_node_expr_stmt,  /**< An expression used as a statement. */
    cnor_node_if,         /**< `if cond:` block [`else:` block | `else if`]. */
    cnor_node_while,      /**< `while cond:` block. */
    cnor_node_for,        /**< `for name in from..to:` block. */
    cnor_node_match,      /**< `match subject:` arms. */
    cnor_node_match_arm,  /**< `pattern => statement`; `_` has no pattern. */
    cnor_node_return,     /**< `return [value]`. */
    cnor_node_break,      /**< `break`. */
    cnor_node_continue,   /**< `continue`. */
    /* expressions */
    cnor_node_ident,      /**< A name in use. */
    cnor_node_int,        /**< Whole number literal. */
    cnor_node_real,       /**< Real literal; its text is in `name`. */
    cnor_node_string,     /**< String literal; unescaped text is in `name`. */
    cnor_node_true,       /**< `true`. */
    cnor_node_false,      /**< `false`. */
    cnor_node_none,       /**< `none`. */
    cnor_node_call,       /**< `name(args)`; callee name is in `name`. */
    cnor_node_binary,     /**< `left op right`. */
    cnor_node_unary,      /**< `op operand`. */
    cnor_node_index,      /**< `target[index]`. */
    cnor_node_array,      /**< `[value; count]`: an array of `count` elements. */
    cnor_node_kind_count  /**< Number of kinds; not a kind itself. */
};
/** @copybrief cnor_node_kind */
typedef enum cnor_node_kind cnor_node_kind_t;

/**
 * @enum cnor_type
 * @brief A type annotation as written. Annotations are parsed and kept, but not
 *        checked yet: values carry their kind at run time.
 */
enum cnor_type
{
    cnor_type_none = 0, /**< No annotation. */
    cnor_type_i32,      /**< `i32` */
    cnor_type_u32,      /**< `u32` */
    cnor_type_f32,      /**< `f32` */
    cnor_type_bool,     /**< `bool` */
    cnor_type_str,      /**< `str` */
    cnor_type_array,    /**< `array` */
    cnor_type_count
};
/** @copybrief cnor_type */
typedef enum cnor_type cnor_type_t;

/**
 * @enum cnor_binary_op
 * @brief Operator of a ::cnor_node_binary node.
 */
enum cnor_binary_op
{
    cnor_binary_add = 0, /**< `+` */
    cnor_binary_sub,     /**< `-` */
    cnor_binary_mul,     /**< `*` */
    cnor_binary_div,     /**< `/` */
    cnor_binary_mod,     /**< `%` */
    cnor_binary_eq,      /**< `==` */
    cnor_binary_ne,      /**< `!=` */
    cnor_binary_lt,      /**< `<` */
    cnor_binary_le,      /**< `<=` */
    cnor_binary_gt,      /**< `>` */
    cnor_binary_ge,      /**< `>=` */
    cnor_binary_and,     /**< `and`, short-circuit */
    cnor_binary_or,      /**< `or`, short-circuit */
    cnor_binary_band,    /**< `&`, bitwise and */
    cnor_binary_bor,     /**< `|`, bitwise or */
    cnor_binary_bxor,    /**< `^`, bitwise exclusive or */
    cnor_binary_shl,     /**< `<<`, shift left */
    cnor_binary_shr,     /**< `>>`, shift right */
    cnor_binary_count
};
/** @copybrief cnor_binary_op */
typedef enum cnor_binary_op cnor_binary_op_t;

/**
 * @enum cnor_unary_op
 * @brief Operator of a ::cnor_node_unary node.
 */
enum cnor_unary_op
{
    cnor_unary_neg = 0, /**< `-x` */
    cnor_unary_not,     /**< `not x` */
    cnor_unary_bnot,    /**< `~x`, bitwise not */
    cnor_unary_count
};
/** @copybrief cnor_unary_op */
typedef enum cnor_unary_op cnor_unary_op_t;

/**
 * @typedef cnor_int_literal_t
 * @brief Value of a whole number literal. Literals are at most 32 bits wide.
 */
typedef lh_u32_t cnor_int_literal_t;

struct cnor_node;
/** @copybrief cnor_node */
typedef struct cnor_node cnor_node_t;

/**
 * @union cnor_node_data
 * @brief Per-kind members. The member named after the kind is the valid one.
 */
union cnor_node_data
{
    struct
    {
        cnor_node_t *params;     /**< ::cnor_node_param list. */
        cnor_node_t *body;       /**< Statement list; ::lh_null for a native. */
        cnor_type_t returns;     /**< `->` type or ::cnor_type_none. */
        cnor_argc_t param_count; /**< Length of `params`. */
    } fn;
    struct
    {
        cnor_type_t type; /**< Annotated type. */
    } param;
    struct
    {
        cnor_node_t *init;    /**< Initializer or ::lh_null (binds `none`). */
        cnor_type_t type;     /**< Annotated type or ::cnor_type_none. */
        lh_bool_t is_mutable; /**< Declared with `mut`. */
    } let;
    struct
    {
        cnor_node_t *target; /**< What is written; an identifier. */
        cnor_node_t *value;  /**< What is written into it. */
    } assign;
    struct
    {
        cnor_node_t *expr; /**< The expression. */
    } expr_stmt;
    struct
    {
        cnor_node_t *cond;      /**< Condition. */
        cnor_node_t *then_body; /**< Statements run when true. */
        cnor_node_t *else_body; /**< Statements run when false, or ::lh_null. */
    } if_;
    struct
    {
        cnor_node_t *cond; /**< Condition, tested before every iteration. */
        cnor_node_t *body; /**< Statements. */
    } while_;
    struct
    {
        cnor_node_t *from; /**< First value. */
        cnor_node_t *to;   /**< End, exclusive; evaluated once. */
        cnor_node_t *body; /**< Statements; the variable name is in `name`. */
    } for_;
    struct
    {
        cnor_node_t *subject; /**< Value matched on; evaluated once. */
        cnor_node_t *arms;    /**< ::cnor_node_match_arm list, in source order. */
    } match;
    struct
    {
        cnor_node_t *pattern; /**< Literal compared with `==`, ::lh_null for `_`. */
        cnor_node_t *body;    /**< Statement list run when it matches. */
    } arm;
    struct
    {
        cnor_node_t *value; /**< Returned value or ::lh_null. */
    } return_;
    struct
    {
        cnor_int_literal_t value; /**< The number. */
    } int_;
    struct
    {
        cnor_node_t *args; /**< Argument list. */
        cnor_argc_t argc;  /**< Length of `args`. */
    } call;
    struct
    {
        cnor_binary_op_t op; /**< Operator. */
        cnor_node_t *left;   /**< Left operand. */
        cnor_node_t *right;  /**< Right operand. */
    } binary;
    struct
    {
        cnor_unary_op_t op;   /**< Operator. */
        cnor_node_t *operand; /**< Operand. */
    } unary;
    struct
    {
        cnor_node_t *target; /**< Indexed value. */
        cnor_node_t *index;  /**< Index. */
    } index;
    struct
    {
        cnor_node_t *value; /**< The value every element holds. */
        lh_u32_t count;     /**< How many elements. */
    } array;
};

/**
 * @struct cnor_node
 * @brief One node of the tree.
 */
struct cnor_node
{
    cnor_node_kind_t kind;     /**< Which construct. */
    cnor_line_t line;          /**< Line it starts on. */
    lh_str_view_t name;        /**< Name or literal text; arena-owned. */
    cnor_node_t *next;         /**< Next sibling in a list. */
    union cnor_node_data data; /**< Per-kind members. */
};

LH_COMPILER_EXTERN_C_BEGIN

/** @brief Kind of @p self. */
cnor_node_kind_t
cnor_node_get_kind(const cnor_node_t *self);

/** @brief Line @p self starts on. */
cnor_line_t
cnor_node_get_line(const cnor_node_t *self);

/** @brief Name or literal text of @p self. */
lh_str_view_t
cnor_node_get_name(const cnor_node_t *self);

/** @brief Next sibling of @p self, or ::lh_null. */
const cnor_node_t *
cnor_node_get_next(const cnor_node_t *self);

/** @brief Number of nodes in the list starting at @p head. */
lh_u32_t
cnor_node_list_length(const cnor_node_t *head);

/** @brief True for kinds that produce a value. */
lh_bool_t
cnor_node_kind_is_expr(cnor_node_kind_t kind);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_AST_H */
