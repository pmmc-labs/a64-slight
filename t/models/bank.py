# t/295's bank, by Kiefer and Wolfowitz's recursion: each customer, in
# order of arrival, takes the teller free soonest, or waits for it; with
# whole milliseconds and the queue first come, first served, that's what
# any order of the events at one moment comes to. Prints the expected
# output: the customers, their waits in all and how many waited, then the
# time the last teller came free, once alone and once with a listener open.
import heapq

def bank(n):
    free, x, t, total, delayed, last = [0, 0, 0], 1, 0, 0, 0, 0
    for _ in range(n):
        y = 48271 * x % 2147483647
        x = 48271 * y % 2147483647
        start = max(t, heapq.heappop(free))
        total += start - t
        delayed += start > t
        done = start + 1 + y % 5399
        last = max(last, done)
        heapq.heappush(free, done)
        t += 1 + x % 1999
    return '(%d %d %d) %d' % (n, total, delayed, last)

r = bank(2000)
print('(%s)' % r)
print('(ok (%s))' % r)
print('done')
