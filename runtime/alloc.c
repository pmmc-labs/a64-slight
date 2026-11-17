// alloc.c -- an island's memory for heap chunks and messages (D296).
//
// Every chunk of every heap, and every message, came from malloc, which
// keeps what it's given back: after a burst of processes, nearly all of
// the burst's memory stayed with the program (docs/memory-review-2026-11-13.md).
// And what suited malloc shaped the collector: on glibc a request of 1 KB
// made it gather up every freed message first (D295). So an arena for
// each island, which nothing else allocates from and no lock guards.
//
// Addresses come from the system in regions, and are carved into runs of
// whole units (64 KB), each with a header at its start, which the address
// of anything in its first unit finds. A block of 16,400 bytes or less is
// one of a size class's, cut from a span, a run of one unit: taken from
// the span's freed blocks, newest first, or else from the part of it never
// yet given out, so that a span fresh from the system is touched only as
// far as it's used. A bigger block is a run of its own. A span with
// nothing out, unless it's the one its class allocates from, is a free
// run again, for any class or any block; and free runs are kept by
// length, in arrays beside them, not lists through them, so that their
// pages can go back to the system while their addresses are kept: what's
// past RUNS_HELD of them, held between the islands, and all of an
// island's when it's about to wait with more than RUNS_KEPT (as a
// stack's pages go back, D291). A run longer than
// RT_ARENA_RUNS units is mapped and unmapped on its own.
//
// A block belongs to the island that made it, and a message's is often
// freed by another, which took it. That island keeps such blocks by
// their home, and sends each home's back in one push onto its stack of
// returns (rt_returns), a compare and swap, once there are HOME_BATCH of
// them or it's about to wait; and wakes the home island if it's waiting,
// since its own freeing is all that gives the memory back. An island
// takes its returns at each of its scheduler's decisions, and before it
// asks the system for more.
//
// Nothing here is the program's to see, so none of it is recorded: a
// replay may reuse other addresses than its run did.

#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define UNIT       ((size_t)64 << 10)   // a run's unit, and a span's size
#define REGION     ((size_t)64 << 20)   // addresses asked of the system at a time
#define SMALL_MAX  16400                // the biggest block cut from a span: 16 KB and a chunk's header, three to a span
#define RUNS_HELD  ((size_t)64 << 20)   // free runs in memory the islands may hold between them, at the most, before
                                        // they go back unasked (rt_arena_hold)
#define RUNS_KEPT  ((size_t)1 << 20)    // and what it may keep when it waits
#define HOME_BATCH 32                   // another island's blocks kept before they're sent home...
#define HOME_BYTES ((size_t)256 << 10)  // ...or this much of them

enum { CLASS_RUN = RT_ARENA_CLASSES, CLASS_ALONE };

// A run's header. A span's blocks follow it; a big block's bytes do.
typedef struct rt_span {
    uint32_t        island;             // whose arena it's from
    uint16_t        class;              // a span's size class; or CLASS_RUN, CLASS_ALONE
    uint16_t        used;               // a span's blocks given out
    uint32_t        units;              // a run's length
    uint32_t        block;              // a span's block size
    void           *free;               // its freed blocks, through their first words
    char           *bump, *end;         // those never given out
    struct rt_span *next, *prev;        // among its class's spans with a block free
    uint64_t        pad;
} span_t;

_Static_assert(sizeof(span_t) == 64, "a run's header is a cache line, and keeps what follows 16-aligned");

// Another island's blocks, waiting to go home: a chain through their first
// words, newest first.
typedef struct rt_home {
    void    *first, *last;
    uint32_t n;
    size_t   bytes;
} home_t;

rt_returns_t rt_returns[RT_ISLANDS_MAX];

// The units in free runs still in memory, every island's together, and
// the most there may be before an island that frees one gives its own
// back: the islands share the one allowance, so that one busy island has
// all of it, and twelve don't hold twelve.
static _Alignas(128) _Atomic size_t warm_all;
static size_t                       warm_max = RUNS_HELD / UNIT;

void rt_arena_hold(size_t bytes) {
    warm_max = (bytes < RUNS_HELD ? bytes : RUNS_HELD) / UNIT;
}

#ifdef __linux__
#define GIVE_BACK MADV_DONTNEED         // at once, so the resident set says what's held
#else
#define GIVE_BACK MADV_FREE             // the system's to take when it wants it
#endif

static _Noreturn void no_memory(void) {
    fputs("slight: out of memory\n", stderr);
    exit(2);
}

static span_t *span_of(const void *block) {
    return (span_t *)((uintptr_t)block & ~(uintptr_t)(UNIT - 1));
}

