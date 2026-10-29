#!/usr/bin/env python3
"""t/314-bytes-pack's expected output, from Python's struct: the bytes
each spec makes, what they read back as, and the faults, whose lines the
runtime logs with the call's place in the test."""

import math
import struct

CODES = {'u8': '>B', 'i8': '>b', 'u16be': '>H', 'u16le': '<H', 'i16be': '>h', 'i16le': '<h', 'u32be': '>I',
         'u32le': '<I', 'i32be': '>i', 'i32le': '<i', 'u64be': '>Q', 'u64le': '<Q', 'i64be': '>q', 'i64le': '<q',
         'f32be': '>f', 'f32le': '<f', 'f64be': '>d', 'f64le': '<d'}


def pack(specs, values):
    out = b''
    for spec, v in zip(specs, values):
        out += v.encode() if isinstance(spec, tuple) else struct.pack(CODES[spec], v)
    return out


def unpack(specs, s, at):
    got = []
    for spec in specs:
        n = spec[1] if isinstance(spec, tuple) else struct.calcsize(CODES[spec])
        if at + n > len(s):
            return '(error short)'
        got.append(s[at:at + n].decode() if isinstance(spec, tuple) else struct.unpack(CODES[spec], s[at:at + n])[0])
        at += n
    return f'(ok ({" ".join(show(v) for v in got)}) {at})'


def show(v):
    if isinstance(v, str):
        return f'"{v}"'
    if isinstance(v, float):
        return repr(v) if math.isfinite(v) else v
    return str(v)


specs = ['u8', 'i8', 'u16be', 'u16le', 'i16be', 'i16le', 'u32be', 'u32le', 'i32be', 'i32le', 'u64be', 'u64le',
         'i64be', 'i64le', 'f32be', 'f32le', 'f64be', 'f64le', ('bytes', 3)]
values = [255, -128, 65535, 258, -32768, 32767, 4294967295, 16909060, -2147483648, -2, 4611686018427387903, 1,
          -4611686018427387904, -1, 0.1, -2.5, 1e300, -0.0, 'abc']
s = pack(specs, values)
print('(' + ' '.join(str(b) for b in s) + ')')
print(unpack(specs, s, 0))
print(unpack(['u16le', 'u8'], s, 2))
print(unpack(['u32be'], s, len(s) - 2))
print(unpack([], s, len(s)))
print('(' + ' '.join(str(b) for b in pack(['f64be', 'u8'], [1.0, 0])) + ')')
print(unpack(['i8', 'u8'], pack(['u8', 'i8'], [200, -56]), 0))

# The faults: each line's place in the test, and what the runtime says.
source = open('t/314-bytes-pack.slight').read().split('\n')


def place(call, nth=0):
    hits = [(i + 1, line.index(call) + 1) for i, line in enumerate(source) if call in line]
    return hits[nth]


faults = [
    ('(bytes/pack (list :u8) (list 256))', 'out of range: 256', 'out-of-range'),
    ('(bytes/pack (list :i8) (list 128))', 'out of range: 128', 'out-of-range'),
    ('(bytes/pack (list (list :bytes 2)) (list "abc"))', 'out of range: "abc"', 'out-of-range'),
    ('(bytes/pack (list :u7) (list 1))', 'out of range: u7', 'out-of-range'),
    ('(bytes/pack (list :u8 :u8) (list 1))', 'out of range: (1)', 'out-of-range'),
    ('(bytes/pack (list :u8) (list 1 2))', 'out of range: (1 2)', 'out-of-range'),
    ('(bytes/pack (list :f64le) (list "x"))', 'not a number: "x"', 'not-a-number'),
    ('(bytes/unpack (list :u64be)', 'integer overflow', 'overflow'),
    ('(bytes/unpack (list :u8) "ab" 3)', 'out of range: 3', 'out-of-range'),
]
for pid, (call, said, kind) in enumerate(faults, start=2):
    line, col = place(call)
    name = call[1:call.index(' ')]
    print(f'fault in #<pid {pid}>: {said} ({name} at t/314-bytes-pack.slight:{line}:{col})')
    print(kind)
