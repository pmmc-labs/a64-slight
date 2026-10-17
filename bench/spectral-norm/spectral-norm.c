// spectral-norm, the Benchmarks Game's, single-threaded.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double a(int i, int j) {
    return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1);
}

static void times_a(int n, const double *u, double *v) {
    for (int i = 0; i < n; i++) {
        double s = 0;
        for (int j = 0; j < n; j++) s += a(i, j) * u[j];
        v[i] = s;
    }
}

static void times_at(int n, const double *u, double *v) {
    for (int i = 0; i < n; i++) {
        double s = 0;
        for (int j = 0; j < n; j++) s += a(j, i) * u[j];
        v[i] = s;
    }
}

static void times_ata(int n, const double *u, double *v, double *w) {
    times_a(n, u, w);
    times_at(n, w, v);
}

int main(int argc, char **argv) {
    int     n = atoi(argv[1]);
    double *u = malloc(n * sizeof *u), *v = malloc(n * sizeof *v), *w = malloc(n * sizeof *w);
    for (int i = 0; i < n; i++) u[i] = 1;
    for (int i = 0; i < 10; i++) {
        times_ata(n, u, v, w);
        times_ata(n, v, u, w);
    }
    double vbv = 0, vv = 0;
    for (int i = 0; i < n; i++) {
        vbv += u[i] * v[i];
        vv  += v[i] * v[i];
    }
    printf("%.0f\n", floor(sqrt(vbv / vv) * 1e9 + 0.5));
    return 0;
}
