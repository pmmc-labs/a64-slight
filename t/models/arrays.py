#!/usr/bin/env python3
"""t/321-array-math's expected output, and its program (python3
t/models/arrays.py --program), so the two use the same numbers: arrays of
numbers (D253) element by element, reduced, and multiplied as matrices,
each the obvious loop's answer, left to right, which is what Python's
floats (IEEE doubles, each operation rounded) and integers give; an
integer result past 63 bits is an overflow. Floats are laid out as
slight prints them, as in t/models/math.py."""

import math
import random
import sys
from decimal import Decimal

MAX63 = (1 << 62) - 1
MIN63 = -(1 << 62)


def show(d):
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


class Overflow(Exception):
    pass


class DivZero(Exception):
    pass


def int63(n):
    if n < MIN63 or n > MAX63:
        raise Overflow
    return n


def array(kind, xs):
    return (kind, [float(x) for x in xs] if kind == 'float' else list(xs))


def shown(a):
    kind, xs = a
    return f'#{kind}({" ".join(show(x) for x in xs)})'


def elementwise(op, a, b):
    arrays = [x for x in (a, b) if isinstance(x, tuple)]
    kind, n = arrays[0][0], len(arrays[0][1])
    at = lambda x, i: x[1][i] if isinstance(x, tuple) else x
    if op == '/':
        if any(float(at(b, i)) == 0 for i in range(n)):
            raise DivZero
        return ('float', [float(at(a, i)) / float(at(b, i)) for i in range(n)])
    f = {'+': lambda x, y: x + y, '-': lambda x, y: x - y, '*': lambda x, y: x * y}[op]
    if kind == 'int':
        return ('int', [int63(f(at(a, i), at(b, i))) for i in range(n)])
    return ('float', [f(float(at(a, i)), float(at(b, i))) for i in range(n)])


def total(a):
    kind, xs = a
    s = 0 if kind == 'int' else 0.0
    for x in xs:
        s = int63(s + x) if kind == 'int' else s + x
    return s


def extreme(a, least):
    xs = a[1]
    m = xs[0]
    for x in xs[1:]:
        if (x < m) if least else (x > m):
            m = x
    return m


def dot(kind, xs, ys):
    s = 0 if kind == 'int' else 0.0
    for x, y in zip(xs, ys):
        s = int63(s + int63(x * y)) if kind == 'int' else s + x * y
    return s


def matmul(a, b, n, k, m):
    kind = a[0]
    return (kind, [dot(kind, a[1][i * k:(i + 1) * k], [b[1][p * m + j] for p in range(k)])
                   for i in range(n) for j in range(m)])


# The numbers: floats with up to 6 significant digits across a few
# magnitudes, written as slight reads them; integers small and large.
rng = random.Random(321)
FA = [float(f'{rng.uniform(-1, 1) * 10 ** rng.randint(-3, 4):.6g}') for _ in range(8)]
FB = [float(f'{rng.uniform(-1, 1) * 10 ** rng.randint(-3, 4):.6g}') for _ in range(8)]
IA = [rng.randint(-1000, 1000) for _ in range(8)]
IB = [rng.randint(-1000, 1000) for _ in range(8)]
M1 = [float(f'{rng.uniform(-10, 10):.4g}') for _ in range(12)]    # 3 by 4
M2 = [float(f'{rng.uniform(-10, 10):.4g}') for _ in range(8)]     # 4 by 2
N1 = [rng.randint(-50, 50) for _ in range(6)]                       # 2 by 3
N2 = [rng.randint(-50, 50) for _ in range(6)]                       # 3 by 2
BIG = 3037000499            # its square is under 2^63, but not under 2^62


def lit(x):
    return show(x) if isinstance(x, float) else str(x)


def lits(xs):
    return ' '.join(lit(x) for x in xs)


