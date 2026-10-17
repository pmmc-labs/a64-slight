// nqueens, after Gabriel's: the lists are arrays, copied where the list
// version makes new lists.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ok(int row, int dist, const int *placed, int n) {
    for (int i = 0; i < n; i++, dist++) {
        if (placed[i] == row + dist || placed[i] == row - dist) return 0;
    }
    return 1;
}

// x the rows still to try, y those passed over for this column, z those
// placed, the last placed first.
static long try_it(const int *x, int nx, const int *y, int ny, const int *z, int nz) {
    if (nx == 0) return ny == 0;
    long count = 0;
    if (ok(x[0], 1, z, nz)) {
        int x2[nx + ny], z2[nz + 1];          // x2 holds nx - 1 + ny, but an array can't be empty
        memcpy(x2, x + 1, (nx - 1) * sizeof *x);
        memcpy(x2 + nx - 1, y, ny * sizeof *y);
        z2[0] = x[0];
        memcpy(z2 + 1, z, nz * sizeof *z);
        count += try_it(x2, nx - 1 + ny, NULL, 0, z2, nz + 1);
    }
    int y2[ny + 1];
    y2[0] = x[0];
    memcpy(y2 + 1, y, ny * sizeof *y);
    return count + try_it(x + 1, nx - 1, y2, ny + 1, z, nz);
}

int main(int argc, char **argv) {
    int n = atoi(argv[1]), rows[n];
    for (int i = 0; i < n; i++) rows[i] = i + 1;
    printf("%ld\n", try_it(rows, n, NULL, 0, NULL, 0));
    return 0;
}
