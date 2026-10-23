"""Run the cnor workloads in Lua 5.4 and Python 3, if installed, and print a
table next to cnor's own numbers.

    python3 compare.py [--iterations N] [--warmup N] [--cnor PATH/TO/cnor_bench]

Lua times are CPU time (os.clock), Python times are time.perf_counter_ns. Each
script checks nothing itself; this runner compares its result with cnor's
expected value.
"""

import argparse
import os
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
WORKLOADS = {
    "fib": 75025,
    "loop": 49999995000000,
    "nested": 2570569,
    "text": 120000,
    "native": 250000000000,
    "match": 1750000,
}


def find(*names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def version(cmd):
    out = subprocess.run(cmd, capture_output=True, text=True)
    return (out.stdout + out.stderr).strip().splitlines()[0]


def run_script(interpreter, script, name, iterations, warmup):
    out = subprocess.run([interpreter, os.path.join(HERE, script), name, str(iterations),
                          str(warmup)], capture_output=True, text=True, check=True)
    median, minimum, result = out.stdout.split()
    if int(result) != WORKLOADS[name]:
        raise SystemExit(f"{script} {name}: wrong result {result}")
    return int(median), int(minimum)


def run_cnor(path, iterations, warmup):
    out = subprocess.run([path, "--csv", "--iterations", str(iterations), "--warmup",
                          str(warmup)], capture_output=True, text=True, check=True)
    rows = {}
    for line in out.stdout.splitlines()[1:]:
        cells = line.split(",")
        rows[cells[0]] = int(cells[4])
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--cnor", default=None)
    args = parser.parse_args()

    lua = find("lua5.4", "lua54", "lua")
    python = find("python3", "python")
    cnor = run_cnor(args.cnor, args.iterations, args.warmup) if args.cnor else {}
    if lua:
        print("lua:", version([lua, "-v"]))
    if python:
        print("python:", version([python, "--version"]))
    print(f"{'workload':8} {'cnor ms':>10} {'C ms':>10} {'lua ms':>10} {'python ms':>10}")
    for name in WORKLOADS:
        cells = [cnor.get(name), cnor.get("c_" + name)]
        cells.append(run_script(lua, "workloads.lua", name, args.iterations, args.warmup)[0]
                     if lua else None)
        cells.append(run_script(python, "workloads.py", name, args.iterations, args.warmup)[0]
                     if python else None)
        text = ["-" if c is None else f"{c / 1e6:.3f}" for c in cells]
        print(f"{name:8} {text[0]:>10} {text[1]:>10} {text[2]:>10} {text[3]:>10}", flush=True)


if __name__ == "__main__":
    main()
