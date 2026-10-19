# The expected output of the string tests (D219): a small evaluator for
# the part of slight they use, with each string builtin written again in
# Python, after JavaScript's string methods, over characters (a
# well-formed UTF-8 sequence, or a stray byte on its own, whose code point
# is U+FFFD). Python's own Unicode does the case mapping, at the version
# runtime/unicode.c was made from (15.1, Python 3.13's).
#
#     python3 t/models/strings.py t/261-str-chars.slight > t/261-str-chars.expected
#
# A string is Python bytes; its characters are what decoding them with
# surrogateescape gives, which turns each stray byte into one lone
# surrogate, U+DC80 to U+DCFF, as the runtime makes it one character.
# Symbols, #true and #false among them, are Sym; () and lists are lists.
# Faults print as the runtime logs them, at the call's line and column,
# and end only the process they're in: a fork here runs its expression
# at once, which is what the tests' (kind (fork e)) waits for anyway.
import re
import sys
import unicodedata

assert unicodedata.unidata_version == '15.1.0', unicodedata.unidata_version


class Sym:
    def __init__(self, name): self.name = name
    def __eq__(self, o): return isinstance(o, Sym) and o.name == self.name
    def __hash__(self): return hash(self.name)


TRUE, FALSE = Sym('#true'), Sym('#false')


class Lambda:
    def __init__(self, params, body, env): self.params, self.body, self.env = params, body, env


class Fault(Exception):
    def __init__(self, kind, value, site): self.kind, self.value, self.site = kind, value, site


KINDS = {'not-an-int': 'not an integer', 'not-a-list': 'not a list', 'not-a-string': 'not a string',
         'not-a-symbol': 'not a symbol', 'out-of-range': 'out of range', 'heap': 'heap exhausted'}


# --- reading --------------------------------------------------------------------

class Node:
    """A form as read, with where it starts, for a call's site."""
    def __init__(self, items, line, col): self.items, self.line, self.col = items, line, col


class Atom:
    def __init__(self, text, line=0, col=0): self.text, self.line, self.col = text, line, col


ESCAPES = {'"': '"', '\\': '\\', 'n': '\n', 't': '\t', 'r': '\r', 'e': '\x1b'}


def read(src):
    forms, stack, i, line, col = [], [[]], 0, 1, 1

    def advance(n):
        nonlocal i, line, col
        for c in src[i:i + n]:
            line, col = (line + 1, 1) if c == '\n' else (line, col + 1)
        i += n

    quotes = [[]]                                  # for each open list, where a ' waits
    while i < len(src):
        c = src[i]
        if c in ' \t\r\n':
            advance(1)
        elif c == ';':
            advance(src.find('\n', i) - i if '\n' in src[i:] else len(src) - i)
        elif c == '(':
            stack.append(Node([], line, col)); quotes.append([])
            advance(1)
        elif c == ')':
            node = stack.pop(); quotes.pop()
            put(stack, quotes, node)
            advance(1)
        elif c == "'":
            quotes[-1].append(len(stack[-1].items if isinstance(stack[-1], Node) else stack[-1]))
            advance(1)
        elif c == '"':
            j, out = i + 1, []
            while src[j] != '"':
                if src[j] == '\\':
                    e = src[j + 1]
                    if e == 'u':
                        k = src.index('}', j)
                        out.append(chr(int(src[j + 3:k], 16))); j = k + 1
                    else:
                        out.append(ESCAPES[e]); j += 2
                else:
                    out.append(src[j]); j += 1
            put(stack, quotes, ''.join(out).encode())
            advance(j + 1 - i)
        else:
            j = i
            while j < len(src) and src[j] not in ' \t\r\n()";':
                j += 1
            put(stack, quotes, atom(src[i:j], line, col))
            advance(j - i)
    assert len(stack) == 1
    return stack[0]


def put(stack, quotes, x):
    items = stack[-1].items if isinstance(stack[-1], Node) else stack[-1]
    if quotes[-1] and quotes[-1][-1] == len(items):
        quotes[-1].pop()
        x = Node([Atom('quote'), x], 0, 0)
    items.append(x)


