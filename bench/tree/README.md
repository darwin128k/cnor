# The tree-walker benchmark

The question this answers: **how much slower is a tree-walker than the bytecode
machine?** — with a number, not an opinion.

The bytecode machine compiles a program to instructions and runs them. A
tree-walker reads a program into a tree and walks it, executing each node as it
goes. Both can run the same language; the question is what the second one
costs.

## What is here

- `tree_run.c` — the tree-walker: it reads a program with cnor's own parser,
  resolves the names once, and walks the tree.
- `tree_bench.c` — the bench: the same programs, run by the machine and by the
  tree-walker, in one process.

The programs come from the benchmark's own workloads (`../workloads.c`), so the
two machines run the very same text.

## What is fair about it

- **The same values.** The tree-walker uses lh's `lh_value_t` and the
  machine's own helpers, so both work on the same tagged 16-byte values and
  apply the same rules.
- **The names are resolved once, before the run**, in both. The bytecode
  machine has that from its compiler; the tree-walker has a resolver of its
  own. So the comparison is not about looking a name up at run time — it is the
  one advantage of the machine that the tree-walker does not have to begin
  with.
- **The rounds are interleaved**, so a busy machine hits both alike, and the
  report gives the median and the minimum: the minimum is the least disturbed
  round, and the ratio of the two minima is the fairest number a noisy machine
  gives.
- **Both results are checked** against the known value, so a fast wrong answer
  fails the bench instead of reporting itself.

## What is not fair (and cannot be)

The tree-walker visits more nodes than the machine executes instructions: a
node is finer than an instruction (the machine's `add_ll` is one instruction,
the tree's `s = s + i` is five nodes). The report gives both counts, so the
reader can see how much of the difference is that and how much is the dispatch.

The tree-walker also recurses in C, so the C stack is the limit on how deep a
call chain may be; the machine keeps its frames in a stack of its own.

## Running

```sh
cmake -S . -B build-bench -DCNOR_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench
./build-bench/bench/cnor_tree_bench
```

`--iterations N` and `--warmup N` control the rounds. Build it optimised: the
measurement is of the release build.

## The result

Measured 2026-10-26 on a Windows host (MinGW GCC 16, the same flags as the
benchmark). The times are the minimum of nine rounds, the ratio the minimum
over the rounds (the round where the machine was quietest for both):

| Workload | Machine | Tree-walker | Slower |
|---|---:|---:|---:|
| `fib` (recursive) | 19 ns a call | 46 ns | 2–4x |
| `loop` (a hot while loop) | 8.2 ns an iteration | 86 ns | 7–10x |
| `nested` (1000 x 1000 for) | 15 ns an iteration | 47 ns | 3–5x |

On 32-bit the same programs give 2–3x, 7–9x and 3x.

So the tree-walker is **two to ten times slower**, and the hotter the loop,
the worse it gets: the dispatch (a call and a switch per node) is what the
machine's computed goto does not pay. The tree also visits one and a half to
one and a half and three quarters times as many nodes as the machine executes
instructions — a node is finer than an instruction — so part of the gap is
the shape of the work and part is the dispatch itself.
