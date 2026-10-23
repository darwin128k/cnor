# cnor architecture

cnor is a pipeline of small, separately testable stages. Each stage has a
public header under `include/cnor/` and its implementation under `src/cnor/`;
headers under `src/cnor/` are library-private.

```
source text (the main file; imported files through the host's loader)
   |  lexer      include/cnor/lexer.h        src/cnor/lexer/
   v
tokens (pulled one at a time)
   |  parser     include/cnor/parser.h       src/cnor/parser/
   v
AST in an arena  include/cnor/ast.h, arena.h
   |  compiler   include/cnor/compiler.h     src/cnor/compiler/
   v
image (.tbc v4)  include/cnor/image.h        src/cnor/image/
   |  loader + verifier                      src/cnor/image/view.c, verify.c
   v
VM               include/cnor/vm.h           src/cnor/vm/
```

Everything reports failures through one type, `cnor_diag_t` (code, line,
offset, copied name or detail, copied file name), with codes in
`cnor_error_t` and messages from `cnor_error_get_message`.

## Modules

| Module | Files | Role |
|---|---|---|
| types | `include/cnor/types.h` | Semantic typedefs (`cnor_line_t`, `cnor_slot_t`, `cnor_code_addr_t`, ...) and index limits |
| error | `error.h`, `error.c` | Error enum, grouped by stage, and the message table |
| diag | `diag.h`, `diag/fields.h`, `diag/initializer.h`, `diag.c` | One diagnostic for every stage; first failure wins |
| lexer | `lexer.h`, `lexer/token.h`, `src/cnor/lexer/{lexer,indent,scan,punct,keyword}.c` | Pull lexer: NEWLINE / INDENT / DEDENT, brackets suppress them, `#` comments, numbers, strings, two-character punctuation first |
| arena | `arena.h`, `arena.c` | Chunked bump allocator for the AST and copied text |
| ast | `ast.h`, `ast.c` | Node kinds, a per-kind data union, accessors |
| parser | `parser.h`, `src/cnor/parser/{parser,token,literal,expr,stmt,control,fn}.c` | Recursive descent with a statement rule table and a binary precedence table |
| op | `op.h`, `op.c` | The opcode enum, the info table (operands, size, stack effect, flags), the operand-form mapping and the shared decoder |
| byte | `src/cnor/byte/{reader,writer}.{h,c}` | Little-endian byte writer over a doubling runtime-allocator block, bounds-checked sticky reader |
| image | `image.h`, `src/cnor/image/{header,view,names,writer,verify}.c` | v4 format: header read, section views, line and file lookup, writer, verifier |
| native | `native.h`, `native/*.h`, `native.c` | Host native entries and tables, lookup by name, arity check |
| compiler | `compiler.h`, `src/cnor/compiler/*.c` | AST to image; see below |
| vm | `vm.h`, `vm/stack.h`, `vm/str_pool.h`, `src/cnor/vm/**` | Loader, dispatch loop with a handler table, opcode families under `vm/op/` |
| cnor | `cnor.h`, `cnor.c` | Library version |
| command line | `cli/*.{h,c}` | The `cnor` program (`CNOR_BUILD_CLI`); see below |

### Compiler modules (`src/cnor/compiler/`)

