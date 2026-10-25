# Pawn in the benchmark

The Pawn programs here are the same work as cnor's (`bench/workloads.c`) and
Lua's (`bench/compare/workloads.lua`), so the three can be compared on one
machine. Pawn is the language of the Half-Life 1.6 plugins (AMX), so it is the
bar cnor has to clear for that target.

## What is here

| File | What |
|---|---|
| `pawnbench.c` | The runner: loads a compiled `.amx`, runs it a few times and prints `median_ns min_ns result`, like the Lua harness |
| `fib.p`, `loop.p`, `nested.p`, `text.p`, `native.p`, `match.p` | The workloads |

## Building

Pawn 4.1 is at <https://github.com/compuphase/pawn>; the compiler is
`compiler/*.c` and the machine is `amx/{amx,amxcore,amxaux,amxpool}.c`.

```sh
gcc -O2 -std=gnu17 -DHAVE_ALLOCA_H=0 -DAMX_NODYNALOAD -DFLOATPOINT -DFIXEDPOINT \
    -Iamx -o pawnbench pawnbench.c amx/amx.c amx/amxcore.c amx/amxaux.c amx/amxpool.c
```

`-DHAVE_ALLOCA_H=0` keeps `amx.h` from including `alloca.h`, which MinGW does
not have; `-std=gnu17` keeps `constexpr` a plain identifier, which Pawn 4.1
uses as a function name.

The compiler is built the same way, from `compiler/*.c` and `amx/keeloq.c`:

```sh
gcc -O2 -std=gnu17 -DHAVE_ALLOCA_H=0 -Icompiler -Iinclude -o pawncc \
    compiler/sc1.c compiler/sc2.c ... compiler/memfile.c amx/keeloq.c
```

## Running

```sh
./pawncc -O2 -d0 fib.p        # writes fib.amx
./pawnbench fib.amx 5 1       # 5 rounds after 1 warmup
```

`compare.py` in the parent directory does this for every workload:

```sh
python3 bench/compare/compare.py --pawncc ./pawncc --pawn-harness ./pawnbench
```

## Two things about Pawn 4 that the programs have to respect

- **Strings in double quotes are packed**: four characters per cell. The text
  is written in two single quotes (`''like this''`) to keep one character per
  cell, like cnor's strings.
- **A cell is 32 bits**, so `loop` and `native` overflow where cnor and Lua
  keep 64 bits. The runner checks the wrapped value.
