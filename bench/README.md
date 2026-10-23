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

### Against Lua and Python

`compare/workloads.lua` and `compare/workloads.py` are the same programs in
Lua 5.4 and Python 3 (locals throughout, a C function in place of the native:
`math.abs`, `abs`). `compare/compare.py` runs whichever interpreters are
installed, checks their results, and prints them next to cnor's:

```sh
python3 bench/compare/compare.py --cnor build-bench/bench/cnor_bench --iterations 7
```

Lua times are `os.clock` (CPU time), Python times `time.perf_counter_ns`.

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