| File | Role |
|---|---|
| `compiler.c` | Entry points, state setup, the pipeline (load files, declare, compile bodies, check the order of globals, write), moving the entry function's code to the front, building the image parts, diagnostics with their file |
| `module.c` | The files of a compilation: resolving each `import`, loading through the host's loader, parsing once, finding cycles, ordering files after what they import |
| `path.c` | Import paths by text: absolute or not, directory part, joining with `.` / `..` folded and `/` separators |
| `function.c` | Declaration pass (function 0 = the main file's top level; per file in import order: a function for its top-level code, then `fn`, `native fn`, top-level `let`), the entry function (calls to each imported file's code, then the main file's statements), function bodies, the "always returns" rule, frame sizes |
| `type.c` | Static types: names, what accepts what, the types of literals, operators and variables, `type_mismatch` / `operand_type` details |
| `order.c` | Globals used before their `let`: checked directly in top-level code; for functions, the highest global each names and the call graph, checked at each top-level call once every body is compiled |
| `rules.c` | **The dispatch table by node kind**, `cnor_compile_expr` / `cnor_compile_stmt` / `cnor_compile_block` |
| `emit.c` | Byte emission, stack-depth tracking from the op table, labels and jump patch chains, line table |
| `scope.c` | Functions, natives, imports, globals, block-scoped locals, name resolution; top-level names are keyed by (file, name) in a hash index, so resolution stays constant-time in large files; a file sees its own names, then those of the files it imports directly (two of those defining a name: ambiguous) |
| `constant.c` | Constant pool with de-duplication |
| `expr.c` | Literals, names, unary and binary operators (choosing the operand form, see below), indexing |
| `logic.c` | Short-circuit `and` / `or` as values, and conditions (`and` / `or` / `not` in `if` and `while`) as jumps |
| `call.c` | Script calls, native calls, built-in `len` |
| `stmt.c` | `let`, assignment, expression statements, `return` |
| `control_if.c`, `control_loop.c`, `control_match.c` | `if`, `while` (condition tested at the bottom: one conditional jump per iteration), `for` (one `for_loop` per iteration) / `break` / `continue`, `match` (dense integer arms through one `switch` and a table of jumps, other arms test in turn, integer ones with `eq_li`) |

Two passes: the declaration pass registers every function, native and global,
so calls and globals may appear before their definition; the second pass
compiles bodies. Function 0 holds the top-level statements and a hidden result
slot (slot 0) that receives each top-level expression statement's value.

With imports, `module.c` first loads every file depth first, and the files are
declared in that order (imported files first), so global indices follow the
order the `let`s run in. Bodies are compiled file by file in the same order, a
file's top-level code before its functions, so unannotated globals have their
inferred types before any function uses them. The main file's code
(function 0) is therefore compiled after the imported files; `compiler.c`
moves it, and its line records, to the front of the code before writing the
image (jumps are relative and calls use indices, so nothing is patched). Each
function starts a line record of its own in that case. A single file compiles
exactly as before: function 0 first, the same code.

Types are tracked alongside code generation: each expression leaves its
static type in `cnor_compiler_t.type`, each variable keeps one, and the
checks in `type.c` run where the code is emitted. Code generation does not
look at the types.

Stack depth is tracked while emitting: every instruction applies its stack
effect from the op table, labels remember the depth at their first jump, and
code after a terminator is unreachable until a label is bound. The largest
depth of each function becomes its `max_stack`; the header's `stack_slots` is
the largest `slot_count + max_stack`. Every function ends with `push_none; ret`
(the entry with `load_local result; ret`), so control never falls between
functions and no `halt` is needed.

### VM modules (`src/cnor/vm/`)

| File | Role |
|---|---|
| `vm.c` | Lifetime, load/reset/run, queries, failure reporting |
| `load.c` | View, verify, function table, constants (reals parsed), native binding by name, allocation |
| `dispatch.c` | **The dispatch loop** (fast paths inline, see below), the handler table by opcode for everything else, the instruction budget |
| `fast.h` | Inline value access and moves, operand reads and the string primitives (equality, join into the pool) shared by the dispatch loop and the string handlers |
| `stack.c`, `str_pool.c` | Value stack and string pool |
| `value.c` | Truthiness, numeric ordering and equality across kinds (mixed signed/unsigned by value, reals IEEE: NaN is unordered) |
| `op/data.c` | Pushes, locals, globals, `dup` / `swap` / `pop` |
| `op/arith.c` | `add` (numbers or strings) `sub` `mul` `div` `mod` `neg` |
| `op/compare.c` | `eq` `ne` `lt` `le` `gt` `ge` `not` |
| `op/operand.c` | The operand forms `<op>_ll` / `_li` / `_i`: fetch the operands, then the same value-level code as `op/arith.c` and `op/compare.c` |
| `op/control.c` | Jumps, `for_loop`, `switch`, `call`, `ret`, `halt`, `native` |
| `op/text.c` | String join, `len`, `index` |

## Extension points

All extension points are tables indexed by an enum, filled with designated
initializers, so a missing entry is visible in one place.

### Adding a language construct

1. **Lexer** (only if it needs new spelling): add the keyword to
   `cnor_keyword_t` and `m_keywords` (`src/cnor/lexer/keyword.c`), or the
   punctuation to `cnor_punct_t` and `m_spellings` (`src/cnor/lexer/punct.c`,
   two-character spellings first).
2. **AST**: add a `cnor_node_kind_t` value and, if needed, a member of
   `union cnor_node_data` (`include/cnor/ast.h`).
3. **Parser**: a statement that starts with a keyword is one row in
   `m_stmt_rules` (`src/cnor/parser/stmt.c`; `compound` says whether it owns a
   block). A binary operator is one row in `m_binary_rules`
   (`src/cnor/parser/expr.c`) with its precedence.
4. **Compiler**: write `cnor_compile_<kind>` in the module of its family,
   declare it in `src/cnor/compiler/internal.h`, and set its row in `m_rules`
   (`src/cnor/compiler/rules.c`): `.expr` for expressions, `.stmt` for
   statements. A node kind without a handler fails with `unknown_construct`.
   A binary operator that maps to one instruction is one row in
   `m_binary_ops` (`src/cnor/compiler/expr.c`).
5. **Types**: give the construct's expressions a static type in
   `self->type` and check its operands with `cnor_type_require` /
   `cnor_type_binary` (`src/cnor/compiler/type.c`).
6. **Tests**: a parser case in `tests/test_parser.c` and an end-to-end case in
   `tests/test_compiler.c` (`tests/test_language.c` for typing, the order of
   globals and imports).

Most constructs need no new instruction: control flow is labels and jumps
(`cnor_label_init`, `cnor_emit_jump`, `cnor_emit_bind`), and the emitter keeps
the stack depth right as long as every path reaching a label has the same
depth.

### Adding an instruction

1. Append it to `cnor_op_t` (`include/cnor/op.h`); never insert in the middle,
   the number is part of the format.
2. Add its row to `m_ops` (`src/cnor/op.c`): name, up to three operand kinds,
   size, pops, pushes, flags (`JUMP`, `TERMINATOR`, `POPS_ARGC`). The
   compiler's depth tracking, the verifier and the decoder all follow from
   this row. A local slot is the `slot` kind, which the verifier bounds by the
   function's slot count on its own; a jump offset must be operand 0.
3. If another operand indexes a table, teach `cnor_verify_operands`
   (`src/cnor/image/verify.c`) its bound.
4. A change to what an existing opcode means, or a new one, is a new format:
   raise `CNOR_IMAGE_VERSION` (`include/cnor/image.h`).
5. Write the handler in `src/cnor/vm/op/<family>.c`, declare it in
   `src/cnor/vm/internal.h`, and set its slot in `m_handlers`
   (`src/cnor/vm/dispatch.c`).
6. Give it a label in the dispatch loop: an entry in `m_labels` and a
   `CNOR_VM_OP(name)` block that either runs the common case inline or calls
   `CNOR_VM_SLOW()`, which runs the handler. The `_Static_assert` on
   `cnor_op_count` in `dispatch.c` fails until this is done.
7. Add an image test in `tests/test_image_vm.c`.

The operand forms are three contiguous families (`_ll`, `_li`, `_i`), each in
the order of `m_binary_ops` (`src/cnor/op.c`); `cnor_op_get_operand_form` and
`cnor_op_get_binary_base` map between a form and its plain operator for the
compiler and the shared slow-path handler, and a `_Static_assert` checks the
families' ends. The families cannot grow without inserting in the middle, so an
operator added later gets its forms as a new family appended at the end.

### Adding an error

Add the code to `cnor_error_t` in its stage's group and its message to
`m_messages` (`src/cnor/error.c`); the table is indexed by the code.

## Memory

- The parser allocates nodes and copied text from a `cnor_arena_t`; freeing the
  arena frees the tree.
- The compiler uses `lh_array_t` for code, constants, lines and scope tables
  and hands back one `lh_runtime_allocator` block (`cnor_image_t`).
- The VM copies the image at load and allocates the stack, frames, constants,
  bindings and string pool once; nothing is allocated while running.
- Diagnostics copy the name they report (up to `CNOR_DIAG_NAME_CAPACITY`
  bytes), so they outlive the tree or image they came from.

## The dispatch loop

`cnor_vm_dispatch` (`src/cnor/vm/dispatch.c`) is where a program spends its
time, so it is written for speed while every rule of the language stays in one
place, the handlers under `src/cnor/vm/op/`:

- **Locals, not fields.** The instruction pointer, the stack top, the frame
  base and the top of the call stack live in locals for the whole run and are
  written back to the machine only around a slow-path call and at exit. The
  call depth and stack limits are read from the machine in `call` instead:
  two more locals across the loop made the `switch` build 10 to 20% slower on
  `calls`, `globals` and `streq`.
- **No per-instruction decode or bounds checks.** Operands are read straight
  from the code. The verifier proved at load that every instruction decodes
  inside its function, every jump lands on an instruction start, every index
  operand is in range and the operand stack never under- or overflows its
  function's declared depth (section 8.1 of the spec), so the loop does not
  check any of it again. A call still checks the call depth and that the
  callee's frame fits, once per call. With `switch` dispatch the `default`
  is `lh_compiler_unreachable()`, so the jump table needs no range check
  either.
- **Fast paths and the slow path.** Each opcode has a label. The common case
  runs inline: constants, locals, globals, signed-integer arithmetic and
  comparisons, jumps, `call`, `ret`, `native`, and on strings indexing, `len`,
  equality and joins (`add`, `add_ll`). Anything else (unsigned and real
  numbers, an unsigned index, a full string pool, `halt`, every failure) goes
  through `CNOR_VM_SLOW()`: the locals are written back,
  the instruction is decoded and its handler from `m_handlers` runs exactly as
  it would without the fast path. A fast path never decides a language rule on
  its own: when its operands are not the kinds it knows, it defers.
- **Fusion.** A few instructions look at the opcode after them and run their
  usual partner without going back through dispatch: a comparison with the
  conditional jump after it (the boolean is never built), arithmetic with the
  `store_local` after it, two `load_local`s, and `push_i32` with the `add`,
  `sub` or `lt` after it, and every operand form with the `store_local` or
  conditional jump after it; the stack `add` / `sub` / `mul` with a
  `store_global` after them (`g = g + i`), and `switch` with the table `jmp`
  it picks. The partner is a separate
  instruction of the image: it is counted against the budget, and taken only
  when the budget has room.
- **Conditions as jumps.** In `if` and `while` only the truth of the
  condition matters, so `cnor_compile_branch` turns `and`, `or` and `not`
  into conditional jumps straight to the branch targets instead of building
  the deciding value with `dup` / `jmp_if_*` / `pop`. Each comparison then
  fuses with its own jump. Operands are evaluated in the same order and as far
  as the value would evaluate them.
- **`switch`.** A `match` with at least four distinct integer arms (and at
  most a final `_`) spanning at most twice as many values, and at most 255,
  compiles to `switch slot, first, count` and a table of `count` `jmp`s:
  a signed subject picks its entry by one subtraction; other kinds compare
  against each `first + i` by the rule of `==`, as the tests in turn would.
  Holes jump to the default. The last arm of every `match` falls through to
  the end instead of jumping there.
- **Operand forms.** Most instructions in a loop body used to be
  `load_local` and `push_i32` feeding an operator. `<op>_ll` reads two locals,
  `<op>_li` a local and an immediate, `<op>_i` the top and an immediate, so
  with fusion `i = i + 1` or `if x < n` is one dispatch. The fast path handles
  signed integers (with `eq` / `ne` also comparing any two values in place);
  anything else, and a `div` / `mod` by 0 or -1, defers to `op/operand.c`,
  which fetches the operands and runs the plain operator's value-level code.
- **`for_loop`.** One instruction per `for` step: add one to the variable,
  compare with the hidden bound slot, jump back. Signed integers run inline;
  other kinds run `add` and `lt`'s value-level code, so wrapping, mixed kinds
  and reals behave exactly as the written-out loop would.
- **Computed goto.** With GCC and Clang the next instruction is reached through
  a table of label addresses, one indirect branch per instruction (15 to 25%
  faster than the switch on this code); `CNOR_VM_COMPUTED_GOTO` (CMake option,
  ON by default) set to OFF, or any other compiler, uses a portable `switch`.
  Both are built and tested.
- **Immediates.** A signed 32-bit operand is decoded as
  `(bits ^ 0x80000000) - 0x80000000` in 64 bits: two additions, no
  implementation-defined conversion, where the earlier sign test took a
  compare and a conditional move.
- **`for_loop`'s jump back** is marked likely and has its own dispatch, apart
  from the fall-through: its target is always the loop's first instruction,
  so with computed goto that indirect jump predicts on its own, and GCC keeps
  it on the straight path (`stops_on_both_sides_of_a_for_loop`).
- **Step budget.** `budget` counts down the steps left (`limit - steps`), so
  the per-instruction check is a test for 0 and a decrement; whether a limit
  is set at all is only asked at 0, out of line (`cnor_vm_budget_spent`), and
  a fused partner only checks `budget != 0`. With no limit the count is
  `-budget` modulo 2^32, which wraps exactly like the old counter
  (`counts_unlimited_steps_across_the_wrap`).
- **Inline value access.** `src/cnor/vm/fast.h` reads and writes the
  `lh_value_t` fields directly; lh's accessors are out-of-line functions, and a
  call per field was most of the cost of an instruction before. Strings follow
  the same rule: a string value is written in place (`cnor_vm_fast_set_str`)
  instead of returning a 24-byte value from `lh_value_make_str`; `text[i]`
  narrows the view it indexes; equality compares lengths, then the view
  pointers (the same view or two empty ones are equal without reading a byte),
  then the last 8 bytes, then the rest 8 bytes at a time (words read with
  `lh_bit_unpack_le64`, so alignment does not matter; the last word overlaps
  instead of a byte loop over the tail; 4-byte words and single bytes below 8;
  `lh_memory_std_compare` beyond `CNOR_VM_FAST_LONG_TEXT` bytes), instead of
  the `lh_value_equals` -> `lh_str_view_equals` -> `lh_str_ptr_equals_by_size`
  -> `lh_memory_compare` chain; a join takes its bytes straight from the pool
  and copies them a word at a time the same way.
  The slow-path handlers (`op/text.c`, `value.c`) use the same primitives, so
  the two paths cannot drift apart. A join followed by `store_local` stores
  directly, like integer arithmetic.
- **Moving values.** `cnor_vm_fast_move` copies a value field by field: the
  kind, the first 8-byte word, and for a string its length. A struct copy of
  the 24-byte `lh_value_t` reads 16 bytes at once, and a load that wide
  cannot be forwarded from the narrower stores that wrote the value an
  instruction earlier (`cnor_vm_fast_set_sint` writes a kind and a word), so
  every `load_local` of a fresh result waited for those stores to retire.
  Copying both words unconditionally did not help: GCC merges the two loads
  back into one 16-byte load. `load_local`, `store_local`, globals, `dup`,
  `swap`, the fused stores, a native's result and `ret` all move values this
  way; `ret` moves the result straight into the slot of the returning
  frame's first local, which is where the caller's stack continues.

  Measured and left out (2026-10-23, `bench/` and string micro-benchmarks):
  interning runtime strings (a hash and a probe per join made `strbuild` 80%
  slower and only sped up comparing two equal strings built at run time;
  constants are already shared by the compiler's constant table, so equal
  literals already compare by pointer); a cached hash per string (a view into
  the image, the pool or another string has nowhere to keep it without
  changing `lh_value_t`, and the length plus the last word already reject
  almost every unequal pair at once); appending in place to the last pool
  allocation (4% on `strbuild`, but it changes how much of the pool a program
  uses, so where `str_pool_full` happens); fusing `push_const` with `eq` / `ne`
  / `add` and `load_local load_local` with `index` (no gain or slower: the
  extra checks cost what the saved dispatch did).

  Also measured and left out (2026-10-23, win #4): checking the budget only at
  back-jumps and calls (removing the check altogether bought 0 to 4%, within
  noise on most benchmarks, and an exact budget then needs a second dispatch
  table); checking a native's arity at load instead of per call (0 to 3%, and
  a bad call that never runs would start failing at load); the
  `store_global` fusion on the operand forms too (`for` loops on locals got
  slower by more than globals gained); fusing `load_global` with a following
  `load_local` (no gain on top of the `store_global` fusion).

  Calls (2026-10-23, win #5): field-by-field moves and the call stack top in
  a local took `calls` from 20.1 to 11.1 ms, `fib` 2.6 to 2.1, `native` 15.3
  to 8.4, `globals` 7.2 to 4.5, `nested` 7.6 to 6.4, `text` 9.3 to 8.0 and
  `strbuild` 6.2 to 5.2 (computed goto; the step counts are unchanged).
  Measured and left out: fusing `ret` with the stack `add` / `sub` / `mul`
  before it (`calls` 11 to 14 ms: the shared return path cost more than the
  saved dispatch, so `ret_local` and fused `load_local ... call` forms were
  not pursued); not tracking the running function in the loop (no gain);
  keeping the function table, the depth limit and the stack end in locals
  (noise with computed goto, slower with `switch`). Tail calls were not tried:
  they would change which programs hit `call_depth` and the frames an error
  reports.

  Win #6 (2026-10-23): the cheaper immediates (instructions executed down 3
  to 14% on every workload; `match` -15%, `logic` -20% alone), the step
  budget counted down (`switch`: `fib` -20%, `loop` -16%, `nested`, `text`,
  `calls` and `strbuild` 6 to 9% on top of the immediates; computed goto: up
  to 2% fewer instructions), `add_ll` testing integers before strings (2%
  fewer instructions on `loop`) and the unchecked `switch` (5 to 7% fewer
  instructions in that build). Together, computed goto: `logic` -18%, `match`
  -14%, `text` -12%, `loop`, `fib` and `streq` -7%; `switch`: `match` -17%,
  `logic` -8%, `text` and `nested` -6%, `calls` -6%. Step counts
  are unchanged. Measured and left out: a 32-bit division when both operands
  fit (no gain: 64-bit division is fast on this core); a mask for a
  remainder by a power of two (`match` -22%, but the test made `logic` 6%
  slower, and doing it at compile time needs a new opcode); reciprocals of
  recent divisors cached in the loop (slower everywhere); fusing arithmetic
  with a following `eq_i` / `ne_i` (40% fewer dispatches on `logic`, no
  time gained, more instructions elsewhere); a `ret` followed by
  `store_local` storing straight into the caller's slot (`calls` -2 to -7%,
  `fib` +2% instructions); a masked, branch-free `for_loop` jump (gave back
  the whole `match` / `logic` gain). Not attempted: a 16-byte internal value
  (it needs its own value type across every handler and a conversion at each
  native call, for one address computation per operand) and register or
  accumulator operands (a new instruction set).

  Win #6 regressions, found and fixed (2026-10-23; medians of 15 interleaved
  rounds against win #5, with callgrind counts and per-line `perf` samples
  to tell cost from layout):
  - `native` +12 to 17% with computed goto (first reported as 9%): every
    variant of the immediate decode, a plain `movslq` included, moved
    `for_loop`'s jump back out of line, behind a taken branch, and merged it
    with the exit into one indirect jump; the samples piled up on that jump.
    Marking the jump back likely and giving it its own dispatch: `native`
    level with win #5, `globals` -7% (was +3 to 6%, the same jump).
  - `switch` `fib` +3%, `strbuild` +9%: `budget == 0 && limit != 0` was
    computed as a truth value (`sete`, `and`) in every handler before the
    shared dispatch, 5 to 9% more instructions than win #5. Testing
    `budget` alone and asking about the limit out of line takes 9 to 15% of
    the instructions off the `switch` build and about 1% off computed goto;
    `fib` is now -15%, `strbuild` +2% (noise level).
  - Tried and dropped: the decrement as the test (`budget-- == 0`, GCC then
    kept two copies of the budget alive and reloaded the label table:
    instructions +3 to 5% with computed goto); `-fno-gcse` (`native` +14%);
    `-falign-labels=16` and `-falign-jumps=32` as a default (the first only
    hid the `for_loop` layout, the second made `globals` 13% slower).
  - After the fixes, against win #5 (computed goto / `switch`): `fib` -10 /
    -15%, `loop` -15 / -19%, `nested` -12 / -10%, `text` -12 / -2%, `native`
    +0.5 / -3%, `match` -15 / -19%, `calls` -3 / -15%, `globals` -7 / -21%,
    `logic` -18 / -23%, `strbuild` -8 / +2%, `streq` -15 / +1%. Step counts
    are unchanged.

