#!/usr/bin/env python3
"""Writes runtime/unicode.c, the tables the str/ builtins need (D219),
from the Unicode Character Database:

    runtime/gen-unicode.py UCD-DIR > runtime/unicode.c

UCD-DIR holds UnicodeData.txt, SpecialCasing.txt and
DerivedCoreProperties.txt of one version, which is pinned to the one
t/models' Python has, so the tests' expected output agrees with the
tables: 15.1.0, as ICU's release-74-1 carries them,
https://raw.githubusercontent.com/unicode-org/icu/release-74-1/icu4c/source/data/unidata/

What it writes: each code point's full uppercase and lowercase mappings
where they aren't the code point itself (UnicodeData's simple ones, with
SpecialCasing's unconditional ones over them; the conditional ones are
left out, but for Final_Sigma, which strings.c works out), and the code
points that are Cased and Case_Ignorable, which Final_Sigma asks about.
"""
import os
import re
import sys


def load(ucd, name):
    with open(os.path.join(ucd, name), encoding='utf-8') as f:
        return f.read()


def version(text):
    m = re.search(r'-(\d+\.\d+\.\d+)\.txt', text.split('\n', 1)[0])
    return m.group(1) if m else None


def code_points(field):
    return [int(x, 16) for x in field.split()]


def mappings(ucd):
    upper, lower = {}, {}
    for line in load(ucd, 'UnicodeData.txt').split('\n'):
        if not line:
            continue
        f = line.split(';')
        c = int(f[0], 16)
        if f[12]:
            upper[c] = [int(f[12], 16)]
        if f[13]:
            lower[c] = [int(f[13], 16)]
    for line in load(ucd, 'SpecialCasing.txt').split('\n'):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        f = [x.strip() for x in line.split(';')]
        if len(f) > 4 and f[4]:
            continue                                # conditional: by language, or Final_Sigma
        c = int(f[0], 16)
        lower[c] = code_points(f[1])
        upper[c] = code_points(f[3])
    same = lambda c, m: m == [c]
    return ({c: m for c, m in upper.items() if not same(c, m)},
            {c: m for c, m in lower.items() if not same(c, m)})


def ranges(ucd, prop):
    out = []
    for line in load(ucd, 'DerivedCoreProperties.txt').split('\n'):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        span, name = [x.strip() for x in line.split(';')][:2]
        if name != prop:
            continue
        lo, _, hi = span.partition('..')
        out.append((int(lo, 16), int(hi or lo, 16)))
    out.sort()
    merged = []
    for lo, hi in out:
        if merged and lo == merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], hi)
        else:
            merged.append((lo, hi))
    return merged


def table(name, entries):
    rows = [f'    {{ 0x{c:04x}, {{ {", ".join(f"0x{x:04x}" for x in m)} }}, {len(m)} }},' for c, m in sorted(entries.items())]
    return [f'static const mapping_t {name}[] = {{'] + rows + ['};', '']


def span_table(name, spans):
    rows = [f'    {{ 0x{lo:04x}, 0x{hi:04x} }},' for lo, hi in spans]
    return [f'static const span_t {name}[] = {{'] + rows + ['};', '']


def main():
    ucd = sys.argv[1]
    v = version(load(ucd, 'DerivedCoreProperties.txt'))
    assert v == version(load(ucd, 'SpecialCasing.txt')), 'the files are of different versions'
    upper, lower = mappings(ucd)
    assert all(len(m) <= 3 for m in list(upper.values()) + list(lower.values()))
    out = [
        f'// unicode.c -- made by runtime/gen-unicode.py from the Unicode Character',
        f'// Database, version {v}; don\'t edit it, run that again (D219).',
        '',
        '#include "unicode.h"',
        '',
        '#include <stddef.h>',
        '',
        '// A code point and what it maps to: up to three code points.',
        'typedef struct {',
        '    uint32_t c, to[3];',
        '    uint8_t  n;',
        '} mapping_t;',
        '',
        '// A run of code points, lo to hi.',
        'typedef struct {',
        '    uint32_t lo, hi;',
        '} span_t;',
        '',
        f'const char rt_unicode_version[] = "{v}";',
        '',
    ]
    out += table('upper', upper) + table('lower', lower)
    out += span_table('cased', ranges(ucd, 'Cased')) + span_table('case_ignorable', ranges(ucd, 'Case_Ignorable'))
    out += [
        'static size_t map(const mapping_t *t, size_t n, uint32_t c, uint32_t out[3]) {',
        '    size_t lo = 0, hi = n;',
        '    while (lo < hi) {',
        '        size_t mid = lo + (hi - lo) / 2;',
        '        if (t[mid].c < c) lo = mid + 1;',
        '        else hi = mid;',
        '    }',
        '    if (lo < n && t[lo].c == c) {',
        '        for (size_t i = 0; i < t[lo].n; i++) out[i] = t[lo].to[i];',
        '        return t[lo].n;',
        '    }',
        '    out[0] = c;',
        '    return 1;',
        '}',
        '',
        'static int in(const span_t *t, size_t n, uint32_t c) {',
        '    size_t lo = 0, hi = n;',
        '    while (lo < hi) {',
        '        size_t mid = lo + (hi - lo) / 2;',
        '        if (t[mid].hi < c) lo = mid + 1;',
        '        else hi = mid;',
        '    }',
        '    return lo < n && t[lo].lo <= c;',
        '}',
        '',
        '#define COUNT(t) (sizeof t / sizeof t[0])',
        '',
        'size_t rt_unicode_upper(uint32_t c, uint32_t out[3]) { return map(upper, COUNT(upper), c, out); }',
        'size_t rt_unicode_lower(uint32_t c, uint32_t out[3]) { return map(lower, COUNT(lower), c, out); }',
        'int    rt_unicode_cased(uint32_t c)                  { return in(cased, COUNT(cased), c); }',
        'int    rt_unicode_case_ignorable(uint32_t c)         { return in(case_ignorable, COUNT(case_ignorable), c); }',
        '',
    ]
    sys.stdout.write('\n'.join(out))


if __name__ == '__main__':
    main()
