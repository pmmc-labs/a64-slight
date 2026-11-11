#!/usr/bin/env python3
"""What copying a waiting process off its stack gives and costs: the
measurements PLAN.md's "A waiting process copied off its stack" asks of
its prototype, one tree against another.

    bench/copy-out.py OLD [NEW] [--rounds N] [--quick]

OLD and NEW are checkouts of this repository (NEW is this one unless
given): the runtime before the change and after it. To make OLD:

    git worktree add /tmp/slight-old 36f76d4

Each program is built by each tree's own slightc, from NEW's source, and
run in turns, old then new, N rounds (5 unless said), the best of each
kept, since a machine's noise only ever adds. It prints Markdown tables:

- idle-memory at 30,000 and 1,000,000: its time, and its peak resident
  set less startup's over n, the bytes a waiting process costs;
- a ring of m processes passing a token 5 million times (thread-ring
  with its size an argument, bench/thread-ring/ring.slight), m = 2, 503,
  4,000 and 30,000: nanoseconds a hop;
- ping-pong, payload, thread-ring and fan-in, which must not move;
- the instructions ping-pong and thread-ring retire: /usr/bin/time -l's
  on macOS, callgrind's on Linux (at a tenth of the size), if it's there;
- and NEW's ring of 2 with SLIGHT_COPY_OUT, every wait copied out and
  every wake copied in, against without: what a copy out and back costs.

--quick runs everything at a tenth of the size, to try the script.
"""

import os
import platform
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

PROGRAMS = {
    'startup':     'startup/startup.slight',
    'idle-memory': 'idle-memory/idle-memory.slight',
    'ring':        'thread-ring/ring.slight',
    'thread-ring': 'thread-ring/thread-ring.slight',
    'ping-pong':   'ping-pong/ping-pong.slight',
    'payload':     'payload/payload.slight',
    'fan-in':      'fan-in/fan-in.slight',
}
HOPS = 5000000


def fail(msg):
    sys.exit('copy-out.py: ' + msg)


def target():
    return 'aarch64' if platform.machine() in ('arm64', 'aarch64') else 'x86_64'


def build(tree, tag, new, out):
    for name, src in PROGRAMS.items():
        cmd = ['node', os.path.join(tree, 'bin', 'slightc.ts'), '--target', target(),
               '-o', os.path.join(out, tag + '-' + name), os.path.join(new, 'bench', src)]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            fail('building %s with %s:\n%s' % (name, tree, done.stdout + done.stderr))


def clean_env(extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('SLIGHT_')}
    env.update(extra or {})
    return env


# One run: (wall seconds, peak resident bytes), by bench/rusage.c.
def run(out, tag, name, args, env=None):
    report = os.path.join(out, 'report')
    cmd = [os.path.join(out, 'rusage'), report, os.path.join(out, tag + '-' + name)] + [str(a) for a in args]
    done = subprocess.run(cmd, capture_output=True, text=True, env=clean_env(env))
    if done.returncode != 0:
        fail('%s-%s %s exited %d:\n%s' % (tag, name, args, done.returncode, done.stdout + done.stderr))
    wall, _cpu, rss = open(report).read().split()
    return float(wall), int(rss)


# The instructions one run retires, or None where nothing counts them.
def instructions(out, tag, name, args, env=None):
    binary = os.path.join(out, tag + '-' + name)
    if platform.system() == 'Darwin':
        done = subprocess.run(['/usr/bin/time', '-l', binary] + [str(a) for a in args],
                              capture_output=True, text=True, env=clean_env(env))
        found = re.search(r'(\d+)\s+instructions retired', done.stderr)
        return int(found.group(1)) if found else None
    if shutil.which('valgrind'):
        log = os.path.join(out, 'callgrind.log')
        subprocess.run(['valgrind', '--tool=callgrind', '--callgrind-out-file=/dev/null', '--log-file=' + log, binary]
                       + [str(a) for a in args], capture_output=True, text=True, env=clean_env(env))
        found = re.search(r'Collected : (\d+)', open(log).read())
        return int(found.group(1)) if found else None
    return None


