# binary-trees, single-threaded: a node is a tuple, a leaf's (None, None).
import sys


def make(d):
    return (None, None) if d == 0 else (make(d - 1), make(d - 1))


def check(n):
    l, r = n
    return 1 if l is None else 1 + check(l) + check(r)


top = max(6, int(sys.argv[1]))
print(check(make(top + 1)))
long_lived = make(top)
for d in range(4, top + 1, 2):
    print(sum(check(make(d)) for _ in range(2 ** (top - d + 4))))
print(check(long_lived))
