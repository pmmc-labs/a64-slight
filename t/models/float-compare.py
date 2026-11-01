#!/usr/bin/env python3
"""t/334-float-compare: the six comparisons on floats, inline since D259,
as a test (an if's) and as a value (a let's), for every pair of a list of
numbers: NaN, both zeros, both infinities, a float past 2^53, and some
integers, so that the pairs that aren't two floats still go to C. And a
float against integer literals, on either side, inline as a test: 0, -3
and 2^53 + 1, which as C's (double) makes it, and so rt_compare, is
2^53. Python's float comparisons are IEEE's, as the hardware's are; an
integer is made a float first, as rt_compare does, since Python would
compare it exactly.

    python3 t/models/float-compare.py --program > t/334-float-compare.slight
    python3 t/models/float-compare.py > t/334-float-compare.expected
"""

import operator
import sys

NAN = float('nan')
# (as slight writes it, the value)
VALUES = [
    ('0.0', 0.0), ('-0.0', -0.0), ('1.5', 1.5), ('-2.5', -2.5),
    ('math/INF', float('inf')), ('(- 0 math/INF)', float('-inf')), ('(math/sqrt -1)', NAN),
    ('3', 3), ('2.0', 2.0), ('9007199254740992.0', 9007199254740992.0), ('1.0e300', 1e300), ('-7', -7),
]
OPS = [('==', operator.eq), ('!=', operator.ne), ('<', operator.lt), ('<=', operator.le), ('>', operator.gt), ('>=', operator.ge)]
LITERALS = [0, -3, 9007199254740993]
LEFT = [0, 2]

def name(op): return {'==': 'eq', '!=': 'ne', '<': 'lt', '<=': 'le', '>': 'gt', '>=': 'ge'}[op]

def program():
    out = [
        '; Comparisons of floats, inline since D259, as a test and as a value,',
        '; for every pair of the numbers below (t: holds, f: doesn\'t; lower case',
        '; the test\'s, upper the value\'s); then a float against integer literals,',
        '; to the right and to the left. Expected output and this program from',
        '; t/models/float-compare.py.',
        '',
        '(defun numbers () (list ' + ' '.join(v for v, _ in VALUES) + '))',
        '',
    ]
    for op, _ in OPS:
        out += [
            f'(defun {name(op)} (a b)',
            f'    (let v ({op} a b))',
            f'    (str (if ({op} a b) "t" "f") (if v "T" "F")))',
        ]
    out.append('')
    for op, _ in OPS:
        tests = ' '.join([f'(if ({op} a {n}) "t" "f")' for n in LITERALS] + [f'(if ({op} {n} a) "t" "f")' for n in LEFT])
        out.append(f'(defun {name(op)}-literals (a) (str {tests}))')
    out += [
        '',
        '(defun row (f) (str/join " " (map (lambda (a) (str/join "" (map (lambda (b) (f a b)) (numbers)))) (numbers))))',
        '(defun literal-row (f) (str/join " " (map f (numbers))))',
        '',
    ]
    for op, _ in OPS:
        out.append(f'(tty/write "{op} " (row {name(op)}) "\\n")')
    for op, _ in OPS:
        out.append(f'(tty/write "{op} literals " (literal-row {name(op)}-literals) "\\n")')
    out.append(':done')
    return '\n'.join(out) + '\n'

def holds(f, a, b):
    return f(float(a), float(b))

def expected():
    out = []
    for op, f in OPS:
        out.append(op + ' ' + ' '.join(''.join(('t' if holds(f, a, b) else 'f') + ('T' if holds(f, a, b) else 'F') for _, b in VALUES) for _, a in VALUES))
    for op, f in OPS:
        cells = []
        for _, a in VALUES:
            cells.append(''.join('t' if holds(f, a, n) else 'f' for n in LITERALS) + ''.join('t' if holds(f, n, a) else 'f' for n in LEFT))
        out.append(f'{op} literals ' + ' '.join(cells))
    out.append('done')                  # the root's value, a symbol
    return '\n'.join(out) + '\n'

sys.stdout.write(program() if sys.argv[1:] == ['--program'] else expected())
