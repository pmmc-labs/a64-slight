# The expected output of t/168-json.slight: each JSON text in
# t/data/json.txt as Python's json module reads it, printed compactly as
# is, then with everything past ASCII escaped (ensure_ascii). Strings go
# through json.dumps itself; numbers are laid out as slight prints them
# (JavaScript's layout, plus a ".0" or an exponent on a float).
#
#     python3 t/models/json-round-trip.py > t/168-json.expected
import json
import sys
from decimal import Decimal


def number(x):
    if isinstance(x, int):
        return str(x)
    if x == 0:
        return '-0.0' if str(x).startswith('-') else '0.0'
    sign = '-' if x < 0 else ''
    # repr gives the shortest digits that read back as x
    t = Decimal(repr(abs(x))).normalize().as_tuple()
    digits = ''.join(map(str, t.digits))
    k, n = len(digits), len(t.digits) + t.exponent      # the point goes after n digits
    if k <= n <= 21:
        return sign + digits + '0' * (n - k) + '.0'
    if 0 < n <= 21:
        return sign + digits[:n] + '.' + digits[n:]
    if -6 < n <= 0:
        return sign + '0.' + '0' * -n + digits
    rest = '.' + digits[1:] if k > 1 else ''
    return sign + digits[0] + rest + 'e' + ('-' if n - 1 < 0 else '+') + str(abs(n - 1))


def dumps(v, ascii):
    if v is None:
        return 'null'
    if v is True or v is False:
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return number(v)
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=ascii)
    if isinstance(v, dict):
        return '{' + ','.join(dumps(k, ascii) + ':' + dumps(x, ascii) for k, x in v.items()) + '}'
    return '[' + ','.join(dumps(x, ascii) for x in v) + ']'


# As pprint shows the slight value the parser makes.
def slight(v):
    if v is None:
        return 'null'
    if v is True or v is False:
        return '#true' if v else '#false'
    if isinstance(v, (int, float)):
        return number(v)
    if isinstance(v, str):
        return '"' + v + '"'
    if isinstance(v, dict):
        return '(' + ' '.join(['object'] + ['("' + k + '" ' + slight(x) + ')' for k, x in v.items()]) + ')'
    return '(' + ' '.join(slight(x) for x in v) + ')'


out = []
with open('t/data/json.txt', encoding='utf-8') as f:
    for line in f.read().splitlines():
        v = json.loads(line)
        out += [dumps(v, False), dumps(v, True)]

backslash = chr(92)
out.append(slight(json.loads('{"a": [1, 2.5, "' + backslash + 'u00e9"], "b": {"c": null, "d": false}}')))
out.append(slight(json.loads('\n{\n    "spaced" :\n        [ true ]\n}\n')))

# The bad texts, by hand: what the parser in the test does with them.
out.append('(error (json "not a value"))')          # [1, 2,]: a ] where a value goes
out.append('(error (json "expected :"))')           # {"a" 1}
out.append('(ok "' + chr(0xfffd) + '")')            # a lone surrogate
out.append('(error (json "text after the value"))') # [1] 2, the root's value

sys.stdout.buffer.write(('\n'.join(out) + '\n').encode('utf-8'))
