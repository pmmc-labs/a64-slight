import sys


def tak(x, y, z):
    return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y)) if y < x else z


n = int(sys.argv[1])
print(tak(3 * n, 2 * n, n))
