// process.c -- processes: the table, the run queue, stacks, heaps,
// messages, how processes end, and the scheduler.
//
// One core to each island (D256): a thread with its own table, run
// queue, timers and devices, what's marked RT_ISLAND, which no other
// island's thread touches. The scheduler runs on the C stack and switches
// to a process with rt_switch; the process switches back when it pauses
// (preempted, or yield), waits in recv, or ends. A process holds a stack only while it
// needs one: waiting in recv, it's just (code, args, mailbox), and it gives
// its stack back to the pool. A message arriving puts it back in the run
// queue, and it starts again at code, on a stack from the pool.
//
// A process that ends tells its result to those waiting for it then, and
// leaves an exit record in the table: a task's value, if its parent
// hasn't had it yet, kept till it has (D244); else just that it ended,
// which goes, with the table's page, once nothing more can come of it.
// Everything else it had is freed.
//
// When nothing can run, the scheduler waits for the next timer (after,
// sleep) or key (:keypress). The program ends when nothing can run and
// nothing more can come: no timer is pending, and no process is connected
// to :keypress, or stdin has ended.
//
// A file, a socket or a command opened with connect is a device: a pid
// that the runtime serves instead of compiled code (see "devices", below).

#include "rt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "io.h"

#define QUOTA          1000             // reductions between preemptions
#define STACK_BYTES    (8 << 20)        // every process's stack, mapped lazily
#define STACK_HEADROOM (64 << 10)       // room left below the limit for one frame and a C call
#define CHUNK_FIRST    (4 << 10)        // a process's nursery at first, and its first old chunk...
#define CHUNK_MAX      (1 << 20)        // ...each doubling up to this
#define HEAP_DEFAULT   (1 << 30)        // all of a process's chunks, both generations, unless SLIGHT_HEAP_MAX says
#define GC_MIN         (256 << 10)      // an old generation smaller than this is never collected whole
#define READ_MAX       (64 << 20)       // a line, a count or a value read from a device must be less (D160)

// A pid's entry in the table: the process while it runs, then its exit
// record: (:ok value) or (:error value), a task's, kept till its parent
// has it (D244). An actor's keeps none, and nor does a task's once its
// parent has had it: those waiting as it ended were told, and it's
// :ended after (D243). Or a device while it's open; to join, monitor and
// kill, a device is a process that has ended.
typedef struct {
    rt_proc_t        *proc;             // NULL once it has ended, and for a device
    rt_value_t        value;
    rt_chunk_t       *chunk;            // where value lives, or NULL if it needs no space
    uint8_t           ok;
    uint8_t           actor;            // started with spawn or connect: it can't be joined
    uint8_t           ended;            // ended, and keeping no value: :ended
    uint8_t           dead;             // and nothing more can come of it: its page can go
    struct rt_device *device;           // an open device, or NULL
} entry_t;

// The table is in pages, each freed once every entry in it is dead, so a
// program that runs for good, starting processes as it goes, holds the
// entries of those still running or keeping a value, not of every one it
// ever started (D244). Pids are never reused, so one whose page has gone
// reads as ended, keeping nothing: gone, which is never written.
#define PAGE_BITS 10
#define PAGE_SIZE (1u << PAGE_BITS)

typedef struct {
    uint32_t live;                      // entries made and not yet dead
    entry_t  e[PAGE_SIZE];
} page_t;

static RT_ISLAND uint64_t  island;       // which island this thread is (D256), 0 the main one
static RT_ISLAND page_t  **pages;        // by pid number >> PAGE_BITS; the root is 1
static RT_ISLAND uint64_t  nprocs, pages_cap;   // pids made; and room in pages
static RT_ISLAND uint64_t  nrecords;     // entries made and not yet dead, for :system
static entry_t             gone = { .ended = 1, .dead = 1 };

// The entry of pid number id.
static entry_t *at(uint64_t id) {
    page_t *g = pages[id >> PAGE_BITS];
    return g ? &g->e[id & (PAGE_SIZE - 1)] : &gone;
}

static entry_t *slot(rt_value_t pid) {
    return at(rt_pid_number(pid));
}
static RT_ISLAND rt_proc_t *ready_head, *ready_tail;
static RT_ISLAND rt_ctx_t   scheduler;
static RT_ISLAND void      *free_stacks;        // a list through each stack's first word
static RT_ISLAND uint64_t   stressed;           // the collections SLIGHT_GC_STRESS has made, every eighth a major one
static RT_ISLAND uint64_t   sched;              // SLIGHT_SCHED's generator, or 0: first in, first out

// The program's settings, the same for every island.
static size_t  page;
static int     poison;                          // SLIGHT_POISON: fill what the collector frees
static int64_t gc_stress;                       // SLIGHT_GC_STRESS=n: collect at every nth call (n <= QUOTA), and every recv
static int     gc_verify;                       // SLIGHT_GC_VERIFY: check that nothing old points at anything young
static size_t  heap_max;                        // the most a process's chunks can hold (D202)

// --- the run queue ------------------------------------------------------------
//
// First in, first out (D86). With SLIGHT_SCHED=n, a generator seeded with
// n picks which ready process runs next instead, and how many calls each
// slice is (D170), so a program can be tried under other orders, each
// seed the same order every time. Picking walks the queue, which is fine
// for trying a program out, if not a million processes.

