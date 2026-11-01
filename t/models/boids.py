# The expected output of examples/boids.slight, for the arguments on its
# "; args:" line, worked out from the example's rules: the same birds from
# the same generator, and the same arithmetic in the same order, so that
# every float comes out to the last bit (Python's floats are IEEE doubles,
# and +, -, * and / round the same everywhere; hypot, sin, cos and atan2
# are slight's correctly rounded ones, D253, so here they're mpmath's,
# rounded once to the nearest double). How the flock is cut into shares,
# and which island each is on, doesn't matter, since each bird sums over
# the others by id. The screen is 24 by 80, as it is when stdout isn't a
# terminal. On the virtual clock a tick takes just the 50 ms it's held
# to, which it can say from its second frame on, once it has two
# readings of the clock. The root's value comes last. Needs mpmath.
#
#     python3 t/models/boids.py 30 4 8 > examples/boids.expected
import os
import sys
from math import floor, pi
import mpmath
mpmath.mp.prec = 256

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rand import Rand

SIGHT, ROOM = 10.0, 2.0
COHESION, ALIGNMENT, SEPARATION = 0.01, 0.125, 0.1
MAX, MIN = 1.0, 0.5

args = [int(a) for a in sys.argv[1:]]
n = args[0] if len(args) > 0 else 30
frames = args[1] if len(args) > 1 else 0
seed = args[2] if len(args) > 2 else 2026
w, h = 80 - 2, 2 * (24 - 3)

def cr(f, *xs): return float(f(*[mpmath.mpf(x) for x in xs]))
def hypot(x, y): return cr(lambda a, b: mpmath.sqrt(a * a + b * b), x, y)

def add(a, b): return (a[0] + b[0], a[1] + b[1])
def sub(a, b): return (a[0] - b[0], a[1] - b[1])
def mul(a, k): return (a[0] * k, a[1] * k)
def div(a, k): return (a[0] / k, a[1] / k)
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
    speed = hypot(v[0], v[1])
    if speed > MAX: return mul(v, MAX / speed)
    if speed < MIN and speed > 0: return mul(v, MIN / speed)
    return v

def steer(me, flock):
    (x, y), v = flock[me]
    others = []
    for i, (p, pv) in enumerate(flock):
        if i == me: continue
        d = (across(x, p[0], w), across(y, p[1], h))
        d2 = d[0] * d[0] + d[1] * d[1]
        if d2 < SIGHT * SIGHT: others.append((d, pv, d2))
    if others:
        k = len(others)
        cohesion = mul(div(vsum([o[0] for o in others]), k), COHESION)
        alignment = mul(sub(div(vsum([o[1] for o in others]), k), v), ALIGNMENT)
        separation = mul(vsum([o[0] for o in others if o[2] < ROOM * ROOM]), 0 - SEPARATION)
        v = add(v, add(cohesion, add(alignment, separation)))
    v = limit(v)
    return ((around(x + v[0], w), around(y + v[1], h)), v)

rand = Rand(seed)                     # the root's, after (rand/seed seed)
flock = []
for _ in range(n):
    a, b, c, d = rand.float(), rand.float(), rand.float(), rand.float()
    heading, speed = c * (2 * pi), MIN + d * (MAX - MIN)
    flock.append(((a * w, b * h), (speed * cr(mpmath.cos, heading), speed * cr(mpmath.sin, heading))))

# The eighth of a turn the heading is nearest, rounding halves up, as
# JavaScript's Math.round; y grows downward.
def arrow(v):
    eighth = floor(4 * cr(mpmath.atan2, v[1], v[0]) / pi + 0.5) % 8
    return '→↘↓↙←↖↑↗'[eighth]

out = []
for tick in range(frames):
    grid = [[' '] * w for _ in range(h // 2)]
    for (x, y), v in flock:
        grid[floor(y / 2) % (h // 2)][floor(x) % w] = arrow(v)
    edge = '+' + '-' * w + '+'
    out += [edge] + ['|' + ''.join(row) + '|' for row in grid] + [edge]
    out.append(f'tick {tick}, {n} birds' + (', 50.0 ms a tick' if tick >= 1 else ''))
    flock = [steer(i, flock) for i in range(n)]
out.append(f'"{frames} frames"')
print('\n'.join(out))
