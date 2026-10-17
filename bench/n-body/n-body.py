# n-body, after the C version: a body is a list [x, y, z, vx, vy, vz, mass].
import math
import sys

PI = 3.141592653589793
SOLAR_MASS = 4 * PI * PI
YEAR = 365.24


def planet(x, y, z, vx, vy, vz, mass):
    return [x, y, z, vx * YEAR, vy * YEAR, vz * YEAR, mass * SOLAR_MASS]


bodies = [
    [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, SOLAR_MASS],
    planet(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
           1.66007664274403694e-03, 7.69901118419740425e-03, -6.90460016972063023e-05,
           9.54791938424326609e-04),
    planet(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
           -2.76742510726862411e-03, 4.99852801234917238e-03, 2.30417297573763929e-05,
           2.85885980666130812e-04),
    planet(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
           2.96460137564761618e-03, 2.37847173959480950e-03, -2.96589568540237556e-05,
           4.36624404335156298e-05),
    planet(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
           2.68067772490389322e-03, 1.62824170038242295e-03, -9.51592254519715870e-05,
           5.15138902046611451e-05),
]
PAIRS = [(bodies[i], bodies[j]) for i in range(len(bodies)) for j in range(i + 1, len(bodies))]


def advance(dt):
    for b, o in PAIRS:
        dx, dy, dz = b[0] - o[0], b[1] - o[1], b[2] - o[2]
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        mag = dt / (dist * dist * dist)
        b[3] -= dx * o[6] * mag
        b[4] -= dy * o[6] * mag
        b[5] -= dz * o[6] * mag
        o[3] += dx * b[6] * mag
        o[4] += dy * b[6] * mag
        o[5] += dz * b[6] * mag
    for b in bodies:
        b[0] += dt * b[3]
        b[1] += dt * b[4]
        b[2] += dt * b[5]


def energy():
    e = 0.0
    for i, b in enumerate(bodies):
        e += 0.5 * b[6] * (b[3] * b[3] + b[4] * b[4] + b[5] * b[5])
        for o in bodies[i + 1:]:
            dx, dy, dz = b[0] - o[0], b[1] - o[1], b[2] - o[2]
            e -= (b[6] * o[6]) / math.sqrt(dx * dx + dy * dy + dz * dz)
    return e


def offset():
    px = py = pz = 0.0
    for b in bodies:
        px += b[3] * b[6]
        py += b[4] * b[6]
        pz += b[5] * b[6]
    bodies[0][3] = -px / SOLAR_MASS
    bodies[0][4] = -py / SOLAR_MASS
    bodies[0][5] = -pz / SOLAR_MASS


def shown(e):
    return math.floor(e * 1e9 + 0.5)


offset()
print(shown(energy()))
for _ in range(int(sys.argv[1])):
    advance(0.01)
print(shown(energy()))
