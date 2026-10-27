/**
 * @file internal.h
 * @brief Library-private: compiler state and the interfaces between its
 *        modules (files, emit, scope, types, initialization order, constant,
 *        rules, expression and statement modules).
 */

#ifndef CNOR_SRC_COMPILER_INTERNAL_H
#define CNOR_SRC_COMPILER_INTERNAL_H

#include <cnor/arena.h>
#include <cnor/builtin.h>
#include <cnor/byte/writer.h>
#include <cnor/compiler.h>
#include <cnor/lexer.h>
#include <cnor/op.h>
#include <lh/array.h>

/** @def CNOR_COMPILER_MAX_LOOPS @brief Loops nested in one function. */
#define CNOR_COMPILER_MAX_LOOPS CNOR_LEXER_MAX_INDENTS

/** @def CNOR_COMPILER_MAX_TRIES @brief `try` blocks nested in one function. */
#define CNOR_COMPILER_MAX_TRIES CNOR_LEXER_MAX_INDENTS

/**
 * @def CNOR_EXIT_JUMP @brief An exit that goes on somewhere: a `break` or a
 *        `continue`.
 */
#define CNOR_EXIT_JUMP 1U

/**
 * @def CNOR_EXIT_RETURN @brief An exit that leaves the function: a `return`.
 */
#define CNOR_EXIT_RETURN 2U


/* -- static types (type.c) ------------------------------------------------- */

/**
 * @enum cnor_static_type
 * @brief What the compiler knows about a value's kind before it runs.
 *
 * `any` is "not known": a native's result, an unannotated function's result,
 * a variable that started as `none`. Nothing is checked against `any`.
 */
enum cnor_static_type
{
    cnor_static_any = 0, /**< Unknown; accepted everywhere. */
    cnor_static_none,    /**< `none`. */
    cnor_static_bool,    /**< `bool` */
    cnor_static_i32,     /**< `i32` */
    cnor_static_u32,     /**< `u32` */
    cnor_static_f32,     /**< `f32` */
    cnor_static_str,     /**< `str` */
    cnor_static_array,   /**< An array of values; its elements hold `any`. */
    cnor_static_dict,    /**< A dictionary of pairs; its values hold `any`. */
    cnor_static_type_count
};
/** @copybrief cnor_static_type */
typedef enum cnor_static_type cnor_static_type_t;

/* -- files (module.c, path.c) ---------------------------------------------- */

/**
 * @typedef cnor_module_index_t
 * @brief A source file of the compilation; 0 is the main file. Equal to its
 *        index in the image's file table.
 */
typedef lh_u16_t cnor_module_index_t;

/** @def CNOR_MODULE_HOST @brief The "file" the host's natives belong to. */
#define CNOR_MODULE_HOST 0xFFFFU

/**
 * @enum cnor_module_state
 * @brief Where a file is in the depth-first walk over the imports.
 */
enum cnor_module_state
{
    cnor_module_loading = 0, /**< Its imports are being followed: meeting it again is a cycle. */
    cnor_module_loaded       /**< Done, with everything it imports. */
};
/** @copybrief cnor_module_state */
typedef enum cnor_module_state cnor_module_state_t;

/** @brief One source file: the main one or one it imports, directly or not. */
struct cnor_module
{
    lh_str_view_t path;               /**< Resolved path, its identity; arena-owned. */
    lh_str_view_t name;               /**< What the image and diagnostics call it. */
    const cnor_node_t *program;       /**< Its top-level items. */
    lh_array_t imports;               /**< cnor_module_index_t: the files it imports. */
    cnor_function_index_t function;   /**< The function holding its top-level code. */
    lh_u32_t first_global;            /**< Index of its first global. */
    cnor_module_state_t state;        /**< Walk state. */
};

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
    cnor_binding_native,
    cnor_binding_ambiguous /**< Defined by two files the current one imports. */
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
    cnor_static_type_t type;  /**< For variables: what they hold. */
    lh_u16_t count;           /**< For an array: how many elements. */
};
/** @copybrief cnor_binding */
typedef struct cnor_binding cnor_binding_t;

/** @brief A top-level name and the file it belongs to; first in every declaration. */
struct cnor_decl_key
{
    lh_str_view_t name;         /**< Empty for a file's top-level code. */
    cnor_module_index_t module; /**< Declaring file, or ::CNOR_MODULE_HOST. */
};

/** @brief A script function: declaration and, once compiled, its table entry. */
struct cnor_function_decl
{
    struct cnor_decl_key key;
    const cnor_node_t *node; /**< ::lh_null for a file's top-level code. */
    cnor_image_function_t image;
    lh_u32_t globals_needed; /**< 1 + highest global its code (or a callee's) uses; 0: none. */
};

