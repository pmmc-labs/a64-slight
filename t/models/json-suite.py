# The expected output of t/177-json-suite.slight. A y_ file parses, and
# is printed back as json/print prints it; an n_ file doesn't parse. An i_
# file is the parser's choice (D162): slight's turns down the four whose
# text isn't UTF-8 at the top level (UTF-16, and UTF-8's byte order mark,
# neither of which is JSON's whitespace), and takes the rest: numbers too
# big or too small for a double, \u escapes of lone surrogates, bytes in
# strings that aren't UTF-8, and 500 nested arrays.
#
#     python3 t/models/json-suite.py > t/177-json-suite.expected
import json
import math
import os
import sys
from decimal import Decimal

SUITE = 't/data/json-suite'
TURNED_DOWN = {
    'i_string_UTF-16LE_with_BOM.json',
    'i_string_utf16BE_no_BOM.json',
    'i_string_utf16LE_no_BOM.json',
    'i_structure_UTF-8_BOM_empty_object.json',
}


# As slight lays out a float (t/models/json-round-trip.py has the same):
# JavaScript's layout, plus a ".0" or an exponent.
def number(x):
    if isinstance(x, int):
        return str(x)
    assert math.isfinite(x)
    if x == 0:
        return '-0.0' if math.copysign(1, x) < 0 else '0.0'
    sign = '-' if x < 0 else ''
    t = Decimal(repr(abs(x))).normalize().as_tuple()
    digits = ''.join(map(str, t.digits))
    k, n = len(digits), len(t.digits) + t.exponent
    if k <= n <= 21:
        return sign + digits + '0' * (n - k) + '.0'
    if 0 < n <= 21:
        return sign + digits[:n] + '.' + digits[n:]
    if -6 < n <= 0:
        return sign + '0.' + '0' * -n + digits
    rest = '.' + digits[1:] if k > 1 else ''
    return sign + digits[0] + rest + 'e' + ('-' if n - 1 < 0 else '+') + str(abs(n - 1))


# An integer if it fits in 63 bits, as slight's are; a float otherwise.
def integer(text):
    n = int(text)
    return n if -2**62 <= n < 2**62 else float(text)


# Objects as (pairs,), keeping duplicates and their order, as slight does.
class Object(tuple):
    pass


def dumps(v):
    if v is None:
        return 'null'
    if v is True or v is False:
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return number(v)
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, Object):
        return '{' + ','.join(dumps(k) + ':' + dumps(x) for k, x in v[0]) + '}'
    return '[' + ','.join(dumps(x) for x in v) + ']'


out = []
for name in sorted(os.listdir(SUITE), key=lambda s: s.encode()):
    if not name.endswith('.json'):
        continue
    if name.startswith('y_'):
        with open(os.path.join(SUITE, name), 'rb') as f:
            v = json.loads(f.read().decode('utf-8'), parse_int=integer, object_pairs_hook=lambda p: Object((p,)))
        out.append(name + ' ok ' + dumps(v))
    elif name.startswith('n_') or name in TURNED_DOWN:
        out.append(name + ' error')
    else:
        out.append(name + ' ok')
out.append('(devices-agree #true)')
out.append('()')
sys.stdout.buffer.write(('\n'.join(out) + '\n').encode('utf-8'))