static uint64_t sched_next(void) {      // splitmix64
    uint64_t z = (sched += 0x9e3779b97f4a7c15u);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

static void enqueue(rt_proc_t *p) {
    p->state      = RT_READY;
    p->next_ready = NULL;
    if (ready_tail) ready_tail->next_ready = p;
    else            ready_head = p;
    ready_tail = p;
}

static rt_proc_t *dequeue(void) {
    if (sched && ready_head) {
        uint64_t n = 0;
        for (rt_proc_t *q = ready_head; q; q = q->next_ready) n++;
        rt_proc_t **link = &ready_head, *prev = NULL;
        for (uint64_t k = sched_next() % n; k; k--) {
            prev = *link;
            link = &prev->next_ready;
        }
        rt_proc_t *p = *link;
        *link = p->next_ready;
        if (ready_tail == p) ready_tail = prev;
        return p;
    }
    rt_proc_t *p = ready_head;
    if (p) {
        ready_head = p->next_ready;
        if (!ready_head) ready_tail = NULL;
    }
    return p;
}

// Takes p out of the run queue, wherever it is: only kill needs this.
static void unqueue(rt_proc_t *p) {
    rt_proc_t **link = &ready_head, *prev = NULL;
    while (*link != p) {
        prev = *link;
        link = &prev->next_ready;
    }
    *link = p->next_ready;
    if (ready_tail == p) ready_tail = prev;
}

// Back to the scheduler. Returns when the scheduler switches back, which
// for a process that's waiting or done is never.
static void to_scheduler(rt_proc_t *p) {
    rt_switch(&p->ctx, &scheduler);
}

// --- stacks -------------------------------------------------------------------

// A stack with a guard page under it, so running off the end crashes
// instead of corrupting memory. Returns its bottom, above the guard.
static void *get_stack(void) {
    if (free_stacks) {
        void *s = free_stacks;
        free_stacks = *(void **)s;
        return s;
    }
    char *base = mmap(NULL, page + STACK_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED || mprotect(base, page, PROT_NONE) != 0) {
        perror("rt: can't allocate a stack");
        exit(2);
    }
    return base + page;
}

static void release_stack(rt_proc_t *p) {
    *(void **)p->stack = free_stacks;
    free_stacks = p->stack;
    p->stack    = NULL;
}

// The calls p has made: those of its slices before this one, and this
// one's so far, which is what's left of it taken from what it had.
static uint64_t calls_of(const rt_proc_t *p) {
    return p->calls + (uint64_t)(p->slice - p->reductions - p->slice_rest);
}

// A new slice of QUOTA calls, the last of which calls rt_preempt. With
// SLIGHT_GC_STRESS=n, every nth one does, to collect, and the slice goes on
// (slice_rest) till its end, so a process is preempted after the same
// calls as without it.
static void refill(rt_proc_t *p) {
    int64_t calls = sched ? 1 + (int64_t)(sched_next() % QUOTA) : QUOTA;
    p->calls      = calls_of(p);
    p->slice      = calls;
    p->reductions = gc_stress && gc_stress < calls ? gc_stress : calls;
    p->slice_rest = calls - p->reductions;
}

// A stack, and registers that make the first switch land in rt_trampoline
// with x28 (r15) = p, which calls p->code with p->args.
static void start(rt_proc_t *p) {
    p->stack       = get_stack();
    p->stack_limit = (uintptr_t)p->stack + STACK_HEADROOM;
    refill(p);
    memset(&p->ctx, 0, sizeof p->ctx);
#if defined(__x86_64__)
    // x86's ret takes its address from the stack, not a link register: the
    // trampoline's goes in the top word, and popping it leaves rsp 16-aligned.
    uint64_t *top = (uint64_t *)((char *)p->stack + STACK_BYTES) - 1;
    *top       = (uint64_t)rt_trampoline;
    p->ctx.rsp = (uint64_t)top;
    p->ctx.r15 = (uint64_t)p;
#else
    p->ctx.sp         = (uint64_t)p->stack + STACK_BYTES;
    p->ctx.lr         = (uint64_t)rt_trampoline;
    p->ctx.x19_x28[9] = (uint64_t)p;
#endif
}

// --- heaps --------------------------------------------------------------------

// A process's handles to shared buffers (D253, below): its young ones and
// its old, kept apart, as on its side (rt_side_t), made when first wanted.
typedef struct {
    uint64_t **at;
    size_t     n, cap;
    size_t     bytes;                   // the buffers' lengths, all told
} handles_t;

static handles_t *handles_of(rt_proc_t *p, int old);
static handles_t *handles_had(const rt_proc_t *p, int old);    // NULL if it has none

static size_t shared_bytes(const rt_proc_t *p, int old) {
    const handles_t *h = handles_had(p, old);
    return h ? h->bytes : 0;
}

// What p's heap holds against its cap: its chunks, and what its handles
// keep alive.
static size_t held(const rt_proc_t *p) {
    return p->heap_bytes + shared_bytes(p, 0) + shared_bytes(p, 1);
}

static void handles_add(handles_t *h, uint64_t *box) {
    if (h->n == h->cap) {
        h->cap = h->cap ? 2 * h->cap : 16;
        h->at  = rt_must(realloc(h->at, h->cap * sizeof *h->at));
    }
    h->at[h->n++] = box;
    h->bytes     += ((rt_buffer_t *)box[2])->len;
}
//
// Two generations (D200). Everything is allocated in the young one: the
// nursery, a chunk of 4 KB at first, twice that at each collection that
// finds it overflowed, up to 1 MB; and once it's full, overflow chunks,
// listed after it, till the next call's entry, where it's collected. The
// old generation is a list of chunks: what collections have kept, the
// messages taken, a fork's values, joined results, and big strings. The
// first is the one collections promote into, once there's been one, and
// old_ptr says how far it's filled. A waiting process costs its struct,
// so these are as few fields as will do.

static rt_chunk_t *new_chunk(size_t bytes) {
    rt_chunk_t *c = rt_must(malloc(sizeof *c + bytes));
    c->next     = NULL;
    c->size     = bytes;
    c->handles  = NULL;
    c->nhandles = 0;
    return c;
}

static size_t young_bytes(const rt_proc_t *p) {
    size_t bytes = 0;
    for (const rt_chunk_t *c = p->nursery; c; c = c->next) bytes += c->size;
    return bytes;
}

// What's left in the old chunk that collections promote into.
static size_t old_left(const rt_proc_t *p) {
    return p->old_ptr ? (uintptr_t)(p->chunks + 1) + p->chunks->size - p->old_ptr : 0;
}

// The bytes p's heap has used, and its old generation: their chunks, less
// what's left in those allocated in.
static size_t in_use(const rt_proc_t *p) {
    return p->heap_bytes - (p->heap_limit - p->heap_ptr) - old_left(p);
}

static size_t old_in_use(const rt_proc_t *p) {
    return p->heap_bytes - young_bytes(p) - old_left(p);
}

// Collect at the next call's entry, in rt_preempt, where every live value
// is in a frame. The rest of the slice is kept for after, so collecting
// doesn't change when the process is preempted.
static void cut_slice(rt_proc_t *p) {
    if (p->reductions > 1) {
        p->slice_rest += p->reductions - 1;
        p->reductions  = 1;
    }
}

// Into the old generation, after the chunk promoted into: nothing in c
// points at anything outside it but static data.
static void adopt(rt_proc_t *p, rt_chunk_t *c) {
    rt_chunk_t **at = p->old_ptr ? &p->chunks->next : &p->chunks;
    c->next        = *at;
    *at            = c;
    p->heap_bytes += c->size;
    if (c->nhandles) {
        handles_t *old = handles_of(p, 1);
        for (uint64_t i = 0; i < c->nhandles; i++) handles_add(old, c->handles[i]);
        free(c->handles);
        c->handles  = NULL;
        c->nhandles = 0;
    }
    if (old_in_use(p) + shared_bytes(p, 1) >= p->gc_at) cut_slice(p);
}

void rt_heap_grow(uint64_t bytes, const char *site) {
    rt_proc_t *p = rt_current;
    if (!p->nursery) {
        if (held(p) + CHUNK_FIRST > heap_max) rt_fault(RT_FAULT_HEAP, 0, site);
        p->nursery     = new_chunk(CHUNK_FIRST);
        p->heap_bytes += CHUNK_FIRST;
        p->heap_ptr    = (uintptr_t)(p->nursery + 1);
        p->heap_limit  = p->heap_ptr + CHUNK_FIRST;
        if (bytes <= CHUNK_FIRST) return;
    }
    size_t young = young_bytes(p), size = young < CHUNK_MAX ? young : CHUNK_MAX;
    if (size < bytes) size = (bytes + 15) & ~(size_t)15;
    if (held(p) + size > heap_max) rt_fault(RT_FAULT_HEAP, 0, site);
    rt_chunk_t *c = new_chunk(size);
    c->next          = p->nursery->next;
    p->nursery->next = c;
    p->heap_bytes   += size;
    p->heap_ptr      = (uintptr_t)(c + 1);
    p->heap_limit    = p->heap_ptr + size;
    cut_slice(p);
}

// --- shared buffers (D253) -----------------------------------------------------
//
// A string, or an array of numbers, of RT_SHARED_MIN bytes or more is a
// buffer off the heap, its references counted, and a handle on the heap:
// a box of its header with RT_BOX_SHARED, where the data is, and the
// buffer. A send
// copies the handle and counts a reference; nothing copies the bytes. It's
// safe for the reason the collector needs no write barrier (D200): what's
// shared never changes, and holds no pointers, so no buffer points into a
// heap, and no heap's collector has to know about another's.
//
// Each process keeps a list of the handles in its heap, the young apart
// from the old, as its collector does. After a collection, a handle that
// wasn't copied is garbage, and its buffer loses a reference; one at none
// is freed. A copy (a message, a fork's values, a result) lists the
// handles it made in its chunk, and whoever adopts the chunk takes them
// on; a chunk freed unadopted (a message never taken, a result let go) lets
// them go. A process's end lets go of all of its own.
//
// What a process's young handles hold counts toward collecting it, so one
// that makes big strings and little else collects them (Erlang's lesson);
// and what all its handles hold counts toward its heap's cap, since it's
// what they keep alive.

#define SHARED_YOUNG_MAX CHUNK_MAX      // young handles' bytes past this, and the next call collects

static void buffer_release(rt_buffer_t *b) {
    if (--b->refs) return;
    if (poison) memset(b->bytes, 0xab, b->len + 1);
    free(b);
}

// A chunk let go of without being adopted: its handles' references go.
static void free_chunk(rt_chunk_t *c) {
    for (uint64_t i = 0; i < c->nhandles; i++) buffer_release((rt_buffer_t *)c->handles[i][2]);
    free(c->handles);
    free(c);
}

static void free_chunks(rt_chunk_t *c) {
    for (rt_chunk_t *next; c; c = next) {
        next = c->next;
        free_chunk(c);
    }
}

// A message's record is in its chunk (new_msg).
static void free_msg(rt_msg_t *m) {
    free_chunk(m->chunk);
}

// A shared box of header for len bytes (RT_SHARED_MIN or more), for the
// caller to fill through *data. The handle first, so that a heap at its
// cap faults before the buffer's made.
rt_value_t rt_new_shared(uint64_t header, size_t len, char **data, const char *site) {
    rt_proc_t *p = rt_current;
    if (held(p) + len > heap_max) rt_fault(RT_FAULT_HEAP, 0, site);
    uint64_t    *box = rt_alloc(32, site);
    rt_buffer_t *b   = rt_must(malloc(sizeof *b + len + 1));
    b->refs       = 1;
    b->len        = len;
    b->bytes[len] = '\0';
    box[0] = header | RT_BOX_SHARED;
    box[1] = (uint64_t)(uintptr_t)b->bytes;
    box[2] = (uint64_t)(uintptr_t)b;
    box[3] = 0;
    handles_t *young = handles_of(p, 0);
    handles_add(young, box);
    if (young->bytes > SHARED_YOUNG_MAX) cut_slice(p);
    *data = b->bytes;
    return (rt_value_t)box | RT_TAG_BOXED;
}

// A string of len bytes, copied from bytes, or left for the caller to
// fill and mark (rt_string_mark).
rt_value_t rt_new_shared_string(const char *bytes, size_t len, const char *site) {
    char      *data;
    rt_value_t s = rt_new_shared((uint64_t)len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING, len, &data, site);
    if (bytes) {
        memcpy(data, bytes, len);
        rt_string_mark(s);
    }
    return s;
}

static void free_heap(rt_proc_t *p) {
    for (int old = 0; old < 2; old++) {
        handles_t *h = (handles_t *)handles_had(p, old);
        if (!h) continue;
        for (size_t i = 0; i < h->n; i++) buffer_release((rt_buffer_t *)h->at[i][2]);
        free(h->at);
        *h = (handles_t){ 0 };
    }
    free_chunks(p->chunks);
    free_chunks(p->nursery);
    p->chunks     = p->nursery = NULL;
    p->heap_ptr   = p->heap_limit = p->old_ptr = 0;
    p->heap_bytes = 0;
    for (rt_msg_t *m = p->mail, *next; m; m = next) {
        next = m->next;
        free_msg(m);
    }
    p->mail = p->mail_last = NULL;
}

// --- copying values between processes -----------------------------------------
//
// A message, or a forked process's captured values, is deep-copied: once
// into a chunk of its own, which the receiver adopts. Static data (literals,
// quoted lists, closures that capture nothing) is shared, never copied.
// Sharing inside a value is not preserved: a sublist that appears twice is
// copied twice, as in the BEAM.

static int is_static(rt_value_t v) {
    uintptr_t p = v & ~(uintptr_t)RT_TAG_MASK;
    return (p >= (uintptr_t)slight_rodata_start && p < (uintptr_t)slight_rodata_end)
        || (p >= (uintptr_t)slight_const_start && p < (uintptr_t)slight_const_end);
}

static int is_pointer(rt_value_t v) {
    return (rt_is_cons(v) || (v & RT_TAG_MASK) == RT_TAG_BOXED) && !is_static(v);
}

static size_t box_bytes(rt_value_t v) {
    uint64_t header = rt_box(v)[0], size = header >> RT_BOX_SIZE_SHIFT;
    switch (header & RT_BOX_TYPE_MASK) {
        case RT_BOX_STRING:  return header & RT_BOX_SHARED ? 32 : (8 + size + 1 + 15) & ~(size_t)15;
        case RT_BOX_FLOAT:   return 16;
        case RT_BOX_CLOSURE: return (32 + 8 * size + 15) & ~(size_t)15;
        case RT_BOX_TUPLE:   return (8 + 8 * size + 15) & ~(size_t)15;
        case RT_BOX_FLOATS:
        case RT_BOX_INTS:    return header & RT_BOX_SHARED ? 32 : (8 + 8 * size + 15) & ~(size_t)15;
        default:             return 0;
    }
}

// Both walks over values below, the message copy and the collector's,
// keep what changes at every word in a struct of their own that only
// their helpers point at, and the helpers are always inlined, so the
// compiler can keep that struct in registers. Kept in globals, or in a
// struct handed to a function that isn't inlined, it was loaded and
// stored again around every word copied, a word copied being, as far as
// the compiler knew, one of them (D239, D240). What would let the struct
// escape, an array growing, is a function given values, not pointers.
#define WALK static inline __attribute__((always_inline))

// A work stack twice the size of todo, which held cap words and began as
// local, on the C stack.
static rt_value_t *stack_grow(rt_value_t *todo, size_t cap, rt_value_t *local) {
    rt_value_t *more = rt_must(malloc(2 * cap * sizeof *more));
    memcpy(more, todo, cap * sizeof *more);
    if (todo != local) free(todo);
    return more;
}

// Copying a value is one walk over it (D147), into a scratch buffer kept
// from one copy to the next; then a chunk of just the size the copy came
// to takes it, so the receiver's heap holds nothing it doesn't use.
// Scratch can grow, and move, while the copy is made, so the copy refers
// to itself by offsets into scratch: a pointer in it is an offset with its
// tag, and slots[] says where each such word is. Moving the copy into its
// chunk adds the chunk's address to each. (Copying was two walks till the
// boids example's broadcasts showed the first, which only added up the
// bytes, to be half the time a send took.)
//
// The walk is the collector's, with nothing to forward (D240): a box is
// copied when the walk comes to it, and a cell with the rest of its list,
// each cdr as the walk comes to it; a car that's a list, and a box of
// values (a closure or a tuple), goes on the stack, with where its copy
// is, for after. So a flat list never touches the stack. Sharing isn't
// kept: a DAG is copied as a tree.

static RT_ISLAND char   *scratch;        // the copy being made, kept from one copy to the next
static RT_ISLAND size_t *slots;          // the offsets in scratch of words that point into it
static RT_ISLAND size_t  scratch_cap;    // in bytes; slots has room for one for each word

#define SCRATCH_KEPT (1 << 20)          // scratch bigger than this is let go after a copy

typedef struct {
    char       *scratch;
    size_t     *slots;
    size_t      used, cap, nslots;
    rt_value_t *todo, *local;           // pairs: the offset of a copy, and what it's a copy of
    size_t      n, todo_cap;
    uintptr_t   lo, hi;                 // static data's bounds, and its two ranges:
    uintptr_t   r0, r1, c0, c1;         // read-only, and the quoted constants
    size_t     *handles;                // the offsets of the shared strings' handles copied (D253)
    size_t      nhandles, handles_cap;
} copying_t;

// Whether v is copied: a cell or a box, but not static data, which is
// tested in full only between the lowest and the highest of its bounds,
// where no heap pointer has been.
WALK int copied(const copying_t *c, rt_value_t v) {
    uint64_t tag = v & RT_TAG_MASK;
    if ((tag != RT_TAG_LIST && tag != RT_TAG_BOXED) || v == RT_NIL) return 0;
    uintptr_t a = v & ~(uintptr_t)RT_TAG_MASK;
    return a - c->lo >= c->hi - c->lo || !(a - c->r0 < c->r1 - c->r0 || a - c->c0 < c->c1 - c->c0);
}

// The offset of n more bytes at the end of scratch (n a multiple of 16, so
// every offset is 16-aligned, as a cell or a box must be). Slots grow with
// it, so that noting one never needs to.
WALK size_t scratch_take(copying_t *c, size_t n) {
    if (c->used + n > c->cap) {
        size_t cap = c->cap;
        while (c->used + n > cap) cap = cap ? cap * 2 : 4096;
        c->scratch = rt_must(realloc(c->scratch, cap));
        c->slots   = rt_must(realloc(c->slots, cap / 8 * sizeof *c->slots));
        c->cap     = cap;
    }
    size_t at = c->used;
    c->used  += n;
    return at;
}

// The tagged offset of a copy into the word at offset at, noted.
WALK void scratch_link(copying_t *c, size_t at, rt_value_t v) {
    *(rt_value_t *)(c->scratch + at) = v;
    c->slots[c->nslots++] = at;
}

// A shared string's handle copied to offset box: one more reference to its
// buffer, and noted, for the chunk to list.
WALK void copy_handle(copying_t *c, size_t box, const uint64_t *from) {
    ((rt_buffer_t *)from[2])->refs++;
    if (c->nhandles == c->handles_cap) {
        c->handles_cap = c->handles_cap ? 2 * c->handles_cap : 8;
        c->handles     = rt_must(realloc(c->handles, c->handles_cap * sizeof *c->handles));
    }
    c->handles[c->nhandles++] = box;
}

WALK void copy_push(copying_t *c, size_t at, rt_value_t v) {
    if (c->n + 2 > c->todo_cap) {
        c->todo      = stack_grow(c->todo, c->todo_cap, c->local);
        c->todo_cap *= 2;
    }
    c->todo[c->n++] = at;
    c->todo[c->n++] = v;
}

// v into the word at offset at: v itself, if it isn't copied, or where its
// copy is. A box is copied now, and a cell taken for a list, whose cells,
// like a box's values, are copied from the stack.
WALK void copy_word(copying_t *c, rt_value_t v, size_t at) {
    if (!copied(c, v)) {
        *(rt_value_t *)(c->scratch + at) = v;
    } else if ((v & RT_TAG_MASK) == RT_TAG_LIST) {
        size_t cell = scratch_take(c, 16);
        scratch_link(c, at, (rt_value_t)cell | RT_TAG_LIST);
        copy_push(c, cell, v);
    } else {
        size_t          bytes = box_bytes(v), box = scratch_take(c, bytes);
        uint64_t       *to    = (uint64_t *)(c->scratch + box);
        const uint64_t *from  = rt_box(v);
        if (bytes <= 64) {
            for (size_t i = 0; i < bytes / 8; i++) to[i] = from[i];
        } else {
            memcpy(to, from, bytes);
        }
        scratch_link(c, at, (rt_value_t)box | RT_TAG_BOXED);
        if (rt_box_holds_values(from[0])) copy_push(c, box, v);
        else if (from[0] & RT_BOX_SHARED) copy_handle(c, box, from);
    }
}

// Copies v into the word at offset at, and all it leads to.
WALK void copy(copying_t *c, rt_value_t v, size_t at) {
    copy_word(c, v, at);
    while (c->n) {
        v  = c->todo[--c->n];
        at = (size_t)c->todo[--c->n];
        if (rt_is_cons(v)) {
            // Along the list: each cdr to copy is taken as the walk comes
            // to it, and the walk goes on into it.
            for (;;) {
                copy_word(c, rt_car(v), at);
                rt_value_t d = rt_cdr(v);
                if ((d & RT_TAG_MASK) != RT_TAG_LIST || !copied(c, d)) {
                    copy_word(c, d, at + 8);
                    break;
                }
                size_t cell = scratch_take(c, 16);
                scratch_link(c, at + 8, (rt_value_t)cell | RT_TAG_LIST);
                at = cell;
                v  = d;
            }
        } else {
            const uint64_t *in    = rt_box(v);
            uint64_t        first = rt_box_first_value(in[0]), count = in[0] >> RT_BOX_SIZE_SHIFT;
            for (uint64_t i = 0; i < count; i++) copy_word(c, in[first + i], at + 8 * (first + i));
        }
    }
}

// Copies n values into one new chunk, after reserve bytes (a multiple of
// 16) left for the caller, and puts what to use for each in into. The
// values themselves go first in scratch, as words that point into it like
// any other, and are taken out of it at the end. Each word noted is made
// where it will be before the copy is moved.
static rt_chunk_t *copy_walk(const rt_value_t *from, rt_value_t *into, uint64_t n, size_t reserve) {
    rt_value_t local[RT_WORK_LOCAL];
    copying_t  c = {
        .scratch = scratch, .slots = slots, .cap = scratch_cap,
        .todo = local, .local = local, .todo_cap = RT_WORK_LOCAL,
        .r0 = (uintptr_t)slight_rodata_start, .r1 = (uintptr_t)slight_rodata_end,
        .c0 = (uintptr_t)slight_const_start, .c1 = (uintptr_t)slight_const_end,
    };
    c.lo = c.r0 < c.c0 ? c.r0 : c.c0;
    c.hi = c.r1 > c.c1 ? c.r1 : c.c1;
    size_t roots = (8 * n + 15) & ~(size_t)15;
    scratch_take(&c, roots);
    for (uint64_t i = 0; i < n; i++) copy(&c, from[i], 8 * i);
    if (c.todo != local) free(c.todo);
    size_t      bytes = c.used - roots;
    rt_chunk_t *chunk = reserve + bytes ? new_chunk(reserve + bytes) : NULL;
    if (bytes) {
        char      *data  = (char *)(chunk + 1) + reserve;
        rt_value_t shift = (rt_value_t)data - roots;    // offset + shift: where it will be
        for (size_t i = 0; i < c.nslots; i++) *(rt_value_t *)(c.scratch + c.slots[i]) += shift;
        memcpy(data, c.scratch + roots, bytes);
        if (c.nhandles) {
            chunk->handles  = rt_must(malloc(c.nhandles * sizeof *chunk->handles));
            chunk->nhandles = c.nhandles;
            for (size_t i = 0; i < c.nhandles; i++) chunk->handles[i] = (uint64_t *)(data + c.handles[i] - roots);
        }
    }
    free(c.handles);
    for (uint64_t i = 0; i < n; i++) into[i] = ((rt_value_t *)c.scratch)[i];
    if (c.cap > SCRATCH_KEPT) {
        free(c.scratch), free(c.slots);
        c.scratch = NULL, c.slots = NULL;
        c.cap = 0;
    }
    scratch     = c.scratch;
    slots       = c.slots;
    scratch_cap = c.cap;
    return chunk;
}

// Values with no pointer among them, as fork's and join's often are, and
// many messages, need no walk.
static rt_chunk_t *copy_values_after(const rt_value_t *from, rt_value_t *into, uint64_t n, size_t reserve) {
    for (uint64_t i = 0; i < n; i++) {
        if (rt_is_cons(from[i]) || (from[i] & RT_TAG_MASK) == RT_TAG_BOXED) return copy_walk(from, into, n, reserve);
    }
    for (uint64_t i = 0; i < n; i++) into[i] = from[i];
    return reserve ? new_chunk(reserve) : NULL;
}

static rt_chunk_t *copy_values(const rt_value_t *from, rt_value_t *into, uint64_t n) {
    return copy_values_after(from, into, n, 0);
}

// A copy of v in the current process's heap.
static rt_value_t copy_here(rt_value_t v) {
    rt_value_t  out;
    rt_chunk_t *c = copy_values(&v, &out, 1);
    if (c) adopt(rt_current, c);
    return out;
}

// --- garbage collection -------------------------------------------------------
//
// When a receive function asks for its next message (D105), and at the
// entry of a compiled function, in rt_preempt (D166). The roots are in
// frames: compiled code keeps nothing in a register across a call, a slot
// only ever holds a value, and a function that calls another (not in tail
// position) zeroes its slots first, so that none holds a stale word. At a
// function's entry its parameters and captured values are in its first
// slots, and each frame below holds its slots between the linkage of the
// one above (frame pointer and return address) and its own; the frame
// pointer saved by the first is 0. No C frame comes between them, since C
// never calls compiled code (apply jumps), so C code never sees anything
// move. At recv, the recv rule means the stack is empty.
//
// Two kinds (D200). A minor collection, once the nursery has overflowed,
// copies what's young and reachable into the old generation, all of it,
// and starts the young one again. It follows only pointers into the young
// generation, and never looks inside an old object, which works because
// nothing old points at anything young. An object points only at objects
// made before it, since nothing changes once it's made: compiled code
// fills an object before anything else runs, and a C builtin fills what it
// allocates before it returns, with no collection between. A minor
// collection rewrites every pointer into the young generation there is
// (the roots', and the young objects' own), and what's adopted points only
// into itself. So there's no write barrier and no remembered set. A major
// collection, once the old generation is twice what was live after the
// last one (D106, D196), copies everything live, in both, into fresh
// chunks, and frees the old ones.
//
// Copying leaves a forwarding pointer behind, so sharing is kept: a DAG
// stays a DAG. Cons cells have no header, so to-space can't be scanned in
// order as Cheney's algorithm would; instead, each copied closure, tuple,
// and cons that heads a list goes on a stack of objects whose fields still
// point at the old heap. A list's spine never does: along a list the walk
// copies each cdr as it comes to it and goes on from the copy (D239).

#define GC_MOVED_CONS RT_TAG_BOXED      // in a moved cell's car: a boxed null, which no value is
#define GC_MOVED_BOX  0                 // in a moved box's header: no box has type 0

typedef struct {
    const rt_proc_t *young;             // whose young generation it's promoting, or NULL in a major collection
    rt_chunk_t      *chunks;            // to-space, newest first: in a minor collection, the old generation
    uintptr_t        ptr, limit;
    size_t           bytes, live, size; // to-space's capacity, what's been copied, the size of a new chunk
} gc_t;

// A new chunk of to-space, for at least bytes: where it starts.
static uintptr_t gc_chunk(gc_t *g, size_t bytes) {
    size_t      size = bytes > g->size ? bytes : g->size;
    rt_chunk_t *c    = new_chunk(size);
    c->next   = g->chunks;
    g->chunks = c;
    g->bytes += size;
    g->limit  = (uintptr_t)(c + 1) + size;
    g->size   = g->size * 2 < CHUNK_MAX ? g->size * 2 : CHUNK_MAX;
    return (uintptr_t)(c + 1);
}

// Whether v, a pointer, is into p's nursery or what it overflowed into
// (usually nothing).
static int is_young(const rt_proc_t *p, rt_value_t v) {
    uintptr_t a = v & ~(uintptr_t)RT_TAG_MASK;
    for (const rt_chunk_t *c = p->nursery; c; c = c->next) {
        if (a - (uintptr_t)(c + 1) < c->size) return 1;
    }
    return 0;
}

// What changes at every object the walk copies: where the next copy goes,
// and the copies whose fields still point at the old heap. It's kept out
// of gc_t, which the walk hands to gc_chunk, so it can live in registers
// (D239, and WALK, above). The walk is compiled twice, for a minor
// collection and a major one, and what moves is worked out once: in a
// minor one the young chunks, the nursery and the chunk it overflowed
// into, if just one (if more, a walk along them all); in a major one, all
// but static data, which is two ranges, tested only for what's between
// the lowest and the highest of their bounds, where no heap pointer has
// been.
typedef struct {
    uintptr_t        ptr, limit;
    size_t           live;
    rt_value_t      *todo, *local;      // the stack, and the one it starts as, on the C stack
    size_t           n, cap;
    const rt_proc_t *young;
    uintptr_t        n0, n1, o0, o1;    // the nursery, and the chunk it overflowed into
    int              more;              // it overflowed into more than one
    uintptr_t        lo, hi;            // static data's bounds, and its two ranges:
    uintptr_t        r0, r1, c0, c1;    // read-only, and the quoted constants
} walk_t;

WALK void gc_push(walk_t *w, rt_value_t v) {
    if (w->n == w->cap) {
        w->todo  = stack_grow(w->todo, w->cap, w->local);
        w->cap  *= 2;
    }
    w->todo[w->n++] = v;
}

WALK int gc_moves(const walk_t *w, uintptr_t a, int minor) {
    if (minor) {
        if (a - w->n0 < w->n1 - w->n0 || a - w->o0 < w->o1 - w->o0) return 1;
        return w->more && is_young(w->young, a);
    }
    if (a - w->lo >= w->hi - w->lo) return 1;
    return !(a - w->r0 < w->r1 - w->r0 || a - w->c0 < w->c1 - w->c0);
}

WALK void *gc_take(gc_t *g, walk_t *w, size_t bytes) {
    if (w->limit - w->ptr < bytes) {
        w->ptr   = gc_chunk(g, bytes);
        w->limit = g->limit;
    }
    void *at = (void *)w->ptr;
    w->ptr  += bytes;
    w->live += bytes;
    return at;
}

// The copy of the cell old, made now; old says where it went.
WALK rt_value_t gc_move_cons(gc_t *g, walk_t *w, rt_value_t *old) {
    rt_value_t *new = gc_take(g, w, 16);
    rt_value_t  to  = (rt_value_t)new | RT_TAG_LIST;
    new[0] = old[0];
    new[1] = old[1];
    old[0] = GC_MOVED_CONS;
    old[1] = to;
    return to;
}

// Where v lives now, copying it there if it hasn't been yet. In a minor
// collection, only what's young moves.
WALK rt_value_t forward(gc_t *g, walk_t *w, rt_value_t v, int minor) {
    uint64_t tag = v & RT_TAG_MASK;
    if ((tag != RT_TAG_LIST && tag != RT_TAG_BOXED) || v == RT_NIL || !gc_moves(w, v & ~(uintptr_t)RT_TAG_MASK, minor)) return v;
    if (tag == RT_TAG_LIST) {
        rt_value_t *old = (rt_value_t *)(v - RT_TAG_LIST);
        if (old[0] == GC_MOVED_CONS) return old[1];
        rt_value_t to = gc_move_cons(g, w, old);
        gc_push(w, to);
        return to;
    }
    uint64_t *old = (uint64_t *)(v - RT_TAG_BOXED);
    if (old[0] == GC_MOVED_BOX) return old[1];
    size_t     bytes = box_bytes(v);
    uint64_t  *new   = gc_take(g, w, bytes);
    rt_value_t to    = (rt_value_t)new | RT_TAG_BOXED;
    if (bytes <= 64) {
        for (size_t i = 0; i < bytes / 8; i++) new[i] = old[i];
    } else {
        memcpy(new, old, bytes);
    }
    old[0] = GC_MOVED_BOX;
    old[1] = to;
    if (rt_box_holds_values(new[0])) gc_push(w, to);
    return to;
}

// Forwards roots[0..n), and the frames below fp, if it's given, and then
// all they lead to.
WALK void walk(gc_t *g, rt_value_t *roots, uint64_t n, const uintptr_t *fp, int minor) {
    rt_value_t local[RT_WORK_LOCAL];
    walk_t     w = {
        .ptr = g->ptr, .limit = g->limit, .live = g->live,
        .todo = local, .local = local, .cap = RT_WORK_LOCAL,
        .young = g->young,
        .r0 = (uintptr_t)slight_rodata_start, .r1 = (uintptr_t)slight_rodata_end,
        .c0 = (uintptr_t)slight_const_start, .c1 = (uintptr_t)slight_const_end,
    };
    w.lo = w.r0 < w.c0 ? w.r0 : w.c0;
    w.hi = w.r1 > w.c1 ? w.r1 : w.c1;
    if (minor) {
        const rt_chunk_t *c = g->young->nursery, *o = c->next;
        w.n0   = (uintptr_t)(c + 1);
        w.n1   = w.n0 + c->size;
        w.o0   = o ? (uintptr_t)(o + 1) : 0;
        w.o1   = o ? w.o0 + o->size : 0;
        w.more = o && o->next;
    }
    for (uint64_t i = 0; i < n; i++) roots[i] = forward(g, &w, roots[i], minor);
    for (; fp && fp[0]; fp = (const uintptr_t *)fp[0]) {
        for (rt_value_t *s = (rt_value_t *)(fp + 2); s < (rt_value_t *)fp[0]; s++) *s = forward(g, &w, *s, minor);
    }
    while (w.n) {
        rt_value_t v = w.todo[--w.n];
        if (rt_is_cons(v)) {
            // Along the list: each cdr still to move is copied as the walk
            // comes to it, and the walk goes on from the copy.
            rt_value_t *cell = (rt_value_t *)(v - RT_TAG_LIST);
            for (;;) {
                cell[0] = forward(g, &w, cell[0], minor);
                rt_value_t  d    = cell[1];
                rt_value_t *next = (rt_value_t *)(d - RT_TAG_LIST);
                if (!rt_is_cons(d) || !gc_moves(&w, (uintptr_t)next, minor) || next[0] == GC_MOVED_CONS) {
                    cell[1] = forward(g, &w, d, minor);
                    break;
                }
                rt_value_t to = gc_move_cons(g, &w, next);
                cell[1] = to;
                cell    = (rt_value_t *)(to - RT_TAG_LIST);
            }
        } else {
            uint64_t *box   = (uint64_t *)(v - RT_TAG_BOXED);
            uint64_t  first = rt_box_first_value(box[0]), count = box[0] >> RT_BOX_SIZE_SHIFT;
            for (uint64_t i = 0; i < count; i++) box[first + i] = forward(g, &w, box[first + i], minor);
        }
    }
    if (w.todo != local) free(w.todo);
    g->ptr   = w.ptr;
    g->limit = w.limit;
    g->live  = w.live;
}

static void trace(gc_t *g, rt_value_t *roots, uint64_t n, const uintptr_t *fp) {
    if (g->young) walk(g, roots, n, fp, 1);
    else          walk(g, roots, n, fp, 0);
}

static size_t chunk_size(size_t bytes) {
    return bytes < CHUNK_FIRST ? CHUNK_FIRST : bytes < CHUNK_MAX ? bytes : CHUNK_MAX;
}

// A handle after a collection: kept, where it moved to, if it moved, since
// it's live; or its buffer's reference let go.
static void sweep_handle(uint64_t *h, handles_t *kept) {
    if (h[0] == GC_MOVED_BOX) handles_add(kept, (uint64_t *)(h[1] - RT_TAG_BOXED));
    else                      buffer_release((rt_buffer_t *)h[2]);
}

// A minor collection and a major one, named for what they collect, since
// macOS's <sys/types.h> has major() and minor() macros. The minor one
// copies what's young and reachable into the old chunk promoted into, and
// new ones before it, as big as the old generation is, up to 1 MB.
static void collect_young(rt_proc_t *p, rt_value_t *roots, uint64_t n, const uintptr_t *fp) {
    gc_t g = {
        .young = p, .chunks = p->chunks, .ptr = p->old_ptr, .limit = p->old_ptr + old_left(p),
        .bytes = p->heap_bytes, .size = chunk_size(p->heap_bytes - young_bytes(p)),
    };
    trace(&g, roots, n, fp);
    p->chunks     = g.chunks;
    p->heap_bytes = g.bytes;
    p->old_ptr    = g.ptr;
    handles_t *young = handles_had(p, 0);
    if (young && young->n) {
        handles_t *old = handles_of(p, 1);
        for (size_t i = 0; i < young->n; i++) sweep_handle(young->at[i], old);
        young->n     = 0;
        young->bytes = 0;
    }
}

static void collect_all(rt_proc_t *p, rt_value_t *roots, uint64_t n, const uintptr_t *fp) {
    gc_t g = { .size = chunk_size(p->heap_bytes) };
    trace(&g, roots, n, fp);
    // Every live handle has moved, the young and the old, before what they
    // were in is poisoned and freed.
    if (p->side) {
        handles_t kept = { 0 };
        for (int old = 0; old < 2; old++) {
            handles_t *h = handles_of(p, old);
            for (size_t i = 0; i < h->n; i++) sweep_handle(h->at[i], &kept);
            free(h->at);
            *h = (handles_t){ 0 };
        }
        *handles_of(p, 1) = kept;
        g.live += kept.bytes;
    }
    // With SLIGHT_POISON set (t/run.sh sets it), a pointer the collector
    // missed finds garbage at once instead of memory that looks fine
    // until it's reused: 0xabab... is a boxed pointer to nowhere.
    for (rt_chunk_t *c = p->chunks; c && poison; c = c->next) memset(c + 1, 0xab, c->size);
    free_chunks(p->chunks);
    p->chunks     = g.chunks;
    p->heap_bytes = g.bytes + young_bytes(p);
    p->old_ptr    = g.ptr;
    // Next at twice what's live, but never nearer the cap than an eighth
    // of it: at twice, a process with more than half the cap live would
    // reach the cap first, and fail there with a heap of garbage (D196).
    // Nor at or below what's live, or it would collect at every slice's
    // end: when that's nearer the cap than the eighth, halfway from there
    // to the cap.
    size_t next    = 2 * g.live > GC_MIN ? 2 * g.live : GC_MIN;
    size_t half    = g.live < heap_max ? g.live + (heap_max - g.live) / 2 : heap_max;
    size_t ceiling = heap_max - heap_max / 8 > half ? heap_max - heap_max / 8 : half;
    p->gc_at       = next < ceiling ? next : ceiling;
}

// The young generation starts again, empty: its overflow chunks are freed,
// and the nursery is allocated in from its beginning, and is twice the size
// if it overflowed, up to 1 MB.
static void restart(rt_proc_t *p) {
    if (!p->nursery) return;
    size_t size = p->nursery->size;
    int    grow = p->nursery->next && size < CHUNK_MAX;
    for (rt_chunk_t *c = p->nursery; c && poison; c = c->next) memset(c + 1, 0xab, c->size);
    for (rt_chunk_t *c = p->nursery->next; c; c = c->next) p->heap_bytes -= c->size;
    free_chunks(p->nursery->next);
    p->nursery->next = NULL;
    if (grow && held(p) + size <= heap_max) {
        free(p->nursery);
        p->nursery     = new_chunk(2 * size);
        p->heap_bytes += size;
        size          *= 2;
    }
    // What's promoted isn't checked against the cap as it's copied, and
    // under SLIGHT_GC_STRESS the nursery may never overflow, where the cap
    // is checked. So a heap past its cap gets no room: its next allocation
    // goes to rt_heap_grow, and faults there, with its own site.
    p->heap_ptr   = (uintptr_t)(p->nursery + 1);
    p->heap_limit = held(p) > heap_max ? p->heap_ptr : p->heap_ptr + size;
}

// SLIGHT_GC_VERIFY=1: before a minor collection, a walk over all that's
// reachable, which ends the program at an old object pointing at a young
// one, since the collection wouldn't see that pointer, and would leave it
// pointing into a nursery started again. It's a walk over the whole heap,
// with a table of what it's seen so as not to walk what's shared twice,
// so only when asked for.
typedef struct {
    uintptr_t *slots;                   // addresses, 0 for none
    size_t     cap, n;
} seen_t;

// Whether a was seen before; it has been now.
static int seen(seen_t *s, uintptr_t a) {
    if (2 * (s->n + 1) > s->cap) {
        seen_t bigger = { 0, s->cap ? 2 * s->cap : 1024, 0 };
        bigger.slots  = rt_must(calloc(bigger.cap, sizeof *bigger.slots));
        for (size_t i = 0; i < s->cap; i++) {
            if (s->slots[i]) seen(&bigger, s->slots[i]);
        }
        free(s->slots);
        *s = bigger;
    }
    size_t i = (size_t)((a >> 4) * 0x9e3779b97f4a7c15u >> 32) & (s->cap - 1);
    for (; s->slots[i]; i = (i + 1) & (s->cap - 1)) {
        if (s->slots[i] == a) return 1;
    }
    s->slots[i] = a;
    s->n++;
    return 0;
}

static void check(const rt_proc_t *p, const rt_value_t *roots, uint64_t n, const uintptr_t *fp) {
    seen_t    s = { 0 };
    rt_work_t todo;
    rt_work_init(&todo);
    for (uint64_t i = 0; i < n; i++) rt_work_push(&todo, roots[i]);
    for (; fp && fp[0]; fp = (const uintptr_t *)fp[0]) {
        for (const rt_value_t *slot = (const rt_value_t *)(fp + 2); slot < (const rt_value_t *)fp[0]; slot++) rt_work_push(&todo, *slot);
    }
    while (todo.n) {
        rt_value_t v = rt_work_pop(&todo);
        if (!is_pointer(v) || seen(&s, v & ~(uintptr_t)RT_TAG_MASK)) continue;
        const rt_value_t *field = (const rt_value_t *)(v & ~(uintptr_t)RT_TAG_MASK);
        uint64_t          count = 2;
        if (!rt_is_cons(v)) {
            if (!rt_box_holds_values(field[0])) continue;
            count  = field[0] >> RT_BOX_SIZE_SHIFT;
            field += rt_box_first_value(field[0]);
        }
        int old = !is_young(p, v);
        for (uint64_t i = 0; i < count; i++) {
            if (old && is_pointer(field[i]) && is_young(p, field[i])) {
                fflush(stdout);
                char who[64];
                rt_pid_text(who, sizeof who, p->pid);
                fprintf(stderr, "slight: SLIGHT_GC_VERIFY: an old object in %s points at a young one\n", who);
                abort();
            }
            rt_work_push(&todo, field[i]);
        }
    }
    rt_work_free(&todo);
    free(s.slots);
}

// A minor collection once the nursery has overflowed; a major one once the
// old generation has reached gc_at, or reaches it with what the minor one
// promotes. SLIGHT_GC_STRESS makes one at every nth call's entry and every
// recv, and every eighth of those a major one.
static int due(const rt_proc_t *p) {
    return gc_stress || (p->nursery && p->nursery->next) || old_in_use(p) + shared_bytes(p, 1) >= p->gc_at
        || shared_bytes(p, 0) > SHARED_YOUNG_MAX;
}

static void collect(rt_proc_t *p, rt_value_t *roots, uint64_t n, const uintptr_t *fp) {
    int whole = old_in_use(p) + shared_bytes(p, 1) >= p->gc_at || (gc_stress && ++stressed % 8 == 0);
    if (!whole && p->nursery) {
        if (gc_verify) check(p, roots, n, fp);
        collect_young(p, roots, n, fp);
        whole = old_in_use(p) + shared_bytes(p, 1) >= p->gc_at;
    }
    if (whole && (p->chunks || p->nursery)) collect_all(p, roots, n, fp);
    restart(p);
}

// --- processes ----------------------------------------------------------------

// A new pid, for e. Pids start at 1: the root.
static rt_value_t new_entry(entry_t e) {
    uint64_t id = ++nprocs, n = id >> PAGE_BITS;
    if (n >= pages_cap) {
        uint64_t cap = pages_cap ? pages_cap * 2 : 64;
        pages = rt_must(realloc(pages, cap * sizeof *pages));
        memset(pages + pages_cap, 0, (cap - pages_cap) * sizeof *pages);
        pages_cap = cap;
    }
    if (!pages[n]) pages[n] = rt_must(calloc(1, sizeof *pages[n]));
    pages[n]->live++;
    pages[n]->e[id & (PAGE_SIZE - 1)] = e;
    nrecords++;
    return island << RT_ISLAND_SHIFT | id << RT_PID_SHIFT | RT_TAG_PID;
}

// Whether nothing more can come of pid number id's entry: no process, no
// device, and no value kept. If so, it's dead, and its page goes once all
// of its entries are, and the last pid in it has been made.
static void settle(uint64_t id) {
    entry_t *e = at(id);
    if (e->dead || e->proc || e->device || !e->ended) return;
    e->dead = 1;
    nrecords--;
    uint64_t n = id >> PAGE_BITS;
    if (--pages[n]->live == 0 && nprocs >= (n + 1) * PAGE_SIZE - 1) {
        free(pages[n]);
        pages[n] = NULL;
    }
}

rt_proc_t *rt_new_process(rt_code_t code, rt_value_t parent) {
    rt_proc_t *p = rt_must(calloc(1, sizeof *p));
    p->pid      = new_entry((entry_t){ .proc = p });
    p->parent   = parent;
    p->code     = code;
    p->gc_at    = GC_MIN;
    enqueue(p);
    return p;
}

// pid's entry. Every pid came from fork, so it has one. Don't hold on to
// it across a switch: a fork can move the table.
static entry_t *entry(rt_value_t pid, const char *site) {
    if ((pid & RT_TAG_MASK) != RT_TAG_PID || rt_is_address(pid)) rt_fault(RT_FAULT_NOT_PID, pid, site);
    return slot(pid);
}

// An actor whose connect failed to open its source ends with the reason
// as it first runs, rather than at once: a task that reads it as soon as
// it's started is then waiting when it ends, and told, since an actor's
// result isn't kept (D243). start_failed is called as its code, the
// reason its argument.
static rt_value_t start_failed(rt_value_t reason) {
    rt_end(0, reason, 0);
}

static void fail_start(rt_proc_t *p, rt_value_t reason) {
    p->code = (rt_code_t)start_failed;
    rt_chunk_t *c = copy_values(&reason, p->args, 1);
    if (c) adopt(p, c);
}

// A task (fork) or an actor (spawn, and connect's processes): only an
// actor can recv or be sent to (D243).
static rt_proc_t *new_child(rt_code_t code, uint64_t n, const rt_value_t *values, int actor) {
    rt_proc_t  *p = rt_new_process(code, rt_current->pid);
    p->actor = (uint8_t)actor;
    slot(p->pid)->actor = (uint8_t)actor;
    rt_rand_split(rt_current, p);
    rt_chunk_t *c = copy_values(values, p->args, n);
    if (c) adopt(p, c);
    return p;
}

rt_value_t rt_fork(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) {
    (void)site;
    return new_child(code, n, values, 0)->pid;
}

rt_value_t rt_spawn(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) {
    (void)site;
    return new_child(code, n, values, 1)->pid;
}

// A live process that's sent a message, a timer's included, or told of
// another's end, must be an actor: a task has nowhere to take it (D243).
static void need_actor(const entry_t *e, rt_value_t pid, const char *site) {
    if (e->proc && !e->proc->actor) rt_fault(RT_FAULT_NOT_ACTOR, pid, site);
}

// --- addresses (D243) ---------------------------------------------------------
//
// A task makes addresses to be sent to, and reads what comes to each. A
// message to one goes into the task's mailbox, marked with the address
// (the record's device field, which a task has no other use for till it
// owns devices), so read takes it by that, and nothing else can.

typedef struct rt_addresses {
    uint64_t  next;                     // the number the next address gets
    uint64_t  n, cap;
    uint64_t *open;                     // the numbers of those not closed
} rt_addresses_t;

// The children whose values a process's records keep for it, since they
// ended before it had them (D244). Few at a time: a parent joins what it
// forks, mostly before it ends.
typedef struct rt_kept {
    uint64_t  n, cap;
    uint64_t *ids;
} rt_kept_t;

// What only some processes need, apart, so that one waiting in recv, of
// which there may be a million, costs a pointer for them all.
typedef struct rt_side {
    struct rt_device *devices;          // the devices it owns, newest first
    rt_addresses_t addresses;           // a task's (D243)
    rt_kept_t      kept;                // a parent's (D244)
    rt_msg_t      *given;               // the result a join or race was woken with, till it runs (D244)
    handles_t      young_handles, old_handles;   // to shared buffers (D253)
} rt_side_t;

static rt_side_t *side_of(rt_proc_t *p) {
    if (!p->side) p->side = rt_must(calloc(1, sizeof *p->side));
    return p->side;
}

static handles_t *handles_of(rt_proc_t *p, int old) {
    rt_side_t *s = side_of(p);
    return old ? &s->old_handles : &s->young_handles;
}

static handles_t *handles_had(const rt_proc_t *p, int old) {
    if (!p->side) return NULL;
    return old ? &p->side->old_handles : &p->side->young_handles;
}

static struct rt_device *devices_of(const rt_proc_t *p) {
    return p->side ? p->side->devices : NULL;
}

#define ADDRESS_NUMBERS (1ULL << (RT_ISLAND_SHIFT - RT_ADDRESS_SHIFT))

static int address_open(const rt_proc_t *p, rt_value_t a) {
    const rt_addresses_t *s = p->side ? &p->side->addresses : NULL;
    for (uint64_t i = 0; s && i < s->n; i++) if (s->open[i] == rt_address_number(a)) return 1;
    return 0;
}

// The task a is for, while it runs and has a open; or NULL.
static rt_proc_t *address_owner(rt_value_t a) {
    uint64_t   i = rt_address_task(a);
    rt_proc_t *p = i >= 1 && i <= nprocs ? at(i)->proc : NULL;
    return p && address_open(p, a) ? p : NULL;
}

static void deliver(rt_proc_t *p, rt_msg_t *m);

// To an address, as send to a process: a message to one that's closed, or
// whose task has ended, disappears (D92).
static void to_address(rt_value_t a, rt_msg_t *m) {
    rt_proc_t *p = address_owner(a);
    if (!p) {
        free_msg(m);
        return;
    }
    m->device = a;
    deliver(p, m);
}

// A new address of the calling task's. Its number wraps past 2^20 (a
// million), skipping any still open, of which a task has few.
rt_value_t rt_address(const char *site) {
    rt_proc_t *p = rt_current;
    if (p->actor) rt_fault(RT_FAULT_NOT_TASK, p->pid, site);   // an actor takes its answers as messages
    rt_addresses_t *s = &side_of(p)->addresses;
    if (s->n == s->cap) {
        s->cap  = s->cap ? s->cap * 2 : 4;
        s->open = rt_must(realloc(s->open, s->cap * sizeof *s->open));
    }
    uint64_t num;
    do num = s->next++ % ADDRESS_NUMBERS;
    while (address_open(p, RT_ADDRESS_BIT | num << RT_ADDRESS_SHIFT | p->pid));
    s->open[s->n++] = num;
    return RT_ADDRESS_BIT | num << RT_ADDRESS_SHIFT | p->pid;
}

// Closes a, of p's: what's sent to it after goes nowhere, and what's
// waiting for it is freed.
static void close_address(rt_proc_t *p, rt_value_t a) {
    rt_addresses_t *s = &p->side->addresses;    // open, so it has one
    for (uint64_t i = 0; i < s->n; i++) {
        if (s->open[i] != rt_address_number(a)) continue;
        s->open[i] = s->open[--s->n];
        break;
    }
    rt_msg_t *last = NULL;
    for (rt_msg_t **link = &p->mail; *link; ) {
        rt_msg_t *m = *link;
        if (m->device == a) {
            *link = m->next;
            free_msg(m);
        } else {
            last = m;
            link = &m->next;
        }
    }
    p->mail_last = last;
}


// A message holding a copy of msg: the message's record and the copy in
// one chunk, the record first, so a send is one malloc, and taking the
// message adopts the chunk and frees nothing. The record is then dead
// space in the taker's heap till it next collects; but a message with
// nothing to copy (an integer, a keyword, static data) is a chunk of just
// its record, which is freed as it's taken (D199).
_Static_assert(sizeof(rt_msg_t) % 16 == 0, "a message's record keeps the copy after it 16-aligned");

static rt_msg_t *new_msg(rt_value_t msg) {
    rt_value_t  v;
    rt_chunk_t *c = copy_values_after(&msg, &v, 1, sizeof(rt_msg_t));
    rt_msg_t   *m = (rt_msg_t *)(c + 1);
    *m = (rt_msg_t){ .next = NULL, .value = v, .chunk = c, .device = 0 };
    return m;
}

// Into p's mailbox; p wakes if it's waiting for a message.
static int among(rt_value_t xs, rt_value_t v) {
    for (; xs != RT_NIL; xs = rt_cdr(xs)) if (rt_car(xs) == v) return 1;
    return 0;
}

static void deliver(rt_proc_t *p, rt_msg_t *m) {
    if (p->mail_last) p->mail_last->next = m;
    else              p->mail = m;
    p->mail_last = m;
    if (p->state == RT_WAITING || (p->state == RT_READING && among(p->joining, m->device))) enqueue(p);
}

static void command(struct rt_device *d, rt_value_t msg);
static void read_on(rt_proc_t *p, rt_value_t pid);

// To a process that has ended, a message just disappears, as in Erlang. A
// device takes it at once.
rt_value_t rt_send(rt_value_t pid, rt_value_t msg, const char *site) {
    if (rt_is_address(pid)) {
        to_address(pid, new_msg(msg));
        return RT_NIL;
    }
    entry_t *e = entry(pid, site);
    need_actor(e, pid, site);
    if (e->proc) {
        deliver(e->proc, new_msg(msg));
    } else if (e->device) {
        rt_current->at = (uintptr_t)__builtin_return_address(0);   // for :system
        command(e->device, msg);
    }
    return RT_NIL;
}

// A device's message is cut only now, when its owner waits again after
// taking the last one, so whatever the owner did with that one, (:read
// how) or disconnect, applies to the next (D158).
rt_value_t rt_recv(rt_value_t *args, uint64_t n, rt_code_t code) {
    rt_proc_t *p = rt_current;
    if (!p->actor) rt_fault(RT_FAULT_NOT_ACTOR, p->pid, "recv");   // the compiler refuses it; this is a backstop
    if (p->reading) {
        rt_value_t device = p->reading;
        p->reading = 0;
        read_on(p, device);                     // which may end p, if reading fails
    }
    if (due(p)) collect(p, args, n, NULL);
    rt_msg_t  *m = p->mail;
    if (m) {
        p->mail = m->next;
        if (!p->mail) p->mail_last = NULL;
        rt_value_t v = m->value;
        p->reading = m->device;
        if (m->chunk->size == sizeof *m) free_msg(m);
        else                             adopt(p, m->chunk);
        return v;
    }
    p->code = code;
    memcpy(p->args, args, n * sizeof *args);
    p->state = RT_WAITING;
    to_scheduler(p);
    __builtin_unreachable();
}

void rt_dead_letter(rt_value_t msg, const char *site) {
    rt_buf_t b = { 0 };
    rt_render(&b, msg, 0);
    fflush(stdout);
    fprintf(stderr, "dead letter: %.*s (%s)\n", (int)b.len, b.bytes, site);
    rt_buf_free(&b);
}

// --- how processes end --------------------------------------------------------
//
// Values built here, like (:ok value) around a record's value, are built
// on the C stack, and copied to where they're going.

// The result in e, (:ok value) or (:error reason), in cells (4 words); or
// :ended, for an actor that has ended (D243).
static rt_value_t result_at(rt_value_t *cells, const entry_t *e) {
    if (e->ended) return rt_symbol(RT_SYM_SYSTEM + RT_SYS_ENDED);
    return rt_cons_at(cells, rt_symbol(e->ok ? RT_SYM_OK : RT_SYM_ERROR), rt_cons_at(cells + 2, e->value, RT_NIL));
}

// Sends (:exit pid result) to `to`, about the process that ended at e.
static void notify(rt_value_t to, rt_value_t pid, const entry_t *e) {
    rt_proc_t *w = slot(to)->proc;
    if (!w) return;
    _Alignas(16) rt_value_t cells[10];
    rt_value_t result = result_at(cells, e);
    deliver(w, new_msg(rt_cons_at(cells + 4, rt_symbol(RT_SYM_EXIT), rt_cons_at(cells + 6, pid, rt_cons_at(cells + 8, result, RT_NIL)))));
}

// Takes w off both its lists, and frees it.
static void unwatch(rt_watch_t *w) {
    if (w->prev)      w->prev->next           = w->next;      else w->watched->watchers     = w->next;
    if (w->next)      w->next->prev           = w->prev;      else w->watched->last_watcher = w->prev;
    if (w->prev_held) w->prev_held->next_held = w->next_held; else w->watcher->watching     = w->next_held;
    if (w->next_held) w->next_held->prev_held = w->prev_held;
    free(w);
}

// p ends. Its result goes into its exit record, its joiners wake, its
// monitors hear, and its heap and mail are freed. Its stack and struct are
// the caller's to free (retire), since p may be running on that stack.
static void disconnect(rt_proc_t *p);
static void close_all(rt_proc_t *p);

// What a watch does when its process ends: tell the watcher, as a
// monitor; wake a race; or, a read's, wake or tell a reader (D243).
enum { WATCH_TELLS, WATCH_RACE, WATCH_READ };

// A copy of the result in e, for a joiner or racer that ends waits for it
// with: it runs later, when the value may have gone (D244).
static rt_msg_t *result_msg(const entry_t *e) {
    _Alignas(16) rt_value_t cells[4];
    return new_msg(result_at(cells, e));
}

// A task reading pid, which ends with the result in e, is told it now, in
// a message marked with pid, since it may not be kept (D243, D244).
static void tell_reader(rt_proc_t *reader, rt_value_t pid, const entry_t *e) {
    rt_msg_t *m = result_msg(e);
    m->device = pid;
    deliver(reader, m);
}

static void keep_for(rt_proc_t *parent, uint64_t id) {
    rt_kept_t *k = &side_of(parent)->kept;
    if (k->n == k->cap) {
        k->cap = k->cap ? k->cap * 2 : 4;
        k->ids = rt_must(realloc(k->ids, k->cap * sizeof *k->ids));
    }
    k->ids[k->n++] = id;
}

// The value id's record keeps goes: it's :ended from now on.
static void release(uint64_t id) {
    entry_t *e = at(id);
    free_chunks(e->chunk);
    e->chunk = NULL;
    e->value = RT_NIL;
    e->ended = 1;
    settle(id);
}

// p, the caller, has had pid's result from its record: if pid is a child
// of p's, its value was kept for p, and now goes.
static void taken(rt_proc_t *p, rt_value_t pid) {
    rt_kept_t *k  = p->side ? &p->side->kept : NULL;
    uint64_t   id = rt_pid_number(pid);
    for (uint64_t i = k ? k->n : 0; i-- > 0; ) {
        if (k->ids[i] != id) continue;
        k->ids[i] = k->ids[--k->n];
        release(id);
        return;
    }
}

// What was kept for p goes, since no one else is waiting for it; and a
// result it was given, if it was killed before it ran again; and its
// addresses.
static void free_side(rt_proc_t *p) {
    rt_side_t *s = p->side;
    if (!s) return;
    for (uint64_t i = 0; i < s->kept.n; i++) release(s->kept.ids[i]);
    if (s->given) free_msg(s->given);
    free(s->kept.ids);
    free(s->addresses.open);
    free(s);
    p->side = NULL;
}

// The result a join or race was woken with, now in the caller's heap.
static rt_value_t take_given(rt_proc_t *p) {
    rt_msg_t *m = p->side->given;
    p->side->given = NULL;
    rt_value_t v = m->value;
    if (m->chunk->size == sizeof *m) free_msg(m);
    else                             adopt(p, m->chunk);
    return v;
}

static void finish(rt_proc_t *p, int ok, rt_value_t value, int logged) {
    uint64_t id = rt_pid_number(p->pid);
    entry_t *e  = at(id);
    int      had = 0;                          // whether the parent is told now, as those waiting are
    e->ok    = ok;
    e->value = value;                          // in p's heap till it's told to whoever waits, below
    if (id == 1 && !ok && !logged) {
        rt_buf_t b = { 0 };
        rt_render(&b, e->value, 0);
        fflush(stdout);
        fprintf(stderr, "error: %.*s\n", (int)b.len, b.bytes);
        rt_buf_free(&b);
    }
    for (rt_proc_t *j = p->joiners, *next; j; j = next) {
        next     = j->next_joiner;
        side_of(j)->given = result_msg(e);
        had     |= j->pid == p->parent;
        enqueue(j);
    }
    while (p->watchers) {
        rt_watch_t *w = p->watchers;
        if (w->wakes == WATCH_RACE) {
            // A race wakes on the first of its pids to end; one that ends
            // before the racer has run again only goes.
            if (w->watcher->state == RT_RACING) {
                w->watcher->joining = p->pid;
                side_of(w->watcher)->given = result_msg(e);
                enqueue(w->watcher);
            }
        } else if (w->wakes == WATCH_READ) {
            // A reader's watch, kept from one read to the next while the
            // pid is among its sources: the result is told it, even if
            // it's no longer waiting, so that its next read has it.
            tell_reader(w->watcher, p->pid, e);
            had |= w->watcher->pid == p->parent;
        } else if (w->watcher != p) {
            // Not p itself: its notice would be dropped with its mail, and
            // if it was killed waiting in recv, it would go back on the run
            // queue as it's freed.
            notify(w->watcher->pid, p->pid, e);
            had |= w->watcher->pid == p->parent;
        }
        unwatch(w);
    }
    // A task's value is kept for its parent till it has it: the root's
    // for the program to print, and one that ended before its parent
    // joined, read or was told of it (D244). So is an actor's whose
    // parent is a task, which can read it, so that it needn't be reading
    // as the actor ends (D245); an actor's parent can't read, and is told
    // only if it monitors. Else it's :ended now (D243).
    rt_proc_t *parent = p->parent == RT_NIL ? NULL : slot(p->parent)->proc;
    if (p->parent == RT_NIL || (!had && parent && (!p->actor || !parent->actor))) {
        e->chunk = copy_values(&value, &e->value, 1);
        if (parent) keep_for(parent, id);
    } else {
        e->value = RT_NIL;
        e->ended = 1;
    }

    // The monitors p set go too. Their notices would be dropped, and kept,
    // they'd pile up on a process that lives long: each ask's box left one
    // on the structure it asked, and each monitor walked them all (D183).
    while (p->watching) unwatch(p->watching);
    free_heap(p);
    if (p->keypress) disconnect(p);
    close_all(p);
    free_side(p);
    p->state = RT_DONE;
}

static RT_ISLAND uint64_t retired_calls;    // the calls of the processes that have ended

static void retire(rt_proc_t *p) {
    retired_calls += calls_of(p);
    if (p->stack) release_stack(p);
    slot(p->pid)->proc = NULL;
    settle(rt_pid_number(p->pid));
    free(p);
}

void rt_end(int ok, rt_value_t value, int logged) {
    rt_proc_t *p = rt_current;
    finish(p, ok, value, logged);
    to_scheduler(p);                            // which retires it
    __builtin_unreachable();
}

void rt_exit(rt_proc_t *p, rt_value_t result) {
    (void)p;                                    // it's rt_current
    rt_end(1, result, 0);
}

rt_value_t rt_raise(rt_value_t reason, const char *site) {
    (void)site;
    rt_end(0, reason, 0);
}

rt_value_t rt_join(rt_value_t pid, const char *site) {
    rt_proc_t *p = rt_current;
    entry_t   *e = entry(pid, site);
    rt_proc_t *t = e->proc;
    if (e->actor && (t || !e->ended)) rt_fault(RT_FAULT_NOT_TASK, pid, site);   // read, not joined, even once kept (D243, D245)
    if (pid == p->pid) rt_fault(RT_FAULT_JOIN_SELF, pid, site);
    if (t) {
        rt_proc_t **link = &t->joiners;
        while (*link) link = &(*link)->next_joiner;
        *link          = p;
        p->next_joiner = NULL;
        p->joining     = pid;
        p->state       = RT_JOINING;
        p->at          = (uintptr_t)__builtin_return_address(0);
        to_scheduler(p);                        // until t ends, and finish wakes us with its result
        return take_given(p);
    }
    _Alignas(16) rt_value_t cells[4];
    rt_value_t result = copy_here(result_at(cells, e));
    taken(p, pid);
    return result;
}

// A watch by me on t: last on t's list, and first on mine.
static void watch(rt_proc_t *t, rt_proc_t *me, int wakes) {
    rt_watch_t *w = rt_must(malloc(sizeof *w));
    *w = (rt_watch_t){ .prev = t->last_watcher, .next_held = me->watching, .watched = t, .watcher = me, .wakes = wakes };
    if (t->last_watcher) t->last_watcher->next = w;
    else                 t->watchers           = w;
    t->last_watcher = w;
    if (me->watching) me->watching->prev_held = w;
    me->watching = w;
}

// The oldest of p's watches. A race adds its watches in the order of its
// pids, each first, so from here back to the newest they're in that order.
static const rt_watch_t *race_last(const rt_proc_t *p) {
    const rt_watch_t *w = p->watching;
    while (w && w->next_held) w = w->next_held;
    return w;
}

// The first of pids to end, as (pid result). If some have ended already,
// it's the first of those in the list, as JavaScript's Promise.race
// settles; else a watch on each wakes the caller when the first of them
// ends (D220). The pids are all checked before it waits, so a bad one
// faults whichever ends first.
rt_value_t rt_join_race(rt_value_t pids, const char *site) {
    rt_proc_t *p   = rt_current;
    rt_value_t won = 0;
    if (pids == RT_NIL) rt_fault(RT_FAULT_NOT_PID, pids, site);   // nothing to wait for, ever
    for (rt_value_t xs = pids; xs != RT_NIL; xs = rt_cdr(xs)) {
        if (!rt_is_cons(xs)) rt_fault(RT_FAULT_NOT_LIST, pids, site);
        rt_value_t pid = rt_car(xs);
        entry_t   *e   = entry(pid, site);
        if (pid == p->pid) rt_fault(RT_FAULT_JOIN_SELF, pid, site);
        if (e->actor && (e->proc || !e->ended)) rt_fault(RT_FAULT_NOT_TASK, pid, site);
        if (!won && !e->proc) won = pid;
    }
    if (!won) {
        for (rt_value_t xs = pids; xs != RT_NIL; xs = rt_cdr(xs)) watch(slot(rt_car(xs))->proc, p, WATCH_RACE);
        p->state = RT_RACING;
        p->at    = (uintptr_t)__builtin_return_address(0);
        to_scheduler(p);                        // until the first ends, and finish wakes us
        won = p->joining;
        for (rt_watch_t *w = p->watching, *next; w; w = next) {
            next = w->next_held;
            if (w->wakes == WATCH_RACE) unwatch(w);
        }
        rt_value_t  result = take_given(p);
        rt_value_t *cells  = rt_alloc(4 * sizeof *cells, site);
        return rt_cons_at(cells, won, rt_cons_at(cells + 2, result, RT_NIL));
    }
    // A race doesn't take a value: join/all and join/any join what they race.
    _Alignas(16) rt_value_t cells[8];
    rt_value_t result = result_at(cells, slot(won));
    return copy_here(rt_cons_at(cells + 4, won, rt_cons_at(cells + 6, result, RT_NIL)));
}

static int owns(const rt_proc_t *p, const struct rt_device *d);

// Whether p has a read's watch on t already.
static int reads(const rt_proc_t *p, const rt_proc_t *t) {
    for (const rt_watch_t *w = p->watching; w; w = w->next_held) if (w->wakes == WATCH_READ && w->watched == t) return 1;
    return 0;
}

// The first of srcs with something for the calling task: a message to one
// of its addresses, or a process's end, taken in the order of srcs, as
// join/race takes the first of its pids to have ended (D220); as (src
// what) if pair is set, or just what. Waits, keeping the stack, till one
// has something: a watch on each pid among them, as a race's, and a
// message to one of them, wake it (D243).
static rt_value_t read_from(rt_value_t srcs, int pair, uintptr_t at, const char *site) {
    rt_proc_t *p = rt_current;
    if (p->actor) rt_fault(RT_FAULT_NOT_TASK, p->pid, site);   // an actor takes its messages with recv
    if (srcs == RT_NIL) rt_fault(RT_FAULT_NOT_PID, srcs, site);  // nothing to wait for, ever
    for (rt_value_t xs = srcs; xs != RT_NIL; xs = rt_cdr(xs)) {
        if (!rt_is_cons(xs)) rt_fault(RT_FAULT_NOT_LIST, srcs, site);
        rt_value_t src = rt_car(xs);
        if (rt_is_address(src)) {
            if (rt_address_task(src) != rt_pid_number(p->pid) || !address_open(p, src)) rt_fault(RT_FAULT_NOT_PID, src, site);
        } else {
            entry(src, site);
            if (src == p->pid) rt_fault(RT_FAULT_JOIN_SELF, src, site);
        }
    }
    if (p->reading) {                           // the device it took a message from last cuts its next (D158)
        rt_value_t device = p->reading;
        p->reading = 0;
        read_on(p, device);
    }
    for (rt_watch_t *w = p->watching, *next; w; w = next) {   // watches kept from a read before, no longer wanted
        next = w->next_held;
        if (w->wakes == WATCH_READ && !among(srcs, w->watched->pid)) unwatch(w);
    }
    for (;;) {
        for (rt_value_t xs = srcs; xs != RT_NIL; xs = rt_cdr(xs)) {
            rt_value_t     src = rt_car(xs);
            const entry_t *e   = rt_is_address(src) ? NULL : slot(src);
            rt_msg_t      *last = NULL;             // what waits for an address, or from a device, closed since or not
            for (rt_msg_t **link = &p->mail; *link; link = &(*link)->next) {
                rt_msg_t *m = *link;
                if (m->device != src) {
                    last = m;
                    continue;
                }
                *link = m->next;
                if (p->mail_last == m) p->mail_last = last;
                rt_value_t v = m->value;
                if (!rt_is_address(src)) p->reading = src;
                if (m->chunk->size == sizeof *m) free_msg(m);
                else                             adopt(p, m->chunk);
                if (!pair) return v;
                rt_value_t *cells = rt_alloc(4 * sizeof *cells, site);
                return rt_cons_at(cells, src, rt_cons_at(cells + 2, v, RT_NIL));
            }
            if (e && !e->proc && !owns(p, e->device)) {   // ended, or a device closed or another's
                _Alignas(16) rt_value_t cells[8];
                rt_value_t result = result_at(cells, e);
                result = copy_here(pair ? rt_cons_at(cells + 4, src, rt_cons_at(cells + 6, result, RT_NIL)) : result);
                taken(p, src);
                return result;
            }
        }
        for (rt_value_t xs = srcs; xs != RT_NIL; xs = rt_cdr(xs)) {
            rt_proc_t *t = rt_is_address(rt_car(xs)) ? NULL : slot(rt_car(xs))->proc;
            if (t && !reads(p, t)) watch(t, p, WATCH_READ);
        }
        p->joining = srcs;
        p->state   = RT_READING;
        p->at      = at;
        to_scheduler(p);                        // till a message or an end wakes it; its watches stay
    }
}

rt_value_t rt_read(rt_value_t src, const char *site) {
    _Alignas(16) rt_value_t cell[2];
    return read_from(rt_cons_at(cell, src, RT_NIL), 0, (uintptr_t)__builtin_return_address(0), site);
}

rt_value_t rt_read_any(rt_value_t srcs, const char *site) {
    return read_from(srcs, 1, (uintptr_t)__builtin_return_address(0), site);
}

rt_value_t rt_monitor(rt_value_t pid, const char *site) {
    entry_t *e = entry(pid, site);
    if (!rt_current->actor) rt_fault(RT_FAULT_NOT_ACTOR, rt_current->pid, site);   // the notice would go nowhere
    if (e->proc) {
        watch(e->proc, rt_current, 0);
    } else {
        notify(rt_current->pid, pid, e);
        taken(rt_current, pid);
    }
    return RT_NIL;
}

// Ends p, which isn't running, with (:error reason), wherever it is.
static void end_other(rt_proc_t *p, rt_value_t reason) {
    if (p->state == RT_READY) unqueue(p);
    if (p->state == RT_JOINING) {
        rt_proc_t  *t    = slot(p->joining)->proc;
        rt_proc_t **link = &t->joiners;
        while (*link != p) link = &(*link)->next_joiner;
        *link = p->next_joiner;
    }
    // A sleeper's timer stays, and is dropped when it's due.
    finish(p, 0, reason, 0);
    retire(p);
}

rt_value_t rt_kill(rt_value_t pid, const char *site) {
    rt_proc_t *p = entry(pid, site)->proc;
    if (!p) return RT_NIL;
    if (p == rt_current) rt_end(0, rt_symbol(RT_SYM_KILLED), 0);
    end_other(p, rt_symbol(RT_SYM_KILLED));
    return RT_NIL;
}

static void events(void);

void rt_preempt(rt_proc_t *p, rt_value_t *frame, uint64_t n, const uintptr_t *fp, uintptr_t entry) {
    p->at = entry;
    if (due(p)) collect(p, frame, n, fp);
    if (p->slice_rest > 0) {                    // here before the slice's end, to collect
        p->reductions  = gc_stress && gc_stress < p->slice_rest ? gc_stress : p->slice_rest;
        p->slice_rest -= p->reductions;
        return;
    }
    refill(p);
    events();                                   // so a busy process can't hold up a timer or a key
    if (ready_head) {
        enqueue(p);
        to_scheduler(p);
    }
}

void rt_yield(void) {
    rt_proc_t *p = rt_current;
    p->at = (uintptr_t)__builtin_return_address(0);
    refill(p);
    enqueue(p);
    to_scheduler(p);
}

// --- timers -------------------------------------------------------------------
//
// after and sleep set timers, kept in a binary heap in the order they're
// due, and then in the order they were set. after's timer holds its
// message, copied when it was set, as send's would be; sleep's wakes the
// sleeper. after's timers are numbered, 1 up, and timer/cancel takes one
// back by its number, found in ids (D232). A timer whose process has
// ended is dropped when it's due, as a message to it would be (D92), or
// sooner, when nothing can run and it's next: nothing can come of it, so
// the program doesn't wait for it, and the clock doesn't move to it.
//
// The clock is the system's monotonic one, in nanoseconds, read through
// replay.c like everything else from outside (D170); or, with
// SLIGHT_CLOCK=virtual (t/run.sh sets it), one for tests that starts at 0
// and moves only when nothing can run, straight to the next timer. Timer
// tests are then exact, and take no real time.

typedef struct {                        // (timer_t is POSIX's)
    uint64_t    due, seq;
    uint64_t    id;                     // after's number, or 0
    rt_value_t  pid;
    rt_msg_t   *msg;                    // after's message, or NULL: sleep
} alarm_t;

static RT_ISLAND alarm_t  *alarms;      // alarms[0] is the next due
static RT_ISLAND uint64_t  nalarms, alarms_cap, alarms_set;
static RT_ISLAND uint64_t  afters;      // after's timers numbered so far

// Where each of after's pending timers is in alarms, by its number:
// open addressing, a slot of number 0 empty, and a slot emptied by
// moving back the ones after it that belong before it, so none is ever
// marked deleted. Updated as the heap moves them.
static RT_ISLAND uint64_t *ids, *places;
static RT_ISLAND uint64_t  ids_cap, nids;   // ids_cap a power of two, or 0
static RT_ISLAND uint64_t  virtual_now;     // each island's clock, if virtual, its own (D256)
static int                 virtual_clock;
static uint64_t            started;         // when the program started, for :system
static int64_t             epoch;           // the time of day then, on the virtual clock (D254)

static uint64_t now(void) {
    return virtual_clock ? virtual_now : rt_io_clock();
}

// When a timer set now for ms milliseconds is due. A negative ms counts
// as 0; one too far off for the clock is due at its end.
static uint64_t due_in(rt_value_t ms, const char *site) {
    int64_t  n = rt_need_int(ms, site);
    uint64_t t = now();
    if (n <= 0) return t;
    return (uint64_t)n > (UINT64_MAX - t) / 1000000 ? UINT64_MAX : t + (uint64_t)n * 1000000;
}

static int before(const alarm_t *a, const alarm_t *b) {
    return a->due < b->due || (a->due == b->due && a->seq < b->seq);
}

static uint64_t id_home(uint64_t id) {
    return (id * 0x9E3779B97F4A7C15u) & (ids_cap - 1);
}

// id's slot in ids, or the empty one where it would go.
static uint64_t id_slot(uint64_t id) {
    uint64_t i = id_home(id);
    while (ids[i] && ids[i] != id) i = (i + 1) & (ids_cap - 1);
    return i;
}

static void id_add(uint64_t id) {
    if ((nids + 1) * 2 > ids_cap) {
        uint64_t *old_ids = ids, *old_places = places, old_cap = ids_cap;
        ids_cap = ids_cap ? ids_cap * 2 : 64;
        ids     = rt_must(calloc(ids_cap, sizeof *ids));
        places  = rt_must(malloc(ids_cap * sizeof *places));
        for (uint64_t i = 0; i < old_cap; i++) {
            if (!old_ids[i]) continue;
            uint64_t j = id_slot(old_ids[i]);
            ids[j]    = old_ids[i];
            places[j] = old_places[i];
        }
        free(old_ids);
        free(old_places);
    }
    ids[id_slot(id)] = id;
    nids++;
}

static void id_drop(uint64_t id) {
    uint64_t mask = ids_cap - 1, i = id_slot(id);
    for (uint64_t j = (i + 1) & mask; ids[j]; j = (j + 1) & mask) {
        if (((j - id_home(ids[j])) & mask) < ((j - i) & mask)) continue;   // its home is after i: it stays
        ids[i]    = ids[j];
        places[i] = places[j];
        i         = j;
    }
    ids[i] = 0;
    nids--;
}

// alarms[i] = a, and where it is noted if it's after's.
static void put(uint64_t i, alarm_t a) {
    alarms[i] = a;
    if (a.id) places[id_slot(a.id)] = i;
}

static void set_alarm(uint64_t due, rt_value_t pid, rt_msg_t *msg, uint64_t id) {
    if (nalarms == alarms_cap) {
        alarms_cap = alarms_cap ? alarms_cap * 2 : 64;
        alarms     = rt_must(realloc(alarms, alarms_cap * sizeof *alarms));
    }
    if (id) id_add(id);
    alarm_t  a = { due, alarms_set++, id, pid, msg };
    uint64_t i = nalarms++;
    for (; i > 0 && before(&a, &alarms[(i - 1) / 2]); i = (i - 1) / 2) put(i, alarms[(i - 1) / 2]);
    put(i, a);
}

// Takes alarms[i] out, the last one going in its place, up or down.
static void remove_alarm(uint64_t i) {
    if (alarms[i].id) id_drop(alarms[i].id);
    alarm_t last = alarms[--nalarms];
    if (i == nalarms) return;
    for (; i > 0 && before(&last, &alarms[(i - 1) / 2]); i = (i - 1) / 2) put(i, alarms[(i - 1) / 2]);
    for (uint64_t c; (c = 2 * i + 1) < nalarms; i = c) {
        if (c + 1 < nalarms && before(&alarms[c + 1], &alarms[c])) c++;
        if (!before(&alarms[c], &last)) break;
        put(i, alarms[c]);
    }
    put(i, last);
}

static alarm_t next_alarm(void) {
    alarm_t first = alarms[0];
    remove_alarm(0);
    return first;
}

// A timer a device sets itself (a lookup's), unset when it's no longer
// wanted, since a pending timer keeps the program running: its seq + 1.
static uint64_t own_alarm(uint64_t due, rt_value_t pid) {
    set_alarm(due, pid, NULL, 0);
    return alarms_set;
}

static void unset_alarm(uint64_t *alarm) {
    for (uint64_t i = 0; *alarm && i < nalarms; i++) {
        if (alarms[i].seq + 1 == *alarm) remove_alarm(i);
    }
    *alarm = 0;
}

static void lookup_timed_out(struct rt_device *d, uint64_t seq);

// Fires every timer that's due.
static void ring(void) {
    for (uint64_t t = nalarms ? now() : 0; nalarms && alarms[0].due <= t; ) {
        alarm_t    a = next_alarm();
        if (rt_is_address(a.pid)) {
            to_address(a.pid, a.msg);
            continue;
        }
        rt_proc_t *p = slot(a.pid)->proc;
        if (!p) {
            struct rt_device *d = slot(a.pid)->device;
            if (d && a.msg)  command(d, a.msg->value);
            else if (d)      lookup_timed_out(d, a.seq);
            if (a.msg) free_msg(a.msg);
        } else if (a.msg) {
            deliver(p, a.msg);
        } else {
            enqueue(p);                         // asleep: only kill could have woken it
        }
    }
}

rt_value_t rt_after(rt_value_t ms, rt_value_t pid, rt_value_t msg, const char *site) {
    uint64_t due = due_in(ms, site);
    if (!rt_is_address(pid)) need_actor(entry(pid, site), pid, site);
    set_alarm(due, pid, new_msg(msg), ++afters);
    return rt_int((int64_t)afters);
}

rt_value_t rt_timer_cancel(rt_value_t t, const char *site) {
    int64_t id = rt_need_int(t, site);
    if (id <= 0 || !ids_cap || ids[id_slot((uint64_t)id)] != (uint64_t)id) return RT_FALSE;
    uint64_t  i = places[id_slot((uint64_t)id)];
    rt_msg_t *m = alarms[i].msg;
    remove_alarm(i);
    free_msg(m);
    return RT_TRUE;
}

// Whether nothing can come of a: its process has ended, or its device
// closed (D232).
static int orphaned(const alarm_t *a) {
    if (rt_is_address(a->pid)) return !address_owner(a->pid);
    const entry_t *e = slot(a->pid);
    return !e->proc && !e->device;
}

// Drops the timers next due that nothing can come of, so that the
// program neither waits for them nor moves the clock to them. Only from
// the top of the heap, so it costs no search: one further down is
// dropped when it comes to the top, or is due.
static void drop_orphans(void) {
    while (nalarms && orphaned(&alarms[0])) {
        alarm_t a = next_alarm();
        if (a.msg) free_msg(a.msg);
    }
}

rt_value_t rt_sleep(rt_value_t ms, const char *site) {
    rt_proc_t *p = rt_current;
    set_alarm(due_in(ms, site), p->pid, NULL, 0);
    p->state = RT_SLEEPING;
    p->at    = (uintptr_t)__builtin_return_address(0);
    to_scheduler(p);                            // until ring wakes it
    return RT_NIL;
}

// Read through now(), so a recorded run replays with the same readings,
// and on the virtual clock it moves only as the timers do.
rt_value_t rt_clock_us(const char *site) {
    (void)site;
    return rt_int((int64_t)((now() - started) / 1000));
}

// The time of day: the system's, or on the virtual clock its start's
// plus the virtual time, so a test's dates are the same every run (D254).
rt_value_t rt_clock_now(const char *site) {
    (void)site;
    return rt_int(virtual_clock ? epoch + (int64_t)((now() - started) / 1000) : rt_io_realtime());
}

// --- the keyboard -------------------------------------------------------------
//
// Every process connected to :keypress gets every key as a message, in
// the order they connected. stdin is read whether or not it's a terminal,
// so a test can pipe keys in; raw mode is on (if it is one) while any
// process is connected. When stdin ends, the keys just stop.
//
// When nothing can run, the scheduler waits in select() for a key or the
// next timer, whichever comes first; while processes are busy, it looks
// at stdin every 10 ms. On the virtual clock, keys come only when nothing
// can run, one at a time, before the clock moves: as if typed by someone
// who waits for the program to settle before each key.

#define INPUT_EVERY 10000000            // ns between looks at stdin while busy

// The terminal is island 0's (D256), so what's here is too: no other
// island's thread touches it.
static rt_proc_t     **connected;
static uint64_t        nconnected, connected_cap;
static unsigned char   input[4096];     // read from stdin...
static size_t          input_at, input_len; // ...and decoded up to input_at
static int             input_ended;
static uint64_t        input_seen;      // when stdin was last looked at while busy

rt_value_t rt_connect(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) {
    rt_value_t pid = rt_spawn(code, n, values, site);
    if (nconnected == connected_cap) {
        connected_cap = connected_cap ? connected_cap * 2 : 8;
        connected     = rt_must(realloc(connected, connected_cap * sizeof *connected));
    }
    rt_proc_t *p = slot(pid)->proc;
    p->keypress  = 1;
    connected[nconnected++] = p;
    rt_tty_raw(1);
    return pid;
}

static void disconnect(rt_proc_t *p) {
    uint64_t i = 0;
    while (connected[i] != p) i++;
    nconnected--;
    memmove(&connected[i], &connected[i + 1], (nconnected - i) * sizeof *connected);
    if (!nconnected) rt_tty_raw(0);
}

static int listening(void) {
    return nconnected && !input_ended;
}

static RT_ISLAND uint64_t nsockets;
static int sockets_waiting(void);
static int working(void);
static void     socket_fds(fd_set *in, fd_set *out, int *max);
static int      socket_events(fd_set *in, fd_set *out);

#define IO_KEYS    1                    // what wait_for saw: stdin has something to read...
#define IO_SOCKETS 2                    // ...or a socket did something
#define SETTLE_US  200000               // how long the virtual clock waits for a lookup's answer, for real

// Waits up to us microseconds (forever, if negative) for stdin to have
// something to read, if anyone's listening and what was read before has
// been decoded, or for a socket or a command to be ready; handles the
// sockets and commands that are, and says what it saw.
static int wait_for(int64_t us) {
    int    keys = listening() && input_at == input_len, max = keys ? STDIN_FILENO : -1;
    fd_set in, out;
    FD_ZERO(&in);
    FD_ZERO(&out);
    if (keys) FD_SET(STDIN_FILENO, &in);
    socket_fds(&in, &out, &max);
    // Nothing to look at and no time to wait: the virtual clock asks at
    // every step it takes, which in a simulation is every event (D242).
    if (max < 0 && us == 0) return 0;
    struct timeval tv, *timeout = NULL;
    if (us >= 0) {
        if (us > 3600000000) us = 3600000000;  // macOS's select refuses more than 10^8 s
        tv      = (struct timeval){ .tv_sec = (time_t)(us / 1000000), .tv_usec = (suseconds_t)(us % 1000000) };
        timeout = &tv;
    }
    if (rt_io_select(max + 1, &in, &out, timeout) <= 0) return 0;
    int saw = keys && FD_ISSET(STDIN_FILENO, &in) ? IO_KEYS : 0;
    return saw | (socket_events(&in, &out) ? IO_SOCKETS : 0);
}

// Reads what stdin has, once everything read before has been decoded.
static void read_input(void) {
    if (input_at < input_len) return;
    ssize_t got = rt_io_read(STDIN_FILENO, input, sizeof input);
    if (got == 0 || (got < 0 && errno != EINTR && errno != EAGAIN)) input_ended = 1;
    input_at  = 0;
    input_len = got > 0 ? (size_t)got : 0;
}

static void send_key(rt_value_t key) {
    for (uint64_t i = 0; i < nconnected; i++) deliver(connected[i], new_msg(key));
}

// Sends the next key that has been read, if there is one.
static int next_key(void) {
    if (input_at == input_len) return 0;
    input_at += rt_key(input + input_at, input_len - input_at, send_key);
    return 1;
}

// At every decision the scheduler makes, and when a process is preempted:
// fires the timers that are due, and on the real clock, every 10 ms, takes
// the keys typed since and sees to the sockets.
static void events(void) {
    ring();
    if (virtual_clock || (!listening() && !nsockets)) return;
    uint64_t t = now();
    if (t - input_seen < INPUT_EVERY) return;
    input_seen = t;
    if (wait_for(0) & IO_KEYS) {
        read_input();
        while (next_key()) {}
    }
}

// Nothing can run: waits for a key, a socket, a command or the next
// timer. Returns 0 if none of them can come: an open socket or a command
// counts only while select() can wait on it (wants); one whose owner has
// its last message and isn't waiting, or that no process has been handed,
// can't do anything till a process runs, and none can. On the virtual
// clock, the sockets and commands come first; then a command whose input
// is closed, which can only be working on its own, is waited for, for
// real, keys and time standing still meanwhile (D185); then a key, then
// the clock moves; only when nothing else can happen does it wait for real.
static int idle(void) {
    fflush(stdout);                             // so a program stopped as it waits has lost none of it (D229)
    drop_orphans();
    if (!listening() && !nalarms && !sockets_waiting()) return 0;
    if (virtual_clock) {
        int saw = wait_for(0);
        if ((saw & IO_KEYS) && input_at == input_len) read_input();
        if (saw & IO_SOCKETS) return 1;
        if (working()) {
            if (wait_for(-1) & IO_KEYS) read_input();
            return 1;
        }
        if (listening() && next_key()) return 1;
        // A lookup's timer is the runtime's, not the program's, so it
        // mustn't beat an answer on its way: from a name server in the same
        // program, say, which on macOS's loopback arrives a moment after
        // it's sent, not by the time select() looks. So before the clock
        // moves to one, the sockets are waited for, for real, a little.
        if (nalarms && !alarms[0].msg && slot(alarms[0].pid)->device && (saw = wait_for(SETTLE_US))) {
            if ((saw & IO_KEYS) && input_at == input_len) read_input();
            return 1;
        }
        if (nalarms) {
            virtual_now = alarms[0].due;
            return 1;
        }
        // Only the world outside can do anything now: wait for it, unless
        // stdin has just ended, and with it the last thing that could come.
        if ((listening() || sockets_waiting()) && (wait_for(-1) & IO_KEYS)) read_input();
        return 1;
    }
    uint64_t t = now();
    if (nalarms && alarms[0].due <= t) return 1;
    if (wait_for(nalarms ? (int64_t)((alarms[0].due - t + 999) / 1000) : -1) & IO_KEYS) {
        read_input();
        while (next_key()) {}
    }
    return 1;
}

// --- devices: files, sockets and commands -------------------------------------
//
// connect opens a device: a pid that the runtime serves instead of compiled
// code, owned by the process connect forks. The device's first message to
// its owner is (:open f), f being its pid. disconnect closes it, and so does
// its owner ending; anything sent to it after that goes nowhere. (connect
// dev expr) hands a device to a new process, which hears (:open f) in turn.
// Anything sent to a device that isn't a (:write ...) it takes is a dead
// letter. If a device can't be opened, its owner ends before it runs, with
// (:error (name path)), name being errno's; if a read or a write fails, it
// ends the same way then.
//
// A reader (a file from :fs/read, or a connection) sends its owner one
// message at a time: it cuts the next when the owner next waits in recv
// after taking the last (rt_recv), so a long file never piles up in a
// mailbox, and whatever the owner does with a message applies to the next
// (D158). It cuts what it reads as (:read how) says, how being :lines,
// the first way, (:line f s) without the newline; :chunks, (:chunk f s) of
// what's been read, up to 64 KB; or a count, one (:chunk f s) of that many
// bytes, fewer if the input ends first, for the next message alone. Then
// (:eof f), and nothing more. A line or count of 64 MB or more, too big
// for a heap, ends the owner with (:error (:too-big where)). With :json, it
// sends (:json f v) for each top-level value; with :json/items, for each
// element of a top-level array, and any other value whole; with :sexp,
// (:sexp f v) for each top-level datum; with :source, (:form f form line)
// for each and (:doc f text line) for each doc block between them, line
// being the one each starts on. Bad text ends the owner with (:error
// (:bad-json where)) or (:bad-sexp where); a value of 64 MB or more,
// :too-big.
//
// A file (connect :fs/read path expr, :fs/write, :fs/append) closes after
// (:eof f). A writer takes (:write x ...) from anyone, renders the xs as
// tty/write does, and writes them at once. A file is read when its owner
// waits for its next message, so a reader is never waited for; one that
// can keep a read waiting (a FIFO, a terminal) waits with the whole
// runtime.
//
// A socket (connect :tcp "host:port" expr, connect :tcp/listen port expr,
// or connect :tcp/listen "host:port" expr) is waited for in select(), with
// stdin and the next timer. Its host is looked up first (dns.c): a name the
// hosts file doesn't have is asked of a name server, over a socket of the
// lookup's own, waited for in select() too. A connection sends (:open c)
// once it's connected, then reads as a file does (but only while its owner
// waits for what it has, so a fast sender is held back by TCP), and (:eof
// c) when the other end closes; it can still be written to then. It takes
// (:write x ...) as a file does, but keeps what the socket can't take yet,
// and writes it when it can; closing it waits for that, and until then it
// keeps the program running. A listener, on every interface or at the
// address given (D226), sends (:open l port), port being the one it got (0
// lets the system pick), then (:accept l conn) for each connection; conn
// reads nothing until (connect conn expr) hands it to a process. An open
// socket counts as something that can still happen while select() can wait
// on it (wants, below). IPv4 only, for now.
//
// A command (connect :exec (list name arg ...) expr, D185) runs in a
// process group of its own, its input and its output (stdout and stderr
// together) on pipes, which are waited for in select() as sockets are, on
// the same list. Its output reads as a connection does, then (:eof p);
// once the command has ended too, (:exited p status), and the device
// closes. It takes (:write x ...), kept till the pipe takes it, and (:eof),
// which closes its input once that's written; input the command doesn't
// take goes nowhere. Closing the device before it has ended kills its
// group, and waits for it, so nothing is left behind; and so does the
// program's end, however it ends. SIGCHLD writes a byte to a pipe that
// select() waits on, which says that one of them may have ended.

#define DEV_TCP      3                  // a device's mode, after RT_FS_READ, _WRITE and _APPEND
#define DEV_LISTEN   4
#define DEV_SYSTEM   5                  // :system (below)
#define DEV_EXEC     6                  // a command
#define ERR_NOTFOUND (-1)               // no such host, which has no errno
#define ERR_TOOBIG   (-2)               // a line, count or value of READ_MAX or more
#define ERR_BADJSON  (-3)
#define ERR_BADSEXP  (-4)
#define READ_CHUNK   (64 << 10)         // the most a chunk holds

typedef struct rt_device {
    struct rt_device *next;             // the owner's next device
    rt_value_t        pid, owner;
    int               fd, mode;
    const char       *site;             // the connect's, for dead letters
    rt_value_t        path;             // a file's path, a connection's "host:port", a command's name, as a
    uint64_t         *path_space;       // string in path_space (string_in), for (:error (name path)); or 0
    int64_t           port;             // a listener's, or the port a connection is to be made to
    int               unix_sock;        // a Unix socket's, at path (D231)
    rt_lookup_t      *lookup;           // while a connection's host is looked up...
    uint64_t          alarm;            // ...the timer for its query (own_alarm)
    char             *buf;              // what's been read, but not yet sent...
    size_t            at, len, cap;     // ...from at to len
    uint64_t          how;              // how it cuts: RT_DEV_LINES, _CHUNKS, _JSON, _JSON_ITEMS or _SEXP
    int64_t           count;            // bytes for its next message alone, or -1
    int64_t           line;             // the line at is on, from 1...
    int               mid_line;         // ...and whether it's after the line's start
    rt_json_t         json;             // the JSON value being read...
    rt_sexp_t         sexp;             // ...or datum...
    size_t            scanned;          // ...as far as from at
    int               items;            // with :json/items, where in a top-level array it is
    int               eof;              // read has returned 0
    int               sent;             // its owner has its last message, and hasn't waited since
    struct rt_device *next_socket;      // in sockets, or graveyard
    int               connecting;       // till connect() has finished
    int               waiting;          // accepted, and not yet handed to a process: it reads nothing
    int               eof_sent;
    int               closing;          // closed, but still writing what it has; no longer its pid's
    int               dead;             // closed: freed once the events in hand are done
    char             *out;              // written to it, and not yet taken by the socket or command...
    size_t            out_at, out_len, out_cap; // ...from out_at to out_len
    int               in_fd;            // a command's input, till it's closed (-1)...
    int               in_closing;       // ...which (:eof) asked for, once out is written
    int               child, group;     // a command, till it's been waited for (0), and its group
    int               status;           // how it ended, as waitpid says
    struct snapshot  *snap;             // :system's processes, still to be sent...
    uint64_t          snap_at, snap_len, snap_cap;  // ...from snap_at to snap_len
    rt_value_t       *snap_pids;        // ...and the devices they own and the pids they race
    uint64_t          snap_npids, snap_pids_cap;
} rt_device_t;

static int owns(const rt_proc_t *p, const rt_device_t *d) {
    return d && d->owner == p->pid;
}

static RT_ISLAND rt_device_t *sockets;      // open sockets and commands, and closing sockets (nsockets counts them)
static RT_ISLAND uint64_t     ndevices;     // open devices
static RT_ISLAND rt_device_t *graveyard;    // closed sockets

uint64_t rt_errno_name(int err) {
    switch (err) {
        case ENOENT:        return RT_ERR_ENOENT;
        case EACCES:        return RT_ERR_EACCES;
        case EPERM:         return RT_ERR_EPERM;
        case EEXIST:        return RT_ERR_EEXIST;
        case EISDIR:        return RT_ERR_EISDIR;
        case ENOTDIR:       return RT_ERR_ENOTDIR;
        case ENAMETOOLONG:  return RT_ERR_ENAMETOOLONG;
        case ELOOP:         return RT_ERR_ELOOP;
        case EROFS:         return RT_ERR_EROFS;
        case ENOSPC:        return RT_ERR_ENOSPC;
        case EFBIG:         return RT_ERR_EFBIG;
        case EMFILE:        return RT_ERR_EMFILE;
        case ENFILE:        return RT_ERR_ENFILE;
        case EIO:           return RT_ERR_EIO;
        case ECONNREFUSED:  return RT_ERR_ECONNREFUSED;
        case ECONNRESET:    return RT_ERR_ECONNRESET;
        case EPIPE:         return RT_ERR_EPIPE;
        case ETIMEDOUT:     return RT_ERR_ETIMEDOUT;
        case EADDRINUSE:    return RT_ERR_EADDRINUSE;
        case EADDRNOTAVAIL: return RT_ERR_EADDRNOTAVAIL;
        case EHOSTUNREACH:  return RT_ERR_EHOSTUNREACH;
        case ENETUNREACH:   return RT_ERR_ENETUNREACH;
        case ERR_NOTFOUND:  return RT_ERR_ENOTFOUND;
        case ERR_TOOBIG:    return RT_ERR_TOOBIG;
        case ERR_BADJSON:   return RT_ERR_BADJSON;
        case ERR_BADSEXP:   return RT_ERR_BADSEXP;
        case ENOTEMPTY:     return RT_ERR_ENOTEMPTY;
        case EXDEV:         return RT_ERR_EXDEV;
        case EBUSY:         return RT_ERR_EBUSY;
        default:            return RT_ERR_OTHER;
    }
}

// (name path), for errno err, in cells (4 words).
static rt_value_t io_reason(rt_value_t *cells, int err, rt_value_t path) {
    return rt_cons_at(cells, rt_symbol(RT_SYM_ERRS + rt_errno_name(err)), rt_cons_at(cells + 2, path, RT_NIL));
}

// A string box of len bytes, in space with room for it (len / 8 + 6
// words): for a value that's copied before space goes.
static rt_value_t string_in(uint64_t *space, const char *bytes, size_t len) {
    uint64_t *box = (uint64_t *)(((uintptr_t)space + 15) & ~(uintptr_t)15);
    box[0] = rt_string_header(bytes, len);
    if (len) memcpy(box + 1, bytes, len);
    ((char *)(box + 1))[len] = '\0';
    return (rt_value_t)box | RT_TAG_BOXED;
}

// A socket or a command: waited for in select(), and on sockets.
static int is_socket(const rt_device_t *d) {
    return d->mode == DEV_TCP || d->mode == DEV_LISTEN || d->mode == DEV_EXEC;
}

static rt_device_t *new_device(rt_proc_t *p, int mode, int fd, const char *site, const char *path, size_t len) {
    rt_device_t *d = rt_must(calloc(1, sizeof *d));
    d->pid   = new_entry((entry_t){ .value = RT_NIL, .ended = 1, .device = d });
    d->owner = p->pid;
    d->fd    = fd;
    d->mode  = mode;
    d->site  = site;
    d->how   = RT_DEV_LINES;
    d->count = -1;
    d->line  = 1;
    d->in_fd = -1;
    if (path) {
        d->path_space = rt_must(malloc((len / 8 + 6) * sizeof *d->path_space));
        d->path       = string_in(d->path_space, path, len);
    }
    d->next             = devices_of(p);
    side_of(p)->devices = d;
    ndevices++;
    if (is_socket(d)) {
        d->next_socket = sockets;
        sockets        = d;
        nsockets++;
    }
    return d;
}

static void end_command(rt_device_t *d);

// Closes d's file descriptor. A socket is freed later (bury), since an
// event in hand may still refer to it.
static void forget_path(int fd);

static void close_now(rt_device_t *d) {
    if (d->mode == DEV_EXEC) end_command(d);
    if (d->mode == DEV_LISTEN && d->unix_sock) forget_path(d->fd);
    if (d->fd >= 0) rt_io_close(d->fd);
    if (d->lookup) {
        unset_alarm(&d->alarm);
        rt_lookup_free(d->lookup);
        d->lookup = NULL;
    }
    if (!is_socket(d)) {
        rt_json_free(&d->json);
        rt_sexp_free(&d->sexp);
        free(d->buf);
        free(d->path_space);
        free(d->snap);
        free(d->snap_pids);
        free(d);
        return;
    }
    rt_device_t **link = &sockets;
    while (*link != d) link = &(*link)->next_socket;
    *link          = d->next_socket;
    nsockets--;
    d->dead        = 1;
    d->next_socket = graveyard;
    graveyard      = d;
}

static void bury(void) {
    while (graveyard) {
        rt_device_t *d = graveyard;
        graveyard = d->next_socket;
        rt_json_free(&d->json);
        rt_sexp_free(&d->sexp);
        free(d->buf);
        free(d->out);
        free(d->path_space);
        free(d);
    }
}

// Closes d, and forgets it: its pid is then like a process that has
// ended. A socket with something still to write lingers till it's
// written. The owner's list is the caller's to fix.
static void shut(rt_device_t *d) {
    slot(d->pid)->device = NULL;
    settle(rt_pid_number(d->pid));
    ndevices--;
    if (d->mode == DEV_TCP && d->out_len > d->out_at && !d->lookup) d->closing = 1;
    else                                                            close_now(d);   // (a host still looked up never connected)
}

static void close_all(rt_proc_t *p) {
    for (rt_device_t *d = devices_of(p), *next; d; d = next) {
        next = d->next;
        shut(d);
    }
    if (p->side) p->side->devices = NULL;
}

// A task reading d, which closes or is handed on, looks at its sources
// again, and finds it ended (D243).
static void wake_reader(rt_proc_t *owner, const rt_device_t *d) {
    if (owner->state == RT_READING && among(owner->joining, d->pid)) enqueue(owner);
}

static void unown(rt_device_t *d) {
    rt_proc_t    *owner = slot(d->owner)->proc;
    rt_device_t **link  = &owner->side->devices;   // it owns d, so it has one
    while (*link != d) link = &(*link)->next;
    *link = d->next;
    wake_reader(owner, d);
}

static void close_device(rt_device_t *d) {
    unown(d);
    shut(d);
}

// d's owner ends with (:error (name path)), which closes d. If the owner
// is the process running, this doesn't return; if not, d is freed (a
// file) or buried (a socket) by the time it does, so the caller returns
// at once, and doesn't touch d again. The path is d's own string, not a
// copy on this stack, since it's as long as the program made it; the
// reason is copied before d closes.
static void fail(rt_device_t *d, int err) {
    rt_proc_t *owner = slot(d->owner)->proc;
    _Alignas(16) rt_value_t cells[10];
    rt_value_t reason = io_reason(cells, err, d->path ? d->path : rt_int(d->port));
    if (!owner->actor) {
        // A task asked for the device (D243), so it's told, (:error d
        // reason), where it reads the device, and goes on.
        rt_value_t error = rt_cons_at(cells + 4, rt_symbol(RT_SYM_ERROR), rt_cons_at(cells + 6, d->pid, rt_cons_at(cells + 8, reason, RT_NIL)));
        rt_msg_t  *m     = new_msg(error);
        m->device = d->pid;
        deliver(owner, m);
        close_device(d);
        return;
    }
    if (owner == rt_current) rt_end(0, reason, 0);
    end_other(owner, reason);
}

// Sends d's owner (sym f rest...); taking it reads on, if more says so.
static void tell_sym(rt_device_t *d, rt_value_t sym, rt_value_t rest, int more) {
    _Alignas(16) rt_value_t cells[4];
    rt_msg_t  *m     = new_msg(rt_cons_at(cells, sym, rt_cons_at(cells + 2, d->pid, rest)));
    rt_proc_t *owner = slot(d->owner)->proc;
    m->device = more || !owner->actor ? d->pid : 0;     // a task reads it by the device, and reading on after one that doesn't need it does nothing
    d->sent   = d->sent || more;
    deliver(owner, m);
}

static void tell_list(rt_device_t *d, uint64_t word, rt_value_t rest, int more) {
    tell_sym(d, rt_symbol(RT_SYM_DEVICE + word), rest, more);
}

static void tell(rt_device_t *d, uint64_t word, int more) {
    tell_list(d, word, RT_NIL, more);
}

// (word f s): a line or a chunk.
static void tell_string(rt_device_t *d, uint64_t word, const char *bytes, size_t len) {
    uint64_t *space = rt_must(malloc((len / 8 + 6) * sizeof *space));
    _Alignas(16) rt_value_t cell[2];
    tell_list(d, word, rt_cons_at(cell, string_in(space, bytes, len), RT_NIL), 1);
    free(space);
}

// The device's first message: (:open f), or a listener's (:open l port),
// or (:open l path) at a Unix socket's.
// :system's reads on too, so that one handed over in the middle of
// sending its processes goes on to its new owner.
static void tell_open(rt_device_t *d) {
    _Alignas(16) rt_value_t cell[2];
    if (d->mode == DEV_LISTEN) tell_list(d, RT_DEV_OPEN, rt_cons_at(cell, d->unix_sock ? d->path : rt_int(d->port), RT_NIL), 0);
    else                       tell(d, RT_DEV_OPEN, d->mode != RT_FS_WRITE && d->mode != RT_FS_APPEND);
}

// Takes n bytes of what's been read, counting the lines they end.
static void consume(rt_device_t *d, size_t n) {
    const char *p = d->buf + d->at, *end = p + n;
    while (p < end && (p = memchr(p, '\n', (size_t)(end - p))) != NULL) {
        d->line++;
        p++;
    }
    if (n) d->mid_line = d->buf[d->at + n - 1] != '\n';
    d->at += n;
}

// The next line in what's been read: 1, with it in *line and *len (good
// till the buffer next changes); or 0. At the end, what's left is a line.
static int take_line(rt_device_t *d, const char **line, size_t *len) {
    char *nl = d->len > d->at ? memchr(d->buf + d->at, '\n', d->len - d->at) : NULL;
    if (!nl && !(d->eof && d->at < d->len)) return 0;
    size_t end = nl ? (size_t)(nl - d->buf) : d->len;
    *line = d->buf + d->at;
    *len  = end - d->at;
    consume(d, (nl ? end + 1 : end) - d->at);
    return 1;
}

// Reads once more: what read returned (0 at the end, which sets eof), or
// -1 with errno set. The buffer doubles till it has room for a chunk, or
// what's left of a count if that's less, or a byte more of a line; so it
// grows with what comes, and a count of 60 MB takes no memory until 60 MB
// does.
static ssize_t fill(rt_device_t *d) {
    if (d->at) memmove(d->buf, d->buf + d->at, d->len - d->at);
    d->len -= d->at;
    d->at   = 0;
    size_t left = d->count > (int64_t)d->len ? (size_t)d->count - d->len : 0;
    size_t room = left ? (left < READ_CHUNK ? left : READ_CHUNK) : d->how == RT_DEV_CHUNKS ? READ_CHUNK : 1;
    size_t cap  = d->cap ? d->cap : 4096;
    while (cap - d->len < room) cap *= 2;
    if (cap != d->cap) {
        d->cap = cap;
        d->buf = rt_must(realloc(d->buf, cap));
    }
    ssize_t got;
    do got = rt_io_read(d->fd, d->buf + d->len, d->cap - d->len); while (got < 0 && errno == EINTR);
    if (got == 0) d->eof = 1;
    if (got > 0)  d->len += (size_t)got;
    return got;
}

// What a JSON value is built in, to be copied into a message: chunks that
// give out once they'd hold as much as a heap can.
typedef struct {
    rt_chunk_t *chunks;
    size_t      used, left, total;
} arena_t;

static void *arena_alloc(void *cx, size_t bytes) {
    arena_t *a = cx;
    bytes = (bytes + 15) & ~(size_t)15;
    if (a->total + bytes >= READ_MAX) return NULL;
    if (a->left < bytes) {
        size_t      size = bytes > CHUNK_MAX ? bytes : CHUNK_MAX;
        rt_chunk_t *c    = new_chunk(size);
        c->next   = a->chunks;
        a->chunks = c;
        a->used   = 0;
        a->left   = size;
    }
    void *p = (char *)(a->chunks + 1) + a->used;
    a->used  += bytes;
    a->left  -= bytes;
    a->total += bytes;
    return p;
}

// Where :json/items is: not in an array; just after its [, or after an
// element; or at an element.
enum { ITEMS_NONE, ITEMS_FIRST, ITEMS_NEXT, ITEMS_ELEMENT };

// Ready to read a value from at: a doc block can begin there only at the
// start of a line.
static void value_restart(rt_device_t *d) {
    rt_json_reset(&d->json);
    rt_sexp_reset(&d->sexp);
    d->sexp.mid_line = d->mid_line;
    d->scanned       = 0;
}

// (word f text line).
static void tell_doc(rt_device_t *d, const char *text, size_t len, int64_t line) {
    uint64_t *space = rt_must(malloc((len / 8 + 6) * sizeof *space));
    _Alignas(16) rt_value_t cells[4];
    tell_list(d, RT_DEV_DOC, rt_cons_at(cells, string_in(space, text, len), rt_cons_at(cells + 2, rt_int(line), RT_NIL)), 1);
    free(space);
}

// The next JSON value, datum or doc block in what's been read, as cut does;
// or 2 if there's nothing but space before the end.
static int cut_value(rt_device_t *d) {
    int sexp = d->how == RT_DEV_SEXP || d->how == RT_DEV_SOURCE, bad = sexp ? ERR_BADSEXP : ERR_BADJSON;
    d->sexp.docs = d->how == RT_DEV_SOURCE;
    for (;;) {
        // between the elements of a top-level array, the [ , and ] are
        // read here, and the elements by the scan
        if (d->how == RT_DEV_JSON_ITEMS && d->items != ITEMS_ELEMENT && d->scanned == 0) {
            while (d->at < d->len && (d->buf[d->at] == ' ' || d->buf[d->at] == '\t' || d->buf[d->at] == '\n' || d->buf[d->at] == '\r')) consume(d, 1);
            if (d->at == d->len) {
                if (!d->eof)                return 0;
                if (d->items == ITEMS_NONE) return 2;
                fail(d, ERR_BADJSON);       // an array not closed
                return 1;
            }
            char c = d->buf[d->at];
            if (d->items == ITEMS_NONE) {
                if (c == '[') {
                    consume(d, 1);
                    d->items = ITEMS_FIRST;
                    continue;
                }
            } else if (c == ']') {
                consume(d, 1);
                d->items = ITEMS_NONE;
                continue;
            } else if (d->items == ITEMS_NEXT) {
                if (c != ',') {
                    fail(d, ERR_BADJSON);
                    return 1;
                }
                consume(d, 1);
                d->items = ITEMS_ELEMENT;
                continue;
            } else {
                d->items = ITEMS_ELEMENT;
            }
        }
        size_t      have = d->len - d->at, used;
        const char *p    = d->buf + d->at + d->scanned;
        int         r    = sexp ? rt_sexp_scan(&d->sexp, p, have - d->scanned, d->eof, &used)
                                : rt_json_scan(&d->json, p, have - d->scanned, d->eof, &used);
        if (r == RT_SCAN_MORE) {
            if (have >= READ_MAX) {             // a value with no end in sight, as a line can be (cut)
                fail(d, ERR_TOOBIG);
                return 1;
            }
            d->scanned = have;
            return 0;
        }
        if (r == RT_SCAN_NONE && d->items == ITEMS_NONE) return 2;
        if (r != RT_SCAN_DONE && r != RT_SCAN_DOC) {
            fail(d, bad);
            return 1;
        }
        size_t  n    = d->scanned + used;
        int64_t line = d->line + (int64_t)d->sexp.first_line;
        if (r == RT_SCAN_DOC) {
            tell_doc(d, d->buf + d->at + d->sexp.doc_from, d->sexp.doc_to - d->sexp.doc_from, line);
            consume(d, n);
            value_restart(d);
            return 1;
        }
        size_t     from = sexp ? d->sexp.from : 0;      // past what came before the datum
        arena_t    a    = { 0 };
        rt_value_t v;
        int        built = sexp ? rt_sexp_build(d->buf + d->at + from, n - from, arena_alloc, &a, &v)
                                : rt_json_build(d->buf + d->at, n, arena_alloc, &a, &v);
        if (built == 0) {
            _Alignas(16) rt_value_t cells[4];
            consume(d, n);
            value_restart(d);
            if (d->items == ITEMS_ELEMENT) d->items = ITEMS_NEXT;
            if (d->how == RT_DEV_SOURCE) tell_list(d, RT_DEV_FORM, rt_cons_at(cells, v, rt_cons_at(cells + 2, rt_int(line), RT_NIL)), 1);
            else                         tell_list(d, sexp ? RT_DEV_SEXP : RT_DEV_JSON, rt_cons_at(cells, v, RT_NIL), 1);
        }
        free_chunks(a.chunks);
        if (built != 0) fail(d, ERR_TOOBIG);
        return 1;
    }
}

static int exited(rt_device_t *d);

// Sends d's next message, if what's been read makes one: 1 if it did, or
// has nothing more to send (and d may be gone); 0 if it needs more bytes,
// or a command's end.
static int cut(rt_device_t *d) {
    size_t      have = d->len - d->at, n;
    const char *line;
    if (d->eof_sent) return d->mode == DEV_EXEC ? exited(d) : 1;
    if (d->count >= READ_MAX) {
        fail(d, ERR_TOOBIG);
        return 1;
    }
    if (d->count >= 0) {
        if (have < (size_t)d->count && !d->eof) return 0;
        n        = have < (size_t)d->count ? have : (size_t)d->count;
        d->count = -1;
        tell_string(d, RT_DEV_CHUNK, d->buf + d->at, n);
        consume(d, n);
        value_restart(d);                       // a value being read has lost its start
        return 1;
    }
    if (d->how == RT_DEV_JSON || d->how == RT_DEV_JSON_ITEMS || d->how == RT_DEV_SEXP || d->how == RT_DEV_SOURCE) {
        int r = cut_value(d);
        if (r != 2) return r;                   // 2: on to (:eof f)
    }
    if (d->how == RT_DEV_LINES && take_line(d, &line, &n)) {
        tell_string(d, RT_DEV_LINE, line, n);
        return 1;
    }
    if (d->how == RT_DEV_CHUNKS && have) {
        n      = have < READ_CHUNK ? have : READ_CHUNK;
        tell_string(d, RT_DEV_CHUNK, d->buf + d->at, n);
        consume(d, n);
        return 1;
    }
    if (d->eof) {
        d->eof_sent = 1;
        tell(d, RT_DEV_EOF, d->mode == DEV_EXEC);    // a command's (:exited p status) is next
        if (d->mode == RT_FS_READ) close_device(d);
        return 1;
    }
    if (have >= READ_MAX) {                     // a line with no end in sight
        fail(d, ERR_TOOBIG);
        return 1;
    }
    return 0;
}

// p, waiting again, took reader pid's message last: on to its next. A
// file reads till it has one; a connection or a command waits in select()
// for more.
static void system_next(rt_device_t *d);

static void read_on(rt_proc_t *p, rt_value_t pid) {
    rt_device_t *d = slot(pid)->device;
    if (!d || d->owner != p->pid) return;       // closed since, or handed over
    d->sent = 0;
    if (d->mode == DEV_SYSTEM) {
        system_next(d);
        return;
    }
    while (!cut(d) && d->mode == RT_FS_READ) {
        if (fill(d) < 0) {
            fail(d, errno);
            return;
        }
    }
}

// (:read how), to reader d: 0 if how isn't one it knows.
static int read_as(rt_device_t *d, rt_value_t args) {
    rt_value_t how = rt_is_cons(args) && rt_cdr(args) == RT_NIL ? rt_car(args) : RT_NIL;
    uint64_t ways[] = { RT_DEV_LINES, RT_DEV_CHUNKS, RT_DEV_JSON, RT_DEV_JSON_ITEMS, RT_DEV_SEXP, RT_DEV_SOURCE }, k = 0;
    while (k < 6 && how != rt_symbol(RT_SYM_DEVICE + ways[k])) k++;
    if (k < 6) {
        d->how   = ways[k];
        d->items = ITEMS_NONE;
        value_restart(d);
    } else if (!(how & RT_TAG_INT_MASK) && rt_int_value(how) >= 0) {
        d->count = rt_int_value(how);
    } else {
        return 0;
    }
    // A connection or command waiting in select() may have enough for the
    // new way.
    if ((d->mode == DEV_TCP || d->mode == DEV_EXEC) && !d->sent && !d->connecting && !d->waiting) cut(d);
    return 1;
}

// Writes what a socket or a command's input has, as far as it takes it:
// 0, or -1 with errno set. (SIGPIPE is ignored, so a closed connection is
// EPIPE.)
static int flush(rt_device_t *d) {
    int fd = d->mode == DEV_EXEC ? d->in_fd : d->fd;
    while (d->out_at < d->out_len) {
        ssize_t n = rt_io_write(fd, d->out + d->out_at, d->out_len - d->out_at);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
        d->out_at += (size_t)n;
    }
    d->out_at = d->out_len = 0;
    return 0;
}

static void queue(rt_device_t *d, const char *bytes, size_t len) {
    if (d->out_len + len > d->out_cap) {        // out of room: first drop what's been written
        if (d->out_at) memmove(d->out, d->out + d->out_at, d->out_len - d->out_at);
        d->out_len -= d->out_at;
        d->out_at   = 0;
    }
    if (d->out_len + len > d->out_cap) {
        d->out_cap = (d->out_len + len) * 2;
        d->out     = rt_must(realloc(d->out, d->out_cap));
    }
    if (len) memcpy(d->out + d->out_len, bytes, len);
    d->out_len += len;
}

static void system_command(rt_device_t *d, rt_value_t msg);
static void close_input(rt_device_t *d);

static void command(rt_device_t *d, rt_value_t msg) {
    rt_value_t word   = rt_is_cons(msg) ? rt_car(msg) : RT_NIL;
    int        reader = d->mode == RT_FS_READ || d->mode == DEV_TCP || d->mode == DEV_EXEC;
    if (d->mode == DEV_SYSTEM) {
        system_command(d, msg);
        return;
    }
    if (reader && word == rt_symbol(RT_SYM_DEVICE + RT_DEV_READ) && read_as(d, rt_cdr(msg))) return;
    if (d->mode == DEV_EXEC && word == rt_symbol(RT_SYM_DEVICE + RT_DEV_EOF) && rt_cdr(msg) == RT_NIL) {
        d->in_closing = 1;
        if (d->out_at == d->out_len) close_input(d);
        return;
    }
    if (d->mode == RT_FS_READ || d->mode == DEV_LISTEN || word != rt_symbol(RT_SYM_DEVICE + RT_DEV_WRITE)) {
        rt_dead_letter(msg, d->site);
        return;
    }
    if (d->mode == DEV_EXEC && (d->in_fd < 0 || d->in_closing)) return;      // input that goes nowhere
    rt_buf_t b = { 0 };
    for (rt_value_t x = rt_cdr(msg); rt_is_cons(x); x = rt_cdr(x)) rt_render(&b, rt_car(x), 1);
    if (d->mode == DEV_EXEC) {
        queue(d, b.bytes, b.len);
        rt_buf_free(&b);
        if (flush(d) < 0) close_input(d);       // the command has closed its input, or ended
        return;
    }
    if (d->mode == DEV_TCP) {
        queue(d, b.bytes, b.len);
        rt_buf_free(&b);
        if (!d->connecting && flush(d) < 0) fail(d, errno);
        return;
    }
    for (size_t at = 0; at < b.len; ) {
        ssize_t n = rt_io_write(d->fd, b.bytes + at, b.len - at);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            int err = errno;
            rt_buf_free(&b);
            fail(d, err);
            return;
        }
        at += (size_t)n;
    }
    rt_buf_free(&b);
}

