#!/usr/bin/env python3
"""Checks no golden test can make (t/run.sh), for $TARGET, with $RUN as
t/run.sh has them: each needs the real clock, a terminal, a second
program, or the system to measure the run.

- keys: t/live/keys.slight decodes a few thousand ESCs, then a key, on a
  process's stack all but full; until D205 that ran off the stack.
- memory: each case of t/live/memory.slight ends as its golden test
  does, and its peak resident set is under PEAK; the code before D204
  and D205 held from half a gigabyte to 4 GB on the way.
- chat: examples/chat.slight in a terminal, with t/live/model.slight as
  its model: what the model sends with terminal escapes in it is shown
  escaped, approvals included; a refused command isn't run; and a fetch
  that's redirected says where, and goes no further (D205).
- airline: a day the size of a small airline's, from
  examples/airline-gen.slight, too big for a golden test: every leg
  lands in the base's record, which is the same in a seeded order
  (D210); and what-ifs run from checkpoints, some kept in a history and
  the rest run from it later, come to what they come to run from the
  start of the day with no checkpoints (D212); random what-ifs in two
  rounds come to what they do in one (D214); and in three shares, run
  apart, to what they do in one run (D218).

Nothing here depends on how long anything takes: each step waits for
what it expects to see, and DEADLINE seconds is only how long it waits
before it says the check failed.
"""

import fcntl
import os
import platform
import pty
import re
import select
import shlex
import signal
import struct
import subprocess
import sys
import termios

TARGET   = os.environ.get('TARGET', 'aarch64')
DEADLINE = 120
PEAK     = 400 << 20
DIR      = os.path.join('build', 't', TARGET, 'live')
PROMPT   = '❯ '.encode()


def run_prefix():
    if 'RUN' in os.environ:
        return shlex.split(os.environ['RUN'])
    native = {('aarch64', 'arm64'), ('aarch64', 'aarch64'), ('x86_64', 'x86_64'), ('x86_64', 'arm64')}
    return [] if (TARGET, platform.machine()) in native else [f'qemu-{TARGET}']


RUN = run_prefix()

# The runs are on the real clock, with nothing else from the golden
# tests' settings.
ENV = {k: v for k, v in os.environ.items() if not k.startswith('SLIGHT_')}

failed = 0


def check(what, ok, detail=''):
    global failed
    print(f'ok   {what}' if ok else f'FAIL {what}')
    if not ok:
        failed += 1
        if detail:
            print(detail)


def build(name, source):
    out = os.path.join(DIR, name)
    r = subprocess.run(['node', 'bin/slightc.ts', '--target', TARGET, '-o', out, source], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f'live: {source} did not compile\n{r.stdout}{r.stderr}')
    return out


def peak_of(rusage):
    # ru_maxrss is in bytes on macOS, and in KB on Linux
    return rusage.ru_maxrss * (1 if sys.platform == 'darwin' else 1024)


def keys():
    prog = build('keys', 't/live/keys.slight')
    r = subprocess.run(RUN + [prog], input=b'\x1b' * 4000 + b'x', stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                       env=ENV, timeout=DEADLINE)
    out, status = r.stdout.decode(), r.returncode
    check('keys after 4000 ESCs, on a stack all but full', status == 0 and out == '("x" alt)\n(ok #true)\n()\n',
          f'exit {status}, printed {out!r}')


# A run's stdout, exit status and peak resident set, from wait4, which
# subprocess would take for itself. An alarm, which exec keeps, ends a run
# that takes past the deadline.
def peak_run(prog, args):
    pid = os.fork()
    if pid == 0:
        signal.alarm(DEADLINE)
        devnull = os.open(os.devnull, os.O_RDWR)
        out = os.open(os.path.join(DIR, 'memory.out'), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        os.dup2(devnull, 0)
        os.dup2(out, 1)
        os.dup2(devnull, 2)
        try:
            os.execvpe((RUN + [prog])[0], RUN + [prog] + args, ENV)
        finally:
            os._exit(127)
    _, status, rusage = os.wait4(pid, 0)
    with open(os.path.join(DIR, 'memory.out')) as f:
        return f.read(), os.waitstatus_to_exitcode(status), peak_of(rusage)


def memory():
    prog = build('memory', 't/live/memory.slight')
    cases = [
        ('endless', '(error (too-big "sh"))\n()\n', 'a JSON value with no end, from a command'),
        ('deep', '(error (too-big "sh"))\n()\n', '30 million arrays deep, from a command'),
        ('wide', None, 'str/pad-start to a width of 4 GB'),
    ]
    for name, expected, what in cases:
        out, status, peak = peak_run(prog, [name])
        ok_out = out == expected if expected else out.startswith('(error (heap () "str/pad-start at ')
        check(f'{what}: {out.splitlines()[0] if out else "nothing"}, at most {peak >> 20} MB',
              status == 0 and ok_out and peak < PEAK, f'exit {status}, printed {out!r}')


# A terminal, read as it's written to: see waits for text after what's
# been seen so far.
class Terminal:
    def __init__(self, argv, env):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        self.proc = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave, env=env, start_new_session=True)
        os.close(slave)
        self.out = b''
        self.at  = 0

    def read(self, wait):
        ready, _, _ = select.select([self.master], [], [], wait)
        if not ready:
            return False
        try:
            more = os.read(self.master, 65536)
        except OSError:                         # the program has ended (EIO on Linux)
            more = b''
        if not more:
            return None
        self.out += more
        return True

    # The text from where it was up to text's end, or None.
    def see(self, text):
        for _ in range(DEADLINE * 10):
            i = self.out.find(text, self.at)
            if i >= 0:
                seen, self.at = self.out[self.at:i + len(text)], i + len(text)
                return seen
            if self.read(0.1) is None:
                return None
        return None

    def type(self, keys):
        os.write(self.master, keys)

    # Its exit status, once it has ended and all it wrote has been read.
    def end(self):
        for _ in range(DEADLINE * 10):
            if self.read(0.1) is None:
                break
        try:
            return self.proc.wait(timeout=DEADLINE)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return None