// The class of a block of `bytes` (SMALL_MAX or less), and its size. Up
// to 528 bytes, every multiple of 16; past that, eight to a doubling (an
// eighth wasted at the worst), each 16 more than the round number, so
// that a chunk of 1 KB, 2 KB, on to 16 KB, with its header, wastes
// nothing.
static unsigned class_of(size_t bytes) {
    if (bytes <= 528) return (unsigned)((bytes + 15) >> 4);
    size_t   x = bytes - 16;                                    // 512 < x <= 16384
    unsigned k = 63 - (unsigned)__builtin_clzll(x - 1);         // 2^k < x <= 2^(k+1)
    unsigned j = (unsigned)((x - ((size_t)1 << k) + ((size_t)1 << (k - 3)) - 1) >> (k - 3));    // 1 to 8
    return 34 + (k - 9) * 8 + (j - 1);
}

static size_t class_size(unsigned class) {
    if (class < 34) return (size_t)class << 4;
    unsigned k = 9 + (class - 34) / 8, j = 1 + (class - 34) % 8;
    return ((size_t)1 << k) + ((size_t)j << (k - 3)) + 16;
}

// --- runs ---------------------------------------------------------------------

// `units` of fresh addresses, UNIT-aligned.
static void *carve(rt_arena_t *a, size_t units) {
    size_t bytes = units * UNIT;
    if ((size_t)(a->end - a->next) < bytes) {
        int flags = MAP_PRIVATE | MAP_ANON;
#ifdef MAP_NORESERVE
        flags |= MAP_NORESERVE;         // only what's touched is memory
#endif
        char *base = mmap(NULL, REGION + UNIT, PROT_READ | PROT_WRITE, flags, -1, 0);
        if (base == MAP_FAILED) no_memory();
#ifdef MADV_NOHUGEPAGE
        madvise(base, REGION + UNIT, MADV_NOHUGEPAGE);  // so that what's touched is what's held, a page at a time, and
#endif                                                  // a run given back is given back
        a->next = (char *)(((uintptr_t)base + UNIT - 1) & ~(uintptr_t)(UNIT - 1));
        a->end  = a->next + REGION;     // what's left of the last region is lost: addresses, not memory
    }
    void *run = a->next;
    a->next += bytes;
    return run;
}

// A run of `units`, its header not yet written: one that's free, in
// memory if there is one, or a fresh one.
static span_t *run_get(rt_arena_t *a, size_t units) {
    rt_runs_t *r = &a->runs[units];
    if (!r->n && rt_arena_pending(a)) rt_arena_returns(a);      // what others freed, before more is asked for
    if (r->n) {
        void *run = r->at[--r->n];
        if (r->cold > r->n) {
            r->cold = r->n;
        } else {
            a->warm -= units;
            atomic_fetch_sub_explicit(&warm_all, units, memory_order_relaxed);
        }
        return run;
    }
    return carve(a, units);
}

// This island's free runs still in memory go back to the system, their
// addresses kept, till it holds `keep` units of them or fewer: the
// longest first, and of a length the ones freed longest ago, since the
// next wanted is the last freed.
static void runs_trim(rt_arena_t *a, size_t keep) {
    size_t gone = 0;
    for (size_t units = RT_ARENA_RUNS; units >= 1 && a->warm - gone > keep; units--) {
        rt_runs_t *r = &a->runs[units];
        while (r->cold < r->n && a->warm - gone > keep) {
            madvise(r->at[r->cold++], units * UNIT, GIVE_BACK);
            gone += units;
        }
    }
    atomic_fetch_sub_explicit(&warm_all, gone, memory_order_relaxed);
    a->warm -= gone;
}

static void run_put(rt_arena_t *a, span_t *run, size_t units) {
    rt_runs_t *r = &a->runs[units];
    if (r->n == r->cap) {
        size_t cap  = r->cap ? 2 * r->cap : 64;
        void **more = realloc(r->at, cap * sizeof *more);
        if (!more) {                    // nowhere to note it: its pages go back, and its addresses are lost
            madvise(run, units * UNIT, GIVE_BACK);
            return;
        }
        r->at  = more;
        r->cap = cap;
    }
    r->at[r->n++] = run;
    a->warm += units;
    // Past the allowance, an eighth of it goes back, of this island's: not
    // all, which a collector that frees a big heap's chunks would only
    // ask for again, a page fault each.
    size_t all = atomic_fetch_add_explicit(&warm_all, units, memory_order_relaxed) + units;
    if (all >= warm_max) {
        size_t over = all - warm_max + warm_max / 8;
        runs_trim(a, a->warm > over ? a->warm - over : 0);
    }
}

// A block too big for the free runs' lengths: a mapping of its own.
static void *alone_new(rt_arena_t *a, size_t units) {
    size_t bytes = units * UNIT;
    char  *base  = mmap(NULL, bytes + UNIT, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) no_memory();
    char *run = (char *)(((uintptr_t)base + UNIT - 1) & ~(uintptr_t)(UNIT - 1));
    if (run > base) munmap(base, (size_t)(run - base));
    munmap(run + bytes, (size_t)(base + UNIT - run));       // what's past it: a unit, less what was before
    span_t *s = (span_t *)run;
    *s = (span_t){ .island = (uint32_t)a->island, .class = CLASS_ALONE, .units = (uint32_t)units };
    return s + 1;
}

// --- spans --------------------------------------------------------------------

