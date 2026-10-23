/**
 * @file internal.h
 * @brief Library-private: compiler state and the interfaces between its
 *        modules (emit, scope, constant, rules, expression and statement
 *        modules).
 */

#ifndef CNOR_SRC_COMPILER_INTERNAL_H
#define CNOR_SRC_COMPILER_INTERNAL_H

#include <cnor/byte/writer.h>
#include <cnor/compiler.h>
#include <cnor/lexer.h>
#include <cnor/op.h>
#include <lh/array.h>

/** @def CNOR_COMPILER_MAX_LOOPS @brief Loops nested in one function. */
#define CNOR_COMPILER_MAX_LOOPS CNOR_LEXER_MAX_INDENTS

/* -- emit (emit.c) --------------------------------------------------------- */

/**
 * @struct cnor_label
 * @brief A jump target. Unbound labels chain their pending jumps through the
 *        jumps' own operands, so a label needs no storage of its own.
 */
struct cnor_label
{
    cnor_code_addr_t addr;  /**< Bound address. */
    lh_u32_t pending;       /**< Last unpatched jump operand, or CNOR_LABEL_END. */
    lh_s32_t depth;         /**< Stack depth at the label, once known. */
    lh_bool_t bound;        /**< `addr` is valid. */
    lh_bool_t has_depth;    /**< `depth` is valid. */
};
/** @copybrief cnor_label */
typedef struct cnor_label cnor_label_t;

/**
 * @struct cnor_emit
 * @brief Code being written, with the stack depth it implies.
 */
struct cnor_emit
{
    cnor_byte_writer_t code;   /**< All functions, back to back. */
    cnor_byte_writer_t lines;  /**< Line records. */
    cnor_count_t line_count;   /**< How many. */
    cnor_line_t last_line;     /**< Line of the last record. */
    cnor_line_t line;          /**< Line of the node being compiled. */
    lh_bool_t emit_lines;      /**< Whether to record lines. */
    lh_s32_t depth;            /**< Operand stack depth now. */
    lh_s32_t max_depth;        /**< Deepest in this function. */
    lh_bool_t reachable;       /**< Whether the next instruction can run. */
};
/** @copybrief cnor_emit */
typedef struct cnor_emit cnor_emit_t;

/* -- scope (scope.c) ------------------------------------------------------- */

/**
 * @enum cnor_binding_kind
 * @brief What a name resolves to.
 */
enum cnor_binding_kind
{
    cnor_binding_none = 0,
    cnor_binding_local,
    cnor_binding_global,
    cnor_binding_function,
    cnor_binding_native
};
/** @copybrief cnor_binding_kind */
typedef enum cnor_binding_kind cnor_binding_kind_t;

/**
 * @struct cnor_binding
 * @brief A resolved name.
 */
struct cnor_binding
{
    cnor_binding_kind_t kind; /**< What it is. */
    lh_u16_t index;           /**< Slot, global, function or native index. */
    lh_bool_t is_mutable;     /**< For variables: declared with `mut`. */
};
/** @copybrief cnor_binding */
typedef struct cnor_binding cnor_binding_t;

/** @brief A script function: declaration and, once compiled, its table entry. */
struct cnor_function_decl
{
    lh_str_view_t name;
    const cnor_node_t *node; /**< ::lh_null for the entry function. */
    cnor_image_function_t image;
};

/** @brief A native the script may call: declared, or known from the host. */
struct cnor_native_decl
{
    lh_str_view_t name;
    cnor_argc_t arity_min;
    cnor_argc_t arity_max;
    lh_u16_t import; /**< Index in the image's native section, or CNOR_INDEX_NONE. */
};

/** @brief A global variable. */
struct cnor_global_decl
{
    lh_str_view_t name;
    lh_bool_t is_mutable;
};

/** @brief A local variable of the function being compiled. */
struct cnor_local_decl
{
    lh_str_view_t name;
    cnor_slot_t slot;
    lh_bool_t is_mutable;
};

