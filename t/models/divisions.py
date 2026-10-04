# The expected output of examples/divisions.slight, worked out from the
# example's rules, for the keys on its "; stdin:" line (2, 5, then q). The
# screen is 24 by 80, as it is when stdout isn't a terminal. The root's
# value, (), comes last.
#
#     python3 t/models/divisions.py > examples/divisions.expected
from math import ceil
E = '\x1b'
rows, cols = 24 - 10, 80 - 1
out = []
def draw(x, y, h, w, color):
    out.append(f'{E}[u')
    for i in range(h):
        out.append(f'{E}[{x + i};{y}H{E}[37;{color}m' + ' ' * w + f'{E}[0m')
    out.append(f'{E}[s')
for key in '25':
    out.append(f'{E}[1J')
    if key == '2':
        mid = ceil(cols * 0.5)
        draw(1, 1, rows, mid - 1, 42)
        draw(1, mid + 2, rows, mid, 43)
    if key == '5':
        mid_w, mid_h = ceil(cols * 0.5), ceil(rows * 0.5)
        qua_h, qua_w = ceil(mid_h * 0.5), ceil(mid_w * 0.5)
        draw(1, 1, rows, mid_w - 1, 42)
        draw(1, mid_w + 2, mid_h - 1, mid_w, 43)
        draw(mid_h + 1, mid_w + 2, qua_h - 1, mid_w, 44)
        draw(mid_h + qua_h + 1, mid_w + 2, qua_h - 1, qua_w - 2, 45)
        draw(mid_h + qua_h + 1, mid_w + qua_w + 2, qua_h - 1, qua_w, 46)
out.append(f'rows:{rows}cols:{cols}{E}[{rows + 5};0H')
print(''.join(out) + '()')