def chat():
    model = build('model', 't/live/model.slight')
    prog  = build('chat', 'examples/chat.slight')
    server = subprocess.Popen(RUN + [model], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=ENV)
    port   = server.stdout.readline().decode().strip()
    home   = os.path.join(DIR, 'home')
    os.makedirs(home, exist_ok=True)
    notes = os.path.join(home, '.slight-chat-notes')
    if os.path.exists(notes):
        os.remove(notes)
    env = dict(ENV, HOME=home, OLLAMA_HOST=f'127.0.0.1:{port}', no_proxy='127.0.0.1', NO_PROXY='127.0.0.1')
    env.pop('CHAT_EXPERT', None)
    t = Terminal(RUN + [prog], env)

    asked = []
    if t.see(PROMPT) is not None:
        t.type(b'hi\r')
        for _ in range(2):                      # run, then fetch: no to the one, yes to the other
            question = t.see(b'[y/n] ')
            if question is None:
                break
            asked.append(question)
            t.type(b'n' if b'run: ' in question else b'y')
        if t.see(b'done') is not None and t.see(PROMPT) is not None:
            t.type(b'\x04')
    status = t.end()
    server.kill()
    told = server.communicate()[0].decode()

    shown = t.out
    check('the chat ends as asked', status == 0 and b'bye' in shown, f'exit {status}, the terminal had {shown!r}')
    check("the model's text is shown with its escape escaped", b'Hello, \\u{1b}[8mhidden\\u{1b}[28m world.' in shown)
    check('so is a command it asks to run, when asked', any(b'run: echo shown\\u{1b}[8m; echo hidden' in q for q in asked),
          f'asked {asked!r}')
    check('and no escape of its own reaches the terminal', b'\x1b[8m' not in shown)
    check("a command that's refused isn't run", "told: the user didn't let it run\n" in told, f'the model was told {told!r}')
    moved = f'told: HTTP 302: a redirect to http://127.0.0.1:{port}/page, which a fetch of its own can get\n'
    check('a redirect is told to the model, and not followed', moved in told and 'the page was fetched' not in told,
          f'the model was told {told!r}')


