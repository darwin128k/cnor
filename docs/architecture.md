# cnor architecture

cnor is a pipeline of small, separately testable stages. Each stage has a
public header under `include/cnor/` and its implementation under `src/cnor/`;
headers under `src/cnor/` are library-private.

```
source text
   |  lexer      include/cnor/lexer.h        src/cnor/lexer/
   v
tokens (pulled one at a time)
   |  parser     include/cnor/parser.h       src/cnor/parser/
   v
AST in an arena  include/cnor/ast.h, arena.h
   |  compiler   include/cnor/compiler.h     src/cnor/compiler/
   v
image (.tbc v1)  include/cnor/image.h        src/cnor/image/
   |  loader + verifier                      src/cnor/image/view.c, verify.c
   v
VM               include/cnor/vm.h           src/cnor/vm/
```

Everything reports failures through one type, `cnor_diag_t` (code, line,
offset, copied name), with codes in `cnor_error_t` and messages from
`cnor_error_get_message`.

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
| op | `op.h`, `op.c` | The opcode enum, the info table (operands, size, stack effect, flags) and the shared decoder |
| byte | `src/cnor/byte/{reader,writer}.{h,c}` | Little-endian byte writer over `lh_array_t`, bounds-checked sticky reader |
| image | `image.h`, `src/cnor/image/{header,view,names,writer,verify}.c` | v1 format: header read, section views, line lookup, writer, verifier |
| native | `native.h`, `native/*.h`, `native.c` | Host native entries and tables, lookup by name, arity check |
| compiler | `compiler.h`, `src/cnor/compiler/*.c` | AST to image; see below |
| vm | `vm.h`, `vm/stack.h`, `vm/str_pool.h`, `src/cnor/vm/**` | Loader, dispatch loop with a handler table, opcode families under `vm/op/` |
| cnor | `cnor.h`, `cnor.c` | Library version |

### Compiler modules (`src/cnor/compiler/`)

| File | Role |
|---|---|
| `compiler.c` | Entry points, state setup, building the image parts, diagnostics |
| `function.c` | Declaration pass (function 0 = top level, then `fn`, `native fn`, top-level `let`), entry function, function bodies, frame sizes |
| `rules.c` | **The dispatch table by node kind**, `cnor_compile_expr` / `cnor_compile_stmt` / `cnor_compile_block` |
| `emit.c` | Byte emission, stack-depth tracking from the op table, labels and jump patch chains, line table |
| `scope.c` | Functions, natives, imports, globals, block-scoped locals, name resolution; a hash index over the top-level names keeps resolution constant-time in large files |
| `constant.c` | Constant pool with de-duplication |
| `expr.c` | Literals, names, unary and binary operators, indexing |
| `logic.c` | Short-circuit `and` / `or` |
| `call.c` | Script calls, native calls, built-in `len` |
| `stmt.c` | `let`, assignment, expression statements, `return` |
| `control_if.c`, `control_loop.c`, `control_match.c` | `if`, `while` / `for` (condition tested at the bottom: one conditional jump per iteration) / `break` / `continue`, `match` |

Two passes: the declaration pass registers every function, native and global,
so calls and globals may appear before their definition; the second pass
compiles bodies. Function 0 holds the top-level statements and a hidden result
slot (slot 0) that receives each top-level expression statement's value.

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
| `fast.h` | Inline value access and operand reads for the dispatch loop only |
| `stack.c`, `str_pool.c` | Value stack and string pool |
| `value.c` | Truthiness, numeric ordering and equality across kinds (mixed signed/unsigned by value, reals IEEE: NaN is unordered) |
| `op/data.c` | Pushes, locals, globals, `dup` / `swap` / `pop` |
| `op/arith.c` | `add` (numbers or strings) `sub` `mul` `div` `mod` `neg` |
| `op/compare.c` | `eq` `ne` `lt` `le` `gt` `ge` `not` |
| `op/control.c` | Jumps, `call`, `ret`, `halt`, `native` |
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
5. **Tests**: a parser case in `tests/test_parser.c` and an end-to-end case in
   `tests/test_compiler.c`.

Most constructs need no new instruction: control flow is labels and jumps
(`cnor_label_init`, `cnor_emit_jump`, `cnor_emit_bind`), and the emitter keeps
the stack depth right as long as every path reaching a label has the same
depth.

### Adding an instruction

