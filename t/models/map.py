# The expected output of t/275-map.slight: the lines worked out by hand,
# then a seeded run of puts and deletes on a Python dict, drawing as the
# test does from t/models/rand.py and hashing with t/models/hash.py.
#
#     python3 t/models/map.py > t/275-map.expected
#
# map->list's order is the trie's: by the hash's lowest 5 bits, then the
# next 5, and so on, which no Python structure gives, so it's sorted for;
# keys with the same hash keep the order they came in, as a dict does.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hash import hash_of
from rand import Rand

print('(2 1 2 not-found #true #false)')
print('(2 1 3 1 2)')
print('(#true 1 not-found)')
print('(0 () not-found #true)')
print('(9 (0 1 2 3 4 5 6 7 8))')
print('(#false #true 200 2646700)')
print('#(2 #(1 #(3 (x 1 0) (y 2 32))))')
print('#(3 #(3 #(3 (x 1 0) (y 2 32)) (z 3 1)))')
print('#(1 #(1 (x 1 0)))')
print('(1 2 not-found)')
print('#true')
print('(3 ((c c) (a a) (b b)) b not-found)')
print('((a a) (b b) (c 0))')
print('#true')

rand = Rand(7)
d = {}
for i in range(3000):
    op = rand.int(10)
    k = rand.int(300)
    k = k if rand.int(2) == 0 else 'k%d' % k
    if op < 6:
        d[k] = i
    else:
        d.pop(k, None)


def chunks(h):
    return [(h >> s) & 31 for s in range(0, 65, 5)]


entries = sorted(d.items(), key=lambda e: chunks(hash_of(e[0])))


def show(x):
    return '"%s"' % x if isinstance(x, str) else str(x)


print('(%d #true %d)' % (len(d), sum(d.values())))
print('(%s)' % ' '.join('(%s %d)' % (show(k), v) for k, v in entries[:8]))
print(hash_of([[k, v] for k, v in entries]))
print('(#true #true)')
print('()')