static void link_first(rt_arena_t *a, span_t *s) {
    s->prev = NULL;
    s->next = a->avail[s->class];
    if (s->next) s->next->prev = s;
    a->avail[s->class] = s;
}

// After the span allocated from, so that one stays the one.
static void link_second(rt_arena_t *a, span_t *s) {
    span_t *first = a->avail[s->class];
    if (!first) {
        link_first(a, s);
        return;
    }
    s->prev = first;
    s->next = first->next;
    if (s->next) s->next->prev = s;
    first->next = s;
}

static void unlink_span(rt_arena_t *a, span_t *s) {
    if (s->prev) s->prev->next = s->next;
    else         a->avail[s->class] = s->next;
    if (s->next) s->next->prev = s->prev;
}

// Whether every block of s is out.
static int full(const span_t *s) {
    return !s->free && (size_t)(s->end - s->bump) < s->block;
}

static __attribute__((noinline)) span_t *span_new(rt_arena_t *a, unsigned class) {
    span_t *s    = run_get(a, 1);
    size_t  size = class_size(class);
    *s = (span_t){
        .island = (uint32_t)a->island, .class = (uint16_t)class, .units = 1, .block = (uint32_t)size,
        .bump = (char *)(s + 1), .end = (char *)s + UNIT,
    };
    link_first(a, s);
    return s;
}

static void free_here(rt_arena_t *a, void *block) {
    span_t *s = span_of(block);
    if (s->class == CLASS_RUN) {
        run_put(a, s, s->units);
        return;
    }
    int was_full = full(s);
    *(void **)block = s->free;
    s->free = block;
    if (was_full) link_second(a, s);
    if (--s->used == 0 && a->avail[s->class] != s) {
        unlink_span(a, s);
        run_put(a, s, 1);
    }
}

// --- blocks -------------------------------------------------------------------

void *rt_block_new(rt_arena_t *a, size_t bytes) {
    if (bytes <= SMALL_MAX) {
        unsigned class = class_of(bytes);
        span_t  *s     = a->avail[class];
        if (!s) s = span_new(a, class);
        void *block = s->free;
        if (block) {
            s->free = *(void **)block;
        } else {
            block    = s->bump;
            s->bump += s->block;
        }
        s->used++;
        if (full(s)) unlink_span(a, s);
        return block;
    }
    size_t units = (bytes + sizeof(span_t) + UNIT - 1) / UNIT;
    if (units > RT_ARENA_RUNS) return alone_new(a, units);
    span_t *s = run_get(a, units);
    s->island = (uint32_t)a->island;
    s->class  = CLASS_RUN;
    s->units  = (uint32_t)units;
    return s + 1;
}

// k's blocks kept here go home, and k is woken if it's waiting.
static void send_home(rt_arena_t *a, uint64_t k) {
    home_t *h = &a->home[k];
    if (!h->n) return;
    void *old = atomic_load_explicit(&rt_returns[k].head, memory_order_relaxed);
    do {
        *(void **)h->last = old;
    } while (!atomic_compare_exchange_weak(&rt_returns[k].head, &old, h->first));
    a->going -= h->n;
    *h = (home_t){ 0 };
    rt_arena_wake(k);
}

static __attribute__((noinline)) void free_away(rt_arena_t *a, span_t *s, void *block) {
    if (s->class == CLASS_ALONE) {      // no island's: unmapped where it's freed
        munmap(s, (size_t)s->units * UNIT);
        return;
    }
    if (!a->home) {
        a->home = calloc(RT_ISLANDS_MAX, sizeof *a->home);
        if (!a->home) no_memory();
    }
    home_t *h = &a->home[s->island];
    size_t  bytes = s->class == CLASS_RUN ? (size_t)s->units * UNIT : s->block;
    *(void **)block = h->first;
    if (!h->first) h->last = block;
    h->first  = block;
    h->bytes += bytes;
    a->going++;
    if (++h->n >= HOME_BATCH || h->bytes >= HOME_BYTES) send_home(a, s->island);
}

void rt_block_free(rt_arena_t *a, void *block) {
    span_t *s = span_of(block);
    if (s->island == a->island && s->class != CLASS_ALONE) free_here(a, block);
    else                                                    free_away(a, s, block);
}

void rt_arena_returns(rt_arena_t *a) {
    void *block = atomic_exchange(&rt_returns[a->island].head, NULL);
    for (void *next; block; block = next) {
        next = *(void **)block;
        free_here(a, block);
    }
}

void rt_arena_idle(rt_arena_t *a) {
    if (rt_arena_pending(a)) rt_arena_returns(a);
    if (a->going) {
        for (uint64_t k = 0; k < RT_ISLANDS_MAX; k++) send_home(a, k);
    }
    if (a->warm * UNIT > RUNS_KEPT) {
        // The spans kept to allocate from, with nothing out, go too.
        for (unsigned class = 0; class < RT_ARENA_CLASSES; class++) {
            span_t *s = a->avail[class];
            if (s && !s->used) {
                unlink_span(a, s);
                run_put(a, s, 1);
            }
        }
        runs_trim(a, 0);
    }
}