# Each case: the expression in slight, and its value in the model, or an
# exception for a fault (each in a task of its own, its kind printed).
fa, fb, ia, ib = array('float', FA), array('float', FB), array('int', IA), array('int', IB)
m1, m2, n1, n2 = array('float', M1), array('float', M2), array('int', N1), array('int', N2)
CASES = [
    ('(array/add fa fb)', lambda: shown(elementwise('+', fa, fb))),
    ('(array/sub fa fb)', lambda: shown(elementwise('-', fa, fb))),
    ('(array/mul fa fb)', lambda: shown(elementwise('*', fa, fb))),
    ('(array/div fa fb)', lambda: shown(elementwise('/', fa, fb))),
    ('(array/add fa 1)', lambda: shown(elementwise('+', fa, 1))),
    ('(array/sub 0.5 fa)', lambda: shown(elementwise('-', 0.5, fa))),
    ('(array/mul fa 3.25)', lambda: shown(elementwise('*', fa, 3.25))),
    ('(array/div 1 fa)', lambda: shown(elementwise('/', 1, fa))),
    ('(array/add ia ib)', lambda: shown(elementwise('+', ia, ib))),
    ('(array/sub ia ib)', lambda: shown(elementwise('-', ia, ib))),
    ('(array/mul ia ib)', lambda: shown(elementwise('*', ia, ib))),
    ('(array/div ia 4)', lambda: shown(elementwise('/', ia, 4))),
    ('(array/mul ia -7)', lambda: shown(elementwise('*', ia, -7))),
    ('(list (array/sum fa) (array/sum fb) (array/sum ia) (array/sum ib))',
     lambda: f'({show(total(fa))} {show(total(fb))} {total(ia)} {total(ib)})'),
    ('(list (array/min fa) (array/max fa) (array/min ib) (array/max ib))',
     lambda: f'({show(extreme(fa, True))} {show(extreme(fa, False))} {extreme(ib, True)} {extreme(ib, False)})'),
    ('(list (array/dot fa fb) (array/dot ia ib))', lambda: f'({show(dot("float", FA, FB))} {dot("int", IA, IB)})'),
    # in order, so 1.0 is lost against 1e16 and comes back after
    ('(array/sum (list->array :float (list 10000000000000000.0 1.0 -10000000000000000.0 1.0)))',
     lambda: show(total(array('float', [1e16, 1.0, -1e16, 1.0])))),
    ('(array/dot (list->array :float (list 0.1 0.2 0.3)) (list->array :float (list 0.3 0.2 0.1)))',
     lambda: show(dot('float', [0.1, 0.2, 0.3], [0.3, 0.2, 0.1]))),
    ('(array/min (list->array :float (list nan 1.0)))', lambda: show(extreme(array('float', [math.nan, 1.0]), True))),
    ('(array/max (list->array :float (list 1.0 nan 2.0)))', lambda: show(extreme(array('float', [1.0, math.nan, 2.0]), False))),
    ('(array/matmul m1 m2 3 4 2)', lambda: shown(matmul(m1, m2, 3, 4, 2))),
    ('(array/matmul n1 n2 2 3 2)', lambda: shown(matmul(n1, n2, 2, 3, 2))),
    ('(array/matmul n2 n1 3 2 3)', lambda: shown(matmul(n2, n1, 3, 2, 3))),
    ('(array/matmul m1 (array/make :float 4 1) 3 4 1)', lambda: shown(matmul(m1, array('float', [1] * 4), 3, 4, 1))),
    ('(array/matmul (array/make :int 0 0) (array/make :int 6 1) 0 2 3)', lambda: '#int()'),
    ('(array/sum (array/make :int 2 4611686018427387903))', lambda: show(total(array('int', [MAX63, MAX63])))),
    ('(array/add (array/make :int 1 4611686018427387903) 1)', lambda: shown(elementwise('+', array('int', [MAX63]), 1))),
    ('(array/sub (array/make :int 1 -4611686018427387904) 1)', lambda: shown(elementwise('-', array('int', [MIN63]), 1))),
    ('(array/mul (array/make :int 1 BIG) BIG)', lambda: shown(elementwise('*', array('int', [BIG]), BIG))),
    ('(array/dot (array/make :int 2 2147483648) (array/make :int 2 1073741824))',
     lambda: show(dot('int', [2 ** 31] * 2, [2 ** 30] * 2))),
    ('(array/dot (array/make :int 1 BIG) (array/make :int 1 BIG))', lambda: show(dot('int', [BIG], [BIG]))),
    ('(array/matmul (array/make :int 4 2147483648) (array/make :int 4 1073741824) 2 2 2)',
     lambda: shown(matmul(array('int', [2 ** 31] * 4), array('int', [2 ** 30] * 4), 2, 2, 2))),
    ('(array/div fa (array/set fb 3 0))', lambda: shown(elementwise('/', fa, array('float', FB[:3] + [0] + FB[4:])))),
    ('(array/div ia 0)', lambda: shown(elementwise('/', ia, 0))),
]

# What each fault says, and its kind.
FAULTS = {Overflow: ('integer overflow', 'overflow'), DivZero: ('division by zero', 'div-by-zero')}


HEADER = """; Arrays of numbers (D253) element by element, reduced, and multiplied as
; matrices: each the obvious loop's answer, left to right (so 1.0 is lost
; against 1e16 and comes back), every product and sum rounded on its own;
; a NaN only the least or greatest when it's first, as math/min has it;
; and the faults, an integer past 63 bits, at once or in a running total,
; and division by zero. This program and its expected output are from
; t/models/arrays.py, on Python's floats and integers.

(defun try (pid)
    (match (join pid)
        ((:ok v)          v)
        ((:error (k & _)) (list :error k))))
"""


def lines():
    out = HEADER.split('\n')
    out.append('(let nan (- math/INF math/INF))')
    out.append(f'(let BIG {BIG})')
    for name, xs, kind in [('fa', FA, 'float'), ('fb', FB, 'float'), ('ia', IA, 'int'), ('ib', IB, 'int'),
                           ('m1', M1, 'float'), ('m2', M2, 'float'), ('n1', N1, 'int'), ('n2', N2, 'int')]:
        out.append(f'(let {name} (list->array :{kind} (list {lits(xs)})))')
    out.append('')
    first = len(out) + 1
    for expr, _ in CASES:
        out.append(f'(pprint (try (fork {expr})))')
    out.append(':done')
    return out, first


def program():
    print('\n'.join(lines()[0]))


def expected():
    first = lines()[1]
    pid = 1
    for i, (expr, f) in enumerate(CASES):
        pid += 1
        try:
            print(f())
        except tuple(FAULTS) as e:
            what, kind = FAULTS[type(e)]
            op = expr[1:expr.index(' ')]
            print(f'fault in #<pid {pid}>: {what} ({op} at t/321-array-math.slight:{first + i}:{len("(pprint (try (fork ") + 1})')
            print(f'(error {kind})')
    print('done')


program() if '--program' in sys.argv else expected()