// Opens the file at path for p: 0, or an errno.
static int fs_open(rt_proc_t *p, const char *site, rt_value_t path, uint64_t mode) {
    const char *bytes = rt_string_bytes(path);
    size_t      len   = rt_string_len(path);
    int         flags = mode == RT_FS_READ ? O_RDONLY : O_WRONLY | O_CREAT | (mode == RT_FS_APPEND ? O_APPEND : O_TRUNC);
    int         fd    = memchr(bytes, '\0', len) ? (errno = EINVAL, -1) : rt_io_open(bytes, flags);
    if (fd < 0) return errno;
    tell_open(new_device(p, (int)mode, fd, site, bytes, len));
    return 0;
}

rt_value_t rt_connect_fs(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                         rt_value_t path, uint64_t mode) {
    if (!rt_is_string(path)) rt_fault(RT_FAULT_NOT_STRING, path, site);
    rt_value_t pid = rt_spawn(code, n, values, site);
    rt_proc_t *p   = slot(pid)->proc;
    int        err = fs_open(p, site, path, mode);
    if (err) {
        _Alignas(16) rt_value_t cells[4];
        fail_start(p, io_reason(cells, err, path));
    }
    return pid;
}

// A socket that doesn't block, isn't inherited, and fits in select():
// 0, or an errno.
static int setup(int fd) {
    return rt_io_setup(fd, 1);
}

