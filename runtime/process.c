// process.c -- processes: the table, the run queue, stacks, heaps,
// messages, and the scheduler.
//
// One core. The scheduler runs on the C stack and switches to a process
// with rt_switch; the process switches back when it pauses (preempted, or
// yield), waits in recv, or ends. A process holds a stack only while it
// needs one: waiting in recv, it's just (code, args, mailbox), and it gives
// its stack back to the pool. A message arriving puts it back in the run
// queue, and it starts again at code, on a stack from the pool.

#include "rt.h"

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
#define HEAP_MAX       (64 << 20)       // all of a process's chunks; there's no GC yet

static rt_proc_t **procs;               // by pid
static uint64_t    nprocs, procs_cap;
static rt_proc_t  *ready_head, *ready_tail;
static rt_ctx_t    scheduler;
static void       *free_stacks;         // a list through each stack's first word
static size_t      page;

static rt_buf_t    root_result;
static int         root_done;

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

static void free_heap(rt_proc_t *p) {
    for (rt_chunk_t *c = p->chunks, *next; c; c = next) {
        next = c->next;
        free(c);
    }
    p->chunks     = NULL;
    p->heap_ptr   = p->heap_limit = 0;
    p->heap_bytes = 0;
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
    procs[id]   = p;
    p->pid      = id << RT_PID_SHIFT | RT_TAG_PID;
    p->parent   = parent;
    p->code     = code;
    enqueue(p);
    return p;
}

static rt_proc_t *lookup(rt_value_t pid) {
    uint64_t id = pid >> RT_PID_SHIFT;
    return id >= 1 && id <= nprocs ? procs[id] : NULL;
}

rt_value_t rt_fork(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) {
    (void)site;
    rt_proc_t  *p = rt_new_process(code, rt_current->pid);
    rt_chunk_t *c = copy_values(values, p->args, n);
    if (c) adopt(p, c);
    return p->pid;
}

// To a process that has ended, a message just disappears, as in Erlang.
rt_value_t rt_send(rt_value_t pid, rt_value_t msg, const char *site) {
    if ((pid & RT_TAG_MASK) != RT_TAG_PID) rt_fault(RT_FAULT_NOT_PID, pid, site);
    rt_proc_t *p = lookup(pid);
    if (!p || p->state == RT_DONE) return RT_NIL;
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
    return RT_NIL;
}

rt_value_t rt_recv(const rt_value_t *args, uint64_t n, rt_code_t code) {
    rt_proc_t *p = rt_current;
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

void rt_exit(rt_proc_t *p, rt_value_t result) {
    if (p == procs[1]) {
        rt_render(&root_result, result, 0);
        root_done = 1;
    }
    p->state = RT_DONE;
    to_scheduler(p);
    __builtin_unreachable();
}

// --- the scheduler ------------------------------------------------------------

void rt_run(void) {
    rt_proc_t *p;
    while ((p = dequeue()) != NULL) {
        if (!p->stack) start(p);
        p->state   = RT_RUNNING;
        rt_current = p;
        rt_switch(&scheduler, &p->ctx);
        // back: it paused (and queued itself again), waits in recv, or ended
        if (p->state == RT_WAITING || p->state == RT_DONE) release_stack(p);
        if (p->state == RT_DONE) {
            free_heap(p);
            for (rt_msg_t *m = p->mail, *next; m; m = next) {
                next = m->next;
                free(m->chunk);
                free(m);
            }
            p->mail = p->mail_last = NULL;
        }
    }
}

int main(void) {
    page = (size_t)sysconf(_SC_PAGESIZE);
    rt_new_process((rt_code_t)slight_main, RT_NIL);
    rt_run();
    if (!root_done) {
        fflush(stdout);
        fputs("deadlock: the root process is waiting for a message, and nothing else can run\n", stderr);
        return 1;
    }
    fwrite(root_result.bytes, 1, root_result.len, stdout);
    putchar('\n');
    return 0;
}