/** @brief One top-level name in ::cnor_name_index. */
struct cnor_name_entry
{
    lh_str_view_t name;       /**< Empty for a free entry. */
    lh_u32_t hash;            /**< Of the name. */
    cnor_binding_kind_t kind; /**< Function, native or global. */
    lh_u16_t index;           /**< In its table. */
};

/**
 * @struct cnor_name_index
 * @brief Hash index of every top-level name (functions, natives, globals),
 *        which share one namespace, so a lookup does not scan the tables.
 *
 * Open addressing with linear probing, kept at most half full. It only speeds
 * lookups up: when it cannot grow, it is dropped and the tables are scanned.
 *
 * @note Candidate lh brick: a string-keyed hash map.
 */
struct cnor_name_index
{
    struct cnor_name_entry *entries; /**< `capacity` entries, or ::lh_null. */
    lh_u32_t capacity;               /**< A power of two, or 0. */
    lh_u32_t count;                  /**< Names in it. */
    lh_bool_t usable;                /**< Cleared when it ran out of memory. */
};

/**
 * @struct cnor_scope
 * @brief Every name table of a compilation.
 */
struct cnor_scope
{
    struct cnor_name_index top;  /**< Index of functions, natives and globals. */
    lh_array_t functions;   /**< struct cnor_function_decl; 0 is the entry. */
    lh_array_t natives;     /**< struct cnor_native_decl. */
    lh_array_t imports;     /**< lh_str_view_t, the image's native section. */
    lh_array_t globals;     /**< struct cnor_global_decl. */
    lh_array_t locals;      /**< struct cnor_local_decl, innermost last. */
    cnor_slot_t next_slot;  /**< First free slot. */
    cnor_slot_t slot_count; /**< Slots the function needs so far. */
};
/** @copybrief cnor_scope */
typedef struct cnor_scope cnor_scope_t;

/**
 * @struct cnor_scope_mark
 * @brief Where a block began, to drop its locals when it ends.
 */
struct cnor_scope_mark
{
    lh_usize_t locals;
    cnor_slot_t next_slot;
};
/** @copybrief cnor_scope_mark */
typedef struct cnor_scope_mark cnor_scope_mark_t;

/* -- constants (constant.c) ------------------------------------------------ */

/** @brief One pooled constant, for de-duplication. */
struct cnor_constant
{
    cnor_const_kind_t kind;
    lh_str_view_t bytes;
};

/**
 * @struct cnor_constant_pool
 * @brief Constants and their encoded records.
 */
struct cnor_constant_pool
{
    lh_array_t entries;         /**< struct cnor_constant. */
    cnor_byte_writer_t records; /**< Encoded section records. */
};
/** @copybrief cnor_constant_pool */
typedef struct cnor_constant_pool cnor_constant_pool_t;

/* -- compiler -------------------------------------------------------------- */

/** @brief Jump targets of the innermost loops. */
struct cnor_loop
{
    cnor_label_t *break_label;
    cnor_label_t *continue_label;
};

/**
 * @struct cnor_compiler
 * @brief The state of one compilation; lives on the stack of
 *        ::cnor_compiler_compile.
 */
struct cnor_compiler
{
    const cnor_compiler_config_t *config;
    cnor_diag_t *diag;
    cnor_emit_t emit;
    cnor_scope_t scope;
    cnor_constant_pool_t constants;
    struct cnor_loop loops[CNOR_COMPILER_MAX_LOOPS];
    lh_u32_t loop_depth;
    cnor_function_index_t function; /**< Being compiled. */
    cnor_slot_t result_slot;        /**< Entry function: last expression value. */
    lh_u32_t block_depth;           /**< 0 at the top level of a function. */
};
/** @copybrief cnor_compiler */
typedef struct cnor_compiler cnor_compiler_t;

