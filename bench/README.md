# cnor benchmarks

`cnor_bench` runs a fixed set of cnor programs, checks every result, and times
three phases separately:

- **compile**: lex + parse + compile (`cnor_compiler_compile_source`);
- **load**: copy, verify and bind the image (`cnor_vm_load`);
- **run**: `cnor_vm_reset` then `cnor_vm_run`, with the instruction budget
  off.

Each phase runs `--warmup` rounds that are thrown away, then `--iterations`
measured rounds; the report gives the median and the minimum. Time comes from a
monotonic clock (`clock_gettime(CLOCK_MONOTONIC)`, `QueryPerformanceCounter`
on Windows). `steps` is what `cnor_vm_get_steps` reports for one run, so
`Msteps/s` is VM instructions per second; `ns/op` divides the run time by the
workload's unit of work (one call, one loop iteration, one character).

| Workload | What it does | Unit |
|---|---|---|
| `fib` | recursive `fib(25)` | call (242785) |
| `loop` | `while` loop summing 0 .. 10M | iteration |
| `nested` | 1000 x 1000 nested `for`, `s + (i * j) % 7` | inner iteration |
| `text` | index and compare a 43-character string 20000 times, join two characters and take `len` each round | character |
| `native` | 1M calls of a host native, `absval(i - 500000)` | call |
| `match` | 1M iterations of a 7-arm `match` on `i % 8` | iteration |
| `compile` | a generated program of 1000 functions (if, while, match each) and 1000 calls | source line (compile time) |

The C references (`reference.c`) do the `fib` and `loop` work in C, compiled
with the same flags, for a ratio against native code. GCC turns naive `fib`
into much less work than the source says, so treat that ratio as a lower
bound of what "native" means.

