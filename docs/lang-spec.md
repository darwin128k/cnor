# The cnor language

Status: draft, language version 0.1, image format v3.

> A small script language for embedded systems and hosts, run by an
> interpreter built on `lh`.

This is the English translation of the original `mote` specification (lh,
`docs/lang-spec.md`, in Russian), renamed to cnor and updated to what cnor
actually implements. Places where cnor deliberately departs from the original
text are marked **[cnor]**; the reasons are collected in
[section 15](#15-decisions-that-differ-from-the-original-draft).

---

## 1. Goals

1. A script is written as text and needs no separate build step on the target:
   the same `.tbc` image runs on an STM32, on a game server and in a browser.
2. The runtime **does not depend on an OS**: no `malloc`, no `printf`, no file
   system, no module loader. Everything goes through `lh`: allocator, strings,
   values, errors.
3. Natives are called **directly as ordinary C functions**. No AMX-style
   native tables, no encoding of parameters into cells.
4. The language stays small: every feature must fit in a microcontroller's
   memory and cost no more than one row in the opcode table.
5. All code is C11 and uses the existing `lh` building blocks.

## 2. Non-goals

- C-level execution speed. This is a language for configuration and logic
  scripts, not for number crunching.
- A JIT. Possibly a later stage; not part of the first version.
- A garbage collector. Memory management is explicit and deterministic.
- Dynamic typing at run time. Types are meant to be known at compile time.
  **[cnor]** The compiler checks types statically (section 6.3). Values still
  carry their kind at run time (`lh_value_t`) and operators still check kinds
  when they execute, for the values whose type the compiler cannot know (a
  host native's result).

## 3. Implementation rules

1. **Look in `lh` first.** Allocator: `lh_runtime_allocator_*`; strings:
   `lh_str_view_t`; values: `lh_value_t`; containers: `lh_array_t`; byte order:
   `lh_bit_pack_le*`.
2. **If `lh` lacks a building block, cnor keeps it internal** and flags it as a
   candidate for `lh` (the arena, the byte reader/writer, the string pool).
   **[cnor]** The original rule was "extend `lh`"; lh's public API is decided
   by its owner, so cnor does not add to it.
3. **Every subsystem has tests.** Without tests it is not done.
4. Style: Doxygen comments, `cnor_` / `CNOR_` prefixes, `extern "C"` through
   `<lh/compiler/extern/c.h>`, errors as enum codes with messages, no
   exceptions and no `longjmp`.

## 4. Value types

The value type is `lh_value_t` from `lh` itself. Its kinds already cover what
is needed: `none`, `bool`, `sint`, `uint`, `real`, `str`, `ptr`.

| Language type | `lh_value_kind_*` | Note |
|---|---|---|
| `i32` | `lh_value_kind_sint` | |
| `u32` | `lh_value_kind_uint` | |
| `f32` | `lh_value_kind_real` | **only with `LH_REAL_ENABLED`** |
| `bool` | `lh_value_kind_bool` | |
| `str` | `lh_value_kind_str` | see ownership below |
| array | `CNOR_VALUE_KIND_ARRAY` (7) | cnor's own kind; see below |
| (none) | `lh_value_kind_ptr` | natives only; not reachable from a script |

An array's value is lh's string view: `begin` points at the elements (values
of their own) and `size` counts them, not bytes. Its elements are slots of the
frame that declares it, so an array is a local and does not outlive its
function.

An integer literal up to `0x7FFFFFFF` is an `i32`; a larger one (up to
`0xFFFFFFFF`) is a `u32`. Arithmetic on two `u32` stays `u32` (a subtraction
that would go below zero yields a negative `i32`); mixed `i32`/`u32` computes
signed; anything with a real computes real. Integer arithmetic wraps on
overflow (64-bit two's complement), it never traps.

Comparisons and `==` compare numbers by value across kinds: a negative `i32`
is below every `u32`, and `u32` values above 2^63 stay above every `i32`.
**[cnor]** With a real on either side the comparison is a real one, as IEEE
754 defines it: NaN is unordered, so `==`, `<`, `<=`, `>`, `>=` are false
and `!=` is true, even for NaN against itself. `%` on reals truncates toward
zero like C's `fmod`; a NaN operand or an infinite left side gives NaN, an
infinite right side gives the left side.

### String ownership

`lh_value_t` **does not own** its content: a string value is an
`lh_str_view_t`, a pointer and a length. Ownership therefore lives elsewhere,
in two places:

- **Constants** live in the machine's private copy of the image, which it keeps
  from load until unload. A value simply points at them; nothing is copied.
- **Strings the program builds** (`+`) are written into the machine's string
  pool. The pool is allocated once at load and rewound by every
  `cnor_vm_reset`. Indexing (`s[i]`) returns a one-character view into the
  indexed string and allocates nothing.

No garbage collector and no reference counting: the pool is released as a
whole and lives exactly as long as one run needs.

## 5. Lexical structure

- **Indentation**: tabs or spaces (a tab counts as 4 columns). Indentation is
  used only while parsing and **never reaches the bytecode**.
- Blank lines and lines holding only a `#` comment are ignored; `#` starts a
  comment anywhere outside a string. **[cnor]** The original lexer used `//`;
  the spec's `#` wins.
- Line breaks inside `()` or `[]` do not end a statement and do not open or
  close a block.
- `;` may end a simple statement, and separates statements on one line.
  **[cnor]** The original lexer rejected `;` although the spec's example used it.
- Literals:
  - integers: `0`, `42`, `0xFF`, `0b1010`;
  - reals: `1.5` (digits on both sides of the dot, so `0..10` stays a range);
    **[cnor]** the spec's `f32` needed them, the original lexer had none;
  - strings in double quotes with the escapes `\n`, `\t`, `\\`, `\"`, `\0`.
- Identifiers: a letter or `_`, then letters, digits and `_`.
- Keywords: a fixed, closed list (section 7.2).

## 6. Syntax

A C skeleton, indentation instead of braces, a little Rust flavour.

```
fn damage(base: i32, mult: f32) -> i32:
    let mut total = base;
    match mult:
        1.0 => total = total * 2,
        _   => total = total - 10,
    if total > 100:
        total = 100;
    return total;
```

The block rule: **a block header always ends with `:`**, and the body is every
following line with a deeper indentation. This removes the line-continuation
ambiguity Python has.

### 6.1 Grammar

```
program     := { import } { top_level }
import      := "import" STRING end
top_level   := fn_decl | native_decl | statement
fn_decl     := "fn" IDENT "(" [ params ] ")" [ "->" type ] ":" block
native_decl := "native" "fn" IDENT "(" [ params ] ")" [ "->" type ] end
params      := param { "," param }
param       := IDENT ":" type
type        := "i32" | "u32" | "f32" | "bool" | "str"
block       := NEWLINE INDENT statement { statement } DEDENT
statement   := simple end | compound
simple      := let | assign | expr | "return" [ expr ] | "break" | "continue"
let         := "let" [ "mut" ] IDENT [ ":" type ] [ "=" expr ]
assign      := IDENT "=" expr | IDENT "[" expr "]" "=" expr
compound    := if | while | for | match
if          := "if" expr ":" block [ "else" ( ":" block | if ) ]
while       := "while" expr ":" block
for         := "for" IDENT "in" ( expr ".." expr | expr ) ":" block
match       := "match" expr ":" NEWLINE INDENT arm { arm } DEDENT
arm         := ( literal | "-" literal | "_" ) "=>" simple [ "," ] end
end         := [ ";" ] ( NEWLINE | DEDENT | EOF )      (or ";" then more on the line)
expr        := binary expression, precedence below
postfix     := primary { "[" expr "]" }
primary     := literal | IDENT | IDENT "(" [ args ] ")" | "(" expr ")" | array
array       := "[" expr ";" INT "]"
literal     := INT | REAL | STRING | "true" | "false" | "none"
```

**[cnor]** An array literal (`[value; count]`) is a declaration, not a value:
its elements are slots of a frame or of the globals, so it is only a `let`'s
initializer. An array is written `let mut scores = [0; 32]`, and read and
written with the index: `scores[i]` and `scores[i] = v`. The bounds are
checked when the program runs.

An array may be a parameter (`fn f(a: array)`) and a global (`let mut
scores = [0; 32]` at the top level). A parameter is a **reference** to the
elements: what the function writes, the caller sees. A global array keeps
its elements in the globals, so they outlive the frame that made them. An
array cannot be returned (its elements would outlive the frame they live in)
and cannot be read as a value: `scores` is an error, `scores[i]` is the
element.

`for v in a` walks the elements of an array, and `len(a)` is how many there
are.

Operator precedence, loosest first; binary operators are left-associative:

| Level | Operators |
|---|---|
| 1 | `or` |
| 2 | `and` |
| 3 | `==` `!=` `<` `<=` `>` `>=` |
| 4 | `+` `-` |
| 5 | `*` `/` `%` |
| unary | `-x`, `not x` |
| postfix | call `f(...)`, index `s[i]` |

`and` / `or` short-circuit and produce the operand that decided (`0 or 5` is
`5`). `not` produces a `bool`. Truthiness: `none`, `false`, `0`, `0.0` and `""`
are false; everything else is true.

### 6.2 Names and scope

- A `let` directly at the top level of the file declares a **global**; every
  function sees it, wherever in the file it is declared. A `let` anywhere else
  is a **local** of the enclosing function, visible until the end of its block.
  When a global's value may be read is section 6.4.
- Without `mut`, a binding is immutable. Parameters and the `for` variable are
  immutable.
- Assigning a name that was never declared is an error; there are no implicit
  globals.
- Functions may be called before their definition in the file.
- A call to a name that is no script function resolves to a `native fn`
  declaration, or to the host's native table given to the compiler.
- `for i in a..b` runs `i` from `a` up to `b`, excluding `b`; `b` is evaluated
  once.
- `match` compares its subject with each arm's literal using `==`, top to
  bottom; the first equal arm runs, `_` always matches, no match does nothing.
- The value of the last top-level expression statement is the program's
  result.
- Built-in: `len(s)` is the length of a string.
- The names a file sees across files are section 6.5.

### 6.3 Types

**[cnor]** The compiler checks types statically, from the annotations and
from the types of literals and expressions. The rules are the conservative
ones: an error only where the program is certainly wrong, and nothing at run
time that was not there before (no casts, no extra instructions).

Every expression has a static type: one of `i32`, `u32`, `f32`, `bool`,
`str`, the type of the `none` literal, **`array`**, or **`any`** when the compiler cannot
know (a host native's result, an unannotated function's result, a variable
that started as `none`). `any` is accepted wherever a type is required, and a
value of type `any` is accepted where anything is; such values are checked by
the machine when an operator executes, as before.

- **Accepts.** A place that requires type `T` accepts `T` and `any`. `i32` and
  `u32` are one family and accept each other (literals above `0x7FFFFFFF` are
  `u32`, and mixed arithmetic is defined, section 4). `f32` is not an integer:
  an `i32` parameter refuses `1.5`, an `f32` one refuses `1`. `none` is
  accepted only where no type is declared. An `array` is only an array: it is
  not a number and not text, so it cannot be used as one.
- **Arrays.** An array's elements hold whatever they were given, so an element
  is of type `any`. The array itself cannot be read as a value: `scores` is an
  error, `scores[i]` is the element.
- **Variables.** `let x: T = e` requires `e` to be a `T`; `let x: T` without
  a value is refused (it would hold `none`). Without an annotation the
  variable takes the type of its initializer, and `let x` or `let x = none`
  gives `any`. Every later assignment must fit that type: `let mut n = 0` then
  `n = "a"` is an error. Parameters have their annotated types; the `for`
  variable has the type of the first bound.
- **Calls.** The argument count must be one the callee accepts. Each argument
  must fit its parameter's type: for script functions, and for natives that a
  `native fn` declaration describes. The result of a call is the declared
  return type, or `any` without one (host natives without a declaration
  included). `len(s)` requires a `str` and gives an `i32`.
- **Returns.** In a function with `-> T`, every `return e` must give a `T`, a
  bare `return` is refused (it returns `none`), and the end of the body must
  be unreachable, decided by the rule a reader applies: some statement of the
  body returns, where an `if` returns when it has an `else` and every branch
  returns, a `match` when it has a `_` and every arm returns, and
  `while true` returns when its body has no `break` of its own (it never ends
  otherwise). `for` and other `while` loops may finish. A function without
  `->` may return anything, or fall off its end with `none`.
- **Operators** fail only where the machine always would: arithmetic
  (`- * / %`) and ordering (`< <= > >=`) need numbers (or `any`); `+` takes
  two numbers or two strings; unary `-` needs a number; `s[i]` needs a `str`
  and an integer index. `==` and `!=` are refused between two types that can
  never be equal (a `str` and a number, a `bool` and a `str`...), because
  the answer is known before the program runs; `none` and `any` compare with
  everything. `and`, `or` and `not` take any type: they test truthiness.
- **Conditions** of `if` and `while` take any type: every kind has a truth
  (section 6.1), and testing a number or a string is idiomatic.
- **`match`.** Each pattern must be able to equal the subject (the rule of
  `==`): a `str` subject cannot have an `1 =>` arm.
- **`for`.** Both bounds must be numbers (or `any`).

The errors are `type_mismatch` ("expected `T`, found `U`"),
`operand_type` (with the types and the operator), `missing_return` and
`wrong_arg_count`, each at the line of the offending expression or statement.

Rationale: the annotations were in the grammar from the start (section 2:
types are meant to be known at compile time); checking them finds mistakes
before a script reaches a device, without changing what any well-typed
program does. `any` keeps the host boundary open: natives return values of
any kind, and a script that tests them at run time still compiles. Code
generation does not use the types (the operand forms already specialise the
hot paths), so an image is the same with or without the checks.

### 6.4 Global initialization

**[cnor]** A global is initialized by its `let`, which runs with the rest of
the file's top-level statements, in source order, every run (section 10). Any
expression may initialize a global: arithmetic, other globals, calls to
functions defined anywhere in the file, natives.

- **Use before `let`.** Top-level code may read or assign a global only after
  its `let` (in source order; a `let` inside a top-level block counts from
  where it is). Anything else is the compile error `not_initialized`, naming
  the global, at the line of the use: `let b = a` before `let a = 1`, and also
  `let g = g + 1` at the top level, where the `g` on the right is the global
  being declared. (Inside a function, `let x = x + 1` reads an outer `x`, as
  before.)
- **Functions.** A function may name any global of its file, wherever it is
  declared. A function cannot know when it runs, so the check is made at the
  calls from top-level code: a call is refused (`not_initialized`, at the
  call) when the function, or any function it may call, names a global whose
  `let` comes after the call. The rule is conservative: a global named on a
  path that never runs counts too; moving the call below the `let`, or the
  `let` above the call, fixes it.
- **Imported files** are initialized before the files that import them
  (section 6.5), so their globals are always ready in the importer.
- **Reset.** `cnor_vm_reset` sets every global to `none`; the next run
  initializes them again. Without a reset a run also re-runs every `let`: the
  initializers are ordinary code at the top of the entry function.

Rationale: the rules keep the one model the machine already had (globals are
slots the entry function assigns), so nothing changes at run time; the errors
catch exactly the cases where a global would be read as `none`. Initial values
stored in the image (the former "not done" item) are not needed: they would
save one store per global per run and need a constant evaluator in the
compiler.

### 6.5 Imports

**[cnor]**

```
import "lib/math.cn"
import "../shared/config.cn"
```

- `import` takes a string literal and must come before every other item of a
  file, at the top level (`misplaced_import` otherwise).
- **Resolution.** The path is resolved by text alone: relative to the
  directory of the importing file, then relative to each of the host's
  **search paths** in order (`cnor_compiler_config_t.search_paths`; the
  command line's `-I`). An absolute path (`/x`, `\x`, `C:...`) is used as it
  is. `/` and `\` both separate segments, `.` and `..` are folded, and the
  result is spelt with `/`, so one file has one spelling: `lib/../a.cn` and
  `a.cn` are the same file. The first candidate the host's loader accepts
  wins; none gives `no_such_module`, a file that cannot be read gives
  `bad_module`.
- **Loader.** The compiler never touches a file system (goal 2). The host
  supplies `load` / `release` callbacks and a context in
  `cnor_compiler_config_t`; an embedded host can serve sources from flash or
  memory. Without a loader every import is `no_such_module`.
- **Once.** Each file is loaded, parsed and compiled once, however many files
  import it. A file that imports itself, directly or through others, is the
  error `import_cycle` at the `import` that closes the cycle. Chains deeper
  than 64 files are `imports_too_deep`.
- **Order.** Files are initialized depth first, each after the files it
  imports, in the order of the `import` lines: every file's top-level code
  runs once per run, before the code of the file importing it, and the main
  file's code runs last.
- **Names.** Each file has its own namespace. A file sees its own top-level
  names (functions, globals, `native fn` declarations) and those of the files
  it imports **directly**; imports are not transitive. Its own names shadow
  imported ones. A name defined by two files it imports is an error
  (`ambiguous_name`) only where it is used. Mutability and types come along: an
  imported immutable global cannot be assigned. Host natives are seen by every
  file; two files declaring the same `native fn` share one import.
- **Linking.** The result is a single image: every file's functions in one
  table, each imported file's top-level code as a function of its own that
  the entry function calls first. The image's file table names each source
  file (section 7), so a runtime error points at the file and line of the
  code that failed.

Rationale: textual resolution and a host loader keep the compiler free of any
OS dependency; per-file namespaces without transitive imports keep a file's
names determined by what it says it imports, and refusing a duplicate only at
use lets two unrelated libraries be imported together. Running each file's
code once, imported files first, extends the global-initialization rule of
section 6.4 across files unchanged.

Not in the language: `&`, `*`, lifetimes, `trait`, `impl`, inheritance,
closures, a preprocessor, metatables, arrays.

---

## 7. The `.tbc` image format, v8

**[cnor]** v1 replaced the original header (whose layout used `sizeof` and
was not portable) and its names section. Every field is a fixed-width
little-endian integer; nothing depends on the compiler's struct layout. v2
keeps the v1 layout and adds opcodes (the operand forms and `for_loop`,
section 8); v3 adds `switch`; v4 gives each function its source file and
turns the file name into a file table, for `import`; v5 adds the index forms
(`index_ll`, `index_li`, `index_i`); v6 adds the array instructions
(`array_new`, `array_get`, `array_set`); v7 adds `call_builtin`, which calls
a function of the language itself (section 9.2). An image of another
version is refused: v6 images must be compiled again.

### 7.1 Header (68 bytes)

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0 | `magic` | 4 bytes | `"cnor"` |
| 4 | `version` | u16 | format version, currently 8 |
| 6 | `flags` | u16 | bit 0: the image carries a line table |
| 8 | `image_size` | u32 | size of the whole image |
| 12 | `stack_slots` | u32 | largest frame (slots + operands), **computed by the compiler** |
| 16 | `globals_count` | u32 | number of globals |
| 20 | `sections[6]` | two u32 each | offset and size of: code, consts, functions, natives, lines, files |

Sections are 4-byte aligned. An absent section has offset 0 and size 0.

- **code**: the instructions of every function, back to back, function 0 first.
- **consts**: u32 count, then per entry: u8 kind (0 string, 1 real), u16
  length, the bytes. A real is stored as its decimal text and parsed at load,
  so the image does not depend on the float format.
- **functions**: u32 count, then 14 bytes per function: u32 entry, u32 size,
  u8 parameter count, u8 slot count (parameters included), u16 max operand
  stack, u16 file (an index into the file table; 0 without one). Function 0
  is the entry point (the main file's top-level statements, after calls to
  each imported file's top-level code).
- **natives**: u32 count, then per import: u16 length, the name.
- **lines**: u32 count, then (u32 code address, u32 line) pairs in ascending
  address order.
- **files**: u32 count, then per file: u16 length, the name (possibly
  empty). File 0 is the main file. Absent when the program is one unnamed
  file. A function naming a file the table does not have is refused at load.

The format version is strict: the machine refuses an image with an unknown
version, and an image produced by a newer version is rejected rather than
"fixed up".

### 7.2 Keywords

```
fn let mut if else match for in while return break continue
true false none and or not import native
```

**[cnor]** `in` (for `for`) and `none` (the literal) were missing from the
original list. `import` is section 6.5.

### 7.3 Addressing

Values are addressed by **indices into frames**, never by pointers:

- locals: `FRAME.<slot>`, slots numbered from 0 in declaration order,
  parameters first;
- globals: `GLOBAL.<index>`;
- function calls: by index into the function table, never by name;
- constants: by ordinal in the constant table. **[cnor]** The original
  compiler wrote a byte offset and the machine read an ordinal.

Jumps are relative to the end of the jump instruction, so code needs no
relocation pass.

## 8. Instruction set

The full list is `include/cnor/op.h` and its info table `src/cnor/op.c`: 76
opcodes. An opcode is one byte; operands follow it, little-endian. The info
table holds each opcode's operands and stack effect; the compiler, the
verifier and the machine all read it.

| Group | Opcodes |
|---|---|
| Data | `push_none`, `push_true`, `push_false`, `push_i32`, `push_u32`, `push_const` |
| Variables | `load_local`, `store_local`, `load_global`, `store_global` |
| Stack | `dup`, `swap`, `pop` |
| Arithmetic | `add`, `sub`, `mul`, `div`, `mod`, `neg` |
| Comparison | `eq`, `ne`, `lt`, `le`, `gt`, `ge` |
| Logic | `not` |
| Operand forms | `add_ll` .. `ge_ll`, `add_li` .. `ge_li`, `add_i` .. `ge_i` |
| Jumps | `jmp`, `jmp_if_false`, `jmp_if_true`, `for_loop`, `switch` |
| Functions | `call`, `ret`, `native` |
| Text | `len`, `index`, `index_ll`, `index_li`, `index_i` |
| Arrays | `array_new`, `array_new_global`, `array_get_l`, `array_set_l`, `array_get`, `array_set` |
| The language's own functions | `call_builtin` |
| Other | `halt` |

Operands: `push_i32` / `push_u32`: 32 bits; `push_const`: u16 ordinal; local
slot: u8; global index: u16; `jmp*`: s32 offset from the end of the
instruction; `call` / `native`: u16 index, then u8 argument count.

**Operand forms.** **[cnor]** Each of the eleven binary operators `add` `sub`
`mul` `div` `mod` `eq` `ne` `lt` `le` `gt` `ge` has three more opcodes that
take their operands from somewhere other than the stack, so the common
`x < n`, `i + 1` or `s + i` is one instruction instead of three:

| Form | Operands | Computes |
|---|---|---|
| `<op>_ll` | slot a, slot b | push `FRAME.a <op> FRAME.b` |
| `<op>_li` | slot a, s32 k | push `FRAME.a <op> k` |
| `<op>_i` | s32 k | replace the top `t` with `t <op> k` |

`k` is a signed 32-bit integer value. A form computes exactly what the plain
operator computes on the same two values: the same kinds, results and errors.
The compiler picks a form when an operand is a local or an integer literal (a
negated literal included) and the plain operator otherwise. A `match` arm with
an integer pattern tests the subject with `eq_li`, unless the whole `match`
uses a `switch` (below).

**`for_loop`** (s32 offset, slot v, slot b) is one step of `for v in a..b`:
`FRAME.v = FRAME.v + 1`, then it jumps when `FRAME.v < FRAME.b`, both by the
rules of `add` and `lt`. The compiler emits `lt_ll v, b` and `jmp_if_false`
before the first pass, so a range that starts at or past its end runs zero
times, then the body, then `for_loop` back to the body; `continue` jumps to
the `for_loop`.

**`switch`** (slot v, s32 k, u8 count; format v3) is followed by its table,
`count` (at least 1) `jmp` instructions. When `FRAME.v == k + i` by the rule
of `==` for some `i` below `count`, execution continues at the `i`-th `jmp`;
otherwise right after the table. The verifier checks that the table is all
`jmp`s inside the function and that code follows it. The compiler uses it for
a `match` with at least four distinct `i32` patterns, then at most a final
`_`, spanning at most twice as many values as arms (and at most 255): arm
bodies follow the default code, holes in the range jump to the default. The
arm that runs is the one the tests in turn would pick, since the patterns
are distinct; it costs two steps (the `switch` and its `jmp`) to reach.

**The index forms** (format v5) take the text and the index from the frame or
the code, so `text[i]` does not copy the text onto the stack first:

| Form | Operands | Computes |
|---|---|---|
| `index_ll` | slot a, slot b | push the one-byte string at `FRAME.a[FRAME.b]` |
| `index_li` | slot a, s32 k | push the one-byte string at `FRAME.a[k]` |
| `index_i` | s32 k | replace the top `t` with the one-byte string at `t[k]` |

A form computes exactly what `index` computes on the same two values: the same
bounds, the same error.

**The array instructions** (format v6, extended in v8) work on the elements of
an array, which are slots of a frame or of the globals:

| Opcode | Operands | Computes |
|---|---|---|
| `array_new` | slot a, u16 count | the `count` elements at `FRAME.a + 1` take the value on the stack; `FRAME.a` becomes the array |
| `array_new_global` | slot a, u16 count | the same, in the globals: the elements at `GLOBALS.a + 1`, and `GLOBALS.a` becomes the array |
| `array_get_l` | slot a | the element of `FRAME.a` at the index on the stack, in its place |
| `array_set_l` | slot a | the element of `FRAME.a` at the index on the stack takes the value on top of it |
| `array_get` | none | the element of the array on the stack, at the index above it, in the index's place |
| `array_set` | none | the element of the array on the stack takes the value at the index above it |

The bounds are checked at run time: an index outside the array fails the run
with `index`.

`array_get_l` and `array_set_l` read the array out of a local slot, so a
local array is not copied onto the stack. `array_get` and `array_set` take the
array as a value, and are how an array that is not a local is read: a
parameter is a local, a global array is not.

**`call_builtin`** (u8 ordinal, u8 argument count; format v7) calls a
function of the language itself (section 9.2). The arguments are on the
stack, as for `native`, and the result takes their place.

**[cnor]** `concat` is gone (`add` joins two strings); `line` is gone (the
optional line table replaces it, so line information costs nothing at run
time); `push_true` / `push_false` replace pushing numbers for booleans.

Three things are deliberately absent:

- **`break`, `continue`, `and`, `or`** are compiled to `jmp*`. The target is
  known when the branch is emitted; a dedicated opcode would make the machine
  guess where a loop ends. In the condition of `if` or `while`, `and`, `or`
  and `not` jump straight to the branch targets and never build a value.
- **`else`** is a jump target, not an instruction.

**[cnor]** Arrays were left out as well ("`lh_value_t` has no array kind"),
until 2026-10-25: a Half-Life plugin needs a state per player, and an array
of a fixed size is the natural shape. cnor adds its own value kind (7) rather
than a kind in `lh`, and keeps the elements in the frame of the function that
declares the array: no allocation, no ownership, and the elements are read
and written where they lie. The price is that an array is a local: its
elements live in a frame, and a frame belongs to a function.

New opcodes are never inserted in the middle of the list: an opcode's number is
part of the file format.

### 8.1 Verification

Before a single instruction runs, the loader proves that functions tile the
code in order; every instruction decodes inside its function and the last one
does not fall through; every operand indexes inside its table (a local slot
inside its function's frame) and every call
passes the callee's parameter count; every jump lands on an instruction start
inside the same function; and the operand stack never underflows, agrees at
every join and stays within the function's declared depth, which itself fits
the header's frame bound. The machine then runs without per-instruction bounds
checks.

## 9. Natives

A native is an ordinary C function. The host passes its table in
`cnor_vm_config_t` (`include/cnor/vm.h`, `include/cnor/native.h`):

```c
struct cnor_native
{
    lh_str_view_t name;
    cnor_native_fn_t fn;
    cnor_argc_t arity_min;
    cnor_argc_t arity_max;
};

typedef void (*cnor_native_fn_t)(const lh_value_t *args, cnor_argc_t argc, lh_ptr ctx,
                                 lh_value_t *result, cnor_error_t *error);
```

The convention:

- arguments arrive ready in an `lh_value_t` array; a string is an
  `lh_str_view_t` into the image or the string pool, which the native may read
  and keep until the next reset, and never frees;
- the result is written to `result`; `lh_value_make_none()` means "nothing".
  **[cnor]** The result shares the slot of the arguments: the machine writes
  it there, so nothing is copied, and a native reads its arguments before it
  writes the result;
- `ctx` is what the host put in the configuration, so a native needs no global
  state and several machines can run at once;
- a native's failure is a code written to `error`, **not** an exception and
  **not** a `longjmp`.

The `native` opcode carries an **index** into the image's import table, not a
name. **[cnor]** The original draft had the compiler resolve names to the
host's table indices; cnor records the imported names in the image and the
machine binds each one to the host's entry **by name once, at load**. A
missing native fails the load; the call itself is still one indexed jump with
no lookup or string comparison. This keeps compiled images valid when the host
reorders or extends its table.

### 9.1 The command line's natives

The language itself has no natives; every host chooses its own. The `cnor`
command line (`cli/`, README.md) gives scripts these, and passes the same
table to the compiler, so no `native fn` declaration is needed:

| Native | Arguments | Does | Returns |
|---|---|---|---|
| `print(...)` | 0 to 255, any kind | writes them to the standard output, separated by one space | `none` |
| `println(...)` | 0 to 255, any kind | the same, then a newline | `none` |
| `eprint(...)` | 0 to 255, any kind | `print` on the standard error | `none` |
| `eprintln(...)` | 0 to 255, any kind | `println` on the standard error | `none` |

A value is written as `none`, `true` or `false`, an integer in decimal, a real
with the fewest digits that read back as the same value (`.0` added when it
would look like an integer; `nan`, `inf`, `-inf`), a string as its bytes
unquoted. A failed write is `native_fail`. A script function of the same name
takes precedence, as for any native.

The command line also loads imported files (section 6.5) from the file system,
relative to the importing file, then in each `-I DIR` given.

### 9.2 The language's own functions

Some functions belong to the language rather than to a host: they work on the
machine's strings, so they put their result into the machine's string pool.
They are called like any other function, and the host may provide a native of
the same name, which then wins; a script function of that name wins over
both.

| Function | Arguments | Does | Returns |
|---|---|---|---|
| `len(text)` | `str` | the length in bytes | `i32` |
| `find(text, needle)` | `str`, `str` | the index of the first occurrence, or -1 | `i32` |
| `contains(text, needle)` | `str`, `str` | whether the needle is in the text | `bool` |
| `replace(text, from, to)` | `str`, `str`, `str` | every occurrence of `from` replaced by `to` | `str` |
| `trim(text)` | `str` | without the spaces and tabs around it | `str` |
| `upper(text)` | `str` | in upper case (ASCII) | `str` |
| `lower(text)` | `str` | in lower case (ASCII) | `str` |
| `starts_with(text, prefix)` | `str`, `str` | whether the text starts with the prefix | `bool` |
| `ends_with(text, suffix)` | `str`, `str` | whether the text ends with the suffix | `bool` |
| `substr(text, start, length)` | `str`, `i32`, `i32` | `length` bytes from `start`, fewer when the text is shorter | `str` |
| `ord(text)` | `str` | the code of the first byte, or -1 for an empty text | `i32` |
| `char(code)` | `i32` | the one-byte string of that code (0 to 255) | `str` |
| `str(value)` | `any` | the value as text | `str` |
| `parse_int(text)` | `str` | the number in the text, or 0 when there is none | `i32` |

The compiler knows the argument types and the result type, so a call with the
wrong type or the wrong number of arguments is refused before it runs. A
`str` argument that is not a string fails the run at the call.

**[cnor]** `split` is not here yet: it would return an array, and an array is
a local (section 8), so it cannot be returned. It comes with the arrays that
can be.

## 10. Runtime

`cnor_vm_t` (`include/cnor/vm.h`) holds:

- `stack`: the value stack, **allocated once at load**, its size set by the
  host (`stack_slots`); the load fails with `stack_too_big` when globals plus
  the image's largest frame do not fit. No reallocation at run time.
- `fp`: the first slot of the current frame.
- `frames`: the call stack, `max_depth` entries, each holding the return
  address, the caller's `fp` and the caller's function.
- `str_pool`: the string pool, allocated at load, rewound by a reset.
- Globals live **at the bottom of the same stack**.

A call frame starts where the arguments are, so the arguments are the callee's
first slots; its other slots are reserved as `none`. A call checks once that
the callee's slots plus its declared operand depth fit; the verifier proved the
callee never needs more. A return drops the whole frame, arguments included,
and leaves the result in its place.

Each run also has an **instruction budget**, `max_steps` in the
configuration (default one million, 0 for unlimited). A run that would exceed
it stops with `step_limit` before the next instruction, unwinds to the
globals and leaves the machine ready for another run: a script with an endless
loop cannot hang the host. **[cnor]** Not in the original draft.

`cnor_vm_run` starts the entry function with the globals as the previous run
left them. **[cnor]** `cnor_vm_reset` sets every global to `none`, rewinds the
string pool and clears the result; a host that handles each event from a clean
state resets before each run.

Everything is allocated through `lh_runtime_allocator_alloc`. Limits (stack
slots, call depth, pool size) are chosen by the host in advance.

## 11. Errors

Consistent with `lh`: no exceptions, no `longjmp`. Codes are the
`cnor_error_t` enum in `include/cnor/error.h`, with `cnor_error_get_message()`
for logs. Every stage reports through one type, `cnor_diag_t`: a code, a line,
an offset (a source offset for the front end, a code address for the verifier
and the machine), when the failure is about a name, a copy of that name (or a
detail such as "expected i32, found str"), and the file it happened in (the
compiler's file, or for the machine the source file of the failing code;
`cnor_vm_get_file_at` answers it for any code address).

- `cnor_vm_load` and `cnor_vm_run` return `lh_bool_t`; the reason is in
  `cnor_vm_get_diag()`, the line comes from the line table (0 without one).
- The format is checked **before** execution: signature, version, section
  bounds and alignment, table sizes, then the verifier (section 8.1).
- Division by zero is a `div_by_zero` code, not a trap: a microcontroller has
  nowhere to trap to.
- Running out of the instruction budget is a `step_limit` code (section 10).

## 12. Versioning

- The image format is versioned by an integer. An incompatible change means a
  new number.
- Images compiled by a newer version are rejected, not repaired.
- The language's semantics are versioned separately from the format: fixing a
  compiler bug need not change the format.

## 13. Deliberately left out

| Left out | Why |
|---|---|
| Closures | They need environments; in a stack VM that is a subsystem of its own |
| Structs and classes | Strings, numbers and native calls are enough for scripts |
| Exceptions | Determinism matters more than convenience in embedded code |
| Overloading, generics | They grow both compile time and bytecode size |
| `f32` without `LH_REAL_ENABLED` | The type is not available on an STM32 by default; a real literal is then a compile error (`no_reals`) |
| Module objects, qualified names (`math.f`), selective imports | A file's names are what it imports directly (section 6.5); one level of names is enough for scripts |
| A file-system loader in the library | Goal 2: the host supplies sources (`cnor_compiler_config_t.load`); the command line has one |

## 14. Status

Done and covered by tests (`tests/`, 196 cases, and 49 runs of the command
line in `tests/cli/`, 50 on Windows):

- [x] lexer: tokens, keywords, indentation as `indent` / `dedent`, newline
  tokens, bracket continuations, `#` comments, integers in three bases, reals,
  `;`;
- [x] parser: functions, `native fn`, `let`, assignments, expressions with
  precedence, calls, indexing, array literals, `if` / `else` chains, `while`,
  `for`, `match` with literal patterns and `_`;
- [x] compiler: every construct above, two passes (declarations, then bodies),
  computed frame sizes and stack depths, optional line table;
- [x] arrays: an array of a fixed size, read and written by index, the bounds
  checked at run time; a local one keeps its elements in the frame, a global one in
  the globals; an array may be a parameter (a reference to the elements), `len(a)` is
  its size and `for v in a` walks them (section 8);
- [x] the language's own functions: `len`, `find`, `contains`, `replace`,
  `trim`, `upper`, `lower`, `starts_with`, `ends_with`, `substr`, `ord`,
  `char`, `str`, `parse_int` (section 9.2);
- [x] static type checking (section 6.3);
- [x] global initializers in source order, with use-before-`let` errors
  (section 6.4);
- [x] `import`: textual resolution, search paths, a host loader, cycles,
  per-file namespaces, one linked image with a file table (section 6.5);
- [x] image format v4: writer, reader, load-time verifier;
- [x] machine: arithmetic, comparisons, jumps, calls, natives bound by name at
  load, strings, globals, line mapping for errors, a per-run instruction
  budget;
- [x] command line: `cnor run`, `compile`, `exec`, natives `print`,
  `println`, `eprint`, `eprintln` (section 9.1), imports from the file
  system with `-I` search paths, tested through CTest;
- [x] performance: a dispatch loop with inline fast paths and computed goto,
  loops tested at the bottom, operand forms on locals and immediates, a
  one-instruction `for` step, inline string indexing, `len`, equality and
  joins, conditions compiled to jumps, jump tables for dense integer
  `match`, in-place global updates, value moves the store buffer can
  forward, a benchmark suite (`bench/`).

Not done:

1. A VM-only build of the library.

Initial values of globals stored in the image, formerly on this list, were
decided against: section 6.4.

## 15. Decisions that differ from the original draft

| Topic | Original | cnor | Why |
|---|---|---|---|
| Comments | lexer: `//`; spec: `#` | `#` | The spec wins |
| `;` | spec example used it; lexer rejected it | optional terminator and separator | The spec wins |
| Reals | spec has `f32` and `1.0`; lexer had no reals | `digits.digits`, stored as text, parsed at load | The spec wins; text keeps the image float-format independent |
| `&&` `\|\|` `!` | lexer accepted some | not tokens | Keywords `and` / `or` / `not` are the spec's |
| Newlines | implicit | `NEWLINE` token | Removes statement-boundary ambiguity |
| Match arms | parser bound a name pattern | literal or `_` only | The spec says literal patterns |
| Keywords | no `in`, no `none` | both | `for x in` and the `none` literal need them |
| Header | `sizeof`-based, names section | v1 (now v3), fixed-width LE, function/native/line tables | Portability |
| `line` opcode | inline | line table | Zero run-time cost |
| Natives | resolved to host indices by the compiler | bound by name at load | Images survive host table changes |
| Reset | rewound the stack past globals | sets globals to `none` | Clean state per event |
| Run time | unbounded | instruction budget per run | An endless loop must not hang the host |
| NaN | not specified | IEEE: unordered | A comparison must not claim NaN equals a number |
| Indexing, `len` | not in the spec | `s[i]`, `len(s)` | Already in the original machine |
| Type annotations | parsed | checked at compile time, with `any` for values of unknown type | Section 2 wants types known at compile time; `any` keeps natives usable |
| Global initializers | entry assigns, no rules | source order, use before `let` is a compile error, no image-stored values | One model, no run-time change (section 6.4) |
| `import` | reserved | host loader, per-file names, one linked image (format v4) | No OS dependency in the library (goal 2) |