// Listens on port at addr (network order). where, "host:port", is what
// the listener's errors name, or its port if where is NULL.
static int tcp_listen(rt_proc_t *p, const char *site, uint32_t addr, int64_t port, const char *where, size_t len) {
    int fd = rt_io_socket(AF_INET, SOCK_STREAM);
    if (fd < 0) return errno;
    int got, err;
    if ((err = setup(fd)) != 0 || (err = rt_io_listen(fd, addr, (int)port, &got)) != 0) {
        rt_io_close(fd);
        return err;
    }
    rt_device_t *d = new_device(p, DEV_LISTEN, fd, site, where, len);
    d->port = got;
    tell_open(d);
    return 0;
}

// Starts connecting a socket to addr (network order) and port: 0, with it
// in *fd, or an errno.
static int open_connection(uint32_t addr, int64_t port, int *fd) {
    struct sockaddr_in a = { 0 };
    a.sin_family      = AF_INET;
    a.sin_port        = htons((uint16_t)port);
    a.sin_addr.s_addr = addr;
    int s = rt_io_socket(AF_INET, SOCK_STREAM), err = s < 0 ? errno : setup(s);
    if (!err && rt_io_connect(s, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) err = errno;
    if (err && s >= 0) rt_io_close(s);
    *fd = err ? -1 : s;
    return err;
}

// Splits "host:port" at the last colon, the port a number from 0 to
// 65535: 0, with the host in host, or ERR_NOTFOUND. A host of more than
// 255 bytes, longer than any name (253, dns.c), isn't found.
static int split_where(const char *where, size_t len, char host[256], int64_t *port) {
    size_t colon = len;
    while (colon > 0 && where[colon - 1] != ':') colon--;
    if (colon == 0 || colon == len || colon > 256 || memchr(where, '\0', len)) return ERR_NOTFOUND;
    int64_t n = 0;
    for (size_t i = colon; i < len && n <= 65535; i++) n = where[i] >= '0' && where[i] <= '9' ? n * 10 + where[i] - '0' : 65536;
    if (n > 65535) return ERR_NOTFOUND;
    memcpy(host, where, colon - 1);
    host[colon - 1] = '\0';
    *port = n;
    return 0;
}

// Listens on a port, on every interface; or at "host:port" (D226), the
// host an address or a name the hosts file has, or localhost, but never
// one a name server is asked about: what's listened at is this machine's.
static int listen_at(rt_proc_t *p, const char *site, rt_value_t where) {
    if (!rt_is_string(where)) return tcp_listen(p, site, htonl(INADDR_ANY), rt_int_value(where), NULL, 0);
    const char *s   = rt_string_bytes(where);
    size_t      len = rt_string_len(where);
    char        host[256];
    int64_t     port;
    uint32_t    addr;
    if (split_where(s, len, host, &port) != 0 || !rt_lookup_local(host, &addr)) return ERR_NOTFOUND;
    return tcp_listen(p, site, addr, port, s, len);
}

static void lookup_step(rt_device_t *d, int r, uint32_t addr);

// Starts connecting to "host:port". Even if it has connected already,
// (:open c) comes from the event loop, so it always comes in the same
// order; and a host that has to be asked about is looked up first, the
// device waiting for that.
static int tcp_connect(rt_proc_t *p, const char *site, const char *where, size_t len) {
    char    host[256];
    int64_t port;
    if (split_where(where, len, host, &port) != 0) return ERR_NOTFOUND;
    rt_lookup_t *l;
    uint32_t     addr;
    int          fd, r = rt_lookup_begin(&l, host, &addr), err;
    if (r == RT_LOOKUP_NOTFOUND) return ERR_NOTFOUND;
    if (r == RT_LOOKUP_TIMEOUT)  return ETIMEDOUT;       // no query could even be sent
    if (r == RT_LOOKUP_DONE && (err = open_connection(addr, port, &fd)) != 0) return err;
    rt_device_t *d = new_device(p, DEV_TCP, r == RT_LOOKUP_DONE ? fd : -1, site, where, len);
    d->connecting  = 1;
    d->port        = port;
    if (r == RT_LOOKUP_ASKED) {
        d->lookup = l;
        lookup_step(d, r, 0);
    }
    return 0;
}

// What a step of d's lookup came to: wait on; a new query, its timer set
// afresh; or the end of it, the connection started or failed.
static void lookup_step(rt_device_t *d, int r, uint32_t addr) {
    if (r == RT_LOOKUP_WAIT) return;
    unset_alarm(&d->alarm);
    if (r == RT_LOOKUP_ASKED) {
        uint64_t ms = rt_lookup_timeout(d->lookup);
        d->alarm    = own_alarm(now() + ms * 1000000, d->pid);
        return;
    }
    rt_lookup_free(d->lookup);
    d->lookup = NULL;
    int err   = r == RT_LOOKUP_DONE ? open_connection(addr, d->port, &d->fd) : r == RT_LOOKUP_TIMEOUT ? ETIMEDOUT : ERR_NOTFOUND;
    if (err) fail(d, err);
}

static void lookup_timed_out(rt_device_t *d, uint64_t seq) {
    uint32_t addr;
    if (!d->lookup || d->alarm != seq + 1) return;      // a query since has its own
    d->alarm = 0;
    lookup_step(d, rt_lookup_ready(d->lookup, 1, &addr), addr);
}

// Unix sockets (D231): a connection reads and writes as a TCP one does,
// and a listener at a path makes the socket there, replacing only one
// that nothing accepts on, left by a program that has ended (replay.c).
// The path is removed when the listener closes, and when the program
// ends however it ends, a signal included; one left by SIGKILL is
// replaced next time. paths holds each listener's, by its descriptor, for
// on_end, which a signal runs, to find; none in a replay, which makes no
// socket.
static char *paths[FD_SETSIZE];

static void unlink_paths(void) {
    for (int fd = 0; fd < FD_SETSIZE; fd++) {
        if (paths[fd]) unlink(paths[fd]);
    }
}

static void forget_path(int fd) {
    if (fd < 0 || !paths[fd]) return;
    char *path = paths[fd];
    paths[fd]  = NULL;
    unlink(path);
    free(path);
}

static int unix_listen(rt_proc_t *p, const char *site, const char *path, size_t len) {
    static int ends;
    int        fd = rt_io_socket(AF_UNIX, SOCK_STREAM), err;
    if (fd < 0) return errno;
    if ((err = rt_io_setup(fd, 0)) != 0 || (err = rt_io_listen_unix(fd, path)) != 0) {
        rt_io_close(fd);
        return err;
    }
    if (!rt_io_replaying()) {
        paths[fd] = rt_must(strdup(path));
        if (!ends++) atexit(unlink_paths);
    }
    rt_device_t *d = new_device(p, DEV_LISTEN, fd, site, path, len);
    d->unix_sock = 1;
    tell_open(d);
    return 0;
}

// A Unix socket connects at once or not at all; its (:open c) still comes
// from the event loop, as a TCP connection's does.
static int unix_connect(rt_proc_t *p, const char *site, const char *path, size_t len) {
    struct sockaddr_un a;
    int                fd, err = rt_unix_address(&a, path);
    if (err) return err;
    if ((fd = rt_io_socket(AF_UNIX, SOCK_STREAM)) < 0) return errno;
    if ((err = rt_io_setup(fd, 0)) == 0 && rt_io_connect(fd, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) err = errno;
    if (err) {
        rt_io_close(fd);
        return err;
    }
    rt_device_t *d = new_device(p, DEV_TCP, fd, site, path, len);
    d->connecting  = 1;
    d->unix_sock   = 1;
    return 0;
}

static int unix_at(rt_proc_t *p, const char *site, rt_value_t where, uint64_t mode) {
    const char *path = rt_string_bytes(where);
    size_t      len  = rt_string_len(where);
    if (memchr(path, '\0', len)) return EINVAL;                 // :io-error, as a file's path (D130)
    return mode == RT_UNIX_LISTEN ? unix_listen(p, site, path, len) : unix_connect(p, site, path, len);
}

rt_value_t rt_connect_tcp(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                          rt_value_t where, uint64_t mode) {
    if (mode == RT_TCP_LISTEN && !rt_is_string(where)) {
        int64_t port = rt_need_int(where, site);
        if (port < 0 || port > 65535) rt_fault(RT_FAULT_RANGE, where, site);
    } else if (!rt_is_string(where)) {
        rt_fault(RT_FAULT_NOT_STRING, where, site);
    }
    rt_value_t pid = rt_spawn(code, n, values, site);
    rt_proc_t *p   = slot(pid)->proc;
    int        err = mode == RT_TCP_LISTEN  ? listen_at(p, site, where)
                   : mode == RT_TCP_CONNECT ? tcp_connect(p, site, rt_string_bytes(where), rt_string_len(where))
                                            : unix_at(p, site, where, mode);
    if (err) {
        _Alignas(16) rt_value_t cells[4];
        fail_start(p, io_reason(cells, err, where));
    }
    return pid;
}

rt_value_t rt_connect_device(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                             rt_value_t dev) {
    rt_device_t *d = entry(dev, site)->device;
    if (!d) rt_fault(RT_FAULT_NOT_DEVICE, dev, site);
    rt_value_t pid = rt_spawn(code, n, values, site);
    rt_proc_t *p   = slot(pid)->proc;
    unown(d);
    d->owner   = pid;
    d->next             = devices_of(p);
    side_of(p)->devices = d;
    d->waiting = 0;
    if (!d->connecting) tell_open(d);           // a connection that's still connecting says so when it has
    return pid;
}

rt_value_t rt_disconnect(rt_value_t pid, const char *site) {
    if (rt_is_address(pid)) {
        rt_proc_t *p = address_owner(pid);
        if (p) close_address(p, pid);
        return RT_NIL;
    }
    rt_device_t *d = entry(pid, site)->device;
    if (d) close_device(d);
    return RT_NIL;
}

// The other end's credentials, as the kernel had them when it connected:
// on this machine, a Unix socket's peer is a process with a uid that file
// permissions were checked against, which is all a program needs to
// decide whom it answers (D235).
rt_value_t rt_socket_peer(rt_value_t c, const char *site) {
    rt_device_t *d = entry(c, site)->device;
    int64_t      who[3];
    if (!d || !d->unix_sock || d->mode != DEV_TCP || d->fd < 0 || rt_io_peer(d->fd, who) != 0) return RT_NIL;
    rt_value_t keys[3] = {
        rt_symbol(RT_SYM_PEER + RT_PEER_UID), rt_symbol(RT_SYM_PEER + RT_PEER_GID), rt_symbol(RT_SYM_SYSTEM + RT_SYS_PID),
    };
    rt_value_t *cells = rt_alloc(9 * 2 * sizeof *cells, site), info = RT_NIL;
    for (int i = 2; i >= 0; i--, cells += 6) {
        rt_value_t pair = rt_cons_at(cells, keys[i], rt_cons_at(cells + 2, rt_int(who[i]), RT_NIL));
        info = rt_cons_at(cells + 4, pair, info);
    }
    return info;
}

// A listener has connections to accept: each becomes a device of the
// listener's owner, which hears (:accept l conn).
static void accept_all(rt_device_t *l) {
    rt_proc_t *owner = slot(l->owner)->proc;
    for (;;) {
        char where[INET_ADDRSTRLEN + 8];
        int  fd = rt_io_accept(l->fd, where, sizeof where);
        if (fd < 0 && errno == EINTR) continue;
        if (fd < 0) return;                     // none left, or none to be had for now
        if (rt_io_setup(fd, !l->unix_sock) != 0) {
            rt_io_close(fd);
            continue;
        }
        rt_device_t *c = l->unix_sock ? new_device(owner, DEV_TCP, fd, l->site, rt_string_bytes(l->path), rt_string_len(l->path))
                                      : new_device(owner, DEV_TCP, fd, l->site, where, strlen(where));
        c->unix_sock = l->unix_sock;
        c->waiting   = 1;
        _Alignas(16) rt_value_t cell[2];
        tell_list(l, RT_DEV_ACCEPT, rt_cons_at(cell, c->pid, RT_NIL), 0);
    }
}

// Whether select() has anything to wait for on d, and what: a listener
// reads; a connection writes while it connects or has output waiting,
// and reads while its owner waits for what it has (not accepted and not
// yet handed over, not sent a message it hasn't taken, not at its end,
// not closing); a lookup waits on its query's socket. A command writes
// its input while it has some, reads its output as a connection does,
// and once that has ended, while its owner waits, waits for it to end
// (the SIGCHLD pipe, which select() waits on while any command runs). A
// socket or command that wants nothing can't do anything until a process
// runs, so it doesn't keep the program alive (idle).
static int wants(const rt_device_t *d, int *r, int *w) {
    if (d->mode == DEV_EXEC) {
        *r = !d->sent && !d->eof;
        *w = d->in_fd >= 0 && d->out_len > d->out_at;
        return *r || *w || (d->child && d->eof_sent && !d->sent);
    }
    if (d->lookup) {
        int write;
        rt_lookup_fd(d->lookup, &write);
        *r = !write;
        *w = write;
        return 1;
    }
    *r = d->mode == DEV_LISTEN || (!d->connecting && !d->waiting && !d->sent && !d->eof && !d->closing);
    *w = d->connecting || d->out_len > d->out_at;
    return *r || *w;
}

static int sockets_waiting(void) {
    int r, w;
    for (rt_device_t *d = sockets; d; d = d->next_socket) {
        if (wants(d, &r, &w)) return 1;
    }
    return 0;
}

static int                child_pipe[2] = { -1, -1 };    // SIGCHLD writes a byte to it
static RT_ISLAND uint64_t nchildren;                     // commands not yet waited for
static int                is_init;                       // PID 1, as in a container: orphans are its (D229)

// Whether SIGCHLD's pipe is waited on: while a command runs; and as PID 1,
// once one has, since what a command left running is PID 1's to wait for
// when it ends.
static int watching_children(void) {
    return child_pipe[0] >= 0 && (nchildren || is_init);
}

// The file descriptors select() waits on for d: to read, and to write (a
// command's input).
static void fds_of(const rt_device_t *d, int *r, int *w) {
    int write;
    *r = d->lookup ? rt_lookup_fd(d->lookup, &write) : d->fd;
    *w = d->mode == DEV_EXEC ? d->in_fd : *r;
}

// Adds to the sets the sockets and commands that are waiting to read or
// write, and SIGCHLD's pipe while any command runs, and raises *max to
// their highest file descriptor.
static void socket_fds(fd_set *in, fd_set *out, int *max) {
    bury();
    for (rt_device_t *d = sockets; d; d = d->next_socket) {
        int r, w, rfd, wfd;
        if (!wants(d, &r, &w)) continue;
        fds_of(d, &rfd, &wfd);
        if (r) FD_SET(rfd, in);
        if (w) FD_SET(wfd, out);
        if (r && rfd > *max) *max = rfd;
        if (w && wfd > *max) *max = wfd;
    }
    if (watching_children()) {
        FD_SET(child_pipe[0], in);
        if (child_pipe[0] > *max) *max = child_pipe[0];
    }
}

static void socket_event(rt_device_t *d, int r, int w) {
    if (d->lookup) {
        uint32_t addr;
        lookup_step(d, rt_lookup_ready(d->lookup, 0, &addr), addr);
        return;
    }
    if (d->mode == DEV_LISTEN) {
        accept_all(d);
        return;
    }
    if (d->mode == DEV_EXEC && w && (flush(d) < 0 || (d->in_closing && d->out_at == d->out_len))) close_input(d);
    if (d->connecting && w) {
        int err = rt_io_socket_error(d->fd);
        if (err) {
            if (d->closing) close_now(d);
            else            fail(d, err);
            return;
        }
        d->connecting = 0;
        if (!d->closing) tell_open(d);
    }
    if (w && d->mode == DEV_TCP && !d->connecting && d->out_len > d->out_at) {
        if (flush(d) < 0) {
            if (d->closing) close_now(d);
            else            fail(d, errno);
            return;
        }
        if (d->closing && d->out_len == 0) {
            close_now(d);
            return;
        }
    }
    if (r && !d->closing) {
        if (fill(d) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fail(d, errno);
            return;
        }
        cut(d);
    }
}

typedef struct {
    rt_device_t *d;
    int          r, w;
} ready_t;

static int by_pid(const void *a, const void *b) {
    rt_value_t x = ((const ready_t *)a)->d->pid, y = ((const ready_t *)b)->d->pid;
    return x < y ? -1 : x > y;
}

static void reap(void);

// Handles the sockets and commands select() says are ready, in the order
// of their pids, then the commands that may have ended; returns whether
// there were any. An event can end the process running (fail, when
// events() is called from rt_preempt), and then this never returns: so
// the list of them is kept for the next time, not freed, and the sockets
// closed meanwhile are buried then.
static int socket_events(fd_set *in, fd_set *out) {
    static RT_ISLAND ready_t *ready;
    static RT_ISLAND uint64_t ready_cap;
    if (ready_cap < nsockets + 1) {
        ready_cap = nsockets + 1;
        ready     = rt_must(realloc(ready, ready_cap * sizeof *ready));
    }
    uint64_t n = 0;
    int      ended = watching_children() && FD_ISSET(child_pipe[0], in);
    for (rt_device_t *d = sockets; d; d = d->next_socket) {
        int rfd, wfd;
        fds_of(d, &rfd, &wfd);
        int r = rfd >= 0 && FD_ISSET(rfd, in), w = wfd >= 0 && FD_ISSET(wfd, out);
        if (r || w) ready[n++] = (ready_t){ d, r, w };
    }
    qsort(ready, n, sizeof *ready, by_pid);
    for (uint64_t i = 0; i < n; i++) {
        if (!ready[i].d->dead) socket_event(ready[i].d, ready[i].r, ready[i].w);
    }
    if (ended) reap();
    bury();
    return n > 0 || ended;
}

// --- commands -----------------------------------------------------------------
//
// (connect :exec cmd expr), above, under devices (D185).

static volatile sig_atomic_t groups[FD_SETSIZE];   // the groups of commands running, by their output

static void on_child(int sig) {
    int err = errno;
    (void)sig;
    if (write(child_pipe[1], "", 1) < 0) {}    // full: it has a byte already
    errno = err;
}

// Kills every command still running: at the program's end, however it
// ends.
static void kill_groups(void) {
    for (int fd = 0; fd < FD_SETSIZE; fd++) {
        if (groups[fd]) kill(-groups[fd], SIGKILL);
    }
}

// The program's end by a signal: its commands killed, the terminal given
// back, and the signal taken again, now as it would have been, so the
// program dies of it. Unless it's PID 1, in a container, whose signals the
// kernel ignores when they're left to their default action: then it exits
// as a shell would say it died, 128 and the signal (D227).
static void on_end(int sig) {
    sigset_t s;
    kill_groups();
    unlink_paths();
    rt_tty_raw(0);
    signal(sig, SIG_DFL);
    sigemptyset(&s);
    sigaddset(&s, sig);
    sigprocmask(SIG_UNBLOCK, &s, NULL);         // blocked while its handler runs
    raise(sig);
    _exit(128 + sig);
}

// At the start, so PID 1 can be stopped: SIGINT, SIGTERM and SIGHUP end
// the program by on_end, on a stack of their own, since a process's may
// be nearly full, which SIGCHLD's handler uses too.
static void catch_ends(void) {
    static char      altstack[64 << 10];
    struct sigaction sa = { 0 }, old;
    int              ends[] = { SIGINT, SIGTERM, SIGHUP };
    stack_t          ss     = { .ss_sp = altstack, .ss_size = sizeof altstack };
    sigaltstack(&ss, NULL);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_end;
    sa.sa_flags   = SA_ONSTACK;
    for (int i = 0; i < 3; i++) {               // not one that's ignored, as nohup has SIGHUP
        if (sigaction(ends[i], NULL, &old) == 0 && old.sa_handler == SIG_DFL) sigaction(ends[i], &sa, NULL);
    }
}

// Before the first command: SIGCHLD's pipe and handler. 0, or an errno. A
// replay runs no command, so no signal comes.
static int start_commands(void) {
    struct sigaction sa = { 0 };
    if (child_pipe[0] >= 0) return 0;
    if (rt_io_pipe(child_pipe) < 0) return errno;
    if (rt_io_replaying()) return 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_child;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP | SA_ONSTACK;
    sigaction(SIGCHLD, &sa, NULL);
    atexit(kill_groups);
    return 0;
}

// A command, a list of a name and its arguments, which faults the caller
// if it isn't one: its length.
static uint64_t need_command(rt_value_t cmd, const char *site) {
    uint64_t len = 0;
    if (!rt_is_cons(cmd)) rt_fault(RT_FAULT_NOT_CONS, cmd, site);
    for (rt_value_t x = cmd; rt_is_cons(x); x = rt_cdr(x), len++) {
        if (!rt_is_string(rt_car(x))) rt_fault(RT_FAULT_NOT_STRING, rt_car(x), site);
    }
    return len;
}

// Starts cmd for p: 0, or an errno.
static int exec_start(rt_proc_t *p, const char *site, rt_value_t cmd, uint64_t len) {
    rt_value_t name = rt_car(cmd), x = cmd;
    char     **argv = rt_must(calloc(len + 1, sizeof *argv));
    int        err  = 0, child, in, out;
    for (uint64_t i = 0; i < len; i++, x = rt_cdr(x)) {
        argv[i] = (char *)rt_string_bytes(rt_car(x));
        if (memchr(argv[i], '\0', rt_string_len(rt_car(x)))) err = EINVAL;     // :io-error, as a path's (D130)
    }
    if (!err) err = start_commands();
    if (!err) err = rt_io_spawn(argv, &child, &in, &out);
    free(argv);
    if (err) return err;
    rt_device_t *d = new_device(p, DEV_EXEC, out, site, rt_string_bytes(name), rt_string_len(name));
    d->in_fd = in;
    d->child = d->group = child;
    nchildren++;
    if (!rt_io_replaying()) groups[out] = child;
    tell_open(d);
    return 0;
}

rt_value_t rt_connect_exec(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                           rt_value_t cmd) {
    uint64_t   len = need_command(cmd, site);
    rt_value_t pid = rt_spawn(code, n, values, site);
    rt_proc_t *p   = slot(pid)->proc;
    int        err = exec_start(p, site, cmd, len);
    if (err) {
        _Alignas(16) rt_value_t cells[4];
        fail_start(p, io_reason(cells, err, rt_car(cmd)));
    }
    return pid;
}

static const int signals[] = {          // RT_EXEC_HUP and on
    SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2,
    SIGPIPE, SIGALRM, SIGTERM,
};

// A command's last message, once its output has ended, its owner has
// taken (:eof p), and it has ended too: (:exited p status), and the device
// closes. 0 if it hasn't ended yet.
static int exited(rt_device_t *d) {
    if (d->child) return 0;
    _Alignas(16) rt_value_t cells[6];
    rt_value_t status = rt_int(WIFEXITED(d->status) ? WEXITSTATUS(d->status) : 0);
    if (WIFSIGNALED(d->status)) {
        int k = 0, nsignals = (int)(sizeof signals / sizeof *signals), sig = WTERMSIG(d->status);
        while (k < nsignals && signals[k] != sig) k++;
        rt_value_t name = k < nsignals ? rt_symbol(RT_SYM_EXEC + RT_EXEC_HUP + (uint64_t)k) : rt_int(sig);
        status = rt_cons_at(cells + 2, rt_symbol(RT_SYM_EXEC + RT_EXEC_SIGNAL), rt_cons_at(cells + 4, name, RT_NIL));
    }
    tell_sym(d, rt_symbol(RT_SYM_EXEC + RT_EXEC_EXITED), rt_cons_at(cells, status, RT_NIL), 0);
    close_device(d);
    return 1;
}

static void reap_orphans(void);

// SIGCHLD has come since select() last looked: sees which commands have
// ended. One whose output has ended too, and whose owner is waiting,
// sends its last message.
static void reap(void) {
    char bytes[64];
    while (rt_io_read(child_pipe[0], bytes, sizeof bytes) > 0) {}
    for (rt_device_t *d = sockets, *next; d; d = next) {
        int status;
        next = d->next_socket;
        if (d->mode != DEV_EXEC || !d->child || rt_io_wait(d->child, &status, 1) != d->child) continue;
        d->child  = 0;
        d->status = status;
        nchildren--;
        if (d->eof_sent && !d->sent) exited(d);
    }
    if (is_init) reap_orphans();
}

// As PID 1, what a command started and left running is this program's
// once the command has ended, and stays a zombie when it ends unless the
// program waits for it, as an init does (D229). Each that has ended is
// waited for, but not a command's own, which is only looked at
// (WNOWAIT): reap waits for that, as a replay has it.
static void reap_orphans(void) {
    for (;;) {
        siginfo_t info = { 0 };
        if (waitid(P_ALL, 0, &info, WEXITED | WNOHANG | WNOWAIT) != 0 || info.si_pid == 0) return;
        for (rt_device_t *d = sockets; d; d = d->next_socket) {
            if (d->mode == DEV_EXEC && d->child == info.si_pid) return;
        }
        waitpid(info.si_pid, NULL, 0);
    }
}

// A command's input closes: (:eof) asked for it, and what it had is
// written; or the command has closed its end, or ended, and what's left
// goes nowhere.
static void close_input(rt_device_t *d) {
    if (d->in_fd >= 0) rt_io_close(d->in_fd);
    d->in_fd  = -1;
    d->out_at = d->out_len = 0;
}

// A command's device closes. If anything of it may still be running (it
// hasn't ended, or its output hasn't, so something it started holds
// that), its group is killed; and it's waited for, which a killed process
// can't hold up for long. The command is killed itself too, since under
// qemu, whose posix_spawn returns before the child has run, it may not be
// in its group yet.
static void end_command(rt_device_t *d) {
    int status;
    if (d->child || !d->eof) rt_io_kill(-d->group);
    if (d->child) {
        rt_io_kill(d->child);
        rt_io_wait(d->child, &status, 0);
        d->child = 0;
        nchildren--;
    }
    close_input(d);
    if (d->fd >= 0 && d->fd < FD_SETSIZE) groups[d->fd] = 0;
}

// Whether a command whose input is closed has something the program
// waits for: output its owner waits for, or its end. It can only be
// working on its own, so on the virtual clock nothing else moves till
// it's said something or ended (idle).
static int working(void) {
    int r, w;
    for (rt_device_t *d = sockets; d; d = d->next_socket) {
        if (d->mode == DEV_EXEC && (d->in_fd < 0 || d->in_closing) && wants(d, &r, &w)
            && (r || (d->child && d->eof_sent && !d->sent))) return 1;
    }
    return 0;
}

// --- :system -----------------------------------------------------------------
//
// (connect :system expr) forks expr with a device that tells about the
// processes and the runtime (D171). Anyone may ask it; it answers its
// owner. (:processes) is answered with (:process s info) for each live
// process, in the order of their pids, then (:end s); (:process pid) with
// (:process s info), or with (:ended s pid result) if pid has ended (a
// device counts as a process that ended with (:ok ())); (:runtime) with
// (:runtime s info). Anything else is a dead letter.
//
// (:processes) copies what it tells of every process at once, outside any
// heap: with one core, nothing runs meanwhile, so it all describes one
// moment. Then it sends them one at a time, as a reader does: the next
// when its owner next waits in recv after taking the last (D158), so that
// a million processes don't pile up in a mailbox. A (:processes) asked
// before the last one's (:end s) is answered after it.
//
// Where a process is, :in, is the name of the compiled function it's in:
// the receive function it waits in, the function it was about to enter
// when it was preempted, the one that called join, sleep or yield or
// asked :system, or the one it's to start at. The compiler emits every
// function's entry and name (slight_functions).

#define SYS(word) (RT_SYM_SYSTEM + RT_SYS_##word)

typedef struct snapshot {
    rt_value_t pid, parent, joining;    // pid 0: the end of a (:processes)
    uintptr_t  in;                      // where it is in compiled code, or 0
    uint64_t   mail, heap, calls;
    uint64_t   devices, ndevices;       // its devices: snap_pids from devices
    uint64_t   racing, nracing;         // the pids it races, after them
    int        state, stack;            // an RT_SYS_ state; whether it holds a stack
    int        actor;                   // its kind (D243)
} snapshot_t;

typedef struct {
    uintptr_t   entry;
    const char *name;
} function_t;

static function_t *functions;           // slight_functions, sorted by entry

static int by_entry(const void *a, const void *b) {
    uintptr_t x = ((const function_t *)a)->entry, y = ((const function_t *)b)->entry;
    return x < y ? -1 : x > y;
}

// The name of the compiled function pc is in: the one with the last entry
// at or before it. NULL if there's none.
static const char *function_name(uintptr_t pc) {
    uint64_t n = slight_function_count;
    if (!functions) {
        functions = rt_must(malloc((n + 1) * sizeof *functions));
        memcpy(functions, slight_functions, n * sizeof *functions);
        qsort(functions, n, sizeof *functions, by_entry);
    }
    uint64_t lo = 0, hi = n;                    // the first entry after pc
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (functions[mid].entry <= pc) lo = mid + 1;
        else                            hi = mid;
    }
    return lo ? functions[lo - 1].name : NULL;
}

// Where p is (see above), or 0.
static uintptr_t where(const rt_proc_t *p) {
    return p->stack ? p->at : (uintptr_t)p->code;
}

static void *grow(void *array, uint64_t *cap, size_t size) {
    *cap = *cap ? *cap * 2 : 16;
    return rt_must(realloc(array, *cap * size));
}

static void snap_pid(rt_device_t *d, rt_value_t pid) {
    if (d->snap_npids == d->snap_pids_cap) d->snap_pids = grow(d->snap_pids, &d->snap_pids_cap, sizeof *d->snap_pids);
    d->snap_pids[d->snap_npids++] = pid;
}

static snapshot_t take(rt_device_t *d, const rt_proc_t *p) {
    static const int states[] = {
        [RT_READY] = RT_SYS_READY, [RT_RUNNING] = RT_SYS_RUNNING, [RT_WAITING] = RT_SYS_RECV,
        [RT_JOINING] = RT_SYS_JOINING, [RT_RACING] = RT_SYS_JOINING, [RT_SLEEPING] = RT_SYS_SLEEPING,
        [RT_READING] = RT_SYS_READING,
    };
    snapshot_t r = {
        .pid   = p->pid, .parent = p->parent, .joining = p->state == RT_JOINING ? p->joining : RT_NIL,
        .in    = where(p), .heap = in_use(p), .calls = calls_of(p),
        .state = states[p->state], .stack = p->stack != NULL, .devices = d->snap_npids, .actor = p->actor,
    };
    for (rt_msg_t *m = p->mail; m; m = m->next) r.mail++;
    for (rt_device_t *x = devices_of(p); x; x = x->next) snap_pid(d, x->pid);
    r.ndevices = d->snap_npids - r.devices;
    r.racing   = d->snap_npids;
    if (p->state == RT_RACING) {
        for (const rt_watch_t *w = race_last(p); w; w = w->prev_held) if (w->wakes == WATCH_RACE) snap_pid(d, w->watched->pid);
    } else if (p->state == RT_READING) {
        for (rt_value_t xs = p->joining; xs != RT_NIL; xs = rt_cdr(xs)) snap_pid(d, rt_car(xs));
    }
    r.nracing = d->snap_npids - r.racing;
    return r;
}

// (key value), in 4 words at *at, which it moves past them.
static rt_value_t field(rt_value_t **at, uint64_t key, rt_value_t value) {
    rt_value_t *c = *at;
    *at += 4;
    return rt_cons_at(c, rt_symbol(key), rt_cons_at(c + 2, value, RT_NIL));
}

// The list of xs[0..n), in 2n words at *at.
static rt_value_t list_at(rt_value_t **at, const rt_value_t *xs, uint64_t n) {
    rt_value_t list = RT_NIL;
    for (uint64_t i = n; i > 0; i--, *at += 2) list = rt_cons_at(*at, xs[i - 1], list);
    return list;
}

// (:process s info).
static void tell_process(rt_device_t *d, const snapshot_t *r, int more) {
    const char *name  = r->in ? function_name(r->in) : NULL;
    size_t      len   = name ? strlen(name) : 0;
    uint64_t    words = (len / 8 + 7) & ~(uint64_t)1;     // the name's string, then cells
    rt_value_t *space = rt_must(malloc((words + 68 + 2 * (r->ndevices + r->nracing)) * sizeof *space)), *at = space + words;
    rt_value_t  in    = name ? string_in(space, name, len) : RT_NIL;
    rt_value_t  devices = RT_NIL;                          // they're newest first: the list is oldest first
    for (uint64_t i = 0; i < r->ndevices; i++, at += 2) devices = rt_cons_at(at, d->snap_pids[r->devices + i], devices);
    rt_value_t  joining = r->nracing ? list_at(&at, d->snap_pids + r->racing, r->nracing) : r->joining;
    rt_value_t fields[] = {
        field(&at, SYS(PID), r->pid),
        field(&at, SYS(PARENT), r->parent),
        field(&at, RT_SYM_KIND + RT_KIND_KIND, rt_symbol(RT_SYM_KIND + (r->actor ? RT_KIND_ACTOR : RT_KIND_TASK))),
        field(&at, SYS(STATE), rt_symbol(RT_SYM_SYSTEM + (uint64_t)r->state)),
        field(&at, SYS(JOINING), joining),
        field(&at, SYS(IN), in),
        field(&at, SYS(MAIL), rt_int((int64_t)r->mail)),
        field(&at, RT_SYM_FAULTS - 1 + RT_FAULT_HEAP, rt_int((int64_t)r->heap)),
        field(&at, SYS(REDUCTIONS), rt_int((int64_t)r->calls)),
        field(&at, RT_SYM_FAULTS - 1 + RT_FAULT_STACK, r->stack ? RT_TRUE : RT_FALSE),
        field(&at, SYS(DEVICES), devices),
    };
    rt_value_t info = list_at(&at, fields, sizeof fields / sizeof *fields);
    tell_sym(d, rt_symbol(SYS(PROCESS)), rt_cons_at(at, info, RT_NIL), more);
    free(space);
}

// (:runtime s info).
static void tell_runtime(rt_device_t *d) {
    uint64_t live = 0, ready = 0, heap = 0, stacks = 0, calls = retired_calls;
    for (uint64_t i = 1; i <= nprocs; i++) {
        const rt_proc_t *p = at(i)->proc;
        if (!p) continue;
        live++;
        heap   += in_use(p);
        stacks += p->stack != NULL;
        calls  += calls_of(p);
    }
    for (const rt_proc_t *p = ready_head; p; p = p->next_ready) ready++;
    _Alignas(16) rt_value_t space[11 * 6 + 2], *at = space;
    rt_value_t fields[] = {
        field(&at, SYS(PROCESSES), rt_int((int64_t)live)),
        field(&at, SYS(PIDS), rt_int((int64_t)nprocs)),
        field(&at, RT_SYM_TABLE + RT_TABLE_RECORDS, rt_int((int64_t)nrecords)),
        field(&at, SYS(READY), rt_int((int64_t)ready)),
        field(&at, SYS(TIMERS), rt_int((int64_t)nalarms)),
        field(&at, SYS(DEVICES), rt_int((int64_t)ndevices)),
        field(&at, RT_SYM_FAULTS - 1 + RT_FAULT_HEAP, rt_int((int64_t)heap)),
        field(&at, SYS(HEAP_MAX), rt_int((int64_t)heap_max)),
        field(&at, SYS(STACKS), rt_int((int64_t)stacks)),
        field(&at, SYS(CLOCK), rt_int((int64_t)((now() - started) / 1000000))),
        field(&at, SYS(REDUCTIONS), rt_int((int64_t)calls)),
    };
    rt_value_t info = list_at(&at, fields, sizeof fields / sizeof *fields);
    tell_sym(d, rt_symbol(SYS(RUNTIME)), rt_cons_at(at, info, RT_NIL), 0);
}

// Not yet ended. (A process that has is RT_DONE only on its way out,
// between finish and retire, when its exit record is written already.)
static int alive(const rt_proc_t *p) {
    return p && p->state != RT_DONE;
}

static void push_snapshot(rt_device_t *d, snapshot_t r) {
    if (d->snap_len == d->snap_cap) d->snap = grow(d->snap, &d->snap_cap, sizeof *d->snap);
    d->snap[d->snap_len++] = r;
}

// Sends the next of the processes still to be sent, if there are any.
static void system_next(rt_device_t *d) {
    if (d->snap_at == d->snap_len) return;
    snapshot_t r    = d->snap[d->snap_at++];
    int        more = d->snap_at < d->snap_len;
    if (r.pid) tell_process(d, &r, 1);
    else       tell_sym(d, rt_symbol(SYS(END)), RT_NIL, more);
    if (!more) d->snap_at = d->snap_len = d->snap_npids = 0;
}

static void system_command(rt_device_t *d, rt_value_t msg) {
    rt_value_t word = rt_is_cons(msg) ? rt_car(msg) : RT_NIL, args = rt_is_cons(msg) ? rt_cdr(msg) : RT_NIL;
    rt_value_t pid  = rt_is_cons(args) && rt_cdr(args) == RT_NIL ? rt_car(args) : RT_NIL;
    if (word == rt_symbol(SYS(PROCESSES)) && args == RT_NIL) {
        for (uint64_t i = 1; i <= nprocs; i++) {
            if (alive(at(i)->proc)) push_snapshot(d, take(d, at(i)->proc));
        }
        push_snapshot(d, (snapshot_t){ 0 });
        if (!d->sent) system_next(d);               // else when the owner waits for the next
    } else if (word == rt_symbol(SYS(PROCESS)) && (pid & RT_TAG_MASK) == RT_TAG_PID && !rt_is_address(pid)) {
        const entry_t *e = slot(pid);
        if (alive(e->proc)) {
            uint64_t   mark = d->snap_npids;     // its devices go after the ones still to be sent
            snapshot_t r    = take(d, e->proc);
            tell_process(d, &r, 0);
            d->snap_npids = mark;
        } else {
            _Alignas(16) rt_value_t cells[8];
            tell_sym(d, rt_symbol(SYS(ENDED)), rt_cons_at(cells + 4, pid, rt_cons_at(cells + 6, result_at(cells, e), RT_NIL)), 0);
        }
    } else if (word == rt_symbol(SYS(RUNTIME)) && args == RT_NIL) {
        tell_runtime(d);
    } else {
        rt_dead_letter(msg, d->site);
    }
}

rt_value_t rt_connect_system(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) {
    rt_value_t pid = rt_spawn(code, n, values, site);
    tell_open(new_device(slot(pid)->proc, DEV_SYSTEM, -1, site, NULL, 0));
    return pid;
}

// (open source arg): a device the calling task owns, with no process to
// own it (D243), opened as connect opens one, then read till it's open:
// (:ok d), or (:error reason). A listener, and the keyboard, are an
// actor's (connect): something that waits for whatever comes is one.
static rt_value_t read_from(rt_value_t srcs, int pair, uintptr_t at, const char *site);

rt_value_t rt_open(rt_value_t source, rt_value_t arg, const char *site) {
    rt_proc_t *p = rt_current;
    if (p->actor) rt_fault(RT_FAULT_NOT_TASK, p->pid, site);   // an actor connects, and takes (:open f) with recv
    uint64_t which = (source & RT_TAG_MASK) == RT_TAG_SYMBOL ? (source >> RT_SYMBOL_SHIFT) - RT_SYM_SOURCE : RT_SOURCE_COUNT;
    if (which >= RT_SOURCE_COUNT) rt_fault(RT_FAULT_NOT_DEVICE, source, site);
    if (which == RT_SOURCE_EXEC) {
        need_command(arg, site);
    } else if (which != RT_SOURCE_SYSTEM && !rt_is_string(arg)) {
        rt_fault(RT_FAULT_NOT_STRING, arg, site);
    }
    rt_device_t *had = devices_of(p);
    int          err = 0;
    rt_value_t   where = arg;
    switch (which) {
        case RT_SOURCE_FS_READ:   err = fs_open(p, site, arg, RT_FS_READ); break;
        case RT_SOURCE_FS_WRITE:  err = fs_open(p, site, arg, RT_FS_WRITE); break;
        case RT_SOURCE_FS_APPEND: err = fs_open(p, site, arg, RT_FS_APPEND); break;
        case RT_SOURCE_TCP:       err = tcp_connect(p, site, rt_string_bytes(arg), rt_string_len(arg)); break;
        case RT_SOURCE_UNIX:      err = unix_at(p, site, arg, RT_UNIX_CONNECT); break;
        case RT_SOURCE_EXEC:      err = exec_start(p, site, arg, need_command(arg, site)); where = rt_car(arg); break;
        default:                  tell_open(new_device(p, DEV_SYSTEM, -1, site, NULL, 0)); break;
    }
    _Alignas(16) rt_value_t cells[8];
    if (err) return copy_here(rt_cons_at(cells, rt_symbol(RT_SYM_ERROR), rt_cons_at(cells + 2, io_reason(cells + 4, err, where), RT_NIL)));
    rt_value_t d = devices_of(p) != had ? devices_of(p)->pid : RT_NIL;   // new_device puts it first
    rt_value_t m = read_from(rt_cons_at(cells, d, RT_NIL), 0, (uintptr_t)__builtin_return_address(0), site);
    if (rt_car(m) == rt_symbol(RT_SYM_ERROR)) {                        // (:error d reason)
        return copy_here(rt_cons_at(cells, rt_symbol(RT_SYM_ERROR), rt_cons_at(cells + 2, rt_car(rt_cdr(rt_cdr(m))), RT_NIL)));
    }
    return copy_here(rt_cons_at(cells, rt_symbol(RT_SYM_OK), rt_cons_at(cells + 2, d, RT_NIL)));
}

// --- the scheduler ------------------------------------------------------------

void rt_run(void) {
    for (;;) {
        events();
        rt_proc_t *p = dequeue();
        if (!p) {
            if (!idle()) return;
            continue;
        }
        if (!p->stack) start(p);
        p->state   = RT_RUNNING;
        rt_current = p;
        rt_switch(&scheduler, &p->ctx);
        rt_current = NULL;                      // so nothing takes the scheduler for p
        // back: it paused (and queued itself again), waits in recv, join
        // or sleep, or ended
        if (p->state == RT_WAITING)   release_stack(p);
        else if (p->state == RT_DONE) retire(p);
    }
}

// @ARGV: the program's arguments after its name, as a list of strings in
// the root's heap. The root's entry, slight_main, takes it as its one
// parameter, so only the top level sees it (D144).
static rt_value_t arguments(rt_proc_t *root, int argc, char **argv) {
    rt_value_t list = RT_NIL;
    rt_current = root;
    for (int i = argc - 1; i > 0; i--) {
        rt_value_t  s    = rt_new_string(argv[i], strlen(argv[i]), "@ARGV");
        rt_value_t *cell = rt_alloc(16, "@ARGV");
        cell[0] = s;
        cell[1] = list;
        list    = (rt_value_t)cell | RT_TAG_LIST;
    }
    rt_current = NULL;
    return list;
}

static rt_value_t cons_new(rt_value_t car, rt_value_t cdr, const char *site) {
    rt_value_t *cell = rt_alloc(16, site);
    cell[0] = car;
    cell[1] = cdr;
    return (rt_value_t)cell | RT_TAG_LIST;
}

// %ENV: the program's environment, as a list of (name value) lists of
// strings in the root's heap, in the environment's order, for lookup. An
// entry is cut at its first =, and one without an = is left out. The
// root's entry takes it as its second parameter (D186).
static rt_value_t environment(rt_proc_t *root, char **env) {
    uint64_t n = 0;
    while (env[n]) n++;
    rt_value_t list = RT_NIL;
    rt_current = root;
    for (uint64_t i = n; i-- > 0; ) {
        const char *eq = strchr(env[i], '=');
        if (!eq) continue;
        rt_value_t name  = rt_new_string(env[i], (size_t)(eq - env[i]), "%ENV");
        rt_value_t value = rt_new_string(eq + 1, strlen(eq + 1), "%ENV");
        list = cons_new(cons_new(name, cons_new(value, RT_NIL, "%ENV"), "%ENV"), list, "%ENV");
    }
    rt_current = NULL;
    return list;
}

extern char **environ;

// SLIGHT_HEAP_MAX's bytes: digits, with K, M or G (2^10, 2^20, 2^30) after
// them, or nothing; or 0 if it isn't that.
static size_t bytes_of(const char *s) {
    size_t digits = strspn(s, "0123456789");
    int    shift  = !s[digits] ? 0 : s[digits + 1] ? -1
                  : s[digits] == 'K' ? 10 : s[digits] == 'M' ? 20 : s[digits] == 'G' ? 30 : -1;
    if (digits == 0 || digits > 18 || shift < 0) return 0;     // 18 digits fit strtoull
    unsigned long long n = strtoull(s, NULL, 10);
    return n > (SIZE_MAX >> shift) ? 0 : (size_t)n << shift;
}

// A cgroup limit's bytes, read from file: v2's "max", v1's figure for no
// limit (the most a page counter holds, near 2^63), or nothing there, is 0.
static size_t limit_in(const char *file) {
    FILE *f = fopen(file, "r");
    char  line[32];
    int   ok = f && fgets(line, sizeof line, f) && line[0] >= '0' && line[0] <= '9';
    if (f) fclose(f);
    unsigned long long n = ok ? strtoull(line, NULL, 10) : 0;
    return n >= 1ull << 62 || n > SIZE_MAX ? 0 : (size_t)n;
}

// The memory this program may use, as its cgroup says (D233): the least
// of the limits on its own cgroup and those above it, v2's memory.max or
// v1's memory.limit_in_bytes, the path to each from /proc/self/cgroup;
// or 0 if none is found (no limit, or not Linux). In a container that
// has its own view of the cgroups, the path is / and the limit the
// container's; one that hasn't keeps the host's path, which isn't there
// to read, so the walk up comes to the container's own at the top.
static size_t memory_limit(void) {
    FILE  *f = fopen("/proc/self/cgroup", "r");
    char   line[512], file[1024];
    size_t least = 0;
    while (f && fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':'), *path = colon ? strchr(colon + 1, ':') : NULL;
        if (!path) continue;
        *path++ = '\0';
        path[strcspn(path, "\n")] = '\0';
        const char *controllers = colon + 1, *base, *name;
        if (*controllers == '\0') {                           // v2: "0::/path"
            base = "/sys/fs/cgroup";
            name = "memory.max";
        } else {                                                // v1: "4:memory:/path", the controllers by commas
            size_t at = 0, len = strlen("memory");
            while (controllers[at] && !(strncmp(controllers + at, "memory", len) == 0
                                        && (controllers[at + len] == ',' || controllers[at + len] == '\0'))) {
                at += strcspn(controllers + at, ",");
                at += controllers[at] == ',';
            }
            if (!controllers[at]) continue;
            base = "/sys/fs/cgroup/memory";
            name = "memory.limit_in_bytes";
        }
        for (size_t len = strlen(path);; ) {                   // path, then each above it, to /
            while (len > 1 && path[len - 1] == '/') len--;
            snprintf(file, sizeof file, "%s%.*s/%s", base, len == 1 ? 0 : (int)len, path, name);
            size_t n = limit_in(file);
            if (n && (!least || n < least)) least = n;
            if (len <= 1) break;
            while (len > 1 && path[len - 1] != '/') len--;
        }
    }
    if (f) fclose(f);
    return least;
}

