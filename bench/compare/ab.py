"""Compare two or more cnor builds in one interleaved run.

    python3 ab.py [--iterations N] [--warmup N] [--rounds N] label=path ...

    python3 ab.py --only loop,match base=build/bench/cnor_bench new=build2/bench/cnor_bench

Why this exists: on an Intel core with the jump-alignment erratum the code
layout alone moves a benchmark by 10 to 35% (see docs/architecture.md, "Where
the loop starts"), and two builds that differ in one line of code can differ
by that much without the line mattering. The runs of every build are therefore
interleaved and the minimum is reported, so the comparison is about the code
and not about the order the builds ran in. The machine throttles under a
sustained load, so there is a pause between the rounds.
"""

import argparse
import subprocess
import time

WORKLOADS = ["fib", "loop", "nested", "text", "native", "match"]


def run(bench, name, iterations, warmup):
    """One workload of one build: the median and the minimum run time in ms."""
    out = subprocess.run([bench, "--csv", "--iterations", str(iterations), "--warmup",
                          str(warmup), "--only", name], capture_output=True, text=True,
                         check=True)
    cells = out.stdout.splitlines()[1].split(",")
    return int(cells[4]) / 1e6, int(cells[5]) / 1e6


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--pause", type=float, default=2.0)
    parser.add_argument("--only", default=None, help="comma separated workload names")
    parser.add_argument("pairs", nargs="+", help="label=path/to/cnor_bench")
    args = parser.parse_args()

    labels = []
    paths = []
    for pair in args.pairs:
        label, path = pair.split("=", 1)
        labels.append(label)
        paths.append(path)
    workloads = args.only.split(",") if args.only else WORKLOADS

    print(f"{'workload':9}" + "".join(f"{label:>12}" for label in labels) + "   (ms, minimum)")
    for name in workloads:
        best = {label: None for label in labels}
        for round_index in range(args.rounds):
            order = list(range(len(labels)))
            offset = round_index % len(order)
            order = order[offset:] + order[:offset]
            for index in order:
                _, minimum = run(paths[index], name, args.iterations, args.warmup)
                if best[labels[index]] is None or minimum < best[labels[index]]:
                    best[labels[index]] = minimum
            time.sleep(args.pause)
        print(f"{name:9}" + "".join(f"{best[label]:>12.2f}" for label in labels), flush=True)


if __name__ == "__main__":
    main()
