#!/usr/bin/env python3
"""t/317-crypto's expected output, from Python's hashlib, hmac and base64.
Where slight's reading is stricter than Python's (base64 without its
padding, or with too much), what it gives is said by hand, with why."""

import base64
import hashlib
import hmac


def q(s):
    return '"' + s + '"'


def lst(xs):
    return '(' + ' '.join(xs) + ')'


def sha(name, m):
    return q(hashlib.new(name, m).hexdigest())


def hmacs(key, msg):
    return lst([q(hmac.new(key, msg, h).hexdigest()) for h in ('sha256', 'sha512')])


source = open('t/317-crypto.slight').read().split('\n')

print(lst([sha('sha256', m) for m in (b'', b'abc', b'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq')]))
print(lst([sha('sha256', b'a' * n) for n in (55, 56, 63, 64, 65, 119, 1000000)]))
print(lst([sha('sha512', m) for m in (b'', b'abc', b'abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu')]))
print(lst([sha('sha512', b'a' * n) for n in (111, 112, 127, 128, 129, 239, 1000000)]))
print(hmacs(b'\x0b' * 20, b'Hi There'))
print(hmacs(b'Jefe', b'what do ya want for nothing?'))
print(hmacs(b'\xaa' * 131, b'Test Using Larger Than Block-Size Key - Hash Key First'))
print(lst([hmacs(b'k' * n, b'm') for n in (64, 65, 128, 129)]))
print('(#true #false #false #true)')
print('(0 32 300 #false)')
for n, line in ((-1, 'crypto/random-bytes -1'), (1048577, 'crypto/random-bytes 1048577')):
    at = next(i for i, l in enumerate(source) if line in l)
    col = source[at].index('(' + line) + 1
    pid = 2 if n < 0 else 3
    print(f'fault in #<pid {pid}>: out of range: {n} (crypto/random-bytes at t/317-crypto.slight:{at + 1}:{col})')
    print('out-of-range')
print(lst([q(base64.b64encode(m).decode()) for m in (b'', b'f', b'fo', b'foo', b'foob', b'fooba', b'foobar')]))
print(lst([q(base64.urlsafe_b64encode(m).decode().rstrip('=')) for m in (b'f', b'fo', b'foo', b'\xfb\xff', b'\xfb\xff\xbf')]))


def std(s):
    # Python's strict mode, which also wants the padding there and right
    try:
        return q(base64.b64decode(s, validate=True).decode())
    except Exception:
        return '#false'


# "Zg" and "Zm=9": strict mode refuses them too; Python's validate=True
# checks the alphabet and the padding's place.
print(lst([std(s) for s in ('', 'Zg==', 'Zm8=', 'Zm9vYmFy', 'Zg', 'Zg=', 'Zg===', 'Z===', 'Zm9!', 'Zm=9', '-_8=')]))


def url(s):
    # padding optional, but right if it's there: Python wants it, so it's
    # added when it's missing, and a string whose padding is wrong for
    # its length, or that can't have any (one character over a group), isn't.
    if '=' in s and len(s) % 4 != 0 or len(s.rstrip('=')) % 4 == 1:
        return '#false'
    # RFC 4648 §5's alphabet hasn't + or /, which Python's altchars still
    # takes, translating - and _ to them and keeping them as they are
    if any(c in '+/' for c in s):
        return '#false'
    try:
        return q(base64.b64decode(s + '=' * (-len(s) % 4), altchars=b'-_', validate=True).hex())
    except Exception:
        return '#false'


print(lst([url(s) for s in ('Zg', 'Zg==', 'Zm8', 'Zm8=', '-_8', '-_8=', 'Zg=', 'Z', '+/8=')]))
print(lst([q(b''.hex()), q(b'\x00a~'.hex()), q(''), q('ABC'), q('JK'), '#false', '#false']))
print(lst([q('a%20b%26c%3Dd%2F%C3%A9~-._'), q('A/b+c d'), '#false', '#false', '#false']))
