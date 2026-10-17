// deriv, after Gabriel's: an expression is a tree of cells, each symbol
// one cell (so compared by address), allocated from an arena that's
// emptied after each derivative.
#include <stdio.h>
#include <stdlib.h>

typedef struct cell cell;
struct cell {
    enum { NUMBER, SYMBOL, PAIR } kind;
    long  number;
    cell *car, *cdr;                    // a pair's; the empty list is NULL
};

static cell X = { SYMBOL }, A = { SYMBOL }, B = { SYMBOL };
static cell PLUS = { SYMBOL }, MINUS = { SYMBOL }, TIMES = { SYMBOL }, DIVIDE = { SYMBOL };
static cell ZERO = { NUMBER, 0 }, ONE = { NUMBER, 1 }, THREE = { NUMBER, 3 }, FIVE = { NUMBER, 5 };

static cell   arena[4096];
static size_t used;

static cell *cons(cell *car, cell *cdr) {
    if (used == sizeof arena / sizeof *arena) abort();
    cell *c = &arena[used++];
    *c = (cell){ PAIR, 0, car, cdr };
    return c;
}

static cell *list3(cell *a, cell *b, cell *c) { return cons(a, cons(b, cons(c, NULL))); }
static cell *list4(cell *a, cell *b, cell *c, cell *d) { return cons(a, list3(b, c, d)); }

static cell *deriv(cell *a);

static cell *map_deriv(cell *l) {
    if (!l) return NULL;
    cell *d = deriv(l->car);
    return cons(d, map_deriv(l->cdr));
}

static cell *map_quotient(cell *l) {
    if (!l) return NULL;
    cell *q = list3(&DIVIDE, deriv(l->car), l->car);
    return cons(q, map_quotient(l->cdr));
}

static cell *deriv(cell *a) {
    if (a->kind != PAIR) return a == &X ? &ONE : &ZERO;
    cell *op = a->car;
    if (op == &PLUS || op == &MINUS) return cons(op, map_deriv(a->cdr));
    if (op == &TIMES) return list3(&TIMES, a, cons(&PLUS, map_quotient(a->cdr)));
    if (op == &DIVIDE) {
        cell *u = a->cdr->car, *v = a->cdr->cdr->car;
        return list3(&MINUS, list3(&DIVIDE, deriv(u), v), list3(&DIVIDE, u, list4(&TIMES, v, v, deriv(v))));
    }
    abort();
}

static long length(const cell *l) {
    long n = 0;
    for (; l; l = l->cdr) n++;
    return n;
}

static long atoms(const cell *a) {
    if (a->kind != PAIR) return 1;
    long n = 0;
    for (; a; a = a->cdr) n += atoms(a->car);
    return n;
}

int main(int argc, char **argv) {
    cell  *e    = cons(&PLUS, list4(list4(&TIMES, &THREE, &X, &X), list4(&TIMES, &A, &X, &X), list3(&TIMES, &B, &X), &FIVE));
    size_t base = used;
    long   n    = atol(argv[1]), total = 0;
    cell  *last = NULL;
    for (long i = 0; i < n; i++) {
        used  = base;
        last  = deriv(e);
        total += length(last);
    }
    printf("%ld\n%ld\n", total, atoms(last));
    return 0;
}
