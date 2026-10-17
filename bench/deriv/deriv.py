# deriv, after Gabriel's: an expression is a list, a symbol a string.
import sys


def deriv(a):
    if not isinstance(a, list):
        return 1 if a == 'x' else 0
    op = a[0]
    if op == '+' or op == '-':
        return [op] + [deriv(e) for e in a[1:]]
    if op == '*':
        return ['*', a, ['+'] + [['/', deriv(e), e] for e in a[1:]]]
    if op == '/':
        return ['-', ['/', deriv(a[1]), a[2]], ['/', a[1], ['*', a[2], a[2], deriv(a[2])]]]
    raise ValueError('no derivative of ' + op)


def atoms(a):
    return sum(atoms(e) for e in a) if isinstance(a, list) else 1


e = ['+', ['*', 3, 'x', 'x'], ['*', 'a', 'x', 'x'], ['*', 'b', 'x'], 5]
total, last = 0, []
for _ in range(int(sys.argv[1])):
    last = deriv(e)
    total += len(last)
print(total)
print(atoms(last))