- **Where the loop starts.** With GCC and Clang `dispatch.c` is compiled
  with `-falign-functions=64` (CMakeLists.txt), so `cnor_vm_dispatch` starts
  on a 64-byte line whatever is linked before it. Its speed depends on that
  offset: the faster compiler of 2026-10-23 left the dispatch object byte for
  byte the same, but the smaller code before it moved the loop from offset 0
  to offset 32 of a line, and the computed-goto build (`-O2`,
  `-Wa,-mbranches-within-32B-boundaries`) ran `native` 9%, `globals` 8% and
  `nested` 7% slower on the same instruction count; padding the link by 16
  bytes, or the alignment, put it back. Which offset is best is a property of
  the build, not a rule: in the `-O3` `switch` build offset 32 runs `loop`
  and `nested` about 10% faster than offset 0 and `match` 10% slower.

## Bounding a run

Memory is bounded at load (stack slots, call depth, string pool) and time is
bounded per run: `cnor_vm_config_t.max_steps` caps the instructions one
`cnor_vm_run` executes (default `CNOR_VM_DEFAULT_MAX_STEPS`, one million; 0
means unlimited). The check is a decrement and a test per instruction in
`cnor_vm_dispatch`, made before the instruction runs; an exhausted budget
fails with `cnor_error_step_limit` at that instruction's address and unwinds
like any other failure, so the machine can run again (with a fresh budget) or
be reset. `cnor_vm_get_steps` reports what the last run used, which is how a
host sizes the budget for its scripts.

