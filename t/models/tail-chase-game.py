# The expected output of examples/tail-chase-game.slight, for the keys on
# its "; stdin:" line (right, right, down, q), on the virtual clock. Each
# move after the first forks a fader for the square left behind, carrying
# the new step count; it sleeps 1000 ms, 2000 more, then 3000 more,
# drawing a stage each time. On the virtual clock every key comes before
# the clock moves, and the faders all go to sleep at time 0 in the order
# they were forked, so at 1000, 3000 and 6000 ms they draw in that order.
# The root's value, (ok ()) from joining the chaser, comes last, once the
# faders are done.
#
#     python3 t/models/tail-chase-game.py > examples/tail-chase-game.expected
E = '\x1b'
h = w = 40
x, y, count = 10, 20, 0
out, faders = [f'{E}[?25l'], []
def draw():
    out.append(f'{E}[{x};{y}H@{E}[{h + 1};0Hsteps: {count}\r{E}[{h + 2};0Htails: {count * 3}\r')
draw()
for key in ['right', 'right', 'down']:
    left = (x, y)
    if key == 'right': y = min(y + 1, w)
    if key == 'down':  x = min(x + 1, h)
    count += 1
    faders.append((left, count))
    draw()
for stage, (mark, row) in enumerate([('*', h + 3), ('.', h + 4), (' ', h + 5)], 1):
    for (fx, fy), c in faders:
        out.append(f'{E}[{fx};{fy}H{mark}{E}[{row};0Hfade{stage}: {c}\r')
print(''.join(out) + '(ok ())')
