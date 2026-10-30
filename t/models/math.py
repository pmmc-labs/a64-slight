#!/usr/bin/env python3
"""t/319-math-exact's expected output: each math/ function on its inputs,
the exact result, from mpmath at 300 bits, rounded to the nearest double,
which is what CORE-MATH gives (vendor/core-math, D253). Floats are laid
out as slight prints them (rt.c's render_float): the shortest digits that
read back, as JavaScript lays them out, and ".0" on a whole number. With
the function list, t/319-math-exact.slight is written from this too, so
the two use the same inputs (python3 t/models/math.py --program)."""

import sys
from decimal import Decimal

import mpmath

mpmath.mp.prec = 300
M = mpmath

UNARY = [
    ('sin',   M.sin,   ['0.5', '1.0', '3.0', '100.0', '1.0e22', '-2.5', '1.0e-8']),
    ('cos',   M.cos,   ['0.5', '1.0', '3.0', '100.0', '1.0e22', '-2.5', '1.0e-8']),
    ('tan',   M.tan,   ['0.5', '1.0', '3.0', '100.0', '1.0e22', '-2.5', '1.0e-8']),
    ('asin',  M.asin,  ['0.5', '-0.3', '0.99', '1.0e-9']),
    ('acos',  M.acos,  ['0.5', '-0.3', '0.99', '1.0e-9']),
    ('atan',  M.atan,  ['0.5', '-3.0', '1.0e10', '1.0e-9']),
    ('sinh',  M.sinh,  ['0.5', '-2.0', '20.0', '1.0e-9']),
    ('cosh',  M.cosh,  ['0.5', '-2.0', '20.0', '1.0e-9']),
    ('tanh',  M.tanh,  ['0.5', '-2.0', '20.0', '1.0e-9']),
    ('asinh', M.asinh, ['0.5', '-2.0', '1.0e10']),
    ('acosh', M.acosh, ['1.5', '2.0', '1.0e10']),
    ('atanh', M.atanh, ['0.5', '-0.9', '1.0e-9']),
    ('exp',   M.exp,   ['1.0', '-1.0', '0.5', '700.0', '-708.0', '1.0e-10']),
    ('expm1', M.expm1, ['1.0e-10', '-0.5', '2.0']),
    ('log',   M.log,   ['10.0', '0.5', '2.0', '1.0000000000000002', '1.0e300']),
    ('log2',  lambda x: M.log(x, 2), ['10.0', '3.0', '0.1']),
    ('log10', M.log10, ['2.0', '3.0', '1.0e-5']),
    ('log1p', M.log1p, ['1.0e-10', '0.5', '-0.5']),
    ('cbrt',  lambda x: -M.cbrt(-x) if x < 0 else M.cbrt(x), ['-27.0', '2.0', '1.0e-300']),
]
BINARY = [
    ('pow',   M.power, [('2.0', '0.5'), ('1.0000001', '10000000.0'), ('10.0', '-3.0'), ('0.5', '100.5'), ('3.0', '0.333')]),
    ('atan2', M.atan2, [('1.0', '2.0'), ('-1.0', '-2.0'), ('1.0', '-2.0'), ('1.0e-10', '1.0')]),
    ('hypot', M.hypot, [('3.0', '4.0'), ('1.0e300', '1.0e300'), ('1.0e-300', '1.0e-300'), ('1.0', '1.0e-10')]),
]


# The double the program's literal is, exactly: not the decimal, which
# 0.1 isn't.
def exact(literal):
    return M.mpf(float(literal))


def nearest(x):
    return float(x)          # mpmath rounds to the nearest double


def show(d):
    if d == 0:
        return '-0.0' if str(d).startswith('-') else '0.0'
    sign, digits, exp = Decimal(repr(d)).normalize().as_tuple()
    ds = ''.join(map(str, digits))
    k, n = len(ds), len(ds) + exp     # the value is 0.ds times 10^n
    s = '-' if sign else ''
    if k <= n <= 21:
        return s + ds + '0' * (n - k) + '.0'
    if 0 < n <= 21:
        return s + ds[:n] + '.' + ds[n:]
    if -6 < n <= 0:
        return s + '0.' + '0' * -n + ds
    e = n - 1
    return s + ds[0] + ('.' + ds[1:] if k > 1 else '') + 'e' + ('+' if e > 0 else '-') + str(abs(e))


def program():
    print("""; math/ through CORE-MATH (D253): each function on inputs across its
; domain, D76's tan 1.0 among them, and sin of 1e22, whose argument
; reduction is hard, compared exactly: what's printed is the double
; nearest the exact result, the same on every machine. Expected output,
; and this program, from t/models/math.py, on mpmath.
""")
    for name, _, xs in UNARY:
        print(f'(pprint (list :{name} {" ".join(f"(math/{name} {x})" for x in xs)}))')
    for name, _, xys in BINARY:
        print(f'(pprint (list :{name} {" ".join(f"(math/{name} {x} {y})" for x, y in xys)}))')
    print(':done')


def expected():
    for name, f, xs in UNARY:
        print(f'({name} {" ".join(show(nearest(f(exact(x)))) for x in xs)})')
    for name, f, xys in BINARY:
        print(f'({name} {" ".join(show(nearest(f(exact(x), exact(y)))) for x, y in xys)})')
    print('done')


program() if '--program' in sys.argv else expected()
