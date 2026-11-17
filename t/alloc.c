// The arena (runtime/alloc.c, D296) on its own, built and run by `make
// test` for the machine itself, and under ThreadSanitizer by `make tsan`:
// four threads, each an island with an arena, make blocks of every kind
// (a size class's, a run's, one mapped alone), fill each with a pattern
// of its own address and size, and free them, there or on another island,
// which checks the pattern first; and now and then take their returns and
// wait, as an island does. A block given out twice, or cut short, or
// freed into the wrong span, spoils a pattern. ROUNDS in the environment
// says how long (100,000 a thread without it).

#include "../runtime/rt.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISLANDS 4

typedef struct {
    void  *at;
    size_t bytes;
} block_t;

// What each island has been sent by the others, to check and free.
static struct {
    pthread_mutex_t lock;
    block_t        *blocks;
    size_t          n, cap;
} box[ISLANDS];

static _Atomic long wakes;

void rt_arena_wake(uint64_t island) {
    (void)island;
    atomic_fetch_add(&wakes, 1);
}

static uint64_t next(uint64_t *seed) {     // splitmix64
    uint64_t z = (*seed += 0x9e3779b97f4a7c15u);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

static uint64_t word(const void *at, size_t bytes, size_t i) {
    return (uint64_t)(uintptr_t)at ^ (i * 0x9e3779b9u) ^ bytes;
}

static void fill(void *at, size_t bytes) {
    uint64_t *w = at;
    for (size_t i = 0; i < bytes / 8; i++) w[i] = word(at, bytes, i);
}

static void check(const void *at, size_t bytes) {
    const uint64_t *w = at;
    if ((uintptr_t)at % 16) {
        fprintf(stderr, "alloc: %p isn't 16-aligned\n", at);
        exit(1);
    }
    for (size_t i = 0; i < bytes / 8; i++) {
        if (w[i] != word(at, bytes, i)) {
            fprintf(stderr, "alloc: a block of %zu bytes at %p is spoilt at %zu\n", bytes, at, i * 8);
            exit(1);
        }
    }
}

// Mostly a message's size, some a chunk's, a few a run's, and a very few
// more than the runs kept.
static size_t pick(uint64_t *seed) {
    uint64_t r = next(seed) % 100;
    size_t   n = r < 50 ? 16 + next(seed) % 200 : r < 80 ? 16 + next(seed) % 2000 : r < 93 ? 16 + next(seed) % 20000
               : r < 99 ? 20000 + next(seed) % 300000 : 300000 + next(seed) % 3000000;
    return (n + 15) & ~(size_t)15;
}

static void push(block_t **blocks, size_t *n, size_t *cap, block_t b) {
    if (*n == *cap) {
        *cap    = *cap ? *cap * 2 : 256;
        *blocks = rt_must(realloc(*blocks, *cap * sizeof **blocks));
    }
    (*blocks)[(*n)++] = b;
}

static void *island(void *arg) {
    uint64_t    me = (uint64_t)(uintptr_t)arg, seed = me * 7919 + 1;
    rt_arena_t *a  = rt_must(calloc(1, sizeof *a));
    block_t    *mine = NULL;
    size_t      n = 0, cap = 0;
    const char *asked = getenv("ROUNDS");
    long        rounds = asked ? atol(asked) : 100000;
    a->island = me;
    for (long round = 0; round < rounds; round++) {
        uint64_t r = next(&seed) % 100;
        if (r < 45 || n == 0) {
            size_t bytes = pick(&seed);
            void  *at    = rt_block_new(a, bytes);
            fill(at, bytes);
            push(&mine, &n, &cap, (block_t){ at, bytes });
        } else if (r < 80) {
            size_t i = next(&seed) % n;
            check(mine[i].at, mine[i].bytes);
            rt_block_free(a, mine[i].at);
            mine[i] = mine[--n];
        } else if (r < 92) {                    // to another island, or to itself
            size_t   i = next(&seed) % n;
            uint64_t k = next(&seed) % ISLANDS;
            pthread_mutex_lock(&box[k].lock);
            push(&box[k].blocks, &box[k].n, &box[k].cap, mine[i]);
            pthread_mutex_unlock(&box[k].lock);
            mine[i] = mine[--n];
        } else {                                // what it's been sent: checked, and freed here
            pthread_mutex_lock(&box[me].lock);
            block_t *got = box[me].blocks;
            size_t   m   = box[me].n;
            box[me].blocks = NULL;
            box[me].n = box[me].cap = 0;
            pthread_mutex_unlock(&box[me].lock);
            for (size_t i = 0; i < m; i++) {
                check(got[i].at, got[i].bytes);
                rt_block_free(a, got[i].at);
            }
            free(got);
        }
        if (rt_arena_pending(a)) rt_arena_returns(a);
        if (round % 5000 == 4999) rt_arena_idle(a);
        if (n > 3000) {                         // the older half, so that spans empty and runs go back
            for (size_t i = 0; i < n / 2; i++) {
                check(mine[i].at, mine[i].bytes);
                rt_block_free(a, mine[i].at);
            }
            memmove(mine, mine + n / 2, (n - n / 2) * sizeof *mine);
            n -= n / 2;
        }
    }
    for (size_t i = 0; i < n; i++) {
        check(mine[i].at, mine[i].bytes);
        rt_block_free(a, mine[i].at);
    }
    rt_arena_idle(a);
    free(mine);
    return NULL;
}

void *rt_must(void *p) {
    if (!p) {
        fputs("alloc: out of memory\n", stderr);
        exit(2);
    }
    return p;
}

int main(void) {
    pthread_t threads[ISLANDS];
    for (int i = 0; i < ISLANDS; i++) pthread_mutex_init(&box[i].lock, NULL);
    for (int i = 0; i < ISLANDS; i++) pthread_create(&threads[i], NULL, island, (void *)(uintptr_t)i);
    for (int i = 0; i < ISLANDS; i++) pthread_join(threads[i], NULL);
    if (atomic_load(&wakes) == 0) {
        fputs("alloc: no island sent a block home\n", stderr);
        return 1;
    }
    puts("alloc: ok");
    return 0;
}
