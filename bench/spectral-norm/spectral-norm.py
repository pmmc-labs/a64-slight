# spectral-norm, the Benchmarks Game's, single-threaded. The sums are
# loops: sum() of floats rounds differently since Python 3.12.
import math
import sys


def a(i, j):
    return 1.0 / ((i + j) * (i + j + 1) // 2 + i + 1)


def times_a(u):
    v = []
    for i in range(len(u)):
        s = 0.0
        for j, x in enumerate(u):
            s += a(i, j) * x
        v.append(s)
    return v


def times_at(u):
    v = []
    for i in range(len(u)):
        s = 0.0
        for j, x in enumerate(u):
            s += a(j, i) * x
        v.append(s)
    return v


n = int(sys.argv[1])
u = [1.0] * n
for _ in range(10):
    v = times_at(times_a(u))
    u = times_at(times_a(v))
vbv = vv = 0.0
for x, y in zip(u, v):
    vbv += x * y
    vv += y * y
print(math.floor(math.sqrt(vbv / vv) * 1e9 + 0.5))
