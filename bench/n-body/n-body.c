// n-body, the Benchmarks Game's plain C version.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI          3.141592653589793
#define SOLAR_MASS  (4 * PI * PI)
#define YEAR        365.24

typedef struct {
    double x, y, z, vx, vy, vz, mass;
} body;

static body bodies[5] = {
    { 0, 0, 0, 0, 0, 0, SOLAR_MASS },
    { 4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
      1.66007664274403694e-03 * YEAR, 7.69901118419740425e-03 * YEAR, -6.90460016972063023e-05 * YEAR,
      9.54791938424326609e-04 * SOLAR_MASS },
    { 8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
      -2.76742510726862411e-03 * YEAR, 4.99852801234917238e-03 * YEAR, 2.30417297573763929e-05 * YEAR,
      2.85885980666130812e-04 * SOLAR_MASS },
    { 1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
      2.96460137564761618e-03 * YEAR, 2.37847173959480950e-03 * YEAR, -2.96589568540237556e-05 * YEAR,
      4.36624404335156298e-05 * SOLAR_MASS },
    { 1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
      2.68067772490389322e-03 * YEAR, 1.62824170038242295e-03 * YEAR, -9.51592254519715870e-05 * YEAR,
      5.15138902046611451e-05 * SOLAR_MASS },
};

static void advance(double dt) {
    for (int i = 0; i < 5; i++) {
        body *b = &bodies[i];
        for (int j = i + 1; j < 5; j++) {
            body  *o    = &bodies[j];
            double dx   = b->x - o->x, dy = b->y - o->y, dz = b->z - o->z;
            double dist = sqrt(dx * dx + dy * dy + dz * dz);
            double mag  = dt / (dist * dist * dist);
            b->vx -= dx * o->mass * mag;
            b->vy -= dy * o->mass * mag;
            b->vz -= dz * o->mass * mag;
            o->vx += dx * b->mass * mag;
            o->vy += dy * b->mass * mag;
            o->vz += dz * b->mass * mag;
        }
    }
    for (int i = 0; i < 5; i++) {
        body *b = &bodies[i];
        b->x += dt * b->vx;
        b->y += dt * b->vy;
        b->z += dt * b->vz;
    }
}

static double energy(void) {
    double e = 0;
    for (int i = 0; i < 5; i++) {
        body *b = &bodies[i];
        e += 0.5 * b->mass * (b->vx * b->vx + b->vy * b->vy + b->vz * b->vz);
        for (int j = i + 1; j < 5; j++) {
            body  *o  = &bodies[j];
            double dx = b->x - o->x, dy = b->y - o->y, dz = b->z - o->z;
            e -= (b->mass * o->mass) / sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    return e;
}

static void offset(void) {
    double px = 0, py = 0, pz = 0;
    for (int i = 0; i < 5; i++) {
        px += bodies[i].vx * bodies[i].mass;
        py += bodies[i].vy * bodies[i].mass;
        pz += bodies[i].vz * bodies[i].mass;
    }
    bodies[0].vx = -px / SOLAR_MASS;
    bodies[0].vy = -py / SOLAR_MASS;
    bodies[0].vz = -pz / SOLAR_MASS;
}

int main(int argc, char **argv) {
    int n = atoi(argv[1]);
    offset();
    printf("%.0f\n", floor(energy() * 1e9 + 0.5));
    for (int i = 0; i < n; i++) advance(0.01);
    printf("%.0f\n", floor(energy() * 1e9 + 0.5));
    return 0;
}
