// bank in C (D241): not a process for each customer but the floor under
// any simulation engine, an event list. Two kinds of event, an arrival and
// a teller coming free, in a binary heap, and a ring of the customers
// waiting. The times come from Park and Miller's generator, as in
// bank.slight.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { int64_t t; int arrival; } event_t;
typedef struct { int64_t arrived, took; } waiting_t;

static event_t *heap;
static size_t   nheap;

static void push(int64_t t, int arrival) {
    size_t i = nheap++;
    while (i && heap[(i - 1) / 2].t > t) heap[i] = heap[(i - 1) / 2], i = (i - 1) / 2;
    heap[i] = (event_t){ t, arrival };
}

static event_t pop(void) {
    event_t top = heap[0], last = heap[--nheap];
    size_t  i   = 0;
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= nheap) break;
        if (c + 1 < nheap && heap[c + 1].t < heap[c].t) c++;
        if (heap[c].t >= last.t) break;
        heap[i] = heap[c], i = c;
    }
    heap[i] = last;
    return top;
}

static int64_t draw(int64_t x) { return 48271 * x % 2147483647; }

int main(int argc, char **argv) {
    int64_t    n = argc > 1 ? atoll(argv[1]) : 0, arrived = 0, total = 0, delayed = 0, x = 1;
    int        free_ = 3;
    size_t     cap = 1 << 16, head = 0, tail = 0;
    waiting_t *ring = malloc(cap * sizeof *ring);
    heap = malloc(16 * sizeof *heap);
    push(0, 1);
    while (nheap) {
        event_t e = pop();
        if (e.arrival) {
            int64_t y = draw(x), took = 1 + y % 5399;
            x = draw(y);
            if (free_ > 0) {
                free_--;
                push(e.t + took, 0);
            } else {
                if (tail - head == cap) {                   // full: twice the size, the waiting moved to its start
                    waiting_t *more = malloc(2 * cap * sizeof *more);
                    for (size_t i = 0; i < cap; i++) more[i] = ring[(head + i) % cap];
                    free(ring);
                    ring = more, head = 0, tail = cap, cap *= 2;
                }
                ring[tail++ % cap] = (waiting_t){ e.t, took };
            }
            if (++arrived < n) push(e.t + 1 + x % 1999, 1);
        } else if (head == tail) {
            free_++;
        } else {
            waiting_t w = ring[head++ % cap];
            total   += e.t - w.arrived;
            delayed += e.t > w.arrived;
            push(e.t + w.took, 0);
        }
    }
    printf("%lld\n%lld\n%lld\n", (long long)n, (long long)total, (long long)delayed);
    return 0;
}
