// binary-trees, single-threaded, a node per malloc, freed when done with.
#include <stdio.h>
#include <stdlib.h>

typedef struct node {
    struct node *l, *r;
} node;

static node *make(int d) {
    node *n = malloc(sizeof *n);
    if (d == 0) {
        n->l = n->r = NULL;
    } else {
        n->l = make(d - 1);
        n->r = make(d - 1);
    }
    return n;
}

static long check(const node *n) {
    return n->l ? 1 + check(n->l) + check(n->r) : 1;
}

static void drop(node *n) {
    if (n->l) {
        drop(n->l);
        drop(n->r);
    }
    free(n);
}

int main(int argc, char **argv) {
    int   top = atoi(argv[1]) > 6 ? atoi(argv[1]) : 6;
    node *t   = make(top + 1);
    printf("%ld\n", check(t));
    drop(t);
    node *long_lived = make(top);
    for (int d = 4; d <= top; d += 2) {
        long iterations = 1L << (top - d + 4), sum = 0;
        for (long i = 0; i < iterations; i++) {
            t    = make(d);
            sum += check(t);
            drop(t);
        }
        printf("%ld\n", sum);
    }
    printf("%ld\n", check(long_lived));
    return 0;
}
