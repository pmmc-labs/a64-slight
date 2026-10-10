# The expected output of t/190-gc-schedule.slight, from a model of the
# scheduler that shares no code with the runtime: a FIFO run queue (D86),
# and a slice of QUOTA calls, so a process is preempted at the entry of
# its QUOTA-th, 2*QUOTA-th, ... call, counted from its start, before that
# call's body runs, and the next ready process runs. Collecting happens at
# entries too, but takes no turn (D166), so it isn't in the model.
#
# Each of a and b makes N + 2 calls: the fork's own function, then its
# loop for i = 0 .. N; the loop writes its letter for i < N. The root
# forks a, then b, and waits in join, so a runs first.
#
#     python3 t/models/gc-schedule.py > t/190-gc-schedule.expected
from collections import deque

QUOTA, N = 1000, 3000

def process(letter):
    for call in range(1, N + 3):
        yield 'entry'
        if 2 <= call <= N + 1:
            yield letter

out, queue = [], deque([process('a'), process('b')])
calls = {}
while queue:
    p = queue.popleft()
    for step in p:
        if step == 'entry':
            calls[p] = calls.get(p, 0) + 1
            if calls[p] % QUOTA == 0 and queue:
                queue.append(p)
                break
        else:
            out.append(step)
print(''.join(out))
print('(ok b-done)')
