# The expected output of examples/key-catcher.slight, worked out from the
# example's rules, for the keys on its "; stdin:" line: a shifted arrow
# paints the cell under the cursor in the current colour and then moves,
# an alt one clears it; a digit 1-7 picks colour 40 + digit. Ctrl-C ends
# the program with exit status 130, and run.sh adds "exit: 130".
#
#     python3 t/models/key-catcher.py > examples/key-catcher.expected
E = '\x1b'
keys = [('ArrowRight', ['shift']), ('3', []), ('ArrowRight', ['shift']), ('ArrowDown', ['shift']),
        ('ArrowLeft', []), ('ArrowLeft', ['alt']), ('x', ['alt'])]
moves = {'ArrowUp': 'A', 'ArrowDown': 'B', 'ArrowRight': 'C', 'ArrowLeft': 'D'}
out, color = [], 47
for key, mods in keys:
    if mods[:1] == ['shift']: out.append(f'{E}[{color}m {E}[D')
    if mods[:1] == ['alt']:   out.append(f'{E}[49m {E}[D')
    if key in moves:          out.append(f'{E}[{moves[key]}')
    if key.isdigit() and 1 <= int(key) <= 7: color = 40 + int(key)
print(''.join(out) + 'exit: 130')