/** @brief Record a failure at @p node. @return Always ::lh_bool_false. */
lh_bool_t
cnor_compile_fail(cnor_compiler_t *self, const cnor_node_t *node, cnor_error_t code);

/** @brief Record a failure about @p node's name. @return Always ::lh_bool_false. */
lh_bool_t
cnor_compile_fail_name(cnor_compiler_t *self, const cnor_node_t *node, cnor_error_t code);

/* emit.c */
void
cnor_emit_init(cnor_emit_t *self, lh_bool_t emit_lines);
void
cnor_emit_deinit(cnor_emit_t *self);
cnor_code_addr_t
cnor_emit_get_addr(const cnor_emit_t *self);
void
cnor_emit_begin_function(cnor_emit_t *self);
/** @brief Emit @p op with operands @p a, @p b and @p c; tracks the stack. */
lh_bool_t
cnor_emit_insn3(cnor_compiler_t *self, cnor_op_t op, lh_u32_t a, lh_u32_t b, lh_u32_t c);
/** @brief Emit @p op with operands @p a and @p b; tracks the stack. */
lh_bool_t
cnor_emit_insn(cnor_compiler_t *self, cnor_op_t op, lh_u32_t a, lh_u32_t b);
/** @brief Emit an operand-less @p op. */
lh_bool_t
cnor_emit_op(cnor_compiler_t *self, cnor_op_t op);
void
cnor_label_init(cnor_label_t *label);
/**
 * @brief Emit a jump-family @p op to @p label, with @p b and @p c as its
 *        second and third operands (the jump is always the first).
 */
lh_bool_t
cnor_emit_jump_with(cnor_compiler_t *self, cnor_op_t op, cnor_label_t *label, lh_u32_t b,
                    lh_u32_t c);
/** @brief Emit a jump-family @p op to @p label. */
lh_bool_t
cnor_emit_jump(cnor_compiler_t *self, cnor_op_t op, cnor_label_t *label);
/** @brief Bind @p label here and patch every jump waiting for it. */
lh_bool_t
cnor_emit_bind(cnor_compiler_t *self, cnor_label_t *label);

