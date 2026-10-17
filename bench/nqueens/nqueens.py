# nqueens, after Gabriel's: the lists are Python's lists, copied where the
# list version makes new lists.
import sys


def ok(row, dist, placed):
    for q in placed:
        if q == row + dist or q == row - dist:
            return False
        dist += 1
    return True


# x the rows still to try, y those passed over for this column, z those
# placed, the last placed first.
def try_it(x, y, z):
    if not x:
        return 0 if y else 1
    q, rest = x[0], x[1:]
    return (try_it(rest + y, [], [q] + z) if ok(q, 1, z) else 0) + try_it(rest, [q] + y, z)


print(try_it(list(range(1, int(sys.argv[1]) + 1)), [], []))
