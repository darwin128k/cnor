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
compare the two.

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
2026-10-23. Run time, median of 15 (C, Lua and Python: median of 7).

| Workload | cnor ms | Msteps/s | ns/op | C ms | Lua 5.4.7 ms | Python 3.13.5 ms |
|---|---|---|---|---|---|---|
| fib | 3.2 | 760 | 13 / call | 0.09 | 3.3 | 6.1 |
| loop | 106 | 1130 | 10.6 / iteration | 2.5 | 77 | 388 |
| nested | 15.7 | 1020 | 15.7 / iteration | - | 10.7 | 31 |
| text | 29.2 | 450 | 34 / char | - | 32 | 17 |
| native | 20.4 | 735 | 20 / call | - | 22 | 36 |
| match | 32.0 | 1050 | 32 / iteration | - | 19 | 77 |
| compile | 6.1 (compile) | - | 440 / line | - | - | - |

These numbers move by a few percent between runs on a shared machine; compare
builds on the same machine, several runs each.