def atom(t, line, col):
    if re.fullmatch(r'-?\d+', t):
        return int(t)
    if re.fullmatch(r'-?\d+(\.\d+)?([eE][+-]?\d+)?', t):
        return float(t)
    if t.startswith(':'):
        return Sym(t[1:])
    return Atom(t, line, col)


def datum(x):
    """A quoted form as a value."""
    if isinstance(x, Node):
        return [datum(y) for y in x.items]
    if isinstance(x, Atom):
        return Sym(x.text)
    return x


# --- showing --------------------------------------------------------------------

def quote(b):
    """In double quotes, with each of Unicode's control characters (Cc)
    escaped, by a letter where the reader has one; the rest as it is,
    stray bytes too."""
    out = []
    for c in chars(b):
        n = ord(c)
        e = {'"': '\\"', '\\': '\\\\', '\n': '\\n', '\t': '\\t', '\r': '\\r', '\x1b': '\\e'}.get(c)
        if e is None and unicodedata.category(c) == 'Cc':
            e = '\\u{%x}' % n
        out.append(e if e is not None else c)
    return b'"' + back(''.join(out)) + b'"'


def number(d):
    """As JavaScript lays a number out, with a .0 when it would look like an
    integer."""
    if d != d:
        return 'nan'
    if d in (float('inf'), float('-inf')):
        return 'inf' if d > 0 else '-inf'
    if d == 0:
        return '-0.0' if str(d).startswith('-') else '0.0'
    sign = '-' if d < 0 else ''
    digits, n = shortest(abs(d))
    k = len(digits)
    if k <= n <= 21:
        return sign + digits + '0' * (n - k) + '.0'
    if 0 < n <= 21:
        return sign + digits[:n] + '.' + digits[n:]
    if -6 < n <= 0:
        return sign + '0.' + '0' * -n + digits
    exp = n - 1
    return sign + digits[0] + ('.' + digits[1:] if k > 1 else '') + 'e' + ('-' if exp < 0 else '+') + str(abs(exp))


def shortest(d):
    """repr's digits, which are the fewest that read back as d, and where
    the point goes after them."""
    from decimal import Decimal
    t = Decimal(repr(d)).normalize().as_tuple()
    digits = ''.join(map(str, t.digits))
    return digits, len(digits) + t.exponent


def show(v, raw=False):
    if isinstance(v, bytes):
        return v if raw else quote(v)
    if isinstance(v, bool):
        raise TypeError('a Python bool')
    if isinstance(v, int):
        return str(v).encode()
    if isinstance(v, float):
        return number(v).encode()
    if isinstance(v, Sym):
        return v.name.encode()
    if isinstance(v, list):
        return b'(' + b' '.join(show(x) for x in v) + b')'
    if isinstance(v, Lambda):
        return b'#<lambda>'
    raise TypeError(repr(v))


# --- strings ----------------------------------------------------------------------

def chars(s):
    return s.decode('utf-8', 'surrogateescape')


def back(cs):
    return cs.encode('utf-8', 'surrogateescape')


def stray(c):
    return 0xdc80 <= ord(c) <= 0xdcff


def code(c):
    return 0xfffd if stray(c) else ord(c)


def find(cs, cm, start):
    """The first index at or after start where cm's characters are, or -1."""
    for i in range(start, len(cs) - len(cm) + 1):
        if cs[i:i + len(cm)] == cm:
            return i
    return -1


HEAP = 64 << 20         # what t/run.sh caps a heap at


def made(size, site):
    """A string of size bytes, which faults if it's more than the heap
    holds; the runtime makes one whole before it fills it (D205)."""
    if size > HEAP:
        raise Fault('heap', [], site)
    return True


def relative(i, n):
    return max(0, i + n) if i < 0 else min(i, n)


WHITE = set([*range(0x09, 0x0e), 0x20, 0x85, 0xa0, 0x1680, *range(0x2000, 0x200b), 0x2028, 0x2029, 0x202f, 0x205f, 0x3000])
assert all(chr(c).isspace() == (c in WHITE) or chr(c) in '\x1c\x1d\x1e\x1f' for c in range(0x110000) if not 0xd800 <= c <= 0xdfff)


