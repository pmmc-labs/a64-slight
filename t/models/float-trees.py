#!/usr/bin/env python3
"""t/335-float-trees: float expressions worked in registers (D261). Each
function below is a tree of two or more float operations on locals and
literals; called with floats it's worked as doubles and boxed once, and
with anything else it goes the way it went before, faults and all. Its
values are the IEEE operations, in the same order, which Python's floats
are; an integer is made a float as C's (double) makes it, beside a
float; two integers stay integers. A fault's line is worked out here
from where its form is in the program this writes, as rt_fault lays it
out. Floats are laid out as slight prints them (t/models/arrays.py).

    python3 t/models/float-trees.py --program > t/335-float-trees.slight
    python3 t/models/float-trees.py > t/335-float-trees.expected
"""

import math
import sys
from decimal import Decimal


def show(d):
    """As slight prints a number (t/models/arrays.py's, which can't be
    imported without running it)."""
    if isinstance(d, int):
        return str(d)
    if math.isnan(d):
        return 'nan'
    if math.isinf(d):
        return 'inf' if d > 0 else '-inf'
    if d == 0:
        return '-0.0' if math.copysign(1, d) < 0 else '0.0'
    sign, digits, exp = Decimal(repr(d)).normalize().as_tuple()
    ds = ''.join(map(str, digits))
    k, n = len(ds), len(ds) + exp
    s = '-' if sign else ''
    if k <= n <= 21:
        return s + ds + '0' * (n - k) + '.0'
    if 0 < n <= 21:
        return s + ds[:n] + '.' + ds[n:]
    if -6 < n <= 0:
        return s + '0.' + '0' * -n + ds
    e = n - 1
    return s + ds[0] + ('.' + ds[1:] if k > 1 else '') + 'e' + ('+' if e > 0 else '-') + str(abs(e))


NAME = 't/335-float-trees.slight'
NAN, INF = float('nan'), float('inf')


def chain(n):
    """x + (x + (... (x * x))), n additions: n + 1 registers."""
    s = '(* x x)'
    for _ in range(n):
        s = f'(+ x {s})'
    return s


HEAD = [
    '; Float expressions worked in registers and boxed once (D261): each',
    '; function is a tree of float operations on locals and literals. Called',
    '; with floats, every value is as it was, to the bit; with an integer or',
    '; a string, or a divisor of zero, it goes the way it went before, faults',
    '; included. chain-20 needs 21 registers, more than x86-64 has for it, and',
    '; chain-30 more than either target, so those two are worked as before',
    '; where they don\'t fit. Expected output from t/models/float-trees.py.',
    '',
]
DEFUNS = [
    '(defun dist (dx dy dz) (math/sqrt (+ (+ (* dx dx) (* dy dy)) (* dz dz))))',
    '(defun pull (v d m mag) (- v (* (* d m) mag)))',
    '(defun mixed (x) (+ (* x 2) (/ 1 x)))',
    '(defun quot (a b c) (/ a (- b c)))',
    '(defun signs (x) (math/abs (- (math/sqrt x) (* x 0.5))))',
    '(defun scaled (xs k) (map (lambda (y) (* (+ y k) k)) xs))',
    f'(defun chain-20 (x) {chain(20)})',
    f'(defun chain-30 (x) {chain(30)})',
    '(defun failing (thunk) (pprint (join (fork (thunk)))))',
    '',
]
CALLS = [
    '(pprint (dist 1.0 2.0 2.0))',
    '(pprint (dist 0.1 0.2 0.3))',
    '(pprint (dist (math/sqrt -1) 1.0 1.0))',
    '(pprint (pull 1.5 0.25 3.0 0.125))',
    '(pprint (pull 10 1.0 2.0 3.0))',
    '(pprint (mixed 0.5))',
    '(pprint (mixed 3.0))',
    '(pprint (mixed 4))',
    '(pprint (quot 1.0 3.0 0.5))',
    '(pprint (quot 1.0 math/INF 0.0))',
    '(pprint (signs 2.0))',
    '(pprint (signs -0.0))',
    '(pprint (signs -4.0))',
    '(pprint (scaled (list 1.0 2.5 -3.0) 1.5))',
    '(pprint (chain-20 1.5))',
    '(pprint (chain-30 0.1))',
    '(failing (lambda () (quot 1.0 2.0 2.0)))',
    '(failing (lambda () (pull "a" 1.0 2.0 3.0)))',
    '(failing (lambda () (dist 1.0 :two 2.0)))',
    ':done',
]


def program():
    return '\n'.join(HEAD + DEFUNS + CALLS) + '\n'


def site(op, defun):
    """Where op's form is in the defun named, as rt_fault says it."""
    lines = (HEAD + DEFUNS)
    for i, line in enumerate(lines):
        if line.startswith(f'(defun {defun} '):
            return f'{op} at {NAME}:{i + 1}:{line.index("(" + op + " ") + 1}'
    raise KeyError(defun)


def sqrt(x):
    return NAN if x < 0 else math.copysign(math.sqrt(x), x) if x == 0 else math.sqrt(x)


def dist(dx, dy, dz):
    return sqrt((dx * dx + dy * dy) + dz * dz)


def pull(v, d, m, mag):
    return v - (d * m) * mag


def mixed(x):
    a = x * 2 if isinstance(x, int) else x * 2.0
    return a + 1 / x


def quot(a, b, c):
    return a / (b - c)


def signs(x):
    return abs(sqrt(x) - x * 0.5)


def run_chain(n, x):
    r = x * x
    for _ in range(n):
        r = x + r
    return r


def expected():
    out = [
        show(dist(1.0, 2.0, 2.0)),
        show(dist(0.1, 0.2, 0.3)),
        show(dist(NAN, 1.0, 1.0)),
        show(pull(1.5, 0.25, 3.0, 0.125)),
        show(pull(10, 1.0, 2.0, 3.0)),
        show(mixed(0.5)),
        show(mixed(3.0)),
        show(mixed(4)),
        show(quot(1.0, 3.0, 0.5)),
        show(quot(1.0, INF, 0.0)),
        show(signs(2.0)),
        show(signs(-0.0)),
        show(signs(-4.0)),
        '(' + ' '.join(show((y + 1.5) * 1.5) for y in (1.0, 2.5, -3.0)) + ')',
        show(run_chain(20, 1.5)),
        show(run_chain(30, 0.1)),
    ]
    # each fault in a task of its own, pids 2, 3 and 4 on island 0
    s = site('/', 'quot')
    out += [f'fault in #<pid 2>: division by zero ({s})', f'(error (div-by-zero () "{s}"))']
    s = site('-', 'pull')
    out += [f'fault in #<pid 3>: not a number: "a" ({s})', f'(error (not-a-number "a" "{s}"))']
    # (* dy dy) is the first operation on :two, after (* dx dx)
    lines = HEAD + DEFUNS
    i = next(i for i, l in enumerate(lines) if l.startswith('(defun dist '))
    s = f'* at {NAME}:{i + 1}:{lines[i].index("(* dy dy)") + 1}'
    out += [f'fault in #<pid 4>: not a number: two ({s})', f'(error (not-a-number two "{s}"))']
    out.append('done')
    return '\n'.join(out) + '\n'


sys.stdout.write(program() if sys.argv[1:] == ['--program'] else expected())
