# The expected output of examples/boids.slight, for the arguments on its
# "; args:" line, worked out from the example's rules: the same birds from
# the same generator, and the same arithmetic in the same order, so that
# every float comes out to the last bit (Python's floats are IEEE doubles,
# and +, -, *, / and sqrt round the same everywhere; D76). Which order the
# birds' messages come in doesn't matter, since each bird sums over the
# others by id. The screen is 24 by 80, as it is when stdout isn't a
# terminal. The root's value comes last.
#
#     python3 t/models/boids.py 30 4 8 > examples/boids.expected
import sys
from math import floor, sqrt

SIGHT, ROOM = 10.0, 2.0
COHESION, ALIGNMENT, SEPARATION = 0.01, 0.125, 0.1
MAX, MIN = 1.0, 0.5

args = [int(a) for a in sys.argv[1:]]
n = args[0] if len(args) > 0 else 30
frames = args[1] if len(args) > 1 else 0
seed = args[2] if len(args) > 2 else 2026
w, h = 80 - 2, 2 * (24 - 3)

def add(a, b): return (a[0] + b[0], a[1] + b[1])
def sub(a, b): return (a[0] - b[0], a[1] - b[1])
def mul(a, k): return (a[0] * k, a[1] * k)
def div(a, k): return (a[0] / k, a[1] / k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1]
def vsum(vs):
    acc = (0, 0)
    for v in vs: acc = add(acc, v)
    return acc

def across(a, b, size):
    d = b - a
    if d > size / 2: return d - size
    if d < 0 - size / 2: return d + size
    return d

def around(x, size):
    if x < 0: return x + size
    if x >= size: return x - size
    return x

def limit(v):
    speed = sqrt(dot(v, v))
    if speed > MAX: return mul(v, MAX / speed)
    if speed < MIN and speed > 0: return mul(v, MIN / speed)
    return v

def steer(me, flock):
    (x, y), v = flock[me]
    others = []
    for i, (p, pv) in enumerate(flock):
        if i == me: continue
        d = (across(x, p[0], w), across(y, p[1], h))
        if dot(d, d) < SIGHT * SIGHT: others.append((d, pv, dot(d, d)))
    if others:
        k = len(others)
        cohesion = mul(div(vsum([o[0] for o in others]), k), COHESION)
        alignment = mul(sub(div(vsum([o[1] for o in others]), k), v), ALIGNMENT)
        separation = mul(vsum([o[0] for o in others if o[2] < ROOM * ROOM]), 0 - SEPARATION)
        v = add(v, add(cohesion, add(alignment, separation)))
    v = limit(v)
    return ((around(x + v[0], w), around(y + v[1], h)), v)

def random(s): return s * 16807 % 2147483647
def fraction(s): return s / 2147483647

flock = []
for _ in range(n):
    a = random(seed); b = random(a); c = random(b); d = random(c)
    seed = d
    flock.append(((fraction(a) * w, fraction(b) * h),
                  ((fraction(c) * 2 - 1) * MAX, (fraction(d) * 2 - 1) * MAX)))

K = 0.41421356
def arrow(v):
    vx, vy = v
    if abs(vy) <= abs(vx) * K: return '←' if vx < 0 else '→'
    if abs(vx) <= abs(vy) * K: return '↑' if vy < 0 else '↓'
    if vx < 0: return '↖' if vy < 0 else '↙'
    return '↗' if vy < 0 else '↘'

out = []
for tick in range(frames):
    grid = [[' '] * w for _ in range(h // 2)]
    for (x, y), v in flock:
        grid[floor(y / 2) % (h // 2)][floor(x) % w] = arrow(v)
    edge = '+' + '-' * w + '+'
    out += [edge] + ['|' + ''.join(row) + '|' for row in grid] + [edge]
    out.append(f'tick {tick}, {n} birds')
    flock = [steer(i, flock) for i in range(n)]
out.append(f'"{frames} frames"')
print('\n'.join(out))