## Running

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DCNOR_BUILD_BENCH=ON
cmake --build build-bench
./build-bench/bench/cnor_bench                  # every workload, 10 + 2 rounds
./build-bench/bench/cnor_bench --only fib --iterations 30
./build-bench/bench/cnor_bench --csv            # machine-readable
```

`CNOR_VM_COMPUTED_GOTO=OFF` builds the portable `switch` dispatch instead, to
compare the two. `CNOR_IPO=ON` (link-time optimization) and `CNOR_PGO`
(profile-guided optimization) are optional and OFF by default; see "Optional
optimizations" below for what they measured and how to train a profile.

### Against Lua, Pawn and Python

`compare/workloads.lua` and `compare/workloads.py` are the same programs in
Lua 5.4 and Python 3 (locals throughout, a C function in place of the native:
`math.abs`, `abs`). `compare/pawn/` holds the same programs in Pawn 4.1, the
language of the Half-Life 1.6 plugins, with a small runner that times a
compiled `.amx` (`compare/pawn/README.md` has how to build it).
`compare/compare.py` runs whichever interpreters are installed, checks their
results, and prints them next to cnor's:

```sh
python3 bench/compare/compare.py --cnor build-bench/bench/cnor_bench --iterations 7
```

Lua times are `os.clock` (CPU time), Python times `time.perf_counter_ns`.

### Comparing two builds

A change that is not about the hot path can still move a benchmark by 10 to
35%: on an Intel core with the jump-alignment erratum the layout of the
dispatch loop decides (see `docs/architecture.md`, "Where the loop starts"). So
two builds are never compared by running them one after the other.
`compare/ab.py` interleaves the runs of every build and reports the minimum:

```sh
python3 bench/compare/ab.py base=build/bench/cnor_bench new=build2/bench/cnor_bench
```

This is how the changes in "Windows: the Half-Life 1.6 target" below were
measured.

## Sample results

Debian 13 box, Intel Xeon (8 vCPU, shared), GCC 14.2, `-O2`, computed goto,
pinned to one CPU, 2026-10-23, image format v3 (operand forms, `for_loop`,
inline string fast paths with word-at-a-time compare and copy, conditions
compiled to jumps, `switch` tables for dense integer `match`, in-place global
updates, values moved field by field, the call stack top kept in the dispatch
loop, a step budget counted down and tested on its own, cheaper signed
immediates, a separate dispatch for `for_loop`'s jump back). Run time, median
of 15 (C, Lua and Python: median of 7, from the previous session; their
programs and interpreters did not change).

| Workload | cnor ms | Msteps/s | ns/op | C ms | Lua 5.4.7 ms | Python 3.13.5 ms |
|---|---|---|---|---|---|---|
| fib | 2.0 | 736 | 8.2 / call | 0.10 | 3.5 | 6.4 |
| loop | 45 | 1343 | 4.5 / iteration | 2.7 | 83 | 415 |
| nested | 5.7 | 1045 | 5.7 / iteration | - | 8.1 | 33 |
| text | 6.8 | 974 | 7.9 / char | - | 33 | 17 |
| native | 8.1 | 738 | 8.1 / call | - | 15.4 | 39 |
| match | 13.8 | 551 | 13.8 / iteration | - | 18.0 | 82 |
| compile | 6.1 (compile) | - | 435 / line | - | - | - |

The same machine before these changes (format v1): fib 3.2, loop 106, nested
15.7, text 29.2, native 20.4, match 32.0 ms. Fewer, larger instructions mean
fewer steps for the same work (loop: 120M steps then, 60M now; match: 14.3M
with format v2, 7.6M now), so Msteps/s is lower on some workloads although the
run time is shorter.

These numbers move by a few percent between runs on a shared machine; compare
builds on the same machine, several runs each.

Code layout alone can move one workload by 10% or more: with one extra fusion
elsewhere in the dispatch loop `streq` ran 13% slower on this Xeon with the
same instruction count (callgrind), and the gap closed when both builds were
assembled with `-Wa,-mbranches-within-32B-boundaries` (Intel's jump alignment
erratum). Check the instruction count before trusting such a difference.
The same holds on the Xeon these numbers come from (a newer core without that
erratum): building the same source with `-falign-labels=16` alone moved
`native` by 9% and `nested` by 8%, in opposite directions. Not every such
difference is luck, though: after the faster immediates of 2026-10-23
`native` ran 12 to 17% slower with computed goto (the 9% first reported was
the best of a few rounds; the median over 15 interleaved rounds is 12 to
17%) on fewer instructions, because GCC moved `for_loop`'s
jump back out of line, behind a taken branch, and merged it with the exit
into one indirect jump. Marking the jump back likely and giving it its own
dispatch fixed it (see docs/architecture.md, win #6).

## Windows: the Half-Life 1.6 target

Half-Life 1.6 runs on 32-bit x86, so for a plugin the 32-bit numbers are the
ones that count. Measured on a Windows host, Intel i7-3770K (3.5 GHz, an Ivy
Bridge, so it has the jump alignment erratum), MinGW GCC 16, `-O2`, computed
goto, 2026-10-25. Run time, minimum of several interleaved rounds
(`compare/ab.py`).

### Against Pawn and Lua, on one machine

The same programs in cnor, Pawn 4.1 and Lua 5.4, all built here, all run
interleaved on the same Windows host (i7-3770K, MinGW GCC 16, 2026-10-25);
run time, minimum of six interleaved rounds. Pawn's cells are 32 bits, so its
`loop` and `native` results overflow where cnor and Lua keep 64.

| Workload | cnor 32-bit | cnor 64-bit | Pawn 32-bit | Pawn 64-bit | Lua 32-bit | Lua 64-bit |
|---|---|---|---|---|---|---|
| fib | 6.10 | 4.58 | 6.65 | 7.92 | 10.00 | 6.00 |
| loop | 103.02 | 79.51 | 120.13 | 110.14 | 232.00 | 126.00 |
| nested | 21.40 | 21.63 | 31.54 | 25.36 | 26.00 | 22.00 |
| text | 18.89 | 12.45 | 17.68 | 29.95 | 91.00 | 60.00 |
| native | 19.36 | 11.95 | 33.00 | 24.93 | 37.00 | 27.00 |
| match | 24.03 | 19.99 | 33.68 | 30.10 | 44.00 | 41.00 |

In milliseconds; lower is better. cnor 32-bit is faster than Pawn 32-bit on
five of the six workloads (1.1 to 1.7 times) and faster than Lua 32-bit on all
six (1.3 to 4.8 times). The one that Pawn wins is `text`, by 7%: Pawn keeps one
character per cell and reads a character with one load, while cnor copies the
24-byte value of the text onto the stack and then reads the character out of
it (seven instructions per character, against Pawn's three or four). The way
out is an operand form for the index, the same trick the arithmetic operators
already use: `text[i]` would be one instruction instead of three.

### Build with MinGW GCC, not with MSVC

The dispatch loop uses computed goto where the compiler has it (GCC and
Clang, on every platform) and a `switch` where it has not (MSVC). The two are
not close on a 32-bit host: same source, same flags, interleaved runs.

| Workload | computed goto ms | switch ms |
|---|---|---|
| fib | 5.95 | 5.76 |
| loop | 124.65 | 295.98 |
| nested | 31.20 | 60.80 |
| text | 27.27 | 26.58 |
| native | 22.12 | 29.36 |
| match | 24.84 | 31.34 |

A plugin built with MSVC would run its tight loops more than twice as slowly
as the same source built with MinGW GCC, and the Linux build uses the fast
one. So "the same speed on Windows and Linux" holds for the same compiler
family (GCC or Clang), not across compiler families: build the plugin with
`i686-w64-mingw32-gcc` (or another GCC-compatible cross compiler).

### What the changes of 2026-10-25 bought

Measured against the same source without them, interleaved (`compare/ab.py`).
The numbers carry the layout noise of a few percent that every A/B on this
host carries.

| Change | 32-bit | 64-bit |
|---|---|---|
| A remainder by a power of two is a mask | match -30% | match -33% |
| The stack and the string pool survive a load | load 300 to 580 us -> 2.3 to 2.5 us | the same |
| A native reads the fields of a value directly | native -10% | native -17% |

The remainder is the one that mattered: `match` over `i % 8` was the only
workload where Pawn was ahead of cnor.

The last one is not a change in the VM but in the way a native is written:
`lh`'s accessors (`lh_value_get_kind`, `lh_value_get_data`) are ordinary
functions in another library, and a host that reads the arguments of a native
through them pays a call per field. The fields of a value are public, so a
native reads `args[0].kind` and `args[0].data` directly, as
`bench/workloads.c` does.

Static types, global initializers and `import` (image format v4, 2026-10-23)
change no run-time code: every workload compiles to the same code section
byte for byte, and the run takes the same instruction count (callgrind, e.g.
`loop` 930.27M before and 930.28M after, `nested` run 121.13M both). Timed
over 12 heap layouts (`GLIBC_TUNABLES=glibc.malloc.top_pad`, 9 rounds each,
one pinned CPU), both builds assembled with
`-Wa,-mbranches-within-32B-boundaries`, the run medians move by -5.6%
(`nested`) to +2.1% (`match`); without that flag `loop` read +5% on the same
instruction count, a layout effect as described above. Compiling does more
work (types, module table, initialization order): the 1000-function `compile`
workload compiles 2.3% slower, and the small workloads, which compile in about
4 us, take 0.1 to 0.5 us more (+3 to +12%), mostly a few more allocations per
compile. Loading a v4 image is level (-7% to +5%, within the spread).

The compiler was then made faster than before the types (2026-10-23, same
image bytes for every workload and test program). Callgrind showed where the
types and imports had added work: every local operand of a binary operator was
looked up twice (once for its slot, once for its type), a host native was
hashed twice per lookup (its own file, then the host), the statement table got
one more entry (and was scanned twice for every statement), and a small
compile made six more allocations (the module table and order, the file table,
the call graph). The fixes: one lookup per operand, one hash per name, a
length test before comparing names, the statement rule looked up once and only
for a keyword, the main file left out of the module order, calls that cannot
fail the initialization check not recorded, no file table for an unnamed
single file. The largest gain had been there before the types: the byte writer
(code, constants, lines, the image) grew an `lh_array_t` from one byte and
went through it for every byte; it now writes into a block of its own,
doubling from 64 bytes, and the image writer allocates its size once.
`cnor_bench` compile medians (median of 5 processes x 15 iterations, Release,
one pinned CPU), before the types / with them / now:

| Workload | before types us | v4 us | now us | now vs before types |
|---|---|---|---|---|
| fib | 3.6 | 3.6 | 2.7 | -24% |
| loop | 4.0 | 4.1 | 3.1 | -21% |
| nested | 4.7 | 4.9 | 3.5 | -25% |
| text | 8.1 | 8.5 | 7.0 | -14% |
| native | 3.7 | 4.1 | 3.1 | -16% |
| match | 7.7 | 7.9 | 6.0 | -22% |
| compile | 5556 | 5849 | 5147 | -7% |

Instructions per compile (callgrind): `fib` 38.0k before the types, 41.9k
with them, 31.5k now; `compile` 70.4M, 74.2M, 64.9M. The dispatch object is
unchanged, but the smaller compiler moved it within a cache line: up to 9%
slower runs with computed goto, and with the `switch` the block every
instruction goes through straddled two lines (`-O3`: `loop` +6%, `nested`
+8%, `text` +9%; plain `-O2`: up to +36%). `dispatch.c` is now compiled with
its function, loop heads and jump targets on 64-byte lines (see
docs/architecture.md, "Where the loop starts"). Run medians of `cnor_bench`
(`-O3`, 5 processes x 15 iterations) against the build before this work,
types/import, then: computed goto `fib` -2%, `loop` -31%, `nested` -1%,
`text` +2%, `native` -10%, `match` -12%; `switch` -13%, -5%, -6%, -3%, -7%,
-7%. At plain `-O2` the `switch` build still runs `fib` 5% and `text` 4%
slower than with the types/import layout, which was a lucky one there.

## Optional optimizations

Measured 2026-10-23 on the same machine against a plain `-O2` build of the
same source (the win #6 code before its regressions were fixed), as the best of 5 rounds of every workload of `bench/` and of
`calls`, `globals`, `logic`, `strbuild` and `streq` (analysis programs outside
the repository); negative is faster.

| Build | Computed goto | `switch` |
|---|---|---|
| `-O3` (measured on the win #5 code) | 0 to +4%, `strbuild` +8% | not measured |
| `CNOR_IPO=ON` | -11% (`nested`) to +11% (`streq`), most within 5% | -13% (`loop`) to +6% (`streq`), most within 5% |
| `CNOR_PGO`, trained on all workloads | -8% `loop`, -6% `logic`; +45% `text`, +18% `calls`, +17% `strbuild`, +10% `globals` and `streq` | -21% `globals`, -14% `calls`, -13% `loop`, -12% `strbuild`, -11% `native`, -9% `fib`, -7% `logic`, -6% `nested`; `text` +2%, `match` and `streq` level |

None is a default: `-O3` and link-time optimization do not win, and the
profile helps the `switch` build across the board but makes the
computed-goto build slower on string and call-heavy work. For a `switch`
target (a compiler without computed goto), train it on the host's own
scripts:

```sh
cmake -S . -B build-pgo -DCMAKE_BUILD_TYPE=Release -DCNOR_BUILD_BENCH=ON -DCNOR_PGO=GENERATE
cmake --build build-pgo
./build-pgo/bench/cnor_bench --iterations 1    # or the host's own workload
cmake -S . -B build-pgo -DCNOR_PGO=USE
cmake --build build-pgo
```

The profile goes to `CNOR_PGO_DIR` (default `build-pgo/pgo`). GCC names it
after the object files, so both steps must use the same build directory;
with Clang, merge the raw profiles into `CNOR_PGO_DIR/default.profdata` with
`llvm-profdata merge` before the second step. Linking an instrumented cnor
needs no extra flags: the library carries the profiling runtime.
