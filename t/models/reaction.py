#!/usr/bin/env python3
"""examples/reaction.expected: Gray-Scott on a w by h torus, as
examples/reaction.slight has it, each operation in its order (Python's
floats being IEEE doubles, each operation rounded), then v summed row by
row: python3 t/models/reaction.py 200 48 24. The bands the example cuts
the grid into, on whatever islands, don't change a cell's sum, which is
the point of the example."""
import sys
DU, DV, F, K = 0.16, 0.08, 0.037, 0.06

def lcg(x): return (x * 1103515245 + 12345) % 2147483648

def seeds(w, h, n, x):
    out = []
    for _ in range(n):
        x2 = lcg(x); x3 = lcg(x2)
        out.append(((x2 // 7) % w, (x3 // 7) % h))
        x = x3
    return out

def run(steps, w, h):
    ss = seeds(w, h, max(3, (w * h) // 400), 7)
    inside = lambda x, y: any(abs(x - sx) < 3 and abs(y - sy) < 3 for sx, sy in ss)
    u = [[0.5 if inside(x, y) else 1.0 for x in range(w)] for y in range(h)]
    v = [[0.25 if inside(x, y) else 0.0 for x in range(w)] for y in range(h)]
    def lap(a, i, j):
        return ((a[(i - 1) % h][j] + a[(i + 1) % h][j]) + (a[i][(j - 1) % w] + a[i][(j + 1) % w])) - 4.0 * a[i][j]
    for _ in range(steps):
        u2 = [[None] * w for _ in range(h)]; v2 = [[None] * w for _ in range(h)]
        for i in range(h):
            for j in range(w):
                x, y = u[i][j], v[i][j]
                u2[i][j] = x + (DU * lap(u, i, j) + (F * (1.0 - x) - x * (y * y)))
                v2[i][j] = y + (DV * lap(v, i, j) + (x * (y * y) - (F + K) * y))
        u, v = u2, v2
    s = 0.0
    for i in range(h):
        for j in range(w):
            s += v[i][j]
    return s

print(repr(run(int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]))))
