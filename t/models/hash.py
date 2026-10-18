# The hashes t/254-hash.slight pins, from the encoding docs/DESIGN.md
# gives for hash (D209), independently of runtime/rt.c:
#
#     python3 t/models/hash.py
#
# Each value is a sequence of 64-bit words, each mixed into the hash with
# splitmix64's mixer; the result is the hash shifted right by 2.
import struct

M = (1 << 64) - 1
INT, FLOAT, STRING, SYMBOL, PID, FUNCTION, LIST, END, TUPLE = range(1, 10)


class Sym:
    def __init__(self, name): self.name = name


class Fn:
    def __init__(self, name): self.name = name


class Pid:
    def __init__(self, n): self.n = n


def mix(h, w):
    x = ((h ^ w) + 0x9e3779b97f4a7c15) & M
    x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & M
    x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & M
    return x ^ (x >> 31)


def text(kind, b):
    words = [kind, len(b)]
    for i in range(0, len(b), 8):
        words.append(int.from_bytes(b[i:i + 8], 'little'))
    return words


# The words of v, with a stack in place of recursion, so a value nested
# 100000 deep is no trouble. A Python list is a slight list, a tuple a
# tuple.
def words(v):
    out, todo = [], [v]
    while todo:
        x = todo.pop()
        if x is END:
            out.append(END)
        elif isinstance(x, bool):
            raise TypeError(x)
        elif isinstance(x, int):
            out += [INT, x & M]
        elif isinstance(x, float):
            x = 0.0 if x == 0 else x
            out += [FLOAT, struct.unpack('<Q', struct.pack('<d', x))[0]]
        elif isinstance(x, str):
            out += text(STRING, x.encode())
        elif isinstance(x, Sym):
            out += text(SYMBOL, x.name.encode())
        elif isinstance(x, Fn):
            out += text(FUNCTION, x.name.encode())
        elif isinstance(x, Pid):
            out += [PID, x.n]
        elif isinstance(x, list):
            out.append(LIST)
            todo.append(END)
            todo += reversed(x)
        elif isinstance(x, tuple):
            out += [TUPLE, len(x)]
            todo += reversed(x)
    return out


def hash_of(v):
    h = 0
    for w in words(v):
        h = mix(h, w)
    return h >> 2


def nest(n, v):
    for _ in range(n):
        v = [v]
    return v


values = [0, -1, 4611686018427387903, -4611686018427387904, 1.5, -0.0, '', 'slight', 'eight by',
          Sym('ok'), Sym('#true'), [], (), [1, 2, 3], (1, 2, 3), [('a', []), 2.0, 'b'],
          Fn('twice'), Pid(1)]
for v in values:
    print(hash_of(v))
print(hash_of(nest(100000, 7)))
print('(#true #true #true)')
print('(#false #false #false #false #false #false)')
print('#true')
print('#true')
print('()')