def airline():
    gen  = build('airline-gen', 'examples/airline-gen.slight')
    prog = build('airline', 'examples/airline.slight')
    day  = os.path.join(DIR, 'airline-day.sexp')
    made = subprocess.run(RUN + [gen], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=ENV, timeout=DEADLINE)
    with open(day, 'wb') as f:
        f.write(made.stdout)
    legs = sum(1 for line in made.stdout.splitlines() if line.startswith(b'(:leg '))
    runs = [subprocess.run(RUN + [prog, '--record', day], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                           timeout=DEADLINE)
            for env in (ENV, dict(ENV, SLIGHT_SCHED='3'))]
    first, again = runs[0].stdout, runs[1].stdout
    landed = sum(1 for line in first.splitlines() if line.startswith(b'(:flew '))
    check(f'a day of {legs} legs from airline-gen: {landed} land in the base, the same in a seeded order',
          made.returncode == 0 and all(r.returncode == 0 for r in runs) and legs > 1000 and landed == legs
          and first.endswith(f'(recorded {legs})\n'.encode()) and first == again,
          f'made {made.returncode}, recorded {[r.returncode for r in runs]}, the record ends {first[-200:]!r}')

    # What-ifs of the test's own, for each way a checkpoint is found: a hub
    # closed, and quick turns there after it, from the closure's own; a
    # small delay, rejoining early; then, from the history, a closure on
    # top of the small delay, from the base's after it rejoined, and a
    # delay on top of the closure, from the closure's own.
    text = (made.stdout + first).decode()
    hubs = next(line for line in text.splitlines() if line.startswith('; The hubs: '))[len('; The hubs: '):].rstrip('.').split()
    leg_lines = [line.split() for line in text.splitlines() if line.startswith('(:leg ')]
    late = next(l[1] for l in leg_lines if l[3] == f'"{hubs[1]}"' and int(l[5]) >= 900)
    base_day = '\n'.join(line for line in text.splitlines() if not line.startswith('(:branch ')) + '\n'
    kept_branches = [f'(:branch "c1" "base" (:close "{hubs[1]}" 660 780))',
                     f'(:branch "c2" "c1" (:quick-turn "{hubs[1]}" 780 900 25))',
                     f'(:branch "c3" "base" (:delay {leg_lines[0][1]} 15))']
    more_branches = [f'(:branch "c4" "c3" (:close "{hubs[0]}" 900 930))',
                     f'(:branch "c5" "c1" (:delay {late} 40))']
    first_day, more_day = os.path.join(DIR, 'airline-first.sexp'), os.path.join(DIR, 'airline-more.sexp')
    with open(first_day, 'w') as f:
        f.write(base_day + '\n'.join(kept_branches) + '\n')
    with open(more_day, 'w') as f:
        f.write(base_day + '\n'.join(kept_branches + more_branches) + '\n')
    kept = os.path.join(DIR, 'airline-kept.sexp')
    if os.path.exists(kept):
        os.remove(kept)

    def airline_run(*args):
        r = subprocess.run(RUN + [prog, *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=ENV, timeout=DEADLINE)
        return r.returncode, r.stdout.decode()

    def history(out):
        lines = out.splitlines()
        return lines[lines.index('the history:') + 1:-1] if 'the history:' in lines else []

    ran = [airline_run('--keep', kept, first_day), airline_run('--from', kept, more_day), airline_run('--every', '0', more_day)]
    keep_out, from_out, none_out = (out for _, out in ran)
    from_start = history(none_out)
    passed_over = [line for line in from_out.splitlines() if line.endswith(', kept')]
    check('what-ifs from checkpoints, and from a history kept by an earlier run, come to the same as from the start',
          all(status == 0 for status, _ in ran) and len(from_start) == 5
          and history(keep_out) == from_start[:3] and history(from_out) == from_start
          and len(passed_over) == 3 and from_out.splitlines()[-1].startswith('"0 minutes for the base'),
          f'exits {[status for status, _ in ran]}\n--- kept\n{keep_out}\n--- from the history\n{from_out}\n--- from the start\n{none_out}')

    # Random what-ifs on the small day in two rounds, the second from the
    # history the first kept, come to what they do in one batch (D214):
    # r3-20 branches from r3-5, whose checkpoints weren't kept, since
    # nothing in the first round branched from it.
    rounds = os.path.join(DIR, 'airline-rounds.sexp')
    if os.path.exists(rounds):
        os.remove(rounds)
    small = 'examples/airline-day.sexp'
    ran = [airline_run('--random', '10', '--seed', '3', '--keep', rounds, small),
           airline_run('--random', '20', '--seed', '3', '--from', rounds, small),
           airline_run('--random', '20', '--seed', '3', small)]
    second, whole = ran[1][1], ran[2][1]
    check('random what-ifs in two rounds, the second from the history the first kept, come to what they do in one',
          all(status == 0 for status, _ in ran) and len(history(whole)) == 20 and history(second) == history(whole)
          and sum(1 for line in second.splitlines() if line.endswith(', kept')) == 10
          and 'r3-20, from r3-5: ' in second,
          f'exits {[status for status, _ in ran]}\n--- the second round\n{second}\n--- in one\n{whole}')

    # The same what-ifs in three shares, each run apart, report every
    # what-if as the whole run does, and simulate as many minutes (D218).
    def reports(out):
        return {m.group(1): re.sub(r'^    time: .*\n', '', m.group(0), flags=re.M)
                for m in re.finditer(r'^(r\d+-\d+), from .*\n(?:    .*\n)*', out, re.M)}

    def branch_minutes(out):
        m = re.search(r'and (\d+) for its branches, of which (\d+) their own"$', out, re.M)
        return (int(m.group(1)), int(m.group(2))) if m else None

    status_one, one = airline_run('--random', '40', '--seed', '7', small)
    shares = [airline_run('--random', '40', '--seed', '7', '--shard', f'{k}/3', small) for k in (1, 2, 3)]
    together = {}
    for _, out in shares:
        together.update(reports(out))
    counted = [branch_minutes(out) for _, out in shares]
    check('random what-ifs in three shares, run apart, come to what they do in one run',
          status_one == 0 and all(status == 0 for status, _ in shares) and len(together) == 40
          and together == reports(one) and None not in counted and tuple(map(sum, zip(*counted))) == branch_minutes(one),
          f'exits {status_one} and {[status for status, _ in shares]}, {len(together)} what-ifs, '
          f'minutes {counted} against {branch_minutes(one)}')


os.makedirs(DIR, exist_ok=True)
keys()
memory()
chat()
airline()
print(f'live: {failed} failed')
sys.exit(1 if failed else 0)
