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
// A file opened with connect :fs/... is a device: a pid that the runtime
// serves instead of compiled code (see "files", below).

#include "rt.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
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
// with x28 = p, which calls p->code with p->args.
static void start(rt_proc_t *p) {
    p->stack       = get_stack();
    p->stack_limit = (uintptr_t)p->stack + STACK_HEADROOM;
    p->reductions  = QUOTA;
    memset(&p->ctx, 0, sizeof p->ctx);
    p->ctx.sp         = (uint64_t)p->stack + STACK_BYTES;
    p->ctx.lr         = (uint64_t)rt_trampoline;
    p->ctx.x19_x28[9] = (uint64_t)p;
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

static size_t copy_size(rt_value_t v) {
    size_t n = 0;
    for (; is_pointer(v) && rt_is_cons(v); v = rt_cdr(v)) n += 16 + copy_size(rt_car(v));
    if (!is_pointer(v)) return n;
    n += box_bytes(v);
    if (rt_is_closure(v)) {
        for (uint64_t i = 0; i < rt_box(v)[0] >> RT_BOX_SIZE_SHIFT; i++) n += copy_size(rt_box(v)[4 + i]);
    }
    return n;
}

// Copies v to *to, moving *to past it.
static rt_value_t copy(rt_value_t v, char **to) {
    if (!is_pointer(v)) return v;
    if (rt_is_cons(v)) {
        rt_value_t  head;
        rt_value_t *link = &head;
        for (; is_pointer(v) && rt_is_cons(v); v = rt_cdr(v)) {
            rt_value_t *cell = (rt_value_t *)*to;
            *to    += 16;
            *link   = (rt_value_t)cell | RT_TAG_LIST;
            cell[0] = copy(rt_car(v), to);
            link    = &cell[1];
        }
        *link = copy(v, to);                    // (), or a static tail
        return head;
    }
    size_t    bytes = box_bytes(v);
    uint64_t *box   = (uint64_t *)*to;
    *to += bytes;
    memcpy(box, rt_box(v), bytes);
    if (rt_is_closure(v)) {
        for (uint64_t i = 0; i < box[0] >> RT_BOX_SIZE_SHIFT; i++) box[4 + i] = copy(box[4 + i], to);
    }
    return (rt_value_t)box | RT_TAG_BOXED;
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
    rt_value_t *todo;                   // copied, with fields still to forward
    size_t      ntodo, todo_cap;
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

static void gc_todo(gc_t *g, rt_value_t v) {
    if (g->ntodo == g->todo_cap) {
        g->todo_cap = g->todo_cap ? g->todo_cap * 2 : 256;
        g->todo     = realloc(g->todo, g->todo_cap * sizeof *g->todo);
        if (!g->todo) {
            perror("rt: out of memory");
            exit(2);
        }
    }
    g->todo[g->ntodo++] = v;
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
        gc_todo(g, old[1]);
        return old[1];
    }
    uint64_t *old = (uint64_t *)(v - RT_TAG_BOXED);
    if (old[0] == GC_MOVED_BOX) return old[1];
    size_t    bytes = box_bytes(v);
    uint64_t *new   = gc_alloc(g, bytes);
    memcpy(new, old, bytes);
    old[0] = GC_MOVED_BOX;
    old[1] = (rt_value_t)new | RT_TAG_BOXED;
    if ((new[0] & RT_BOX_TYPE_MASK) == RT_BOX_CLOSURE) gc_todo(g, old[1]);
    return old[1];
}

static void collect(rt_proc_t *p, rt_value_t *roots, uint64_t n) {
    gc_t g = { .size = p->heap_bytes < CHUNK_MAX ? p->heap_bytes : CHUNK_MAX };
    for (uint64_t i = 0; i < n; i++) roots[i] = forward(&g, roots[i]);
    while (g.ntodo) {
        rt_value_t v = g.todo[--g.ntodo];
        if (rt_is_cons(v)) {
            rt_value_t *cell = (rt_value_t *)(v - RT_TAG_LIST);
            cell[1] = forward(&g, cell[1]);
            cell[0] = forward(&g, cell[0]);
        } else {
            uint64_t *box = (uint64_t *)(v - RT_TAG_BOXED);
            for (uint64_t i = 0; i < box[0] >> RT_BOX_SIZE_SHIFT; i++) box[4 + i] = forward(&g, box[4 + i]);
        }
    }
    free(g.todo);
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
static void read_on(rt_value_t pid);

// To a process that has ended, a message just disappears, as in Erlang. A
// device takes it at once.
rt_value_t rt_send(rt_value_t pid, rt_value_t msg, const char *site) {
    entry_t *e = entry(pid, site);
    if (e->proc)        deliver(e->proc, new_msg(msg));
    else if (e->device) command(e->device, msg);
    return RT_NIL;
}

rt_value_t rt_recv(rt_value_t *args, uint64_t n, rt_code_t code) {
    rt_proc_t *p = rt_current;
    if (p->heap_bytes - (p->heap_limit - p->heap_ptr) >= p->gc_at) collect(p, args, n);
    rt_msg_t  *m = p->mail;
    if (m) {
        p->mail = m->next;
        if (!p->mail) p->mail_last = NULL;
        if (m->chunk) adopt(p, m->chunk);
        rt_value_t v = m->value, device = m->device;
        free(m);
        if (device) read_on(device);            // which may end p, if reading fails
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

static alarm_t next_alarm(void) {
    alarm_t  first = alarms[0], last = alarms[--nalarms];
    uint64_t i     = 0;
    for (uint64_t c; (c = 2 * i + 1) < nalarms; i = c) {
        if (c + 1 < nalarms && before(&alarms[c + 1], &alarms[c])) c++;
        if (!before(&alarms[c], &last)) break;
        alarms[i] = alarms[c];
    }
    alarms[i] = last;
    return first;
}

// Fires every timer that's due.
static void ring(void) {
    for (uint64_t t = nalarms ? now() : 0; nalarms && alarms[0].due <= t; ) {
        alarm_t    a = next_alarm();
        rt_proc_t *p = procs[a.pid >> RT_PID_SHIFT].proc;
        if (!p) {
            struct rt_device *d = procs[a.pid >> RT_PID_SHIFT].device;
            if (d && a.msg) command(d, a.msg->value);
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

// Waits up to us microseconds (forever, if negative) for stdin to have
// something to read, if anyone's listening; returns whether it has.
static int wait_for(int64_t us) {
    int    keys = listening();
    fd_set in;
    FD_ZERO(&in);
    if (keys) FD_SET(STDIN_FILENO, &in);
    struct timeval tv, *timeout = NULL;
    if (us >= 0) {
        if (us > 3600000000) us = 3600000000;  // macOS's select refuses more than 10^8 s
        tv      = (struct timeval){ .tv_sec = (time_t)(us / 1000000), .tv_usec = (suseconds_t)(us % 1000000) };
        timeout = &tv;
    }
    return select(keys ? STDIN_FILENO + 1 : 0, &in, NULL, NULL, timeout) > 0 && keys && FD_ISSET(STDIN_FILENO, &in);
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
// the keys typed since.
static void events(void) {
    ring();
    if (virtual_clock || !listening()) return;
    uint64_t t = now();
    if (t - input_seen < INPUT_EVERY) return;
    input_seen = t;
    if (wait_for(0)) {
        read_input();
        while (next_key()) {}
    }
}

// Nothing can run: waits for a key or the next timer. Returns 0 if
// neither can come.
static int idle(void) {
    if (!listening() && !nalarms) return 0;
    if (virtual_clock) {
        if (listening() && input_at == input_len && wait_for(0)) read_input();
        if (listening() && next_key()) return 1;
        if (nalarms) {
            virtual_now = alarms[0].due;
            return 1;
        }
        if (listening() && wait_for(-1)) read_input();  // only a key can come: wait for one, for real
        return 1;
    }
    uint64_t t = now();
    if (nalarms && alarms[0].due <= t) return 1;
    if (wait_for(nalarms ? (int64_t)((alarms[0].due - t + 999) / 1000) : -1)) {
        read_input();
        while (next_key()) {}
    }
    return 1;
}

// --- files --------------------------------------------------------------------
//
// (connect :fs/read path expr), and :fs/write and :fs/append: a fork whose
// new process owns a device, a pid that the runtime serves instead of
// compiled code. The device's first message to its owner is (:open f), f
// being its pid. A reader's then come one line at a time, (:line f s),
// without the newline; the next is read only when the owner has taken the
// last, so a long file never piles up in a mailbox; then (:eof f), and it
// closes. A writer takes (:write x ...) from anyone, renders the xs as
// tty/write does, and writes them at once; anything else sent to a device
// is a dead letter. disconnect closes a device, and so does its owner
// ending. If the file can't be opened, the owner ends before it runs,
// with (:error (name path)), name being errno's; if a read or a write
// fails, it ends the same way then.
//
// A file is read when the owner takes a line, so a reader is never waited
// for, and never counts as something that can still happen. Reading a
// file that can keep it waiting (a FIFO, a terminal) waits with the whole
// runtime.

typedef struct rt_device {
    struct rt_device *next;             // the owner's next device
    rt_value_t        pid, owner;
    int               fd, mode;
    const char       *site;             // the connect's, for dead letters
    char             *path;             // as given, NUL-terminated, for errors
    size_t            path_len;
    char             *buf;              // a reader's bytes, read but not yet sent...
    size_t            at, len, cap;     // ...from at to len
    int               eof;
} rt_device_t;

static uint64_t errno_name(int err) {
    switch (err) {
        case ENOENT:       return RT_ERR_ENOENT;
        case EACCES:       return RT_ERR_EACCES;
        case EPERM:        return RT_ERR_EPERM;
        case EEXIST:       return RT_ERR_EEXIST;
        case EISDIR:       return RT_ERR_EISDIR;
        case ENOTDIR:      return RT_ERR_ENOTDIR;
        case ENAMETOOLONG: return RT_ERR_ENAMETOOLONG;
        case ELOOP:        return RT_ERR_ELOOP;
        case EROFS:        return RT_ERR_EROFS;
        case ENOSPC:       return RT_ERR_ENOSPC;
        case EFBIG:        return RT_ERR_EFBIG;
        case EMFILE:       return RT_ERR_EMFILE;
        case ENFILE:       return RT_ERR_ENFILE;
        case EIO:          return RT_ERR_EIO;
        default:           return RT_ERR_OTHER;
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
    memcpy(box + 1, bytes, len);
    ((char *)(box + 1))[len] = '\0';
    return (rt_value_t)box | RT_TAG_BOXED;
}

// Closes d, and forgets it: its pid is then like a process that has
// ended. The owner's list is the caller's to fix.
static void shut(rt_device_t *d) {
    if (d->fd >= 0) close(d->fd);
    procs[d->pid >> RT_PID_SHIFT].device = NULL;
    free(d->buf);
    free(d->path);
    free(d);
}

static void close_all(rt_proc_t *p) {
    for (rt_device_t *d = p->devices, *next; d; d = next) {
        next = d->next;
        shut(d);
    }
    p->devices = NULL;
}

static void close_device(rt_device_t *d) {
    rt_device_t **link = &procs[d->owner >> RT_PID_SHIFT].proc->devices;
    while (*link != d) link = &(*link)->next;
    *link = d->next;
    shut(d);
}

// d's owner ends with (:error (name path)), which closes d.
static void fail(rt_device_t *d, int err) {
    rt_proc_t *owner = procs[d->owner >> RT_PID_SHIFT].proc;
    uint64_t   space[d->path_len / 8 + 6];
    _Alignas(16) rt_value_t cells[4];
    rt_value_t reason = io_reason(cells, err, string_in(space, d->path, d->path_len));
    if (owner == rt_current) rt_end(0, reason, 0);
    end_other(owner, reason);
}

// Sends d's owner (word f), or (word f s) if bytes isn't NULL; taking it
// reads on, if more says so.
static void tell(rt_device_t *d, uint64_t word, const char *bytes, size_t len, int more) {
    uint64_t  *space = bytes ? must(malloc((len / 8 + 6) * sizeof *space)) : NULL;
    _Alignas(16) rt_value_t cells[6];
    rt_value_t rest = bytes ? cons_at(cells + 4, string_in(space, bytes, len), RT_NIL) : RT_NIL;
    rt_msg_t  *m    = new_msg(cons_at(cells, rt_symbol(RT_SYM_DEVICE + word), cons_at(cells + 2, d->pid, rest)));
    free(space);
    m->device = more ? d->pid : 0;
    deliver(procs[d->owner >> RT_PID_SHIFT].proc, m);
}

// The next line: 1, with it in *line and *len (good until the next call);
// 0 at the end of the file; or -1, with errno set.
static int next_line(rt_device_t *d, const char **line, size_t *len) {
    for (;;) {
        char *nl = d->len > d->at ? memchr(d->buf + d->at, '\n', d->len - d->at) : NULL;
        if (nl || (d->eof && d->at < d->len)) {
            size_t end = nl ? (size_t)(nl - d->buf) : d->len;
            *line = d->buf + d->at;
            *len  = end - d->at;
            d->at = nl ? end + 1 : end;
            return 1;
        }
        if (d->eof) return 0;
        memmove(d->buf, d->buf + d->at, d->len - d->at);
        d->len -= d->at;
        d->at   = 0;
        if (d->len == d->cap) {
            d->cap = d->cap ? d->cap * 2 : 4096;
            d->buf = must(realloc(d->buf, d->cap));
        }
        ssize_t got = read(d->fd, d->buf + d->len, d->cap - d->len);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) return -1;
        if (got == 0) d->eof = 1;
        d->len += (size_t)got;
    }
}

// The owner has taken a message from reader pid: on to the next line.
static void read_on(rt_value_t pid) {
    rt_device_t *d = procs[pid >> RT_PID_SHIFT].device;
    if (!d) return;                             // disconnected since
    const char *line;
    size_t      len;
    int         got = next_line(d, &line, &len);
    if (got > 0) {
        tell(d, RT_DEV_LINE, line, len, 1);
    } else if (got == 0) {
        tell(d, RT_DEV_EOF, NULL, 0, 0);
        close_device(d);
    } else {
        fail(d, errno);
    }
}

static void command(rt_device_t *d, rt_value_t msg) {
    if (d->mode == RT_FS_READ || !rt_is_cons(msg) || rt_car(msg) != rt_symbol(RT_SYM_DEVICE + RT_DEV_WRITE)) {
        rt_dead_letter(msg, d->site);
        return;
    }
    rt_buf_t b = { 0 };
    for (rt_value_t x = rt_cdr(msg); rt_is_cons(x); x = rt_cdr(x)) rt_render(&b, rt_car(x), 1);
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
    rt_device_t *d = must(calloc(1, sizeof *d));
    d->pid      = new_entry((entry_t){ .value = RT_NIL, .ok = 1, .device = d });
    d->owner    = pid;
    d->fd       = fd;
    d->mode     = (int)mode;
    d->site     = site;
    d->path     = must(malloc(len + 1));
    d->path_len = len;
    memcpy(d->path, bytes, len + 1);
    d->next     = p->devices;
    p->devices  = d;
    tell(d, RT_DEV_OPEN, NULL, 0, mode == RT_FS_READ);
    return pid;
}

rt_value_t rt_disconnect(rt_value_t pid, const char *site) {
    rt_device_t *d = entry(pid, site)->device;
    if (d) close_device(d);
    return RT_NIL;
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

int main(void) {
    page   = (size_t)sysconf(_SC_PAGESIZE);
    poison = getenv("SLIGHT_POISON") != NULL;
    const char *mode = getenv("SLIGHT_CLOCK");
    virtual_clock = mode && strcmp(mode, "virtual") == 0;
    rt_new_process((rt_code_t)slight_main, RT_NIL);
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
    return 0;
}
