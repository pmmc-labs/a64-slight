#!/usr/bin/env python3
"""t/340-brief's expected output: a fault's value, a tree of pairs 30
deep with leaf at the bottom, as pprint shows it, cut at 64 KB (65,536
bytes), then "..." (D265). The text is made lazily, since all of it
would be 20 GB."""

BRIEF = 64 << 10


def text(n):
    if n == 0:
        yield ':leaf'
        return
    yield '('
    yield from text(n - 1)
    yield ' '
    yield from text(n - 1)
    yield ')'


def prefix(n, size):
    out, have = [], 0
    for piece in text(n):
        out.append(piece)
        have += len(piece)
        if have >= size:
            break
    return ''.join(out)[:size]


site = 't/340-brief.slight'
line = open(site).read().split('\n').index('(pprint (kind (join (fork (lambda () (+ 1 (double :leaf 30)))))))') + 1
col  = '(pprint (kind (join (fork (lambda () (+ 1 (double :leaf 30)))))))'.index('(+ 1') + 1
print(f'fault in #<pid 2>: not a number: {prefix(30, BRIEF)}... (+ at {site}:{line}:{col})')
print('fault in #<pid 2>: heap exhausted (its end)')
print(':heap')
print(':done')