// Where p waits, for the deadlock message: in a recv, or for a process to
// end, and then where that one waits, and so on, since the root is a task
// that joins the actors doing the work (D243). Processes that join each
// other make a cycle, so the chain is cut short after a few.
static void tell_pid(const char *before, rt_value_t v, const char *after) {
    char text[64];
    rt_pid_text(text, sizeof text, v);
    fprintf(stderr, "%s%s%s", before, text, after);
}

static void tell_waiting(const rt_proc_t *p) {
    for (int hops = 0;; hops++) {
        if (hops == 8) {
            fprintf(stderr, ", and so on");
            return;
        }
        const char *in = function_name(where(p));
        fprintf(stderr, "%swaiting in %s for ", hops ? ", which is " : "", in ? in : "?");
        if (p->state == RT_RACING) {
            fprintf(stderr, "the first of");
            const char *sep = " ";
            for (const rt_watch_t *w = race_last(p); w; w = w->prev_held) {
                if (w->wakes != WATCH_RACE) continue;
                tell_pid(sep, w->watched->pid, "");
                sep = ", ";
            }
            fprintf(stderr, " to end");
            return;
        }
        if (p->state == RT_READING) {
            rt_value_t xs = p->joining;
            if (rt_cdr(xs) != RT_NIL || rt_is_address(rt_car(xs))) {
                fprintf(stderr, rt_cdr(xs) == RT_NIL ? "a message to" : "the first of");
                for (const char *sep = " "; xs != RT_NIL; xs = rt_cdr(xs), sep = ", ") {
                    rt_value_t v = rt_car(xs);
                    tell_pid(sep, v, "");
                }
                return;
            }
            tell_pid("", rt_car(xs), " to end");    // read of one pid: as join
            p = slot(rt_car(xs))->proc;
            if (!p) return;
            continue;
        }
        if (p->state != RT_JOINING) {
            fprintf(stderr, "a message");
            return;
        }
        tell_pid("", p->joining, " to end");
        p = slot(p->joining)->proc;
        if (!p) return;
    }
}