/** @brief A native the script may call: declared, or known from the host. */
struct cnor_native_decl
{
    struct cnor_decl_key key;
    const cnor_node_t *node; /**< The `native fn` declaration, ::lh_null for the host's. */
    cnor_argc_t arity_min;
    cnor_argc_t arity_max;
    lh_u16_t import; /**< Index in the image's native section, or CNOR_INDEX_NONE. */
};

/** @brief A global variable. */
struct cnor_global_decl
{
    struct cnor_decl_key key;
    lh_bool_t is_mutable;
    cnor_static_type_t type; /**< Annotated, or inferred once its `let` is compiled. */
};

/** @brief A local variable of the function being compiled. */
struct cnor_local_decl
{
    lh_str_view_t name;
    cnor_slot_t slot;
    lh_bool_t is_mutable;
    cnor_static_type_t type;
    lh_u16_t count; /**< For an array: how many elements. */
};

/** @brief One top-level name in ::cnor_name_index. */
struct cnor_name_entry
{
    lh_str_view_t name;         /**< Empty for a free entry. */
    lh_u32_t hash;              /**< Of the name and the file. */
    cnor_module_index_t module; /**< Declaring file. */
    cnor_binding_kind_t kind;   /**< Function, native or global. */
    lh_u16_t index;             /**< In its table. */
};

/**
 * @struct cnor_name_index
 * @brief Hash index of every top-level name (functions, natives, globals),
 *        keyed by the name and its file: in one file the three share one
 *        namespace. A lookup does not scan the tables.
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
    struct cnor_name_index top;  /**< Index of functions, natives and globals by file. */
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
    lh_u32_t try_depth; /**< `try` blocks open when the loop began. */
};

/**
 * @struct cnor_try
 * @brief One open `try` block: where its finally block is, and where a
 *        returned value waits while that block runs.
 */
struct cnor_try
{
    cnor_label_t *finally_label; /**< Where the finally block starts; null when there is none. */
    cnor_slot_t value_slot;       /**< Where a returned value waits; 0 when there is no finally. */
};

/* -- initialization order (order.c) ---------------------------------------- */

/** @brief A call from one function's code to another's. */
struct cnor_call_edge
{
    cnor_function_index_t caller;
    cnor_function_index_t callee;
};

/** @brief A call from a file's top-level code, checked once every function is compiled. */
struct cnor_early_call
{
    cnor_function_index_t callee;
    lh_u32_t globals_ready;     /**< Globals whose `let` has run at the call. */
    cnor_line_t line;           /**< Of the call. */
    cnor_module_index_t module; /**< File of the call. */
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
    struct cnor_try tries[CNOR_COMPILER_MAX_TRIES];
    lh_u32_t try_depth; /**< `try` blocks open; a `break` closes them. */
    cnor_function_index_t function; /**< Being compiled. */
    cnor_slot_t result_slot;        /**< Entry function: last expression value. */
    lh_u32_t block_depth;           /**< 0 at the top level of a function. */
    cnor_arena_t arena;             /**< Imported trees and resolved paths. */
    lh_array_t modules;             /**< struct cnor_module; 0 is the main file. */
    lh_array_t order;               /**< cnor_module_index_t, imported files only, imports first. */
    cnor_module_index_t module;     /**< File being compiled. */
    lh_bool_t top_code;             /**< Compiling a file's top-level code. */
    lh_u32_t globals_ready;         /**< Top-level code: globals whose `let` has run. */
    cnor_static_type_t type;        /**< Of the expression compiled last. */
    cnor_static_type_t returns;     /**< Declared return type; `any` without one. */
    lh_array_t calls;               /**< struct cnor_call_edge, from function code. */
    lh_array_t early_calls;         /**< struct cnor_early_call, from top-level code. */
};
/** @copybrief cnor_compiler */
typedef struct cnor_compiler cnor_compiler_t;

/** @brief Record a failure at @p node. @return Always ::lh_bool_false. */
lh_bool_t
cnor_compile_fail(cnor_compiler_t *self, const cnor_node_t *node, cnor_error_t code);

/** @brief Record a failure about @p node's name. @return Always ::lh_bool_false. */
lh_bool_t
cnor_compile_fail_name(cnor_compiler_t *self, const cnor_node_t *node, cnor_error_t code);

/** @brief Record a failure at @p line about @p name. @return Always ::lh_bool_false. */
lh_bool_t
cnor_compile_fail_text(cnor_compiler_t *self, cnor_line_t line, cnor_error_t code,
                       lh_str_view_t name);

/**
 * @brief Record why @p binding cannot be used for @p node's name: an unknown
 *        name, or one two imported files define. @return Always ::lh_bool_false.
 */
lh_bool_t
cnor_compile_fail_binding(cnor_compiler_t *self, const cnor_node_t *node,
                          const cnor_binding_t *binding);

/* path.c */

