# Writes t/271-float-to-str.slight and its expected output (D221): each
# case's toFixed from Python's Decimal, which holds a float's exact value,
# rounded half away from zero, and checked against V8's own toFixed for
# the cases it covers.
#
#     python3 t/models/float-to-str.py
import json, subprocess, importlib.util
from decimal import Decimal, ROUND_HALF_UP
spec = importlib.util.spec_from_file_location('m', 't/models/strings.py'); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
# (slight literal, its value, digits or None)
cases = [('2.5', 2.5, 0), ('-2.5', -2.5, 0), ('0.5', 0.5, 0), ('1.5', 1.5, 0), ('0.125', 0.125, 2), ('1.005', 1.005, 2),
         ('1.45', 1.45, 1), ('8.345', 8.345, 2), ('99.995', 99.995, 2), ('9.99', 9.99, 1), ('123.456', 123.456, 1),
         ('-0.001', -0.001, 2), ('-0.0', -0.0, 2), ('0.0', 0.0, 0), ('1.0e-10', 1e-10, 3), ('0.000001', 0.000001, 7),
         ('1234.5678', 1234.5678, 10), ('1.0e20', 1e20, 2), ('1.0e21', 1e21, 2), ('-1.0e21', -1e21, 0),
         ('math/PI', 3.141592653589793, 20), ('math/PI', 3.141592653589793, 0), ('5', 5, 3), ('-7', -7, 1),
         ('math/INF', float('inf'), 2), ('(math/sqrt -1)', float('nan'), 2),
         ('1.0', 1.0, None), ('5', 5, None), ('0.1', 0.1, None), ('1.0e21', 1e21, None)]
def model(x, f):
    if f is None or x != x or abs(x) >= 1e21:
        return m.number(float(x))
    sign = '-' if x < 0 else ''
    return sign + format(Decimal(abs(x)).quantize(Decimal(1).scaleb(-f), rounding=ROUND_HALF_UP), 'f')
want = [model(x, f) for _, x, f in cases]
finite = [(x, f) for _, x, f in cases if f is not None and x == x and abs(x) < 1e21]
js = subprocess.run(['node', '-e', 'const c=JSON.parse(require("fs").readFileSync(0,"utf8")); console.log(JSON.stringify(c.map(([x,f])=>x.toFixed(f))))'],
                    input=json.dumps(finite), capture_output=True, text=True, check=True)
mine = [model(x, f) for x, f in finite]
assert json.loads(js.stdout) == mine, list(zip(json.loads(js.stdout), mine))
src = ["""; float->str (D221): a float as str shows it; or, with digits from 0 to
; 100, in fixed notation, as JavaScript's toFixed: the nearest such
; decimal to the float's exact value, so 1.005, just under 1.005, is
; 1.00, and a tie goes away from zero, so 2.5 is 3. An integer is the
; float it would be. Past 1e21, nan and inf are as str shows them.
"""]
for lit, x, f in cases:
    src.append(f'(pprint (float->str {lit}{"" if f is None else " " + str(f)}))')
src.append("""(defun kind (pid) (car (cadr (join pid))))
(pprint (kind (fork (float->str 1.5 101))))
(pprint (kind (fork (float->str 1.5 -1))))
(pprint (kind (fork (float->str 1.5 2.0))))
(kind (fork (float->str "1.5")))""")
text = '\n'.join(src) + '\n'
open('t/271-float-to-str.slight', 'w').write(text)
lines = text.split('\n')
line = lambda t: next(i + 1 for i, l in enumerate(lines) if t in l)
site = lambda t, col: f'(float->str at t/271-float-to-str.slight:{line(t)}:{col})'
string_arg = '(float->str "1.5")'
out = [m.quote(w.encode()).decode() for w in want] + [
    f'fault in #<pid 2>: out of range: 101 {site("1.5 101", 21)}', 'out-of-range',
    f'fault in #<pid 3>: out of range: -1 {site("1.5 -1", 21)}', 'out-of-range',
    f'fault in #<pid 4>: not an integer: 2.0 {site("1.5 2.0", 21)}', 'not-an-int',
    f'fault in #<pid 5>: not a number: "1.5" {site(string_arg, 13)}', 'not-a-number']
open('t/271-float-to-str.expected', 'w').write('\n'.join(out) + '\n')
print('V8 agrees on', len(mine), 'cases')
