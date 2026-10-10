// process.c -- processes: the table, the run queue, stacks, heaps,
// messages, how processes end, and the scheduler.
//
// One core. The scheduler runs on the C stack and switches to a process
// with rt_switch; the process switches back when it pauses (preempted, or
// yield), waits in recv, or ends. A process holds a stack only while it
// needs one: waiting in recv, it's just (code, args, mailbox), and it gives
// its stack back to the pool. A message arriving puts it back in the run
// queue, and it starts again at code, on a stack from the pool.
//
// A process that ends leaves an exit record in the table, kept for good
// (D88), so join and monitor can ask about it at any time; everything else
// it had is freed.
//
// When nothing can run, the scheduler waits for the next timer (after,
// sleep) or key (:keypress). The program ends when nothing can run and
// nothing more can come: no timer is pending, and no process is connected
// to :keypress, or stdin has ended.
//
// A file or a socket opened with connect is a device: a pid that the
// runtime serves instead of compiled code (see "devices", below).

#include "rt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define QUOTA          1000             // reductions between preemptions
#define STACK_BYTES    (8 << 20)        // every process's stack, mapped lazily
#define STACK_HEADROOM (64 << 10)       // room left below the limit for one frame and a C call
#define CHUNK_FIRST    (4 << 10)        // a process's first heap chunk...
#define CHUNK_MAX      (1 << 20)        // ...doubling up to this
#define HEAP_MAX       (64 << 20)       // all of a process's chunks
#define GC_MIN         (256 << 10)      // a heap smaller than this is never collected

// A pid's entry in the table: the process while it runs, then its exit
// record: (:ok value) or (:error value). Or a device while it's open; to
// join, monitor and kill, a device is a process that ended with (:ok ()).
typedef struct {
    rt_proc_t        *proc;             // NULL once it has ended, and for a device
    rt_value_t        value;
    rt_chunk_t       *chunk;            // where value lives, or NULL if it needs no space
    int               ok;
    struct rt_device *device;           // an open device, or NULL
} entry_t;

static entry_t    *procs;               // by pid number; the root is 1
static uint64_t    nprocs, procs_cap;
static rt_proc_t  *ready_head, *ready_tail;
static rt_ctx_t    scheduler;
static void       *free_stacks;         // a list through each stack's first word
static size_t      page;
static int         poison;              // SLIGHT_POISON: fill what the collector frees

static void *must(void *p) {
    if (!p) {
        perror("rt: out of memory");
        exit(2);
    }
    return p;
}

// --- the run queue ------------------------------------------------------------

static void enqueue(rt_proc_t *p) {
    p->state      = RT_READY;
    p->next_ready = NULL;
    if (ready_tail) ready_tail->next_ready = p;
    else            ready_head = p;
    ready_tail = p;
}