int main(int argc, char **argv) {
    page   = (size_t)sysconf(_SC_PAGESIZE);
    signal(SIGPIPE, SIG_IGN);                   // writing to a closed connection is EPIPE, not the end
    catch_ends();
    is_init = getpid() == 1;
    poison    = getenv("SLIGHT_POISON") != NULL;
    gc_verify = getenv("SLIGHT_GC_VERIFY") != NULL;
    const char *stress = getenv("SLIGHT_GC_STRESS");
    gc_stress = !stress ? 0 : atoll(stress) < 1 ? 1 : atoll(stress) > QUOTA ? QUOTA : atoll(stress);
    const char *mode = getenv("SLIGHT_CLOCK"), *seed = getenv("SLIGHT_SCHED"), *cap = getenv("SLIGHT_HEAP_MAX");
    const char *rand_seed = getenv("SLIGHT_SEED"), *epoch_at = getenv("SLIGHT_EPOCH");
    if (seed && (!*seed || strspn(seed, "0123456789") != strlen(seed))) {
        fputs("slight: SLIGHT_SCHED is a number, a seed for the order processes run in\n", stderr);
        return 2;
    }
    if (rand_seed && (!*rand_seed || strspn(rand_seed, "0123456789") != strlen(rand_seed))) {
        fputs("slight: SLIGHT_SEED is a number, a seed for the random numbers\n", stderr);
        return 2;
    }
    const char *log_at = getenv("SLIGHT_LOG");
    if (log_at) {
        static const char *const levels[] = { "debug", "info", "warn", "error" };
        rt_log_level = -1;
        for (int i = 0; i < 4; i++) if (strcmp(log_at, levels[i]) == 0) rt_log_level = i;
        if (rt_log_level < 0) {
            fputs("slight: SLIGHT_LOG is the least level log writes: debug, info, warn or error\n", stderr);
            return 2;
        }
    }
    if (epoch_at && (!*epoch_at || strlen(epoch_at) > 12 || strspn(epoch_at, "0123456789") != strlen(epoch_at))) {
        fputs("slight: SLIGHT_EPOCH is a number, the seconds since 1970 that the virtual clock's time of day starts at\n", stderr);
        return 2;
    }
    if (cap && bytes_of(cap) < CHUNK_MAX) {
        fputs("slight: SLIGHT_HEAP_MAX is the most a process's heap holds, in bytes or with K, M or G after it, and at least 1M\n", stderr);
        return 2;
    }
    // Without SLIGHT_HEAP_MAX, 1 GB, or a quarter of a smaller limit on the
    // program's memory, as in a container (D233): a process that runs
    // away holds about twice its cap at its last collection, the old
    // chunks and the new, so it faults on its own at half the limit, and
    // the kernel doesn't kill the program.
    size_t limit = cap ? 0 : memory_limit();
    size_t room  = limit && limit / 4 < HEAP_DEFAULT ? (limit / 4 > CHUNK_MAX ? limit / 4 : CHUNK_MAX) : HEAP_DEFAULT;
    rt_io_settings_t settings = {
        mode && strcmp(mode, "virtual") == 0, seed ? strtoull(seed, NULL, 10) : 0, cap ? bytes_of(cap) : room,
    };
    char           **env      = environ;
    rt_io_start(&argc, &argv, &env, &settings); // replaying, the record's settings, arguments and environment
    virtual_clock = settings.virtual_clock;
    sched         = settings.sched_seed;
    heap_max      = settings.heap_max;
    started       = now();
    if (virtual_clock) epoch = rt_io_epoch(epoch_at);
    rt_proc_t *p = rt_new_process((rt_code_t)slight_main, RT_NIL);
    rt_rand_start(p, rt_io_random_seed(rand_seed));
    p->args[0]   = arguments(p, argc, argv);
    p->args[1]   = environment(p, env);
    rt_run();
    const entry_t *root = at(1);
    if (root->proc) {
        fflush(stdout);
        fprintf(stderr, "deadlock: the root process is ");
        tell_waiting(root->proc);
        fprintf(stderr, ", and nothing else can run\n");
        return 1;
    }
    if (!root->ok) return 1;
    rt_buf_t b = { 0 };
    rt_render(&b, root->value, 0);
    fwrite(b.bytes, 1, b.len, stdout);
    putchar('\n');
    rt_buf_free(&b);
    return 0;
}