Instructions the dispatch loop executes together (see below) are still
counted one by one, and a pair is only taken together when the budget has room
for both, so a budget of N stops after exactly N instructions with or without
those shortcuts (`tests/test_compiler.c`,
`stops_after_exactly_the_budget_inside_fused_pairs` and
`stops_after_exactly_the_budget_inside_for_loops`,
`stops_after_exactly_the_budget_inside_tables_and_conditions`). Each operand
form and each `for_loop` is one instruction, so a program now needs fewer
steps than with image format v1 for the same work; v3 needs fewer again where
a condition uses `and` / `or` / `not` (no `dup` / `pop`), a `match` dispatches
through a table (two steps to reach any arm) or a `match` ends (no jump after
the last arm).

## Internal building blocks that could move to lh

| cnor module | Would be in lh |
|---|---|
| `cnor_arena_t` (`arena.h`) | A chunked bump arena on the runtime allocator |
| `cnor_byte_writer_t` (`src/cnor/byte/writer.h`) | A growable little-endian byte buffer with alignment and patching |
| `cnor_byte_reader_t` (`src/cnor/byte/reader.h`) | A bounds-checked, sticky-failure little-endian reader |
| `cnor_str_pool_t` (`vm/str_pool.h`) | A fixed byte arena with rewind |
| `struct cnor_name_index` (`src/cnor/compiler/internal.h`) | A string-keyed hash map (lh has FNV-1a hashing but no map) |
| `src/cnor/vm/fast.h` | Header-inline variants of the `lh_value_t` accessors |
| `bench/clock.c` | A nanosecond monotonic clock (`lh_os_tick_us` is microseconds and needs the OS option) |

