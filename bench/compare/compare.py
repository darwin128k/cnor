"""Run the cnor workloads in Lua 5.4, Pawn and Python 3, if they are installed,
and print a table next to cnor's own numbers.

    python3 compare.py [--iterations N] [--warmup N] [--rounds N]
                       [--cnor PATH/TO/cnor_bench]
                       [--pawncc PATH/TO/pawncc] [--pawn-harness PATH/TO/pawnbench]

The runs of every language are interleaved and the minimum is reported: a
machine that throttles under a sustained load would otherwise measure the
order of the runs, not the languages. The pause between the rounds lets the
clock boost again.

Lua times are CPU time (os.clock), Python times are time.perf_counter_ns, the
Pawn runner and cnor use a monotonic clock. Every script's result is checked
against cnor's expected value.
"""

import argparse
import os
import shutil
import subprocess
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
WORKLOADS = {
    "fib": 75025,
    "loop": 49999995000000,
    "nested": 2570569,
    "text": 120000,
    "native": 250000000000,
    "match": 1750000,
}
# Pawn's cells are 32 bits, so these two overflow where cnor and Lua keep 64.
PAWN_WRAPS = ("loop", "native")


def find(*names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def version(cmd):
    out = subprocess.run(cmd, capture_output=True, text=True)
    return (out.stdout + out.stderr).strip().splitlines()[0]


def wrap32(value):
    """What a 32-bit signed cell holds of `value`."""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def run_script(interpreter, script, name, iterations, warmup):
    """A script that prints "median_ns min_ns result"."""
    out = subprocess.run([interpreter, os.path.join(HERE, script), name, str(iterations),
                          str(warmup)], capture_output=True, text=True, check=True)
    median, minimum, result = out.stdout.split()
    if int(result) != WORKLOADS[name]:
        raise SystemExit(f"{script} {name}: wrong result {result}")
    return int(minimum)


def run_pawn(harness, program, name, iterations, warmup):
    out = subprocess.run([harness, program, str(iterations), str(warmup)],
                         capture_output=True, text=True, check=True)
    median, minimum, result = out.stdout.split()
    expected = WORKLOADS[name]
    if name in PAWN_WRAPS:
        expected = wrap32(expected)
    if int(result) != expected:
        raise SystemExit(f"pawn {name}: wrong result {result}, expected {expected}")
    return int(minimum)


def compile_pawn(pawncc, folder):
    """Compile every Pawn workload into `folder`; returns the folder."""
    for name in WORKLOADS:
        source = os.path.join(HERE, "pawn", name + ".p")
        shutil.copyfile(source, os.path.join(folder, name + ".p"))
        result = subprocess.run([pawncc, "-O2", "-d0", name + ".p"], cwd=folder,
                                capture_output=True, text=True)
        if result.returncode != 0:
            raise SystemExit(f"pawncc failed on {name}: {result.stdout}{result.stderr}")
    return folder


def run_cnor(path, name, iterations, warmup):
    """cnor's own run time for one workload, in ns."""
    out = subprocess.run([path, "--csv", "--iterations", str(iterations), "--warmup",
                          str(warmup), "--only", name], capture_output=True, text=True,
                         check=True)
    cells = out.stdout.splitlines()[1].split(",")
    return int(cells[5])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--pause", type=float, default=2.0)
    parser.add_argument("--cnor", default=None)
    parser.add_argument("--lua", default=None, help="the Lua interpreter")
    parser.add_argument("--python", default=None, help="the Python interpreter")
    parser.add_argument("--pawncc", default=None, help="the Pawn compiler")
    parser.add_argument("--pawn-harness", default=None, help="the Pawn runner (pawnbench)")
    args = parser.parse_args()

    lua = args.lua or find("lua5.4", "lua54", "lua")
    python = args.python or find("python3", "python")
    pawncc = args.pawncc or find("pawncc", "pawncc32", "pawncc64")
    pawn = args.pawn_harness or find("pawnbench", "pawnbench32", "pawnbench64")
    if lua:
        print("lua:", version([lua, "-v"]))
    if python:
        print("python:", version([python, "--version"]))
    if pawn and pawncc:
        print("pawn:", version([pawncc, "-h"]))

    with tempfile.TemporaryDirectory() as pawn_dir:
        if pawn and pawncc:
            compile_pawn(pawncc, pawn_dir)

        languages = []
        if args.cnor:
            languages.append(("cnor", lambda name: run_cnor(args.cnor, name, args.iterations,
                                                            args.warmup)))
        if lua:
            languages.append(("lua", lambda name: run_script(lua, "workloads.lua", name,
                                                              args.iterations, args.warmup)))
        if pawn and pawncc:
            languages.append(("pawn", lambda name: run_pawn(pawn, os.path.join(pawn_dir,
                                                                              name + ".amx"),
                                                            name, args.iterations,
                                                            args.warmup)))
        if python:
            languages.append(("python", lambda name: run_script(python, "workloads.py", name,
                                                                args.iterations, args.warmup)))

        print(f"{'workload':9}" + "".join(f"{label:>12}" for label, _ in languages)
              + "   (ms, minimum of " + str(args.rounds) + " interleaved rounds)")
        for name in WORKLOADS:
            best = {label: None for label, _ in languages}
            for round_index in range(args.rounds):
                order = list(range(len(languages)))
                offset = round_index % len(order)
                order = order[offset:] + order[:offset]
                for index in order:
                    label, run = languages[index]
                    value = run(name)
                    if best[label] is None or value < best[label]:
                        best[label] = value
                time.sleep(args.pause)
            print(f"{name:9}" + "".join(f"{best[label] / 1e6:>12.2f}"
                                        for label, _ in languages), flush=True)


if __name__ == "__main__":
    main()