def main():
    args, rounds, quick, words = [], 5, False, sys.argv[1:]
    while words:
        word = words.pop(0)
        if word == '--rounds' and words and words[0].isdigit():
            rounds = int(words.pop(0))
        elif word == '--quick':
            quick = True
        elif word.startswith('--'):
            sys.exit(__doc__)
        else:
            args.append(word)
    if not args or len(args) > 2 or rounds < 1:
        sys.exit(__doc__)
    old, new = os.path.abspath(args[0]), os.path.abspath(args[1] if len(args) > 1 else os.path.dirname(HERE))
    scale = 10 if quick else 1
    out = os.path.join(new, 'build', 'copy-out')
    os.makedirs(out, exist_ok=True)
    if subprocess.run(['cc', '-O2', '-o', os.path.join(out, 'rusage'), os.path.join(new, 'bench', 'rusage.c')]).returncode:
        fail("can't build bench/rusage.c")
    build(old, 'old', new, out)
    build(new, 'new', new, out)
    # Each binary once before it's timed: macOS checks a new one the first
    # time it runs, a tenth of a second or more.
    for tag in ('old', 'new'):
        for name in PROGRAMS:
            run(out, tag, name, [100, 2] if name == 'ring' else [100])

    hops = HOPS // scale
    runs = [('startup', [0])]
    runs += [('idle-memory', [n // scale]) for n in (30000, 1000000)]
    runs += [('ring', [hops, m]) for m in (2, 503, 4000, 30000)]
    runs += [('ping-pong', [2000000 // scale]), ('payload', [1000000 // scale]),
             ('thread-ring', [10000000 // scale]), ('fan-in', [10000000 // scale])]
    forced = ('ring', [hops, 2])
    best = {}

    def keep(key, got):
        had = best.get(key)
        best[key] = got if had is None else (min(had[0], got[0]), min(had[1], got[1]))

    for r in range(rounds):
        for name, a in runs:
            for tag in ('old', 'new'):
                keep((tag, name, tuple(a)), run(out, tag, name, a))
        keep(('forced',) + (forced[0], tuple(forced[1])), run(out, 'new', forced[0], forced[1], {'SLIGHT_COPY_OUT': '1'}))
        print('round %d of %d' % (r + 1, rounds), file=sys.stderr)

    def got(tag, name, a):
        return best[(tag, name, tuple(a))]

    base = {tag: got(tag, 'startup', [0])[1] for tag in ('old', 'new')}
    print('Best of %d rounds, old and new in turns; %s, %s.\n' % (rounds, platform.platform(), target()))
    print('| | old | new | new ÷ old |')
    print('|---|---:|---:|---:|')
    for name, a in runs[1:3]:
        o, n = got('old', name, a), got('new', name, a)
        print('| `%s %d`, seconds | %.3f | %.3f | %.2f |' % (name, a[0], o[0], n[0], n[0] / o[0]))
        ob, nb = (o[1] - base['old']) / a[0], (n[1] - base['new']) / a[0]
        print('| `%s %d`, bytes a process | %.0f | %.0f | %.2f |' % (name, a[0], ob, nb, nb / ob))
    for name, a in runs[3:7]:
        o, n = got('old', name, a), got('new', name, a)
        print('| ring of %d, ns a hop | %.0f | %.0f | %.2f |' % (a[1], o[0] / a[0] * 1e9, n[0] / a[0] * 1e9, n[0] / o[0]))
    for name, a in runs[7:]:
        o, n = got('old', name, a), got('new', name, a)
        print('| `%s %d`, seconds | %.3f | %.3f | %.2f |' % (name, a[0], o[0], n[0], n[0] / o[0]))

    print('\n| instructions | old | new | new ÷ old |')
    print('|---|---:|---:|---:|')
    small = 1 if platform.system() == 'Darwin' else 10
    for name, a in (('ping-pong', [2000000 // scale // small]), ('thread-ring', [10000000 // scale // small])):
        o, n = instructions(out, 'old', name, a), instructions(out, 'new', name, a)
        if o and n:
            print('| `%s %d` | %d | %d | %.4f |' % (name, a[0], o, n, n / o))
        else:
            print('| `%s %d` | not counted here | | |' % (name, a[0]))

    kept, copied = got('new', *forced), best[('forced', forced[0], tuple(forced[1]))]
    print('\n| new, ring of 2, %d hops | waits keep their stacks | every wait copied out and in | the copy, a hop |' % hops)
    print('|---|---:|---:|---:|')
    print('| ns a hop | %.1f | %.1f | %.1f |' % (kept[0] / hops * 1e9, copied[0] / hops * 1e9, (copied[0] - kept[0]) / hops * 1e9))
    ik = instructions(out, 'new', forced[0], [hops // small, 2])
    ic = instructions(out, 'new', forced[0], [hops // small, 2], {'SLIGHT_COPY_OUT': '1'})
    if ik and ic:
        per = hops // small
        print('| instructions a hop | %.1f | %.1f | %.1f |' % (ik / per, ic / per, (ic - ik) / per))


if __name__ == '__main__':
    main()
