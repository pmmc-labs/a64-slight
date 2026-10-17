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

Nothing here depends on how long anything takes: each step waits for
what it expects to see, and DEADLINE seconds is only how long it waits
before it says the check failed.
"""

import fcntl
import os
import platform
import pty
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
        ('wide', None, 'format-num to a width of 4 GB'),
    ]
    for name, expected, what in cases:
        out, status, peak = peak_run(prog, [name])
        ok_out = out == expected if expected else out.startswith('(error (heap () "format-num at ')
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


os.makedirs(DIR, exist_ok=True)
keys()
memory()
chat()
print(f'live: {failed} failed')
sys.exit(1 if failed else 0)
