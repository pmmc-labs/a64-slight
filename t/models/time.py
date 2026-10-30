#!/usr/bin/env python3
"""t/311-time's expected output, from Python's datetime, which holds the
years 1 to 9999; a year outside them is moved by 400 years, a cycle the
Gregorian calendar repeats exactly, weekdays and all (146097 days)."""

import datetime as dt

EPOCH = dt.datetime(1970, 1, 1, tzinfo=dt.timezone.utc)
CYCLE_US = 146097 * 86400 * 10**6


def parts(us):
    shift = 0
    while us < -62135596800 * 10**6:            # before year 1
        us += CYCLE_US
        shift -= 400
    while us >= 253402300800 * 10**6:           # from year 10000
        us -= CYCLE_US
        shift += 400
    t = EPOCH + dt.timedelta(microseconds=us)
    return t.year + shift, t


def year(y):
    return f'{y:04d}' if 0 <= y <= 9999 else (f'-{-y:06d}' if y < 0 else f'+{y:06d}')


def iso(us, offset=None):
    y, t = parts(us if offset is None else us + offset * 60 * 10**6)
    s = f'{year(y)}-{t:%m-%dT%H:%M:%S}.{t.microsecond:06d}'
    if offset is None:
        return s + 'Z'
    a = abs(offset)
    return s + ('-' if offset < 0 else '+') + f'{a // 60:02d}:{a % 60:02d}'


def http(us):
    y, t = parts(us)
    return f'{t:%a, %d %b} {year(y)} {t:%H:%M:%S} GMT'


def q(s):
    return '"' + s + '"'


def us_of(t):
    return (t - EPOCH) // dt.timedelta(microseconds=1)


start = 1767225600 * 10**6
print(f'({start} 2500000 {q(iso(start))})')
instants = [0, -1, 951782400000000, 1709210096789012, -62135596800000000,
            253402300799999999, 253402300800000000, -62167219200000000, -62167219200000001]
for us in instants:
    print(f'({q(iso(us))} {q(http(us))})')
y, t = parts(1709210096789012)
print(f'((year {y}) (month {t.month}) (day {t.day}) (hour {t.hour}) (minute {t.minute}) (second {t.second}) '
      f'(us {t.microsecond}) (weekday {t.isoweekday()}))')
print('(' + ' '.join('#true' for _ in instants) + ')')
# 2025, month 14, day 32: February 2026, 32 days in: 2026-03-04.
print(q(iso(us_of(dt.datetime(2026, 2, 1, tzinfo=dt.timezone.utc) + dt.timedelta(days=31)))))
# 2024, month 0, day 1, hour 25: December 2023's 1st, 25 hours in.
print(q(iso(us_of(dt.datetime(2023, 12, 1, tzinfo=dt.timezone.utc) + dt.timedelta(hours=25)))))
print(f'({q(iso(1709210096789012, 120))} {q(iso(1709210096789012, -330))} {q(iso(0, 0))})')


# s as it reads, as read: Python's fromisoformat, given as, which is s
# itself, or s with what slight cuts already cut.
def parsed(s, as_=None):
    t = dt.datetime.fromisoformat((as_ or s).replace('t', 'T').replace('z', 'Z'))
    if t.tzinfo is None:
        t = t.replace(tzinfo=dt.timezone.utc)
    return f'({q(s)} {q(iso(us_of(t)))})'


print(parsed('2024-02-29'))
print(parsed('2024-02-29T12:34:56Z'))
print(parsed('2024-02-29 12:34:56.5+02:00'))
print(parsed('2024-02-29t12:34z'))
# Seven digits of fraction, cut to six, as Python reads at most six.
print(parsed('1969-12-31T23:59:59.9999999-00:30', '1969-12-31T23:59:59.999999-00:30'))
for bad in ['2023-02-29', '2024-13-01', '2024-02-29T24:00:00Z', '2024-02-29T12:34:56',
            '2024-02-29T12:34:56.Z', '2024-02-29T12:34:56+0200', '24-02-29']:
    print(f'(error (bad-time {q(bad)}))')
print('done')