## The command line

`cli/` is the `cnor` program (CMake target `cnor_cli`, since `cnor` is the
library), built when `CNOR_BUILD_CLI` is ON: by default when cnor is the
top-level project, not when another project adds it as a subdirectory. It is a
host like any other, written only against the public headers:

| File | Role |
|---|---|
| `main.c` | Entry point: arguments, then the subcommand |
| `args.{h,c}` | Arguments as UTF-8; on Windows re-read from `GetCommandLineW` and converted |
| `options.{h,c}` | Subcommands, options, defaults, `--help`, the exit codes |
| `command.{h,c}` | `run`, `compile`, `exec`: compile, load, run, report `file:line: message` |
| `file.{h,c}` | Whole-file reads and writes through lh's file API (UTF-8 paths, UTF-16 calls on Windows); `-` is the standard stream |
| `loader.{h,c}` | The compiler's import loader on the file system (a file that cannot be opened is not found, one that cannot be read is an error), `-I` search paths, the UTF-8 byte order mark |
| `natives.{h,c}` | `print`, `println`, `eprint`, `eprintln`; their `ctx` holds the two streams |
| `text.{h,c}` | How a value is written, for the natives and the result |

The natives are given to the compiler too, so a script calls them without a
`native fn` declaration, and an image compiled by `cnor compile` binds them by
name when `cnor exec` loads it. `tests/cli/` runs the program on sample
scripts through CTest (`check.cmake` compares the exit status and both
outputs; `CMAKE_CROSSCOMPILING_EMULATOR` runs a Windows build under wine).

## Benchmarks

`bench/` holds a benchmark runner (`CNOR_BUILD_BENCH=ON`, see
[bench/README.md](../bench/README.md)): fixed workloads with known results,
compile, load and run timed separately, median and minimum of several runs,
VM instructions per second from `cnor_vm_get_steps`, and C, Lua and Python
versions of the same work for comparison.