1. Append it to `cnor_op_t` (`include/cnor/op.h`); never insert in the middle,
   the number is part of the format.
2. Add its row to `m_ops` (`src/cnor/op.c`): name, operand kinds, size, pops,
   pushes, flags (`JUMP`, `TERMINATOR`, `POPS_ARGC`). The compiler's depth
   tracking, the verifier and the decoder all follow from this row.
3. If an operand indexes a table, teach `cnor_verify_operands`
   (`src/cnor/image/verify.c`) its bound.
4. Write the handler in `src/cnor/vm/op/<family>.c`, declare it in
   `src/cnor/vm/internal.h`, and set its slot in `m_handlers`
   (`src/cnor/vm/dispatch.c`).
5. Give it a label in the dispatch loop: an entry in `m_labels` and a
   `CNOR_VM_OP(name)` block that either runs the common case inline or calls
   `CNOR_VM_SLOW()`, which runs the handler. The `_Static_assert` on
   `cnor_op_count` in `dispatch.c` fails until this is done.
6. Add an image test in `tests/test_image_vm.c`.

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

- **Locals, not fields.** The instruction pointer, the stack top and the frame
  base live in locals for the whole run and are written back to the machine
  only around a slow-path call and at exit.
- **No per-instruction decode or bounds checks.** Operands are read straight
  from the code. The verifier proved at load that every instruction decodes
  inside its function, every jump lands on an instruction start, every index
  operand is in range and the operand stack never under- or overflows its
  function's declared depth (section 8.1 of the spec), so the loop does not
  check any of it again. A call still checks the call depth and that the
  callee's frame fits, once per call.
- **Fast paths and the slow path.** Each opcode has a label. The common case
  runs inline: constants, locals, globals, signed-integer arithmetic and
  comparisons, jumps, `call`, `ret`, `native`, string indexing and equality.
  Anything else (unsigned and real numbers, string joins, `len`, `halt`,
  every failure) goes through `CNOR_VM_SLOW()`: the locals are written back,
  the instruction is decoded and its handler from `m_handlers` runs exactly as
  it would without the fast path. A fast path never decides a language rule on
  its own: when its operands are not the kinds it knows, it defers.
- **Fusion.** A few instructions look at the opcode after them and run their
  usual partner without going back through dispatch: a comparison with the
  conditional jump after it (the boolean is never built), arithmetic with the
  `store_local` after it, two `load_local`s, and `push_i32` with the `add`,
  `sub` or `lt` after it. The partner is a separate instruction of the image:
  it is counted against the budget, and taken only when the budget has room.
  The image format is unchanged.
- **Computed goto.** With GCC and Clang the next instruction is reached through
  a table of label addresses, one indirect branch per instruction (15 to 25%
  faster than the switch on this code); `CNOR_VM_COMPUTED_GOTO` (CMake option,
  ON by default) set to OFF, or any other compiler, uses a portable `switch`.
  Both are built and tested.
- **Inline value access.** `src/cnor/vm/fast.h` reads and writes the
  `lh_value_t` fields directly; lh's accessors are out-of-line functions, and a
  call per field was most of the cost of an instruction before.

## Bounding a run

Memory is bounded at load (stack slots, call depth, string pool) and time is
bounded per run: `cnor_vm_config_t.max_steps` caps the instructions one
`cnor_vm_run` executes (default `CNOR_VM_DEFAULT_MAX_STEPS`, one million; 0
means unlimited). The check is one compare per instruction in
`cnor_vm_dispatch`, made before the instruction runs; an exhausted budget
fails with `cnor_error_step_limit` at that instruction's address and unwinds
like any other failure, so the machine can run again (with a fresh budget) or
be reset. `cnor_vm_get_steps` reports what the last run used, which is how a
host sizes the budget for its scripts.

Instructions the dispatch loop executes together (see below) are still
counted one by one, and a pair is only taken together when the budget has room
for both, so a budget of N stops after exactly N instructions with or without
those shortcuts (`tests/test_compiler.c`,
`stops_after_exactly_the_budget_inside_fused_pairs`).

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

## Benchmarks

`bench/` holds a benchmark runner (`CNOR_BUILD_BENCH=ON`, see
[bench/README.md](../bench/README.md)): fixed workloads with known results,
compile, load and run timed separately, median and minimum of several runs,
VM instructions per second from `cnor_vm_get_steps`, and C, Lua and Python
versions of the same work for comparison.
