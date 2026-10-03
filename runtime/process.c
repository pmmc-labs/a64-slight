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

#include "rt.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define QUOTA          1000             // reductions between preemptions
#define STACK_BYTES    (8 << 20)        // every process's stack, mapped lazily
#define STACK_HEADROOM (64 << 10)       // room left below the limit for one frame and a C call
#define CHUNK_FIRST    (4 << 10)        // a process's first heap chunk...
#define CHUNK_MAX      (1 << 20)        // ...doubling up to this
#define HEAP_MAX       (64 << 20)       // all of a process's chunks
#define GC_MIN         (256 << 10)      // a heap smaller than this is never collected

// A pid's entry in the table: the process while it runs, then its exit
// record: (:ok value) or (:error value).
typedef struct {
    rt_proc_t  *proc;                   // NULL once it has ended
    rt_value_t  value;
    rt_chunk_t *chunk;                  // where value lives, or NULL if it needs no space
    int         ok;
} entry_t;

static entry_t    *procs;               // by pid number; the root is 1
static uint64_t    nprocs, procs_cap;
static rt_proc_t  *ready_head, *ready_tail;
static rt_ctx_t    scheduler;
static void       *free_stacks;         // a list through each stack's first word
static size_t      page;
static int         poison;              // SLIGHT_POISON: fill what the collector frees

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

static void free_heap(rt_proc_t *p) {
    free_chunks(p->chunks);
    p->chunks     = NULL;
    p->heap_ptr   = p->heap_limit = 0;
    p->heap_bytes = 0;
    for (rt_msg_t *m = p->mail, *next; m; m = next) {
        next = m->next;
        free(m->chunk);
        free(m);
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

rt_proc_t *rt_new_process(rt_code_t code, rt_value_t parent) {
    if (nprocs + 1 >= procs_cap) {
        procs_cap = procs_cap ? procs_cap * 2 : 1024;
        procs     = realloc(procs, procs_cap * sizeof *procs);
        if (!procs) {
            perror("rt: out of memory");
            exit(2);
        }
    }
    rt_proc_t *p = calloc(1, sizeof *p);
    if (!p) {
        perror("rt: out of memory");
        exit(2);
    }
    uint64_t id = ++nprocs;                     // pids start at 1: the root
    procs[id]   = (entry_t){ .proc = p };
    p->pid      = id << RT_PID_SHIFT | RT_TAG_PID;
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

// A copy of msg into p's mailbox; p wakes if it's waiting for one.
static void deliver(rt_proc_t *p, rt_value_t msg) {
    rt_msg_t *m = malloc(sizeof *m);
    if (!m) {
        perror("rt: out of memory");
        exit(2);
    }
    m->next  = NULL;
    m->chunk = copy_values(&msg, &m->value, 1);
    if (p->mail_last) p->mail_last->next = m;
    else              p->mail = m;
    p->mail_last = m;
    if (p->state == RT_WAITING) enqueue(p);
}

// To a process that has ended, a message just disappears, as in Erlang.
rt_value_t rt_send(rt_value_t pid, rt_value_t msg, const char *site) {
    rt_proc_t *p = entry(pid, site)->proc;
    if (p) deliver(p, msg);
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
        rt_value_t v = m->value;
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
    deliver(w, cons_at(cells + 4, rt_symbol(RT_SYM_EXIT), cons_at(cells + 6, pid, cons_at(cells + 8, result, RT_NIL))));
}

// p ends. Its result goes into its exit record, its joiners wake, its
// monitors hear, and its heap and mail are freed. Its stack and struct are
// the caller's to free (retire), since p may be running on that stack.
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

rt_value_t rt_kill(rt_value_t pid, const char *site) {
    rt_proc_t *p = entry(pid, site)->proc;
    if (!p) return RT_NIL;
    if (p == rt_current) rt_end(0, rt_symbol(RT_SYM_KILLED), 0);
    if (p->state == RT_READY) unqueue(p);
    if (p->state == RT_JOINING) {
        rt_proc_t  *t    = procs[p->joining >> RT_PID_SHIFT].proc;
        rt_proc_t **link = &t->joiners;
        while (*link != p) link = &(*link)->next_joiner;
        *link = p->next_joiner;
    }
    finish(p, 0, rt_symbol(RT_SYM_KILLED), 0);
    retire(p);
    return RT_NIL;
}

void rt_preempt(rt_proc_t *p) {
    p->reductions = QUOTA;
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

// --- the scheduler ------------------------------------------------------------

void rt_run(void) {
    rt_proc_t *p;
    while ((p = dequeue()) != NULL) {
        if (!p->stack) start(p);
        p->state   = RT_RUNNING;
        rt_current = p;
        rt_switch(&scheduler, &p->ctx);
        // back: it paused (and queued itself again), waits in recv or
        // join, or ended
        if (p->state == RT_WAITING)   release_stack(p);
        else if (p->state == RT_DONE) retire(p);
    }
}

int main(void) {
    page   = (size_t)sysconf(_SC_PAGESIZE);
    poison = getenv("SLIGHT_POISON") != NULL;
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