/** @brief Whether @p path is absolute: `/`, `\` or a drive letter first. */
lh_bool_t
cnor_path_is_absolute(lh_str_view_t path);

/** @brief The directory part of @p path, separator included; empty for a bare name. */
lh_str_view_t
cnor_path_dir(lh_str_view_t path);

/**
 * @brief Join @p dir and @p path, fold `.` and `..`, write `/` between
 *        segments; the text is allocated in @p arena.
 */
lh_bool_t
cnor_path_join(cnor_arena_t *arena, lh_str_view_t dir, lh_str_view_t path, lh_str_view_t *out);

/* module.c */
void
cnor_modules_init(cnor_compiler_t *self);
void
cnor_modules_deinit(cnor_compiler_t *self);
/** @brief File @p index. */
struct cnor_module *
cnor_module_at(cnor_compiler_t *self, cnor_module_index_t index);
cnor_count_t
cnor_module_count(const cnor_compiler_t *self);
/** @brief Load and parse every file @p program imports, directly or not, and order them. */
lh_bool_t
cnor_modules_load(cnor_compiler_t *self, const cnor_node_t *program);

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
cnor_scope_add_global(cnor_compiler_t *self, const cnor_node_t *node, lh_u16_t count);
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
/** @brief Add the function holding the top-level code of file @p module. */
lh_bool_t
cnor_scope_add_module_code(cnor_compiler_t *self, cnor_module_index_t module);
/** @brief Declare a local in the current block; @p node is for diagnostics. */
lh_bool_t
cnor_scope_add_local(cnor_compiler_t *self, const cnor_node_t *node, lh_str_view_t name,
                     lh_bool_t is_mutable, cnor_static_type_t type, lh_u16_t count,
                     cnor_slot_t *slot);
/**
 * @brief Declare a local dictionary of @p room pairs; the pairs are its own,
 *        so it takes a slot for its value, two for its header and two per
 *        pair.
 */
lh_bool_t
cnor_scope_add_dict_local(cnor_compiler_t *self, const cnor_node_t *node, lh_str_view_t name,
                          lh_bool_t is_mutable, lh_u16_t room, cnor_slot_t *slot);
/**
 * @brief Resolve @p name: locals, then the current file's functions, natives
 *        and globals, then those of the files it imports, then the host's
 *        natives.
 */
cnor_binding_t
cnor_scope_resolve(cnor_compiler_t *self, lh_str_view_t name);
/** @brief The global declaration @p index. */
struct cnor_global_decl *
cnor_scope_global_at(cnor_scope_t *self, lh_u32_t index);
cnor_count_t
cnor_scope_global_count(const cnor_scope_t *self);
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

/* type.c */

/**
 * @brief The builtin of the language with that name.
 * @return ::lh_bool_true and @p out when there is one.
 */
lh_bool_t
cnor_compiler_builtin_find(lh_str_view_t name, cnor_builtin_t *out);

/**
 * @brief The types the builtin takes, one per argument.
 * @return The types; ::lh_null for an ordinal out of range.
 */
const cnor_static_type_t *
cnor_compiler_builtin_params(cnor_builtin_t builtin);

/** @brief The type the builtin gives. */
cnor_static_type_t
cnor_compiler_builtin_result(cnor_builtin_t builtin);

/** @brief The static type an annotation names; `any` without one. */
cnor_static_type_t
cnor_type_of_annotation(cnor_type_t annotation);

/** @brief Spelling of @p type in diagnostics. */
const lh_char_t *
cnor_type_name(cnor_static_type_t type);

/** @brief Whether a value of type @p found may go where @p wanted is required. */
lh_bool_t
cnor_type_accepts(cnor_static_type_t wanted, cnor_static_type_t found);

/** @brief Fail with "expected @p wanted, found @p found" unless it accepts. */
lh_bool_t
cnor_type_require(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t wanted,
                  cnor_static_type_t found);

/**
 * @brief Fail with "expected @p wanted, found @p found"; @p wanted is a text,
 *        for a wanted type that has no name of its own.
 */
lh_bool_t
cnor_type_require_text(cnor_compiler_t *self, const cnor_node_t *node, const lh_char_t *wanted,
                       cnor_static_type_t found);

/** @brief Type of a variable declared with @p annotation and initialized with @p init. */
cnor_static_type_t
cnor_type_of_variable(cnor_type_t annotation, cnor_static_type_t init);

/** @brief Type of a literal node (a negated number included); `any` for other nodes. */
cnor_static_type_t
cnor_type_of_literal(const cnor_node_t *node);

/** @brief Check `left op right` and give its type, or fail when it always fails. */
lh_bool_t
cnor_type_binary(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t left,
                 cnor_static_type_t right, cnor_static_type_t *out);

