# The expected output of examples/even-odd-actors.slight, from a model of
# the scheduler that shares no code with the runtime: a FIFO run queue
# (D86); a process runs until it waits for a message or ends; a message
# to a waiting process queues it; the program ends when nothing can run
# (D93), printing the root's value. Processes are generators that yield
# 'recv' and are sent the message.
#
#     python3 t/models/even-odd-actors.py > examples/even-odd-actors.expected
from collections import deque
out, queue, procs = [], deque(), {}

class P:
    def __init__(self, pid, gen):
        self.pid, self.gen, self.mail, self.state = pid, gen, deque(), 'ready'
        self.started = self.blocked = False
def pid_str(p): return f'#<pid {p}>'
def show(v):
    if isinstance(v, list): return '(' + ' '.join(show(x) for x in v) + ')'
    if isinstance(v, tuple): return pid_str(v[1])
    return str(v)
def send(to, msg):
    p = procs.get(to)
    if p is None or p.state == 'done': return
    p.mail.append(msg)
    if p.state == 'waiting':
        p.state = 'ready'; queue.append(p)
def spawn(pid, gen):
    p = P(pid, gen); procs[pid] = p; queue.append(p); return ('pid', pid)
def run(p):
    # a process woken by a message picks up at its recv with that message
    val = p.mail.popleft() if p.blocked else None
    p.blocked = False
    while True:
        try:
            req = p.gen.send(val) if p.started else next(p.gen)
            p.started = True
        except StopIteration as e:
            p.state = 'done'; p.result = e.value; return
        assert req == 'recv'
        if p.mail:
            val = p.mail.popleft()
        else:
            p.blocked = True; p.state = 'waiting'; return

def actor(me, kind, other_tag, reply):
    # is-it-<kind>?: print, then wait, reply, loop
    while True:
        out.append(f'running-is-it-{kind}?')
        msg = yield 'recv'
        out.append(show([f'is-it-{kind}?', 'got', msg]))
        if msg[0] != ('IS-EVEN?' if kind == 'even' else 'IS-ODD?'):
            return []
        n, ask = msg[1], msg[2]
        if n == 0: send(reply, ['IT-IS-EVEN!' if kind == 'even' else 'IT-IS-ODD!'])
        else:      send(ask[1], [other_tag, n - 1, ('pid', me)])

def root():
    ev = spawn(2, actor(2, 'even', 'IS-ODD?', 1))
    od = spawn(3, actor(3, 'odd', 'IS-EVEN?', 1))
    out.append(pid_str(1))
    for n in (21, 36, 57, 92): send(2, ['IS-EVEN?', n, od])
    answers = []
    for _ in range(4):
        answers.append((yield 'recv'))
    out.append(show(answers))
    send(2, ['STOP']); send(3, ['STOP'])
    return []

r = P(1, root()); procs[1] = r; queue.append(r)
while queue:
    p = queue.popleft(); p.state = 'running'; run(p)
out.append(show(procs[1].result) if procs[1].result != [] else '()')
print('\n'.join(out))
