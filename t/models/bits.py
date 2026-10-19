# Writes t/272-bits.slight and its expected output (D221): the bit
# operations on 63-bit two's-complement integers, worked out with
# Python's integers. and, or, xor and not hold for any width; a shift
# left keeps the low 63 bits; a shift right is Python's >>, which floors;
# a count of 63 or more shifts out every bit, or leaves only the sign's.
#
#     python3 t/models/bits.py
import random

TOP = 1 << 63

def wrap(v):
    v &= TOP - 1
    return v - TOP if v >= TOP >> 1 else v

def ops(a, b, k):
    k = min(k, 63)                      # past that, as at 63, and Python needn't build the shift
    return [a & b, a | b, a ^ b, ~a, wrap(a << k), a >> k, bin(a & (TOP - 1)).count('1')]

rng = random.Random(272)
values = [0, 1, -1, 2, -2, 255, -256, (1 << 62) - 1, -(1 << 62), 1 << 61, -(1 << 61) - 1]
pick = lambda: rng.choice(values) if rng.random() < 0.3 else rng.randrange(-(1 << 62), 1 << 62) >> rng.randrange(0, 62)
counts = [0, 1, 31, 61, 62, 63, 64, 100, 4611686018427387903]

src = ["""; The bit operations (D221) on 63-bit two's-complement integers, from
; t/models/bits.py: each line is and, or and xor of a and b, not of a, a
; shifted left and right by k, and the bits set in a. They're compiled
; inline, the first lines with the operands as literals and the rest as
; values, which the compiler takes another way.
"""]
out = []
lits = [(pick(), pick(), rng.choice(counts) if rng.random() < 0.4 else rng.randrange(0, 70)) for _ in range(20)]
vals = [(pick(), pick(), rng.choice(counts) if rng.random() < 0.4 else rng.randrange(0, 70)) for _ in range(20)]
for a, b, k in lits:
    src.append(f'(pprint (list (bit/and {a} {b}) (bit/or {a} {b}) (bit/xor {a} {b}) (bit/not {a}) (bit/shl {a} {k}) (bit/shr {a} {k}) (bit/count {a})))')
    out.append('(' + ' '.join(map(str, ops(a, b, k))) + ')')
src.append("""(defun all (a b k)
    (list (bit/and a b) (bit/or a b) (bit/xor a b) (bit/not a) (bit/shl a k) (bit/shr a k) (bit/count a)))""")
for a, b, k in vals:
    src.append(f'(pprint (all {a} {b} {k}))')
    out.append('(' + ' '.join(map(str, ops(a, b, k))) + ')')
src.append("""; as values, and folded
(pprint (list (fold/l 0 bit/xor (list 1 2 4 8 2)) (fold/l -1 bit/and (list 7 14 28)) (map bit/not (list 0 -1 7))))""")
out.append(f'({1 ^ 2 ^ 4 ^ 8 ^ 2} {-1 & 7 & 14 & 28} ({~0} {~-1} {~7}))')
src.append("""(defun kind (pid) (car (cadr (join pid))))
(pprint (kind (fork (bit/shl 1 -1))))
(pprint (kind (fork (bit/shr 1 (- 0 5)))))
(pprint (kind (fork (bit/xor 1 1.0))))
(pprint (kind (fork (bit/count "1"))))
(kind (fork (bit/not ())))""")
text = '\n'.join(src) + '\n'
lines = text.split('\n')
line = lambda t: next(i + 1 for i, l in enumerate(lines) if t in l)
site = lambda name, t, col: f'({name} at t/272-bits.slight:{line(t)}:{col})'
count_string = '(bit/count "1")'
out += [f'fault in #<pid 2>: out of range: -1 {site("bit/shl", "(bit/shl 1 -1)", 21)}', 'out-of-range',
        f'fault in #<pid 3>: out of range: -5 {site("bit/shr", "(bit/shr 1 (- 0 5))", 21)}', 'out-of-range',
        f'fault in #<pid 4>: not an integer: 1.0 {site("bit/xor", "(bit/xor 1 1.0)", 21)}', 'not-an-int',
        f'fault in #<pid 5>: not an integer: "1" {site("bit/count", count_string, 21)}', 'not-an-int',
        f'fault in #<pid 6>: not an integer: () {site("bit/not", "(bit/not ())", 13)}', 'not-an-int']
open('t/272-bits.slight', 'w').write(text)
open('t/272-bits.expected', 'w').write('\n'.join(out) + '\n')
