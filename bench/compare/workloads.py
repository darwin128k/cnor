"""The cnor benchmark workloads in Python 3, for comparison. Run by compare.py:
    python3 workloads.py NAME ITERATIONS WARMUP
prints "median_ns min_ns result". Locals throughout, as in the cnor sources."""

import statistics
import sys
import time


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


def loop(n):
    s = 0
    i = 0
    while i < n:
        s = s + i
        i = i + 1
    return s


def nested(n):
    s = 0
    for i in range(n):
        for j in range(n):
            s = s + (i * j) % 7
    return s


def text(t, rounds):
    hits = 0
    joined = 0
    n = len(t)
    for r in range(rounds):
        for i in range(n):
            if t[i] == "o":
                hits = hits + 1
        pair = t[r % n] + t[(r + 1) % n]
        joined = joined + len(pair)
    return hits + joined


def native(n):
    s = 0
    for i in range(n):
        s = s + abs(i - 500000)  # a C builtin, like the cnor native
    return s


def match(n):
    s = 0
    for i in range(n):
        match i % 8:
            case 0:
                s = s + 1
            case 1:
                s = s + 3
            case 2:
                s = s - 2
            case 3:
                s = s + 5
            case 4:
                s = s * 1
            case 5:
                s = s + 7
            case _:
                s = s + 0
    return s


WORK = {
    "fib": lambda: fib(25),
    "loop": lambda: loop(10000000),
    "nested": lambda: nested(1000),
    "text": lambda: text("the quick brown fox jumps over the lazy dog", 20000),
    "native": lambda: native(1000000),
    "match": lambda: match(1000000),
}


def main():
    name, iterations, warmup = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    fn = WORK[name]
    samples = []
    result = None
    for round_ in range(warmup + iterations):
        start = time.perf_counter_ns()
        result = fn()
        elapsed = time.perf_counter_ns() - start
        if round_ >= warmup:
            samples.append(elapsed)
    print(int(statistics.median(samples)), min(samples), result)


if __name__ == "__main__":
    main()