/** @brief Check `-operand` and give its type, or fail when it always fails. */
lh_bool_t
cnor_type_negate(cnor_compiler_t *self, const cnor_node_t *node, cnor_static_type_t operand,
                 cnor_static_type_t *out);

/** @brief Whether `==` between the two can ever hold (`none` and `any` always can). */
lh_bool_t
cnor_type_comparable(cnor_static_type_t a, cnor_static_type_t b);

/* order.c */
void
cnor_order_init(cnor_compiler_t *self);
void
cnor_order_deinit(cnor_compiler_t *self);
/** @brief Note a read or write of global @p index at @p node; top-level code fails early. */
lh_bool_t
cnor_order_use_global(cnor_compiler_t *self, const cnor_node_t *node, lh_u32_t index);
/** @brief Note a call of script function @p callee at @p node. */
void
cnor_order_call(cnor_compiler_t *self, const cnor_node_t *node, cnor_function_index_t callee);
/** @brief Once every function is compiled: no top-level call reaches a global too early. */
lh_bool_t
cnor_order_check(cnor_compiler_t *self);

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
lh_bool_t
cnor_compile_array(cnor_compiler_t *self, const cnor_node_t *node);
/** @brief Whether @p node names a local; its slot through @p slot, its type
 *         through @p type (both left alone when it does not). */
lh_bool_t
cnor_compile_local_operand(cnor_compiler_t *self, const cnor_node_t *node, cnor_slot_t *slot,
                           cnor_static_type_t *type);
/**
 * @brief Whether @p node is an integer literal (or a negated one) that
 *        `push_i32` would push; its operand bits through @p bits.
 */
lh_bool_t
cnor_compile_int_operand(const cnor_node_t *node, lh_u32_t *bits);

/**
 * @enum cnor_dict_form
 * @brief What an initializer says about a dictionary.
 */
enum cnor_dict_form
{
    cnor_dict_not_a_dict = 0, /**< Not a dictionary declaration. */
    cnor_dict_declared,       /**< `dict(n)`: a dictionary of `n` pairs. */
    cnor_dict_malformed       /**< `dict(...)`, but not of a whole number. */
};
/** @copybrief cnor_dict_form */
typedef enum cnor_dict_form cnor_dict_form_t;

/**
 * @brief What the initializer @p init says about a dictionary.
 *
 * `dict(n)` is a declaration, like an array literal: the room is a whole
 * number the compiler knows, since the dictionary takes its slots.
 *
 * @param[out] room How many pairs the dictionary has, when it is one.
 */
cnor_dict_form_t
cnor_compiler_dict_form(const cnor_node_t *init, lh_u32_t *room);

/** @brief Whether the script names a function or native of its own `dict`. */
lh_bool_t
cnor_compiler_names_its_own_dict(cnor_compiler_t *self);

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
/** @brief The type two branches of a ternary agree on. */
cnor_static_type_t
cnor_type_choice(cnor_static_type_t left, cnor_static_type_t right);
/** @brief `value if condition else other`. */
lh_bool_t
cnor_compile_ternary(cnor_compiler_t *self, const cnor_node_t *node);

/* control_loop.c */
lh_bool_t
cnor_compile_while(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_for(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_break(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_continue(cnor_compiler_t *self, const cnor_node_t *node);

/* control_try.c */
lh_bool_t
cnor_compile_try(cnor_compiler_t *self, const cnor_node_t *node);
lh_bool_t
cnor_compile_raise(cnor_compiler_t *self, const cnor_node_t *node);
/**
 * @brief Leave the `try` blocks that are open, running the finally blocks of the
 *        ones the exit crosses.
 *
 * The finally blocks run from the inside out: the machine jumps to the first
 * one and comes back to a trampoline that runs the next. When the last one is
 * done, the machine does what @p kind says: jump to @p target, or return the
 * value that waits in @p slot.
 *
 * @param first The outermost `try` block the exit crosses; the ones above it
 *              are left behind.
 * @param kind  ::CNOR_EXIT_JUMP or ::CNOR_EXIT_RETURN.
 */
lh_bool_t
cnor_compile_exit_tries(cnor_compiler_t *self, lh_u32_t first, lh_u8_t kind,
                         cnor_label_t *target, cnor_slot_t slot);

/* control_match.c */
lh_bool_t
cnor_compile_match(cnor_compiler_t *self, const cnor_node_t *node);

/* function.c */
lh_bool_t
cnor_compile_declaration(cnor_compiler_t *self, const cnor_node_t *node);
/** @brief Declare every file's top-level code, functions, natives and globals. */
lh_bool_t
cnor_compile_declare(cnor_compiler_t *self);
/** @brief Compile every function, then check the order globals are used in. */
lh_bool_t
cnor_compile_bodies(cnor_compiler_t *self);

#endif /* CNOR_SRC_COMPILER_INTERNAL_H */