/* scope.c */
void
cnor_scope_init(cnor_scope_t *self);
void
cnor_scope_deinit(cnor_scope_t *self);
lh_bool_t
cnor_scope_add_function(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_scope_add_native(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_scope_add_global(cnor_compiler_t *self, const cnor_node_t *node);
struct cnor_function_decl *
cnor_scope_function_at(cnor_scope_t *self, cnor_function_index_t index);
cnor_count_t
cnor_scope_function_count(const cnor_scope_t *self);
void
cnor_scope_begin_function(cnor_scope_t *self);
cnor_scope_mark_t
cnor_scope_enter_block(const cnor_scope_t *self);
void
cnor_scope_leave_block(cnor_scope_t *self, cnor_scope_mark_t mark);
/** @brief Declare a local in the current block; @p node is for diagnostics. */
lh_bool_t
cnor_scope_add_local(cnor_compiler_t *self, const cnor_node_t *node, lh_str_view_t name,
                     lh_bool_t is_mutable, cnor_slot_t *slot);
/** @brief Resolve @p name: locals, then globals, functions, natives. */
cnor_binding_t
cnor_scope_resolve(cnor_compiler_t *self, lh_str_view_t name);
/** @brief Native import index for native declaration @p native (adds it on first use). */
lh_u16_t
cnor_scope_import_native(cnor_compiler_t *self, lh_u16_t native);
/** @brief The native declaration @p index. */
const struct cnor_native_decl *
cnor_scope_native_at(const cnor_scope_t *self, lh_u16_t index);

/* constant.c */
void
cnor_constant_pool_init(cnor_constant_pool_t *self);
void
cnor_constant_pool_deinit(cnor_constant_pool_t *self);
cnor_count_t
cnor_constant_pool_count(const cnor_constant_pool_t *self);
/** @brief Index of a constant equal to (@p kind, @p bytes), added if new. */
lh_bool_t
cnor_constant_pool_intern(cnor_compiler_t *self, const cnor_node_t *node, cnor_const_kind_t kind,
                          cnor_const_index_t *index);

/* rules.c: the dispatch table keyed by node kind */

/** @brief Compiles one node. */
typedef lh_bool_t (*cnor_compile_fn)(cnor_compiler_t *self, const cnor_node_t *node);

/**
 * @struct cnor_compile_rule
 * @brief How one node kind compiles as an expression and as a statement;
 *        ::lh_null where it cannot appear.
 */
struct cnor_compile_rule
{
    cnor_compile_fn expr;
    cnor_compile_fn stmt;
};
/** @copybrief cnor_compile_rule */
typedef struct cnor_compile_rule cnor_compile_rule_t;

const cnor_compile_rule_t *
cnor_compile_rule_of(cnor_node_kind_t kind);

/** @brief Compile @p node as an expression: leaves one value on the stack. */
lh_bool_t
cnor_compile_expr(cnor_compiler_t *self, const cnor_node_t *node);

/** @brief Compile @p node as a statement: leaves the stack as it was. */
lh_bool_t
cnor_compile_stmt(cnor_compiler_t *self, const cnor_node_t *node);

/** @brief Compile a statement list in a block scope of its own. */
lh_bool_t
cnor_compile_block(cnor_compiler_t *self, const cnor_node_t *body);

/* expr.c */
lh_bool_t
cnor_compile_literal(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_ident(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_unary(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_binary(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_index(cnor_compiler_t *self, const cnor_node_t *node);
/** @brief Whether @p node names a local; its slot through @p slot. */
lh_bool_t
cnor_compile_local_operand(cnor_compiler_t *self, const cnor_node_t *node, cnor_slot_t *slot);
/**
 * @brief Whether @p node is an integer literal (or a negated one) that
 *        `push_i32` would push; its operand bits through @p bits.
 */
lh_bool_t
cnor_compile_int_operand(const cnor_node_t *node, lh_u32_t *bits);

/* logic.c */
lh_bool_t
cnor_compile_logic(cnor_compiler_t *self, const cnor_node_t *node);

/** @brief Compile @p node as a condition: jump to @p target when its truth is
 *         @p when, fall through otherwise (see logic.c). */
lh_bool_t
cnor_compile_branch(cnor_compiler_t *self, const cnor_node_t *node, lh_bool_t when,
                    cnor_label_t *target);

/* call.c */
lh_bool_t
cnor_compile_call(cnor_compiler_t *self, const cnor_node_t *node);

/* stmt.c */
lh_bool_t
cnor_compile_let(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_assign(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_expr_stmt(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_return(cnor_compiler_t *self, const cnor_node_t *node);
/** @brief Store the top of the stack into a resolved variable. */
lh_bool_t
cnor_compile_store(cnor_compiler_t *self, const cnor_binding_t *binding);

/* control_if.c */
lh_bool_t
cnor_compile_if(cnor_compiler_t *self, const cnor_node_t *node);

/* control_loop.c */
lh_bool_t
cnor_compile_while(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_for(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_break(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_continue(cnor_compiler_t *self, const cnor_node_t *node);

/* control_match.c */
lh_bool_t
cnor_compile_match(cnor_compiler_t *self, const cnor_node_t *node);

/* function.c */
lh_bool_t
cnor_compile_declaration(cnor_compiler_t *self, const cnor_node_t *node);
/** @brief Declare every top-level function, native and global. */
lh_bool_t
cnor_compile_declare(cnor_compiler_t *self, const cnor_node_t *program);
/** @brief Compile the entry function from the top-level statements. */
lh_bool_t
cnor_compile_entry(cnor_compiler_t *self, const cnor_node_t *program);
/** @brief Compile every declared script function after the entry. */
lh_bool_t
cnor_compile_functions(cnor_compiler_t *self);

#endif /* CNOR_SRC_COMPILER_INTERNAL_H */
