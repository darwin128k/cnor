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
  **[cnor]** Type annotations are parsed and validated as names, but not yet
  checked; values carry their kind at run time (`lh_value_t`) and operators
  check kinds when they execute.

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
| (none) | `lh_value_kind_ptr` | natives only; not reachable from a script |

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
program     := { top_level }
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
assign      := IDENT "=" expr
compound    := if | while | for | match
if          := "if" expr ":" block [ "else" ( ":" block | if ) ]
while       := "while" expr ":" block
for         := "for" IDENT "in" expr ".." expr ":" block
match       := "match" expr ":" NEWLINE INDENT arm { arm } DEDENT
arm         := ( literal | "-" literal | "_" ) "=>" simple [ "," ] end
end         := [ ";" ] ( NEWLINE | DEDENT | EOF )      (or ";" then more on the line)
expr        := binary expression, precedence below
postfix     := primary { "[" expr "]" }
primary     := literal | IDENT | IDENT "(" [ args ] ")" | "(" expr ")"
literal     := INT | REAL | STRING | "true" | "false" | "none"
```

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

Not in the language: `&`, `*`, lifetimes, `trait`, `impl`, inheritance,
closures, a preprocessor, metatables, arrays.

---

## 7. The `.tbc` image format, v3

**[cnor]** v1 replaced the original header (whose layout used `sizeof` and
was not portable) and its names section. Every field is a fixed-width
little-endian integer; nothing depends on the compiler's struct layout. v2
keeps the v1 layout and adds opcodes (the operand forms and `for_loop`,
section 8); v3 adds `switch`. An image of another version is refused.

### 7.1 Header (68 bytes)

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0 | `magic` | 4 bytes | `"cnor"` |
| 4 | `version` | u16 | format version, currently 2 |
| 6 | `flags` | u16 | bit 0: the image carries a line table |
| 8 | `image_size` | u32 | size of the whole image |
| 12 | `stack_slots` | u32 | largest frame (slots + operands), **computed by the compiler** |
| 16 | `globals_count` | u32 | number of globals |
| 20 | `sections[6]` | two u32 each | offset and size of: code, consts, functions, natives, lines, file |

Sections are 4-byte aligned. An absent section has offset 0 and size 0.

- **code**: the instructions of every function, back to back, function 0 first.
- **consts**: u32 count, then per entry: u8 kind (0 string, 1 real), u16
  length, the bytes. A real is stored as its decimal text and parsed at load,
  so the image does not depend on the float format.
- **functions**: u32 count, then 12 bytes per function: u32 entry, u32 size,
  u8 parameter count, u8 slot count (parameters included), u16 max operand
  stack. Function 0 is the entry point (the top-level statements).
- **natives**: u32 count, then per import: u16 length, the name.
- **lines**: u32 count, then (u32 code address, u32 line) pairs in ascending
  address order.
- **file**: the source file name, raw bytes.

The format version is strict: the machine refuses an image with an unknown
version, and an image produced by a newer version is rejected rather than
"fixed up".

### 7.2 Keywords

```
fn let mut if else match for in while return break continue
true false none and or not import native
```

**[cnor]** `in` (for `for`) and `none` (the literal) were missing from the
original list. `import` is reserved but not implemented.

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

The full list is `include/cnor/op.h` and its info table `src/cnor/op.c`: 70
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
| Text | `len`, `index` |
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

**[cnor]** `concat` is gone (`add` joins two strings); `line` is gone (the
optional line table replaces it, so line information costs nothing at run
time); `push_true` / `push_false` replace pushing numbers for booleans.

Three things are deliberately absent:

- **`break`, `continue`, `and`, `or`** are compiled to `jmp*`. The target is
  known when the branch is emitted; a dedicated opcode would make the machine
  guess where a loop ends. In the condition of `if` or `while`, `and`, `or`
  and `not` jump straight to the branch targets and never build a value.
- **`else`** is a jump target, not an instruction.
- **Arrays.** `lh_value_t` has no array kind, and adding one for a container is
  exactly the kind of abstraction the language stays tiny to avoid. Indexing
  works on strings.

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

typedef lh_value_t (*cnor_native_fn_t)(const lh_value_t *args, cnor_argc_t argc,
                                       lh_ptr ctx, cnor_error_t *error);
```

The convention:

- arguments arrive ready in an `lh_value_t` array; a string is an
  `lh_str_view_t` into the image or the string pool, which the native may read
  and keep until the next reset, and never frees;
- the return value is an `lh_value_t`; `lh_value_make_none()` means "nothing";
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
and the machine) and, when the failure is about a name, a copy of that name.

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
| Arrays | `lh_value_t` has no array kind (see section 8) |
| Exceptions | Determinism matters more than convenience in embedded code |
| Overloading, generics | They grow both compile time and bytecode size |
| `f32` without `LH_REAL_ENABLED` | The type is not available on an STM32 by default; a real literal is then a compile error (`no_reals`) |
| Modules via `import` | One file first; loading through `lh_io_stream_t` later |

## 14. Status

Done and covered by tests (`tests/`, 169 cases):

- [x] lexer: tokens, keywords, indentation as `indent` / `dedent`, newline
  tokens, bracket continuations, `#` comments, integers in three bases, reals,
  `;`;
- [x] parser: functions, `native fn`, `let`, assignments, expressions with
  precedence, calls, indexing, `if` / `else` chains, `while`, `for`, `match`
  with literal patterns and `_`;
- [x] compiler: every construct above, two passes (declarations, then bodies),
  computed frame sizes and stack depths, optional line table;
- [x] image format v3: writer, reader, load-time verifier;
- [x] machine: arithmetic, comparisons, jumps, calls, natives bound by name at
  load, strings, globals, line mapping for errors, a per-run instruction
  budget;
- [x] performance: a dispatch loop with inline fast paths and computed goto,
  loops tested at the bottom, operand forms on locals and immediates, a
  one-instruction `for` step, inline string indexing, `len`, equality and
  joins, conditions compiled to jumps, jump tables for dense integer
  `match`, in-place global updates, a benchmark suite (`bench/`).

Not done, in a sensible order:

1. Type checking of annotations (section 2).
2. Initial values of globals in the image (today they start as `none` and the
   entry function assigns them).
3. `import`.
4. A command-line compiler and a VM-only build of the library.

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