class Builtins:
    """Each takes its arguments and the call's site, checking them in order."""

    def __init__(self, symbols): self.symbols = symbols

    @staticmethod
    def string(v, site):
        if not isinstance(v, bytes):
            raise Fault('not-a-string', v, site)
        return v

    @staticmethod
    def integer(v, site):
        if not isinstance(v, int):
            raise Fault('not-an-int', v, site)
        return v

    @staticmethod
    def lst(v, site):
        if not isinstance(v, list):
            raise Fault('not-a-list', v, site)
        return v

    def call(self, name, args, site):
        method = 'b_' + ''.join({'-': '_', '/': '_', '?': '_p', '<': 'lt', '~': 'cat'}.get(c, c) for c in name.replace('->', '_to_'))
        return getattr(self, method)(site, *args)

    def b_str(self, site, *xs): return b''.join(show(x, raw=True) for x in xs)

    def b_cat(self, site, *xs): return b''.join(self.string(x, site) for x in xs)   # ~

    def b_str_p(self, site, v): return TRUE if isinstance(v, bytes) else FALSE

    def b_str_len(self, site, s): return len(chars(self.string(s, site)))

    def index(self, s, i, site):
        cs = chars(self.string(s, site))
        k = self.integer(i, site)
        k = k + len(cs) if k < 0 else k
        if not 0 <= k < len(cs):
            raise Fault('out-of-range', i, site)
        return cs[k]

    def b_str_at(self, site, s, i): return back(self.index(s, i, site))

    def b_str_code_at(self, site, s, i): return code(self.index(s, i, site))

    def b_str_slice(self, site, s, a, b=None):
        cs = chars(self.string(s, site))
        n = len(cs)
        a = relative(self.integer(a, site), n)
        b = n if b is None else relative(self.integer(b, site), n)
        return back(cs[a:b]) if a < b else b''

    def b_str_index_of(self, site, s, m, start=None):
        cs, cm = chars(self.string(s, site)), chars(self.string(m, site))
        start = 0 if start is None else min(max(self.integer(start, site), 0), len(cs))
        return find(cs, cm, start)

    def b_str_last_index_of(self, site, s, m, start=None):
        cs, cm = chars(self.string(s, site)), chars(self.string(m, site))
        start = len(cs) if start is None else min(max(self.integer(start, site), 0), len(cs))
        for i in range(min(start, len(cs) - len(cm)), -1, -1):
            if cs[i:i + len(cm)] == cm:
                return i
        return -1

    def b_str_includes_p(self, site, s, m): return TRUE if self.b_str_index_of(site, s, m) >= 0 else FALSE

    def b_str_starts_with_p(self, site, s, m):
        cs, cm = chars(self.string(s, site)), chars(self.string(m, site))
        return TRUE if cs[:len(cm)] == cm else FALSE

    def b_str_ends_with_p(self, site, s, m):
        cs, cm = chars(self.string(s, site)), chars(self.string(m, site))
        return TRUE if len(cm) <= len(cs) and cs[len(cs) - len(cm):] == cm else FALSE

    def b_str_split(self, site, s, sep, limit=None):
        cs, cm = chars(self.string(s, site)), chars(self.string(sep, site))
        limit = None if limit is None else self.integer(limit, site)
        if limit is not None and limit < 0:
            raise Fault('out-of-range', limit, site)
        if not cm:
            pieces = list(cs)
        else:
            pieces, at = [], 0
            while True:
                i = find(cs, cm, at)
                if i < 0:
                    pieces.append(cs[at:])
                    break
                pieces.append(cs[at:i])
                at = i + len(cm)
        return [back(p) for p in pieces[:limit]]

    def b_str_join(self, site, sep, xs):
        self.string(sep, site)
        return sep.join(show(x, raw=True) for x in self.lst(xs, site))

    def b_str_repeat(self, site, s, n):
        self.string(s, site)
        if self.integer(n, site) < 0:
            raise Fault('out-of-range', n, site)
        return made(len(s) * n, site) and s * n

    def pad(self, site, s, n, fill, start):
        cs = chars(self.string(s, site))
        n = self.integer(n, site)
        cf = chars(self.string(b' ' if fill is None else fill, site))
        if n <= len(cs) or not cf:
            return s
        need = n - len(cs)
        made(len(s) + need, site)
        padding = back((cf * (need // len(cf) + 1))[:need])
        return padding + s if start else s + padding

    def b_str_pad_start(self, site, s, n, fill=None): return self.pad(site, s, n, fill, True)

    def b_str_pad_end(self, site, s, n, fill=None): return self.pad(site, s, n, fill, False)

    def trim(self, site, s, start, end):
        cs = list(chars(self.string(s, site)))
        while start and cs and ord(cs[0]) in WHITE:
            cs.pop(0)
        while end and cs and ord(cs[-1]) in WHITE:
            cs.pop()
        return back(''.join(cs))

    def b_str_trim(self, site, s): return self.trim(site, s, True, True)

    def b_str_trim_start(self, site, s): return self.trim(site, s, True, False)

    def b_str_trim_end(self, site, s): return self.trim(site, s, False, True)

    def b_str_upper(self, site, s): return back(chars(self.string(s, site)).upper())

    def b_str_lower(self, site, s): return back(chars(self.string(s, site)).lower())

    def replace(self, site, s, m, r, every):
        cs, cm, cr = chars(self.string(s, site)), chars(self.string(m, site)), chars(self.string(r, site))
        if not cm:
            return back(cr.join(['', *cs, '']) if every else cr + cs)
        out, at = [], 0
        while True:
            i = find(cs, cm, at)
            if i < 0:
                break
            out += [cs[at:i], cr]
            at = i + len(cm)
            if not every:
                break
        return back(''.join(out) + cs[at:])

    def b_str_replace(self, site, s, m, r): return self.replace(site, s, m, r, False)

    def b_str_replace_all(self, site, s, m, r): return self.replace(site, s, m, r, True)

    def b_str_compare(self, site, a, b):
        a, b = self.string(a, site), self.string(b, site)
        return (a > b) - (a < b)

    def b_str_lt(self, site, a, b): return TRUE if self.b_str_compare(site, a, b) < 0 else FALSE

    def b_str_chars(self, site, s): return [back(c) for c in chars(self.string(s, site))]

    def b_str_codes(self, site, s): return [code(c) for c in chars(self.string(s, site))]

    def b_codes_to_str(self, site, xs):
        out = []
        for x in self.lst(xs, site):
            n = self.integer(x, site)
            if not (0 <= n <= 0x10ffff) or 0xd800 <= n <= 0xdfff:
                raise Fault('out-of-range', x, site)
            out.append(chr(n))
        return ''.join(out).encode()

    def b_str_well_formed_p(self, site, s): return FALSE if any(map(stray, chars(self.string(s, site)))) else TRUE

    def b_str_to_well_formed(self, site, s):
        return ''.join('�' if stray(c) else c for c in chars(self.string(s, site))).encode()

    def radix(self, r, site):
        r = 10 if r is None else self.integer(r, site)
        if not 2 <= r <= 36:
            raise Fault('out-of-range', r, site)
        return r

    def b_str_to_int(self, site, s, r=None):
        s, r = self.string(s, site), self.radix(r, site)
        digits = '0123456789abcdefghijklmnopqrstuvwxyz'[:r]
        t = s.decode('latin-1')
        body = t[1:] if t.startswith('-') else t
        if not body or any(c.lower() not in digits or not c.isascii() for c in body):
            return FALSE
        n = int(t, r)
        return n if -2 ** 62 <= n < 2 ** 62 else FALSE

    def b_int_to_str(self, site, n, r=None):
        n, r = self.integer(n, site), self.radix(r, site)
        digits, out, u = '0123456789abcdefghijklmnopqrstuvwxyz', '', abs(n)
        while True:
            out = digits[u % r] + out
            u //= r
            if not u:
                break
        return (('-' if n < 0 else '') + out).encode()

    def b_str_to_float(self, site, s):
        t = self.string(s, site).decode('latin-1')
        if not re.fullmatch(r'-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?', t):
            return FALSE
        d = float(t)
        return FALSE if d in (float('inf'), float('-inf')) else d

    def b_sym_to_str(self, site, v):
        if not isinstance(v, Sym):
            raise Fault('not-a-symbol', v, site)
        return v.name.encode()

    def b_str_to_sym(self, site, s):
        name = self.string(s, site).decode('utf-8', 'surrogateescape')
        return Sym(name) if name in self.symbols else FALSE

    def b_str_to_bytes(self, site, s): return list(self.string(s, site))

    def b_bytes_to_str(self, site, xs):
        out = bytearray()
        for x in self.lst(xs, site):
            if not 0 <= self.integer(x, site) <= 255:
                raise Fault('out-of-range', x, site)
            out.append(x)
        return bytes(out)

    def b_bytes_len(self, site, s): return len(self.string(s, site))

    def b_bytes_at(self, site, s, i):
        s, k = self.string(s, site), self.integer(i, site)
        k = k + len(s) if k < 0 else k
        if not 0 <= k < len(s):
            raise Fault('out-of-range', i, site)
        return s[k]

    def b_bytes_slice(self, site, s, a, b=None):
        s = self.string(s, site)
        a = relative(self.integer(a, site), len(s))
        b = len(s) if b is None else relative(self.integer(b, site), len(s))
        return s[a:b] if a < b else b''

    def b_bytes_index_of(self, site, s, m, start=None):
        s, m = self.string(s, site), self.string(m, site)
        start = 0 if start is None else min(max(self.integer(start, site), 0), len(s))
        return s.find(m, start)


# --- evaluating -------------------------------------------------------------------

class Machine:
    def __init__(self, path, forms):
        self.path, self.out, self.pids, self.fns = path, [], 1, {}
        self.builtins = Builtins(symbols(forms))

    def site(self, name, node):
        return f'{name} at {self.path}:{node.line}:{node.col}'.encode()

    def run(self, forms):
        try:
            return self.top(forms)
        except Fault as e:
            self.out.append(b'fault: %s (%s)\nexit: 1\n' % (message(e), e.site))
            return b''.join(self.out)

    def top(self, forms):
        env, value = {}, []
        for f in forms:
            if isinstance(f, Node) and f.items and isinstance(f.items[0], Atom) and f.items[0].text == 'defun':
                self.fns[f.items[1].text] = Lambda([p.text for p in f.items[2].items], f.items[3:], {})
                continue
            if isinstance(f, Node) and f.items and isinstance(f.items[0], Atom) and f.items[0].text == 'let':
                env[f.items[1].text] = value = self.eval(f.items[2], env)
                continue
            value = self.eval(f, env)
        self.out.append(show(value) + b'\n')
        return b''.join(self.out)

    def body(self, forms, env):
        env, value = dict(env), []
        for f in forms:
            if isinstance(f, Node) and f.items and isinstance(f.items[0], Atom) and f.items[0].text == 'let':
                env[f.items[1].text] = value = self.eval(f.items[2], env)
            else:
                value = self.eval(f, env)
        return value

    def eval(self, x, env):
        if isinstance(x, (bytes, int, float, Sym)):
            return x
        if isinstance(x, Atom):
            t = x.text
            if t in env:
                return env[t]
            if t in ('#true', '#false'):
                return Sym(t)
            if t in ('\\n', '\\t', '\\r', '\\e'):
                return ESCAPES[t[1]].encode()
            if t in self.fns:
                return self.fns[t]
            return ('builtin', t, x)            # a builtin as a value calls it from here
        items = x.items
        if not items:
            return []
        head = items[0]
        if isinstance(head, Atom):
            h = head.text
            if h == 'quote':
                return datum(items[1])
            if h == 'lambda':
                return Lambda([p.text for p in items[1].items], items[2:], env)
            if h == 'if':
                return self.eval(items[2] if self.eval(items[1], env) == TRUE else items[3], env)
            if h == 'fork':
                return self.fork(items[1], env)
        f = self.eval(head, env)
        args = [self.eval(a, env) for a in items[1:]]
        return self.apply(f, args, x)

    def fork(self, x, env):
        self.pids += 1
        try:
            return [Sym('ok'), self.eval(x, env)]
        except Fault as e:
            self.out.append(b'fault in #<pid %d>: %s (%s)\n' % (self.pids, message(e), e.site))
            return [Sym('error'), [Sym(e.kind), e.value if e.kind != 'heap' else [], e.site]]

    def apply(self, f, args, node):
        if isinstance(f, Lambda):
            return self.body(f.body, {**f.env, **dict(zip(f.params, args))})
        _, name, at = f
        if name in PRELUDE:
            return PRELUDE[name](self, node, *args)
        return self.builtins.call(name, args, self.site(name, node or at))


def sort(m, less, xs):
    import functools
    before = lambda a, b: m.apply(less, [a, b], None) == TRUE
    return sorted(xs, key=functools.cmp_to_key(lambda a, b: -1 if before(a, b) else 1 if before(b, a) else 0))


def fold(m, acc, f, xs):
    for x in xs:
        acc = m.apply(f, [acc, x], None)
    return acc


def pprint(m, v):
    m.out.append(show(v) + b'\n')
    return []


# The prelude, and the builtins that aren't about strings, as far as the
# tests use them.
PRELUDE = {
    'pprint':  lambda m, node, v: pprint(m, v),
    'list':    lambda m, node, *xs: list(xs),
    'car':     lambda m, node, xs: xs[0],
    'cdr':     lambda m, node, xs: xs[1:],
    'cadr':    lambda m, node, xs: xs[1],
    'cons':    lambda m, node, x, xs: [x] + xs,
    'join':    lambda m, node, rec: rec,
    'map':     lambda m, node, f, xs: [m.apply(f, [x], None) for x in xs],
    'length':  lambda m, node, xs: len(xs),
    'reverse': lambda m, node, xs: xs[::-1],
    'eq?':     lambda m, node, a, b: TRUE if a == b and type(a) is type(b) else FALSE,
    'not':     lambda m, node, a: FALSE if a == TRUE else TRUE,
    '+':       lambda m, node, a, b: a + b,
    '-':       lambda m, node, a, b: a - b,
    'apply':   lambda m, node, f, args: m.apply(f, args, node),
    'fold/l':  lambda m, node, acc, f, xs: fold(m, acc, f, xs),
    'sort':    lambda m, node, less, xs: sort(m, less, xs),
    'ne?':     lambda m, node, a, b: FALSE if a == b and type(a) is type(b) else TRUE,
    '==':      lambda m, node, a, b: TRUE if a == b else FALSE,
    '<':       lambda m, node, a, b: TRUE if a < b else FALSE,
    'nil?':    lambda m, node, a: TRUE if a == [] else FALSE,
    'tty/write': lambda m, node, *xs: m.out.append(b''.join(show(x, raw=True) for x in xs)) or [],
    'sexp/print': lambda m, node, v: show(v),                          # as far as strings go
    'sexp/parse': lambda m, node, text: [Sym('ok'), read(text.decode('utf-8', 'surrogateescape'))[0]],
}


def message(fault):
    """What the runtime logs for a fault: what it is, and the value."""
    if fault.kind == 'heap':
        return KINDS['heap'].encode()
    return KINDS[fault.kind].encode() + b': ' + show(fault.value)


def symbols(forms):
    """The names the program mentions, which str->sym can make."""
    out = set()

    def walk(x, quoted):
        if isinstance(x, Node):
            q = quoted or (x.items and isinstance(x.items[0], Atom) and x.items[0].text == 'quote')
            for y in x.items:
                walk(y, q)
        elif isinstance(x, Sym):
            out.add(x.name)
        elif isinstance(x, Atom) and quoted:
            out.add(x.text)
    for f in forms:
        walk(f, False)
    return out


def main():
    path = sys.argv[1]
    forms = read(open(path, encoding='utf-8').read())
    m = Machine(path, forms)
    sys.stdout.buffer.write(m.run(forms))


if __name__ == '__main__':
    main()
