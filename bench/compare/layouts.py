"""Measure one source in several code layouts, and report the spread.

    python3 layouts.py [--pads 0,7,19,31,43] [--binary build/bench/cnor_bench]
                        [--source src/cnor/vm/dispatch.c] [--iterations N] [--rounds N]

Where the blocks of the dispatch loop land in the instruction cache decides
much of its speed: on an Intel core with the jump alignment erratum, code
that moves by a few bytes moves a benchmark by tens of percent. The same
source is therefore built several times, with a padding function of a
different size before the loop, and every build is measured. The median over
the layouts is the number that does not depend on where the code happened to
land; the spread is what the layout can do to a single measurement.

The script restores the source it patched.
"""

import argparse
import os
import shutil
import statistics
import subprocess
import sys
import time

WORKLOADS = ["fib", "loop", "nested", "text", "native", "match"]
ANCHOR = "/* -- the loop -------------------------------------------------------------- */"
PADDING = """__attribute__((used)) static void cnor_vm_layout_pad(void)
{
%s    __asm__ volatile("nop");
}

"""


def patch(source, backup, pad):
    with open(backup, "r", encoding="utf-8") as handle:
        text = handle.read()
    body = "".join(['    __asm__ volatile("nop");\n' for _ in range(pad)])
    if ANCHOR not in text:
        raise SystemExit(f"the anchor is not in {source}")
    with open(source, "w", encoding="utf-8", newline="") as handle:
        handle.write(text.replace(ANCHOR, PADDING % body + ANCHOR, 1))


def build(root, build_dir, target, source):
    out = subprocess.run(["cmake", "--build", build_dir, "-j", "8", "--target", target],
                         cwd=root, capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stdout[-4000:])
        print(out.stderr[-4000:], file=sys.stderr)
        raise SystemExit(f"the build of {build_dir} failed")
    os.utime(source, None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pads", default="0,7,19,31,43")
    parser.add_argument("--binary", default=os.path.join("build", "bench", "cnor_bench"))
    parser.add_argument("--source", default=os.path.join("src", "cnor", "vm", "dispatch.c"))
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--target", default="cnor_bench")
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--keep", action="store_true", help="keep the built binaries")
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    source = os.path.join(root, args.source)
    build_dir = os.path.join(root, args.build_dir)
    binary = os.path.join(root, args.binary)
    pads = [int(part) for part in args.pads.split(",")]

    backup = source + ".layoutbak"
    shutil.copyfile(source, backup)
    out_dir = os.path.join(root, ".layout")
    os.makedirs(out_dir, exist_ok=True)
    builds = []
    try:
        for pad in pads:
            patch(source, backup, pad)
            build(root, build_dir, args.target, source)
            copy = os.path.join(out_dir, f"layout_{pad}.exe")
            shutil.copyfile(binary, copy)
            builds.append((pad, copy))
            print(f"built pad {pad}", flush=True)
    finally:
        shutil.copyfile(backup, source)
        os.remove(backup)

    samples = {name: [] for name in WORKLOADS}
    for round_index in range(args.rounds):
        order = builds[round_index % len(builds):] + builds[:round_index % len(builds)]
        for _, path in order:
            for name in WORKLOADS:
                out = subprocess.run([path, "--csv", "--iterations", str(args.iterations),
                                      "--warmup", str(args.warmup), "--only", name],
                                     capture_output=True, text=True, check=True)
                cells = out.stdout.splitlines()[1].split(",")
                samples[name].append(int(cells[5]) / 1e6)
            time.sleep(1.0)

    print(f"\n{'workload':9}{'median':>10}{'min':>10}{'max':>10}{'spread':>10}")
    for name in WORKLOADS:
        values = samples[name]
        low = min(values)
        high = max(values)
        print(f"{name:9}{statistics.median(values):>10.2f}{low:>10.2f}{high:>10.2f}"
              f"{(high - low) / low * 100.0:>9.0f}%")
    if not args.keep:
        shutil.rmtree(out_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