static rt_proc_t *dequeue(void) {
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

// A stack, and registers that make the first switch land in rt_trampoline
// with x28 (r15) = p, which calls p->code with p->args.
static void start(rt_proc_t *p) {
    p->stack       = get_stack();
    p->stack_limit = (uintptr_t)p->stack + STACK_HEADROOM;
    p->reductions  = QUOTA;
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

static rt_chunk_t *new_chunk(size_t bytes) {
    rt_chunk_t *c = malloc(sizeof *c + bytes);
    if (!c) {
        perror("rt: out of memory");
        exit(2);
    }
    c->next = NULL;
    c->size = bytes;
    return c;
}

static void adopt(rt_proc_t *p, rt_chunk_t *c) {
    c->next        = p->chunks;
    p->chunks      = c;
    p->heap_bytes += c->size;
}

void rt_heap_grow(uint64_t bytes, const char *site) {
    rt_proc_t *p    = rt_current;
    size_t     size = p->next_chunk ? p->next_chunk : CHUNK_FIRST;
    p->next_chunk   = size * 2 > CHUNK_MAX ? CHUNK_MAX : size * 2;
    if (size < bytes) size = (bytes + 15) & ~(size_t)15;
    if (p->heap_bytes + size > HEAP_MAX) rt_fault(RT_FAULT_HEAP, 0, site);
    rt_chunk_t *c = new_chunk(size);
    adopt(p, c);
    p->heap_ptr   = (uintptr_t)(c + 1);
    p->heap_limit = p->heap_ptr + size;
}

static void free_chunks(rt_chunk_t *c) {
    for (rt_chunk_t *next; c; c = next) {
        next = c->next;
        free(c);
    }
}

static void free_msg(rt_msg_t *m) {
    free(m->chunk);
    free(m);
}

static void free_heap(rt_proc_t *p) {
    free_chunks(p->chunks);
    p->chunks     = NULL;
    p->heap_ptr   = p->heap_limit = 0;
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
        case RT_BOX_STRING:  return (8 + size + 1 + 15) & ~(size_t)15;
        case RT_BOX_FLOAT:   return 16;
        case RT_BOX_CLOSURE: return (32 + 8 * size + 15) & ~(size_t)15;
        default:             return 0;
    }
}

// Copying a value is two walks over it, each with a work stack (D147):
// one adds up the bytes, so that one chunk can hold the copy, and one
// copies. Each goes along a list's cdrs, dealing with an element that's an
// atom, a string or a float on the spot, and goes down into an element
// that's a list or a closure, keeping the rest of the list (and a
// closure's captured values) on the stack. So the stack grows only as deep
// as the value nests, and a flat list never touches it. Sharing isn't
// kept: a DAG is copied as a tree.

// A string or a float: a box with nothing in it to copy.
static int is_leaf_box(rt_value_t v) {
    return is_pointer(v) && !rt_is_cons(v) && !rt_is_closure(v);
}

static size_t copy_size(rt_value_t v) {
    rt_work_t w;
    rt_work_init(&w);
    size_t n = 0;
    for (;;) {
        if (is_pointer(v) && rt_is_cons(v)) {
            rt_value_t x = rt_car(v);
            n += 16;
            if (is_pointer(x) && !is_leaf_box(x)) {
                rt_work_push(&w, rt_cdr(v));
                v = x;
                continue;
            }
            if (is_leaf_box(x)) n += box_bytes(x);
            v = rt_cdr(v);
            continue;
        }
        if (is_pointer(v)) {
            n += box_bytes(v);
            if (rt_is_closure(v)) {
                for (uint64_t i = 0; i < rt_box(v)[0] >> RT_BOX_SIZE_SHIFT; i++) rt_work_push(&w, rt_box(v)[4 + i]);
            }
        }
        if (!w.n) break;
        v = rt_work_pop(&w);
    }
    rt_work_free(&w);
    return n;
}

// A copy of the box v at *to, moving *to past it.
static rt_value_t copy_box(rt_value_t v, char **to) {
    size_t    bytes = box_bytes(v);
    uint64_t *box   = (uint64_t *)*to;
    *to += bytes;
    memcpy(box, rt_box(v), bytes);
    return (rt_value_t)box | RT_TAG_BOXED;
}

// Copies v to *to, moving *to past it. The work stack holds pairs: where
// a copy goes, and the value to copy there.
static rt_value_t copy(rt_value_t v, char **to) {
    rt_work_t   w;
    rt_value_t  out;
    rt_value_t *dst = &out;
    rt_work_init(&w);
    for (;;) {
        if (!is_pointer(v)) {
            *dst = v;                           // an immediate, (), or static data
        } else if (rt_is_cons(v)) {
            rt_value_t *cell = (rt_value_t *)*to;
            rt_value_t  x    = rt_car(v);
            *to += 16;
            *dst = (rt_value_t)cell | RT_TAG_LIST;
            dst  = &cell[1];
            if (is_pointer(x) && !is_leaf_box(x)) {
                rt_work_push(&w, (rt_value_t)dst);
                rt_work_push(&w, rt_cdr(v));
                dst = &cell[0];
                v   = x;
                continue;
            }
            cell[0] = is_leaf_box(x) ? copy_box(x, to) : x;
            v       = rt_cdr(v);
            continue;
        } else {
            uint64_t *box = (uint64_t *)*to;
            *dst = copy_box(v, to);
            if (rt_is_closure(v)) {
                for (uint64_t i = 0; i < box[0] >> RT_BOX_SIZE_SHIFT; i++) {
                    rt_work_push(&w, (rt_value_t)&box[4 + i]);
                    rt_work_push(&w, box[4 + i]);
                }
            }
        }
        if (!w.n) break;
        v   = rt_work_pop(&w);
        dst = (rt_value_t *)rt_work_pop(&w);
    }
    rt_work_free(&w);
    return out;
}

// Copies n values into one new chunk (NULL if they need none).
static rt_chunk_t *copy_values(const rt_value_t *from, rt_value_t *into, uint64_t n) {
    size_t bytes = 0;
    for (uint64_t i = 0; i < n; i++) bytes += copy_size(from[i]);
    rt_chunk_t *c  = bytes ? new_chunk(bytes) : NULL;
    char       *to = c ? (char *)(c + 1) : NULL;
    for (uint64_t i = 0; i < n; i++) into[i] = copy(from[i], &to);
    return c;
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
// Only when a receive function asks for its next message (D105). The recv
// rule means the stack is empty then, so the roots are just its arguments,
// in its frame. The live data is copied into fresh chunks and the old ones
// freed; C code never sees anything move.
//
// Copying leaves a forwarding pointer behind, so sharing is kept: a DAG
// stays a DAG. Cons cells have no header, so to-space can't be scanned in
// order as Cheney's algorithm would; instead, each copied cons or closure
// goes on a stack of objects whose fields still point at the old heap.
// Taking the cdr first keeps that stack short along a list.

#define GC_MOVED_CONS RT_TAG_BOXED      // in a moved cell's car: a boxed null, which no value is
#define GC_MOVED_BOX  0                 // in a moved box's header: no box has type 0

typedef struct {
    rt_chunk_t *chunks;                 // to-space, newest first
    uintptr_t   ptr, limit;
    size_t      bytes, live, size;      // to-space's capacity, what's in it, the size of a new chunk
    rt_work_t   todo;                   // copied, with fields still to forward
} gc_t;

static void *gc_alloc(gc_t *g, size_t bytes) {
    if (g->limit - g->ptr < bytes) {
        size_t      size = bytes > g->size ? bytes : g->size;
        rt_chunk_t *c    = new_chunk(size);
        c->next   = g->chunks;
        g->chunks = c;
        g->bytes += size;
        g->ptr    = (uintptr_t)(c + 1);
        g->limit  = g->ptr + size;
    }
    void *at = (void *)g->ptr;
    g->ptr  += bytes;
    g->live += bytes;
    return at;
}

// Where v lives now, copying it there if it hasn't been yet.
static rt_value_t forward(gc_t *g, rt_value_t v) {
    if (!is_pointer(v)) return v;
    if (rt_is_cons(v)) {
        rt_value_t *old = (rt_value_t *)(v - RT_TAG_LIST);
        if (old[0] == GC_MOVED_CONS) return old[1];
        rt_value_t *new = gc_alloc(g, 16);
        new[0] = old[0];
        new[1] = old[1];
        old[0] = GC_MOVED_CONS;
        old[1] = (rt_value_t)new | RT_TAG_LIST;
        rt_work_push(&g->todo, old[1]);
        return old[1];
    }
    uint64_t *old = (uint64_t *)(v - RT_TAG_BOXED);
    if (old[0] == GC_MOVED_BOX) return old[1];
    size_t    bytes = box_bytes(v);
    uint64_t *new   = gc_alloc(g, bytes);
    memcpy(new, old, bytes);
    old[0] = GC_MOVED_BOX;
    old[1] = (rt_value_t)new | RT_TAG_BOXED;
    if ((new[0] & RT_BOX_TYPE_MASK) == RT_BOX_CLOSURE) rt_work_push(&g->todo, old[1]);
    return old[1];
}

static void collect(rt_proc_t *p, rt_value_t *roots, uint64_t n) {
    gc_t g = { .size = p->heap_bytes < CHUNK_MAX ? p->heap_bytes : CHUNK_MAX };
    rt_work_init(&g.todo);
    for (uint64_t i = 0; i < n; i++) roots[i] = forward(&g, roots[i]);
    while (g.todo.n) {
        rt_value_t v = rt_work_pop(&g.todo);
        if (rt_is_cons(v)) {
            rt_value_t *cell = (rt_value_t *)(v - RT_TAG_LIST);
            cell[1] = forward(&g, cell[1]);
            cell[0] = forward(&g, cell[0]);
        } else {
            uint64_t *box = (uint64_t *)(v - RT_TAG_BOXED);
            for (uint64_t i = 0; i < box[0] >> RT_BOX_SIZE_SHIFT; i++) box[4 + i] = forward(&g, box[4 + i]);
        }
    }
    rt_work_free(&g.todo);
    // With SLIGHT_POISON set (t/run.sh sets it), a pointer the collector
    // missed finds garbage at once instead of memory that looks fine
    // until it's reused: 0xabab... is a boxed pointer to nowhere.
    for (rt_chunk_t *c = p->chunks; c && poison; c = c->next) memset(c + 1, 0xab, c->size);
    free_chunks(p->chunks);
    p->chunks     = g.chunks;
    p->heap_bytes = g.bytes;
    p->heap_ptr   = g.ptr;
    p->heap_limit = g.limit;
    p->gc_at      = 2 * g.live > GC_MIN ? 2 * g.live : GC_MIN;
}

// --- processes ----------------------------------------------------------------

// A new pid, for e. Pids start at 1: the root.
static rt_value_t new_entry(entry_t e) {
    if (nprocs + 1 >= procs_cap) {
        procs_cap = procs_cap ? procs_cap * 2 : 1024;
        procs     = must(realloc(procs, procs_cap * sizeof *procs));
    }
    procs[++nprocs] = e;
    return nprocs << RT_PID_SHIFT | RT_TAG_PID;
}

rt_proc_t *rt_new_process(rt_code_t code, rt_value_t parent) {
    rt_proc_t *p = must(calloc(1, sizeof *p));
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
    if ((pid & RT_TAG_MASK) != RT_TAG_PID) rt_fault(RT_FAULT_NOT_PID, pid, site);
    return &procs[pid >> RT_PID_SHIFT];
}

rt_value_t rt_fork(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) {
    (void)site;
    rt_proc_t  *p = rt_new_process(code, rt_current->pid);
    rt_chunk_t *c = copy_values(values, p->args, n);
    if (c) adopt(p, c);
    return p->pid;
}

// A message holding a copy of msg, in a chunk of its own.
static rt_msg_t *new_msg(rt_value_t msg) {
    rt_msg_t *m = malloc(sizeof *m);
    if (!m) {
        perror("rt: out of memory");
        exit(2);
    }
    m->next   = NULL;
    m->chunk  = copy_values(&msg, &m->value, 1);
    m->device = 0;
    return m;
}

// Into p's mailbox; p wakes if it's waiting for a message.
static void deliver(rt_proc_t *p, rt_msg_t *m) {
    if (p->mail_last) p->mail_last->next = m;
    else              p->mail = m;
    p->mail_last = m;
    if (p->state == RT_WAITING) enqueue(p);
}

static void command(struct rt_device *d, rt_value_t msg);
static void read_on(rt_proc_t *p, rt_value_t pid);

// To a process that has ended, a message just disappears, as in Erlang. A
// device takes it at once.
rt_value_t rt_send(rt_value_t pid, rt_value_t msg, const char *site) {
    entry_t *e = entry(pid, site);
    if (e->proc)        deliver(e->proc, new_msg(msg));
    else if (e->device) command(e->device, msg);
    return RT_NIL;
}

// A device's message is cut only now, when its owner waits again after
// taking the last one, so whatever the owner did with that one, (:read
// how) or disconnect, applies to the next (D158).
rt_value_t rt_recv(rt_value_t *args, uint64_t n, rt_code_t code) {
    rt_proc_t *p = rt_current;
    if (p->reading) {
        rt_value_t device = p->reading;
        p->reading = 0;
        read_on(p, device);                     // which may end p, if reading fails
    }
    if (p->heap_bytes - (p->heap_limit - p->heap_ptr) >= p->gc_at) collect(p, args, n);
    rt_msg_t  *m = p->mail;
    if (m) {
        p->mail = m->next;
        if (!p->mail) p->mail_last = NULL;
        if (m->chunk) adopt(p, m->chunk);
        rt_value_t v = m->value;
        p->reading = m->device;
        free(m);
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

static rt_value_t cons_at(rt_value_t *cell, rt_value_t car, rt_value_t cdr) {
    cell[0] = car;
    cell[1] = cdr;
    return (rt_value_t)cell | RT_TAG_LIST;
}

// The result in e, (:ok value) or (:error reason), in cells (4 words).
static rt_value_t result_at(rt_value_t *cells, const entry_t *e) {
    return cons_at(cells, rt_symbol(e->ok ? RT_SYM_OK : RT_SYM_ERROR), cons_at(cells + 2, e->value, RT_NIL));
}

// Sends (:exit pid result) to `to`, about the process that ended at e.
static void notify(rt_value_t to, rt_value_t pid, const entry_t *e) {
    rt_proc_t *w = procs[to >> RT_PID_SHIFT].proc;
    if (!w) return;
    _Alignas(16) rt_value_t cells[10];
    rt_value_t result = result_at(cells, e);
    deliver(w, new_msg(cons_at(cells + 4, rt_symbol(RT_SYM_EXIT), cons_at(cells + 6, pid, cons_at(cells + 8, result, RT_NIL)))));
}

// p ends. Its result goes into its exit record, its joiners wake, its
// monitors hear, and its heap and mail are freed. Its stack and struct are
// the caller's to free (retire), since p may be running on that stack.
static void disconnect(rt_proc_t *p);
static void close_all(rt_proc_t *p);

static void finish(rt_proc_t *p, int ok, rt_value_t value, int logged) {
    uint64_t id = p->pid >> RT_PID_SHIFT;
    entry_t *e  = &procs[id];
    e->ok    = ok;
    e->chunk = copy_values(&value, &e->value, 1);
    if (id == 1 && !ok && !logged) {
        rt_buf_t b = { 0 };
        rt_render(&b, e->value, 0);
        fflush(stdout);
        fprintf(stderr, "error: %.*s\n", (int)b.len, b.bytes);
        rt_buf_free(&b);
    }
    for (rt_proc_t *j = p->joiners, *next; j; j = next) {
        next = j->next_joiner;
        enqueue(j);
    }
    for (rt_watch_t *w = p->watchers, *next; w; w = next) {
        next = w->next;
        notify(w->pid, p->pid, e);
        free(w);
    }
    free_heap(p);
    if (p->keypress) disconnect(p);
    close_all(p);
    p->state = RT_DONE;
}

static void retire(rt_proc_t *p) {
    if (p->stack) release_stack(p);
    procs[p->pid >> RT_PID_SHIFT].proc = NULL;
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
    rt_proc_t *t = entry(pid, site)->proc;
    if (pid == p->pid) rt_fault(RT_FAULT_JOIN_SELF, pid, site);
    if (t) {
        rt_proc_t **link = &t->joiners;
        while (*link) link = &(*link)->next_joiner;
        *link          = p;
        p->next_joiner = NULL;
        p->joining     = pid;
        p->state       = RT_JOINING;
        to_scheduler(p);                        // until t ends, and finish wakes us
    }
    _Alignas(16) rt_value_t cells[4];
    return copy_here(result_at(cells, &procs[pid >> RT_PID_SHIFT]));
}

rt_value_t rt_monitor(rt_value_t pid, const char *site) {
    entry_t *e = entry(pid, site);
    if (!e->proc) {
        notify(rt_current->pid, pid, e);
        return RT_NIL;
    }
    rt_watch_t *w = malloc(sizeof *w);
    if (!w) {
        perror("rt: out of memory");
        exit(2);
    }
    w->next = NULL;
    w->pid  = rt_current->pid;
    rt_watch_t **link = &e->proc->watchers;
    while (*link) link = &(*link)->next;
    *link = w;
    return RT_NIL;
}

// Ends p, which isn't running, with (:error reason), wherever it is.
static void end_other(rt_proc_t *p, rt_value_t reason) {
    if (p->state == RT_READY) unqueue(p);
    if (p->state == RT_JOINING) {
        rt_proc_t  *t    = procs[p->joining >> RT_PID_SHIFT].proc;
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

void rt_preempt(rt_proc_t *p) {
    p->reductions = QUOTA;
    events();                                   // so a busy process can't hold up a timer or a key
    if (ready_head) {
        enqueue(p);
        to_scheduler(p);
    }
}

void rt_yield(void) {
    rt_proc_t *p = rt_current;
    p->reductions = QUOTA;
    enqueue(p);
    to_scheduler(p);
}

// --- timers -------------------------------------------------------------------
//
// after and sleep set timers, kept in a binary heap in the order they're
// due, and then in the order they were set. after's timer holds its
// message, copied when it was set, as send's would be; sleep's wakes the
// sleeper. A timer whose process has ended by the time it's due is dropped
// then, as a message to it would be (D92); until then it's something that
// can still happen, so the program waits for it.
//
// The clock is the system's monotonic one, in nanoseconds; or, with
// SLIGHT_CLOCK=virtual (t/run.sh sets it), one for tests that starts at 0
// and moves only when nothing can run, straight to the next timer. Timer
// tests are then exact, and take no real time.

typedef struct {                        // (timer_t is POSIX's)
    uint64_t    due, seq;
    rt_value_t  pid;
    rt_msg_t   *msg;                    // after's message, or NULL: sleep
} alarm_t;

static alarm_t  *alarms;                // alarms[0] is the next due
static uint64_t  nalarms, alarms_cap, alarms_set;
static int       virtual_clock;
static uint64_t  virtual_now;

static uint64_t now(void) {
    if (virtual_clock) return virtual_now;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}

// When a timer set now for ms milliseconds is due. A negative ms counts
// as 0; one too far off for the clock is due at its end.
static uint64_t due_in(rt_value_t ms, const char *site) {
    if (ms & RT_TAG_INT_MASK) rt_fault(RT_FAULT_NOT_INT, ms, site);
    int64_t  n = rt_int_value(ms);
    uint64_t t = now();
    if (n <= 0) return t;
    return (uint64_t)n > (UINT64_MAX - t) / 1000000 ? UINT64_MAX : t + (uint64_t)n * 1000000;
}

static int before(const alarm_t *a, const alarm_t *b) {
    return a->due < b->due || (a->due == b->due && a->seq < b->seq);
}

static void set_alarm(uint64_t due, rt_value_t pid, rt_msg_t *msg) {
    if (nalarms == alarms_cap) {
        alarms_cap = alarms_cap ? alarms_cap * 2 : 64;
        alarms     = realloc(alarms, alarms_cap * sizeof *alarms);
        if (!alarms) {
            perror("rt: out of memory");
            exit(2);
        }
    }
    alarm_t  a = { due, alarms_set++, pid, msg };
    uint64_t i = nalarms++;
    for (; i > 0 && before(&a, &alarms[(i - 1) / 2]); i = (i - 1) / 2) alarms[i] = alarms[(i - 1) / 2];
    alarms[i] = a;
}

// Takes alarms[i] out, the last one going in its place, up or down.
static void remove_alarm(uint64_t i) {
    alarm_t last = alarms[--nalarms];
    if (i == nalarms) return;
    for (; i > 0 && before(&last, &alarms[(i - 1) / 2]); i = (i - 1) / 2) alarms[i] = alarms[(i - 1) / 2];
    for (uint64_t c; (c = 2 * i + 1) < nalarms; i = c) {
        if (c + 1 < nalarms && before(&alarms[c + 1], &alarms[c])) c++;
        if (!before(&alarms[c], &last)) break;
        alarms[i] = alarms[c];
    }
    alarms[i] = last;
}

static alarm_t next_alarm(void) {
    alarm_t first = alarms[0];
    remove_alarm(0);
    return first;
}

// A timer a device sets itself (a lookup's), unset when it's no longer
// wanted, since a pending timer keeps the program running: its seq + 1.
static uint64_t own_alarm(uint64_t due, rt_value_t pid) {
    set_alarm(due, pid, NULL);
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
        rt_proc_t *p = procs[a.pid >> RT_PID_SHIFT].proc;
        if (!p) {
            struct rt_device *d = procs[a.pid >> RT_PID_SHIFT].device;
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
    entry(pid, site);
    set_alarm(due, pid, new_msg(msg));
    return RT_NIL;
}

rt_value_t rt_sleep(rt_value_t ms, const char *site) {
    rt_proc_t *p = rt_current;
    set_alarm(due_in(ms, site), p->pid, NULL);
    p->state = RT_SLEEPING;
    to_scheduler(p);                            // until ring wakes it
    return RT_NIL;
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

static rt_proc_t     **connected;
static uint64_t        nconnected, connected_cap;
static unsigned char   input[4096];     // read from stdin...
static size_t          input_at, input_len; // ...and decoded up to input_at
static int             input_ended;
static uint64_t        input_seen;      // when stdin was last looked at while busy

rt_value_t rt_connect(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) {
    rt_value_t pid = rt_fork(code, n, values, site);
    if (nconnected == connected_cap) {
        connected_cap = connected_cap ? connected_cap * 2 : 8;
        connected     = realloc(connected, connected_cap * sizeof *connected);
        if (!connected) {
            perror("rt: out of memory");
            exit(2);
        }
    }
    rt_proc_t *p = procs[pid >> RT_PID_SHIFT].proc;
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

static uint64_t nsockets;
static void     socket_fds(fd_set *in, fd_set *out, int *max);
static int      socket_events(fd_set *in, fd_set *out);

#define IO_KEYS    1                    // what wait_for saw: stdin has something to read...
#define IO_SOCKETS 2                    // ...or a socket did something
#define SETTLE_US  200000               // how long the virtual clock waits for a lookup's answer, for real

// Waits up to us microseconds (forever, if negative) for stdin to have
// something to read, if anyone's listening, or for a socket to be ready;
// handles the sockets that are, and says what it saw.
static int wait_for(int64_t us) {
    int    keys = listening(), max = keys ? STDIN_FILENO : -1;
    fd_set in, out;
    FD_ZERO(&in);
    FD_ZERO(&out);
    if (keys) FD_SET(STDIN_FILENO, &in);
    socket_fds(&in, &out, &max);
    struct timeval tv, *timeout = NULL;
    if (us >= 0) {
        if (us > 3600000000) us = 3600000000;  // macOS's select refuses more than 10^8 s
        tv      = (struct timeval){ .tv_sec = (time_t)(us / 1000000), .tv_usec = (suseconds_t)(us % 1000000) };
        timeout = &tv;
    }
    if (select(max + 1, &in, &out, NULL, timeout) <= 0) return 0;
    int saw = keys && FD_ISSET(STDIN_FILENO, &in) ? IO_KEYS : 0;
    return saw | (socket_events(&in, &out) ? IO_SOCKETS : 0);
}

// Reads what stdin has, once everything read before has been decoded.
static void read_input(void) {
    if (input_at < input_len) return;
    ssize_t got = read(STDIN_FILENO, input, sizeof input);
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

// Nothing can run: waits for a key, a socket or the next timer. Returns 0
// if none of them can come. On the virtual clock, the sockets come first,
// then a key, then the clock moves; only when nothing else can happen does
// it wait for real.
static int idle(void) {
    if (!listening() && !nalarms && !nsockets) return 0;
    if (virtual_clock) {
        int saw = wait_for(0);
        if ((saw & IO_KEYS) && input_at == input_len) read_input();
        if (saw & IO_SOCKETS) return 1;
        if (listening() && next_key()) return 1;
        // A lookup's timer is the runtime's, not the program's, so it
        // mustn't beat an answer on its way: from a name server in the same
        // program, say, which on macOS's loopback arrives a moment after
        // it's sent, not by the time select() looks. So before the clock
        // moves to one, the sockets are waited for, for real, a little.
        if (nalarms && !alarms[0].msg && procs[alarms[0].pid >> RT_PID_SHIFT].device && (saw = wait_for(SETTLE_US))) {
            if ((saw & IO_KEYS) && input_at == input_len) read_input();
            return 1;
        }
        if (nalarms) {
            virtual_now = alarms[0].due;
            return 1;
        }
        // Only the world outside can do anything now: wait for it, unless
        // stdin has just ended, and with it the last thing that could come.
        if ((listening() || nsockets) && (wait_for(-1) & IO_KEYS)) read_input();
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

// --- devices: files and sockets -----------------------------------------------
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
// A socket (connect :tcp "host:port" expr, connect :tcp/listen port expr)
// is waited for in select(), with stdin and the next timer. Its host is
// looked up first (dns.c): a name the hosts file doesn't have is asked of a
// name server, over a socket of the lookup's own, waited for in select()
// too. A connection sends (:open c) once it's connected, then reads as a
// file does (but only while its owner waits for what it has, so a fast
// sender is held back by TCP), and (:eof c) when the other end closes; it
// can still be written to then. It takes
// (:write x ...) as a file does, but keeps what the socket can't take yet,
// and writes it when it can; closing it waits for that, and until then it
// keeps the program running. A listener sends (:open l port), port being
// the one it got (0 lets the system pick), then (:accept l conn) for each
// connection; conn reads nothing until (connect conn expr) hands it to a
// process. Open sockets count as something that can still happen. IPv4
// only, for now.

#define DEV_TCP      3                  // a device's mode, after RT_FS_READ, _WRITE and _APPEND
#define DEV_LISTEN   4
#define ERR_NOTFOUND (-1)               // no such host, which has no errno
#define ERR_TOOBIG   (-2)               // a line, count or value of HEAP_MAX or more
#define ERR_BADJSON  (-3)
#define ERR_BADSEXP  (-4)
#define READ_CHUNK   (64 << 10)         // the most a chunk holds

typedef struct rt_device {
    struct rt_device *next;             // the owner's next device
    rt_value_t        pid, owner;
    int               fd, mode;
    const char       *site;             // the connect's, for dead letters
    char             *path;             // a file's path, a connection's "host:port"; NUL-terminated
    size_t            path_len;
    int64_t           port;             // a listener's, or the port a connection is to be made to
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
    char             *out;              // written to it, and not yet taken by the socket...
    size_t            out_at, out_len, out_cap; // ...from out_at to out_len
} rt_device_t;

static rt_device_t *sockets;            // open sockets, and closing ones (nsockets counts them)
static rt_device_t *graveyard;          // closed sockets

static uint64_t errno_name(int err) {
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
        default:            return RT_ERR_OTHER;
    }
}

// (name path), for errno err, in cells (4 words).
static rt_value_t io_reason(rt_value_t *cells, int err, rt_value_t path) {
    return cons_at(cells, rt_symbol(RT_SYM_ERRS + errno_name(err)), cons_at(cells + 2, path, RT_NIL));
}

// A string box of len bytes, in space with room for it (len / 8 + 6
// words): for a value that's copied before space goes.
static rt_value_t string_in(uint64_t *space, const char *bytes, size_t len) {
    uint64_t *box = (uint64_t *)(((uintptr_t)space + 15) & ~(uintptr_t)15);
    box[0] = (uint64_t)len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
    if (len) memcpy(box + 1, bytes, len);
    ((char *)(box + 1))[len] = '\0';
    return (rt_value_t)box | RT_TAG_BOXED;
}

static rt_device_t *new_device(rt_proc_t *p, int mode, int fd, const char *site, const char *path, size_t len) {
    rt_device_t *d = must(calloc(1, sizeof *d));
    d->pid   = new_entry((entry_t){ .value = RT_NIL, .ok = 1, .device = d });
    d->owner = p->pid;
    d->fd    = fd;
    d->mode  = mode;
    d->site  = site;
    d->how   = RT_DEV_LINES;
    d->count = -1;
    d->line  = 1;
    if (path) {
        d->path     = must(malloc(len + 1));
        d->path_len = len;
        memcpy(d->path, path, len);
        d->path[len] = '\0';
    }
    d->next    = p->devices;
    p->devices = d;
    if (mode >= DEV_TCP) {
        d->next_socket = sockets;
        sockets        = d;
        nsockets++;
    }
    return d;
}

// Closes d's file descriptor. A socket is freed later (bury), since an
// event in hand may still refer to it.
static void close_now(rt_device_t *d) {
    if (d->fd >= 0) close(d->fd);
    if (d->lookup) {
        unset_alarm(&d->alarm);
        rt_lookup_free(d->lookup);
        d->lookup = NULL;
    }
    if (d->mode < DEV_TCP) {
        rt_json_free(&d->json);
        rt_sexp_free(&d->sexp);
        free(d->buf);
        free(d->path);
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
        free(d->path);
        free(d);
    }
}

// Closes d, and forgets it: its pid is then like a process that has
// ended. A socket with something still to write lingers till it's
// written. The owner's list is the caller's to fix.
static void shut(rt_device_t *d) {
    procs[d->pid >> RT_PID_SHIFT].device = NULL;
    if (d->mode == DEV_TCP && d->out_len > d->out_at && !d->lookup) d->closing = 1;
    else                                                            close_now(d);   // (a host still looked up never connected)
}

static void close_all(rt_proc_t *p) {
    for (rt_device_t *d = p->devices, *next; d; d = next) {
        next = d->next;
        shut(d);
    }
    p->devices = NULL;
}

static void unown(rt_device_t *d) {
    rt_device_t **link = &procs[d->owner >> RT_PID_SHIFT].proc->devices;
    while (*link != d) link = &(*link)->next;
    *link = d->next;
}

static void close_device(rt_device_t *d) {
    unown(d);
    shut(d);
}

// d's owner ends with (:error (name path)), which closes d.
static void fail(rt_device_t *d, int err) {
    rt_proc_t *owner = procs[d->owner >> RT_PID_SHIFT].proc;
    uint64_t   space[d->path_len / 8 + 6];
    _Alignas(16) rt_value_t cells[4];
    rt_value_t path   = d->path ? string_in(space, d->path, d->path_len) : rt_int(d->port);
    rt_value_t reason = io_reason(cells, err, path);
    if (owner == rt_current) rt_end(0, reason, 0);
    end_other(owner, reason);
}

// Sends d's owner (word f rest...); taking it reads on, if more says so.
static void tell_list(rt_device_t *d, uint64_t word, rt_value_t rest, int more) {
    _Alignas(16) rt_value_t cells[4];
    rt_msg_t *m = new_msg(cons_at(cells, rt_symbol(RT_SYM_DEVICE + word), cons_at(cells + 2, d->pid, rest)));
    m->device = more ? d->pid : 0;
    d->sent   = d->sent || more;
    deliver(procs[d->owner >> RT_PID_SHIFT].proc, m);
}

static void tell(rt_device_t *d, uint64_t word, int more) {
    tell_list(d, word, RT_NIL, more);
}

// (word f s): a line or a chunk.
static void tell_string(rt_device_t *d, uint64_t word, const char *bytes, size_t len) {
    uint64_t *space = must(malloc((len / 8 + 6) * sizeof *space));
    _Alignas(16) rt_value_t cell[2];
    tell_list(d, word, cons_at(cell, string_in(space, bytes, len), RT_NIL), 1);
    free(space);
}

// The device's first message: (:open f), or a listener's (:open l port).
static void tell_open(rt_device_t *d) {
    _Alignas(16) rt_value_t cell[2];
    if (d->mode == DEV_LISTEN) tell_list(d, RT_DEV_OPEN, cons_at(cell, rt_int(d->port), RT_NIL), 0);
    else                       tell(d, RT_DEV_OPEN, d->mode == RT_FS_READ || d->mode == DEV_TCP);
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
        d->buf = must(realloc(d->buf, cap));
    }
    ssize_t got;
    do got = read(d->fd, d->buf + d->len, d->cap - d->len); while (got < 0 && errno == EINTR);
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
    if (a->total + bytes >= HEAP_MAX) return NULL;
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
    uint64_t *space = must(malloc((len / 8 + 6) * sizeof *space));
    _Alignas(16) rt_value_t cells[4];
    tell_list(d, RT_DEV_DOC, cons_at(cells, string_in(space, text, len), cons_at(cells + 2, rt_int(line), RT_NIL)), 1);
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
            if (d->how == RT_DEV_SOURCE) tell_list(d, RT_DEV_FORM, cons_at(cells, v, cons_at(cells + 2, rt_int(line), RT_NIL)), 1);
            else                         tell_list(d, sexp ? RT_DEV_SEXP : RT_DEV_JSON, cons_at(cells, v, RT_NIL), 1);
        }
        free_chunks(a.chunks);
        if (built != 0) fail(d, ERR_TOOBIG);
        return 1;
    }
}

// Sends d's next message, if what's been read makes one: 1 if it did, or
// has nothing more to send (and d may be gone); 0 if it needs more bytes.
static int cut(rt_device_t *d) {
    size_t      have = d->len - d->at, n;
    const char *line;
    if (d->eof_sent) return 1;
    if (d->count >= HEAP_MAX) {
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
        tell(d, RT_DEV_EOF, 0);
        if (d->mode != DEV_TCP) close_device(d);
        return 1;
    }
    if (have >= HEAP_MAX) {                     // a line with no end in sight
        fail(d, ERR_TOOBIG);
        return 1;
    }
    return 0;
}

// p, waiting again, took reader pid's message last: on to its next. A
// file reads till it has one; a connection waits in select() for more.
static void read_on(rt_proc_t *p, rt_value_t pid) {
    rt_device_t *d = procs[pid >> RT_PID_SHIFT].device;
    if (!d || d->owner != p->pid) return;       // closed since, or handed over
    d->sent = 0;
    while (!cut(d) && d->mode != DEV_TCP) {
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
    // A connection waiting in select() may have enough for the new way.
    if (d->mode == DEV_TCP && !d->sent && !d->connecting && !d->waiting) cut(d);
    return 1;
}

// Writes what a socket has, as far as it takes it: 0, or -1 with errno
// set. (SIGPIPE is ignored, so a closed connection is EPIPE.)
static int flush(rt_device_t *d) {
    while (d->out_at < d->out_len) {
        ssize_t n = write(d->fd, d->out + d->out_at, d->out_len - d->out_at);
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
        d->out     = must(realloc(d->out, d->out_cap));
    }
    if (len) memcpy(d->out + d->out_len, bytes, len);
    d->out_len += len;
}

static void command(rt_device_t *d, rt_value_t msg) {
    rt_value_t word   = rt_is_cons(msg) ? rt_car(msg) : RT_NIL;
    int        reader = d->mode == RT_FS_READ || d->mode == DEV_TCP;
    if (reader && word == rt_symbol(RT_SYM_DEVICE + RT_DEV_READ) && read_as(d, rt_cdr(msg))) return;
    if (d->mode == RT_FS_READ || d->mode == DEV_LISTEN || word != rt_symbol(RT_SYM_DEVICE + RT_DEV_WRITE)) {
        rt_dead_letter(msg, d->site);
        return;
    }
    rt_buf_t b = { 0 };
    for (rt_value_t x = rt_cdr(msg); rt_is_cons(x); x = rt_cdr(x)) rt_render(&b, rt_car(x), 1);
    if (d->mode == DEV_TCP) {
        queue(d, b.bytes, b.len);
        rt_buf_free(&b);
        if (!d->connecting && flush(d) < 0) fail(d, errno);
        return;
    }
    for (size_t at = 0; at < b.len; ) {
        ssize_t n = write(d->fd, b.bytes + at, b.len - at);
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

rt_value_t rt_connect_fs(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                         rt_value_t path, uint64_t mode) {
    if (!rt_is_string(path)) rt_fault(RT_FAULT_NOT_STRING, path, site);
    rt_value_t  pid   = rt_fork(code, n, values, site);
    rt_proc_t  *p     = procs[pid >> RT_PID_SHIFT].proc;
    const char *bytes = rt_string_bytes(path);
    size_t      len   = rt_string_len(path);
    int         flags = mode == RT_FS_READ ? O_RDONLY : O_WRONLY | O_CREAT | (mode == RT_FS_APPEND ? O_APPEND : O_TRUNC);
    int         fd    = memchr(bytes, '\0', len) ? (errno = EINVAL, -1) : open(bytes, flags | O_CLOEXEC, 0666);
    if (fd < 0) {
        _Alignas(16) rt_value_t cells[4];
        end_other(p, io_reason(cells, errno, path));
        return pid;
    }
    tell_open(new_device(p, (int)mode, fd, site, bytes, len));
    return pid;
}

// A socket that doesn't block, isn't inherited, and fits in select():
// 0, or an errno.
static int setup(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (fd >= FD_SETSIZE) return EMFILE;
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return errno;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);  // messages are small; don't wait to fill a packet
    return 0;
}

static int tcp_listen(rt_proc_t *p, const char *site, int64_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return errno;
    int one = 1, err;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { 0 };
    a.sin_family      = AF_INET;
    a.sin_port        = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t alen    = sizeof a;
    if ((err = setup(fd)) != 0
        || (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 && (err = errno))
        || (listen(fd, 128) < 0 && (err = errno))
        || (getsockname(fd, (struct sockaddr *)&a, &alen) < 0 && (err = errno))) {
        close(fd);
        return err;
    }
    rt_device_t *d = new_device(p, DEV_LISTEN, fd, site, NULL, 0);
    d->port = ntohs(a.sin_port);
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
    int s = socket(AF_INET, SOCK_STREAM, 0), err = s < 0 ? errno : setup(s);
    if (!err && connect(s, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) err = errno;
    if (err && s >= 0) close(s);
    *fd = err ? -1 : s;
    return err;
}

static void lookup_step(rt_device_t *d, int r, uint32_t addr);

// Starts connecting to "host:port" (split at the last colon; the port is a
// number). Even if it has connected already, (:open c) comes from the
// event loop, so it always comes in the same order; and a host that has to
// be asked about is looked up first, the device waiting for that.
static int tcp_connect(rt_proc_t *p, const char *site, const char *where, size_t len) {
    size_t colon = len;
    while (colon > 0 && where[colon - 1] != ':') colon--;
    if (colon == 0 || colon == len || memchr(where, '\0', len)) return ERR_NOTFOUND;
    int64_t port = 0;
    for (size_t i = colon; i < len && port <= 65535; i++) port = where[i] >= '0' && where[i] <= '9' ? port * 10 + where[i] - '0' : 65536;
    if (port > 65535) return ERR_NOTFOUND;
    char host[len + 1];
    memcpy(host, where, colon - 1);
    host[colon - 1] = '\0';
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

rt_value_t rt_connect_tcp(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                          rt_value_t where, uint64_t mode) {
    if (mode == RT_TCP_LISTEN) {
        if (where & RT_TAG_INT_MASK) rt_fault(RT_FAULT_NOT_INT, where, site);
        if (rt_int_value(where) < 0 || rt_int_value(where) > 65535) rt_fault(RT_FAULT_RANGE, where, site);
    } else if (!rt_is_string(where)) {
        rt_fault(RT_FAULT_NOT_STRING, where, site);
    }
    rt_value_t pid = rt_fork(code, n, values, site);
    rt_proc_t *p   = procs[pid >> RT_PID_SHIFT].proc;
    int        err = mode == RT_TCP_LISTEN ? tcp_listen(p, site, rt_int_value(where))
                                           : tcp_connect(p, site, rt_string_bytes(where), rt_string_len(where));
    if (err) {
        _Alignas(16) rt_value_t cells[4];
        end_other(p, io_reason(cells, err, where));
    }
    return pid;
}

rt_value_t rt_connect_device(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                             rt_value_t dev) {
    rt_device_t *d = entry(dev, site)->device;
    if (!d) rt_fault(RT_FAULT_NOT_DEVICE, dev, site);
    rt_value_t pid = rt_fork(code, n, values, site);
    rt_proc_t *p   = procs[pid >> RT_PID_SHIFT].proc;
    unown(d);
    d->owner   = pid;
    d->next    = p->devices;
    p->devices = d;
    d->waiting = 0;
    if (!d->connecting) tell_open(d);           // a connection that's still connecting says so when it has
    return pid;
}

rt_value_t rt_disconnect(rt_value_t pid, const char *site) {
    rt_device_t *d = entry(pid, site)->device;
    if (d) close_device(d);
    return RT_NIL;
}

// A listener has connections to accept: each becomes a device of the
// listener's owner, which hears (:accept l conn).
static void accept_all(rt_device_t *l) {
    rt_proc_t *owner = procs[l->owner >> RT_PID_SHIFT].proc;
    for (;;) {
        struct sockaddr_in a;
        socklen_t          alen = sizeof a;
        int                fd   = accept(l->fd, (struct sockaddr *)&a, &alen);
        if (fd < 0 && errno == EINTR) continue;
        if (fd < 0) return;                     // none left, or none to be had for now
        if (setup(fd) != 0) {
            close(fd);
            continue;
        }
        char where[INET_ADDRSTRLEN + 8];
        inet_ntop(AF_INET, &a.sin_addr, where, INET_ADDRSTRLEN);
        snprintf(where + strlen(where), 8, ":%d", ntohs(a.sin_port));
        rt_device_t *c = new_device(owner, DEV_TCP, fd, l->site, where, strlen(where));
        c->waiting = 1;
        _Alignas(16) rt_value_t cell[2];
        tell_list(l, RT_DEV_ACCEPT, cons_at(cell, c->pid, RT_NIL), 0);
    }
}

// Adds to the sets the sockets that are waiting to read or write, and
// raises *max to their highest file descriptor.
static void socket_fds(fd_set *in, fd_set *out, int *max) {
    bury();
    for (rt_device_t *d = sockets; d; d = d->next_socket) {
        if (d->lookup) {
            int write, fd = rt_lookup_fd(d->lookup, &write);
            FD_SET(fd, write ? out : in);
            if (fd > *max) *max = fd;
            continue;
        }
        int r = d->mode == DEV_LISTEN || (!d->connecting && !d->waiting && !d->sent && !d->eof && !d->closing);
        int w = d->connecting || d->out_len > d->out_at;
        if (r) FD_SET(d->fd, in);
        if (w) FD_SET(d->fd, out);
        if ((r || w) && d->fd > *max) *max = d->fd;
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
    if (d->connecting && w) {
        int       err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(d->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = errno;
        if (err) {
            if (d->closing) close_now(d);
            else            fail(d, err);
            return;
        }
        d->connecting = 0;
        if (!d->closing) tell_open(d);
    }
    if (w && !d->connecting && d->out_len > d->out_at) {
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

// Handles the sockets select() says are ready, in the order of their
// pids; returns whether there were any.
static int socket_events(fd_set *in, fd_set *out) {
    ready_t *ready = must(malloc((nsockets + 1) * sizeof *ready));
    uint64_t n     = 0;
    for (rt_device_t *d = sockets; d; d = d->next_socket) {
        int write, fd = d->lookup ? rt_lookup_fd(d->lookup, &write) : d->fd;
        int r = FD_ISSET(fd, in), w = FD_ISSET(fd, out);
        if (r || w) ready[n++] = (ready_t){ d, r, w };
    }
    qsort(ready, n, sizeof *ready, by_pid);
    for (uint64_t i = 0; i < n; i++) {
        if (!ready[i].d->dead) socket_event(ready[i].d, ready[i].r, ready[i].w);
    }
    free(ready);
    bury();
    return n > 0;
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

int main(int argc, char **argv) {
    page   = (size_t)sysconf(_SC_PAGESIZE);
    signal(SIGPIPE, SIG_IGN);                   // writing to a closed connection is EPIPE, not the end
    poison = getenv("SLIGHT_POISON") != NULL;
    const char *mode = getenv("SLIGHT_CLOCK");
    virtual_clock = mode && strcmp(mode, "virtual") == 0;
    rt_proc_t *p = rt_new_process((rt_code_t)slight_main, RT_NIL);
    p->args[0]   = arguments(p, argc, argv);
    rt_run();
    const entry_t *root = &procs[1];
    if (root->proc) {
        fflush(stdout);
        if (root->proc->state == RT_JOINING) {
            fprintf(stderr, "deadlock: the root process is waiting for #<pid %" PRIu64 "> to end, and nothing else can run\n",
                    root->proc->joining >> RT_PID_SHIFT);
        } else {
            fputs("deadlock: the root process is waiting for a message, and nothing else can run\n", stderr);
        }
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
