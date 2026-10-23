# The index timer/cancel finds after's timers by (process.c, D232), and
# t/288-timer-cancel-churn.slight's sets and cancels: how many cancels
# find their timer, with the table as it is, and with one that leaves a
# gap where a timer was taken back, as the test's check that it reaches
# what it's meant to; and the longest probe, to show the numbers meet.
M = (1 << 64) - 1
C = 0x9E3779B97F4A7C15
class T:
    def __init__(s, shift=True): s.ids, s.cap, s.n, s.shift, s.maxprobe = [], 0, 0, shift, 0
    def home(s, k): return (k * C & M) & (s.cap - 1)
    def slot(s, k):
        i, p = s.home(k), 0
        while s.ids[i] and s.ids[i] != k: i = (i + 1) & (s.cap - 1); p += 1
        s.maxprobe = max(s.maxprobe, p)
        return i
    def add(s, k):
        if (s.n + 1) * 2 > s.cap:
            old, s.cap = s.ids, (s.cap * 2 if s.cap else 64)
            s.ids = [0] * s.cap
            for x in old:
                if x: s.ids[s.slot(x)] = x
        s.ids[s.slot(k)] = k; s.n += 1
    def has(s, k): return s.cap and s.ids[s.slot(k)] == k
    def drop(s, k):
        m, i = s.cap - 1, s.slot(k)
        j = (i + 1) & m
        while s.ids[j]:
            if not s.shift: break
            if ((j - s.home(s.ids[j])) & m) < ((j - i) & m): j = (j + 1) & m; continue
            s.ids[i] = s.ids[j]; i = j; j = (j + 1) & m
        s.ids[i] = 0; s.n -= 1
    def cancel(s, k):
        if not s.has(k): return False
        s.drop(k); return True

def pattern(t):
    # 40 that stay, then rounds: 80 more set, then taken back in a
    # scrambled order; then the 40, last first
    ok, nid, long = 0, 0, []
    for _ in range(40): nid += 1; t.add(nid); long.append(nid)
    for r in range(50):
        block = []
        for _ in range(80): nid += 1; t.add(nid); block.append(nid)
        for i in range(80): ok += t.cancel(block[(i * 37) % 80])
    for k in reversed(long): ok += t.cancel(k)
    return ok, nid
for shift in (True, False):
    t = T(shift); print('shift' if shift else 'no shift', pattern(t), 'cap', t.cap, 'max probe', t.maxprobe)
