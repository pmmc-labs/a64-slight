// rt.c -- the runtime's bookkeeping: processes, mailboxes, the bus, and the
// tick scheduler from docs/CALLING_CONVENTIONS.md ("Delivery and Ordering").
//
// Everything in here runs on the *calling process's* stack (the ops are plain
// calls from actor code), except rt_run, which runs on the main stack. A
// process suspends by calling block(), which is just rt_switch into the
// scheduler; when the scheduler switches back, block() returns.

#include "rt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { READY = 1, WAITING, STOPPED };

typedef struct ctx {
    uint64_t x19_x28[10];
    uint64_t fp, lr, sp;
    uint64_t d8_d15[8];
} ctx_t;

typedef struct env env_t;
struct env {
    env_t          *next;              // mailbox / bus / dead-letter link
    env_t          *frame_next;        // MESSAGE frame stack link
    uint64_t        to, from;
    const rt_tag_t *tag;
    uint64_t        reply;             // reply address (packed), 0 = none
    uint64_t        ref;               // set on replies
    uint64_t        argc;
    uint64_t        args[RT_MAX_ARGS];
};

typedef struct proc proc_t;
struct proc {
    int64_t            reductions;          // RT_PROC_REDUCTIONS
    uint64_t           pid;                 // RT_PROC_PID
    uint64_t           args[RT_MAX_ARGS];   // RT_PROC_ARGS: spawn args, for the trampoline
    ctx_t              ctx;                 // RT_PROC_CTX
    const rt_module_t *module;
    uint64_t           parent;
    int                state;
    uint64_t           born;                // tick it was spawned in
    void              *stack;               // mmap'd, guard page included
    size_t             stack_size;
    env_t             *mbox, **mbox_tail;
    env_t            **cursor;              // link to the message under the cursor
    env_t             *frames;              // MESSAGE frames, innermost first
    uint32_t           next_ref;
    uint64_t           awaiting;            // pid this process is AWAITing, 0 = none
    uint64_t           commit_sp;           // sp at the first YIELD
    uint64_t           ret[RT_MAX_ARGS];    // AWAIT's reply values
    char              *fault;
    proc_t            *next_live;
};

_Static_assert(offsetof(proc_t, reductions) == RT_PROC_REDUCTIONS, "RT_PROC_REDUCTIONS");
_Static_assert(offsetof(proc_t, pid)        == RT_PROC_PID,        "RT_PROC_PID");
_Static_assert(offsetof(proc_t, args)       == RT_PROC_ARGS,       "RT_PROC_ARGS");
_Static_assert(offsetof(proc_t, ctx)        == RT_PROC_CTX,        "RT_PROC_CTX");
_Static_assert(offsetof(env_t,  args)       == RT_ENV_ARGS,        "RT_ENV_ARGS");
_Static_assert(offsetof(ctx_t,  fp)         == RT_CTX_FP,          "RT_CTX_FP");
_Static_assert(offsetof(ctx_t,  lr)         == RT_CTX_LR,          "RT_CTX_LR");
_Static_assert(offsetof(ctx_t,  sp)         == RT_CTX_SP,          "RT_CTX_SP");
_Static_assert(offsetof(ctx_t,  d8_d15)     == RT_CTX_D8,          "RT_CTX_D8");
_Static_assert(sizeof(ctx_t)                == RT_CTX_SIZE,        "RT_CTX_SIZE");
_Static_assert(sizeof(rt_tag_t)             == 32,                 "TAG macro emits 4 quads");
_Static_assert(sizeof(rt_module_t)          == 40,                 "MODULE macro emits 5 quads");

// A pending (caller side) and a reply address (callee side) are the same
// shape, packed into one register: ref:32 | pid:24 | tag index:8
#define HANDLE(pid, idx, ref) (((uint64_t)(ref) << 32) | ((uint64_t)(pid) << 8) | (uint64_t)(idx))
#define H_TAG(h)              ((h) & 0xFF)
#define H_PID(h)              (((h) >> 8) & 0xFFFFFF)
#define H_REF(h)              ((h) >> 32)
#define MAX_PIDS              (1u << 24)

// --- implemented in rt_asm.S --------------------------------------------------

void rt_switch(ctx_t *from, ctx_t *to) RT_ASM(rt_switch);
void rt_trampoline(void)               RT_ASM(rt_trampoline);

// --- called from actor code (directly, or through the stubs in rt_asm.S) ------

uint64_t  rt_spawn_c(const rt_module_t *, uint64_t, const uint64_t *)          RT_ASM(rt_spawn_c);
void      rt_send_c(uint64_t, const rt_tag_t *, uint64_t, const uint64_t *)    RT_ASM(rt_send_c);
uint64_t  rt_request_c(uint64_t, const rt_tag_t *, const rt_tag_t *,
                       uint64_t, const uint64_t *)                              RT_ASM(rt_request_c);
uint64_t *rt_await_c(uint64_t, const rt_tag_t *)                                RT_ASM(rt_await_c);
void      rt_reply_c(uint64_t, const uint64_t *)                                RT_ASM(rt_reply_c);
env_t    *rt_msg_accept_c(void)                                                 RT_ASM(rt_msg_accept_c);
uint64_t  rt_recv(void)                                                         RT_ASM(rt_recv);
void      rt_msg_skip(void)                                                     RT_ASM(rt_msg_skip);
void      rt_msg_drop(void)                                                     RT_ASM(rt_msg_drop);
void      rt_msg_done(void)                                                     RT_ASM(rt_msg_done);
uint64_t  rt_msg_sender(void)                                                   RT_ASM(rt_msg_sender);
void      rt_yield(uint64_t)                                                    RT_ASM(rt_yield);
_Noreturn void rt_stop(void)                                                    RT_ASM(rt_stop);
void      rt_preempt(void)                                                      RT_ASM(rt_preempt);
void      rt_say(int64_t)                                                       RT_ASM(rt_say);
void      rt_say_str(const char *)                                              RT_ASM(rt_say_str);

// --- state ---------------------------------------------------------------------

static ctx_t               sched;          // the scheduler's own context
static proc_t             *current;        // same as x28 in actor code
static proc_t            **procs;          // by pid; NULL once reaped
static const rt_module_t **mods;           // by pid; kept (a pid knows its module)
static uint64_t            npids = 1, cap;
static proc_t             *live, **live_tail = &live;
static env_t              *bus,  **bus_tail  = &bus;
static env_t              *dead, **dead_tail = &dead;
static int64_t             quota;
static size_t              stack_bytes, page;
static rt_stats_t          stats;

static void die(const char *msg) {
    fprintf(stderr, "rt: %s\n", msg);
    exit(2);
}

static const char *who(uint64_t pid) {
    static char bufs[4][64];
    static int  i;
    char *b = bufs[i++ & 3];
    const rt_module_t *m = pid && pid < npids ? mods[pid] : NULL;
    snprintf(b, 64, "<%llu:%s>", (unsigned long long)pid, m ? m->name : "rt");
    return b;
}

static void tagname(char *b, size_t n, const rt_tag_t *t) {
    snprintf(b, n, "^%s:%s", t->module->name, t->name);
}

// A fault is a STOP with a reason.
static _Noreturn void fault(proc_t *p, const char *fmt, ...) {
    char    msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    p->fault = strdup(msg);
    rt_stop();
}

// Suspend until the scheduler switches back to us.
static void block(proc_t *p) {
    p->state = WAITING;
    rt_switch(&p->ctx, &sched);
}

static env_t *unlink_msg(proc_t *p, env_t **pp) {
    env_t *e = *pp;
    *pp = e->next;
    if (p->mbox_tail == &e->next) p->mbox_tail = pp;
    e->next = NULL;
    return e;
}

static void bury(env_t *e) {
    e->next    = NULL;
    *dead_tail = e;
    dead_tail  = &e->next;
}

// --- processes -------------------------------------------------------------------

static proc_t *spawn(const rt_module_t *m, uint64_t parent, const uint64_t *args) {
    if (npids >= MAX_PIDS) die("out of pids");
    if (npids >= cap) {
        cap   = cap ? cap * 2 : 1024;
        procs = realloc(procs, cap * sizeof *procs);
        mods  = realloc(mods,  cap * sizeof *mods);
        if (!procs || !mods) die("out of memory");
    }

    proc_t *p = calloc(1, sizeof *p);
    if (!p) die("out of memory");
    p->pid    = npids++;
    p->module = m;
    p->parent = parent;
    p->state  = READY;
    p->born   = stats.ticks;
    memcpy(p->args, args, m->arity * sizeof *args);
    p->mbox_tail = &p->mbox;
    p->cursor    = &p->mbox;
    procs[p->pid] = p;
    mods[p->pid]  = m;

    // the stack, with a guard page at the low end (stacks grow down)
    p->stack_size = stack_bytes + page;
    p->stack = mmap(NULL, p->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p->stack == MAP_FAILED) die("mmap failed");
    if (mprotect(p->stack, page, PROT_NONE) != 0) die("mprotect failed");

    // first activation: rt_switch "returns" into rt_trampoline
    p->ctx.x19_x28[0] = (uint64_t)m->init;                     // x19
    p->ctx.x19_x28[9] = (uint64_t)p;                           // x28 = self
    p->ctx.lr         = (uint64_t)rt_trampoline;
    p->ctx.sp         = (uint64_t)p->stack + p->stack_size;
    p->ctx.fp         = 0;

    *live_tail = p;
    live_tail  = &p->next_live;
    stats.spawned++;
    return p;
}

uint64_t rt_spawn_c(const rt_module_t *m, uint64_t argc, const uint64_t *args) {
    if (m->arity > RT_MAX_ARGS)
        fault(current, "SPAWN %s: it declares %llu args, the most is %d", m->name,
              (unsigned long long)m->arity, RT_MAX_ARGS);
    if (argc != m->arity)
        fault(current, "SPAWN %s with %llu args: it takes %llu", m->name,
              (unsigned long long)argc, (unsigned long long)m->arity);
    return spawn(m, current->pid, args)->pid;
}

uint64_t rt_spawn_root(const rt_module_t *m, uint64_t argc, const uint64_t *args) {
    if (argc != m->arity || argc > RT_MAX_ARGS) die("rt_spawn_root: wrong number of args");
    return spawn(m, 0, args)->pid;
}

void rt_yield(uint64_t sp) {
    proc_t *p = current;
    if (p->frames)
        fault(p, "YIELD with a live MESSAGE frame (%s)", p->frames->tag->name);
    if (!p->commit_sp)
        p->commit_sp = sp;
    else if (sp != p->commit_sp)
        fault(p, "YIELD at a different stack depth (%+lld bytes)", (long long)(sp - p->commit_sp));
    p->cursor = &p->mbox;
}

_Noreturn void rt_stop(void) {
    current->state = STOPPED;
    rt_switch(&current->ctx, &sched);
    abort();                                    // a stopped process is never resumed
}

void rt_preempt(void) {
    stats.preemptions++;
    rt_switch(&current->ctx, &sched);           // still READY: runs again next tick
}

// --- sending -----------------------------------------------------------------------

static env_t *envelope(proc_t *p, const char *op, uint64_t to, const rt_tag_t *tag,
                       uint64_t argc, const uint64_t *args) {
    char t[96];
    tagname(t, sizeof t, tag);
    if (to == 0 || to >= npids)
        fault(p, "%s %s to %llu: no such process", op, t, (unsigned long long)to);
    if (tag->module != mods[to])
        fault(p, "%s %s to %s: the tag belongs to %s", op, t, who(to), tag->module->name);
    if (argc != tag->arity)
        fault(p, "%s %s with %llu args: its arity is %llu", op, t,
              (unsigned long long)argc, (unsigned long long)tag->arity);
    if (argc > RT_MAX_ARGS)
        fault(p, "%s %s: %llu args, the most is %d", op, t, (unsigned long long)argc, RT_MAX_ARGS);

    env_t *e = calloc(1, sizeof *e);
    if (!e) die("out of memory");
    e->to   = to;
    e->from = p->pid;
    e->tag  = tag;
    e->argc = argc;
    memcpy(e->args, args, argc * sizeof *args);
    return e;
}

// Onto the bus: delivered at the start of the next tick, in send order.
static void post(env_t *e) {
    *bus_tail = e;
    bus_tail  = &e->next;
    stats.messages++;
}

void rt_send_c(uint64_t to, const rt_tag_t *tag, uint64_t argc, const uint64_t *args) {
    post(envelope(current, "SEND", to, tag, argc, args));
}

uint64_t rt_request_c(uint64_t to, const rt_tag_t *tag, const rt_tag_t *reply,
                      uint64_t argc, const uint64_t *args) {
    proc_t *p = current;
    if (reply->module != p->module)
        fault(p, "REQUEST with reply tag ^%s:%s, which is not ours", reply->module->name, reply->name);
    env_t *e = envelope(p, "REQUEST", to, tag, argc, args);
    if (++p->next_ref == 0) p->next_ref = 1;   // 0 means "no ref"
    e->reply = HANDLE(p->pid, reply->index, p->next_ref);
    post(e);
    return HANDLE(to, reply->index, p->next_ref);
}

void rt_reply_c(uint64_t argc, const uint64_t *args) {
    proc_t *p = current;
    env_t  *f = p->frames;
    if (!f)        fault(p, "REPLY outside of a message");
    if (!f->reply) fault(p, "REPLY to %s, which has no reply address", f->tag->name);
    uint64_t to = H_PID(f->reply);
    env_t   *e  = envelope(p, "REPLY", to, &mods[to]->tags[H_TAG(f->reply)], argc, args);
    e->ref = H_REF(f->reply);
    post(e);
}

// --- receiving ---------------------------------------------------------------------

uint64_t rt_recv(void) {
    proc_t *p = current;
    while (!*p->cursor) block(p);
    return (*p->cursor)->tag->index;
}

env_t *rt_msg_accept_c(void) {
    proc_t *p = current;
    if (!*p->cursor) fault(p, "MSG_ACCEPT with no message under the cursor");
    env_t *e = unlink_msg(p, p->cursor);
    p->cursor     = &p->mbox;
    e->frame_next = p->frames;
    p->frames     = e;
    return e;
}

void rt_msg_skip(void) {
    proc_t *p = current;
    if (!*p->cursor) fault(p, "MSG_SKIP with no message under the cursor");
    p->cursor = &(*p->cursor)->next;
}

void rt_msg_drop(void) {
    proc_t *p = current;
    if (!*p->cursor) fault(p, "MSG_DROP with no message under the cursor");
    bury(unlink_msg(p, p->cursor));             // the cursor now points at the next one
}

void rt_msg_done(void) {
    proc_t *p = current;
    env_t  *e = p->frames;
    if (!e) fault(p, "MSG_DONE with no MESSAGE frame");
    p->frames = e->frame_next;
    free(e);
}

uint64_t rt_msg_sender(void) {
    proc_t *p = current;
    if (!p->frames) fault(p, "MSG_SENDER with no MESSAGE frame");
    return p->frames->from;
}

// AWAIT: scan the whole mailbox for the reply first, and only then check
// whether the callee is gone (see "replies beat exits" in the doc).
uint64_t *rt_await_c(uint64_t pending, const rt_tag_t *reply) {
    proc_t  *p      = current;
    uint64_t callee = H_PID(pending);
    uint64_t ref    = H_REF(pending);
    if (reply->module != p->module || reply->index != H_TAG(pending) || callee >= npids)
        fault(p, "AWAIT ^%s:%s on a pending for something else", reply->module->name, reply->name);

    for (;;) {
        for (env_t **pp = &p->mbox; *pp; pp = &(*pp)->next) {
            env_t *e = *pp;
            if (e->tag == reply && e->ref == ref) {
                unlink_msg(p, pp);
                p->cursor   = &p->mbox;
                p->awaiting = 0;
                memcpy(p->ret, e->args, e->argc * sizeof *e->args);
                free(e);
                return p->ret;
            }
        }
        if (!procs[callee]) {                   // reaped without replying
            p->awaiting = 0;
            return NULL;
        }
        p->awaiting = callee;
        block(p);
    }
}

// --- output ------------------------------------------------------------------------

void rt_say(int64_t v)         { printf("say: %lld\n", (long long)v); }
void rt_say_str(const char *s) { printf("say: %s\n", s); }

// --- the scheduler -----------------------------------------------------------------

void rt_init(int64_t q, size_t sb) {
    quota       = q;
    page        = (size_t)sysconf(_SC_PAGESIZE);
    stack_bytes = (sb + page - 1) / page * page;
}

// 1. deliver: the bus -> mailboxes, in send order; wake whoever got mail
static void deliver(void) {
    env_t *e = bus;
    bus      = NULL;
    bus_tail = &bus;
    while (e) {
        env_t  *n = e->next;
        proc_t *p = procs[e->to];
        e->next = NULL;
        if (!p) {
            bury(e);
        } else {
            *p->mbox_tail = e;
            p->mbox_tail  = &e->next;
            if (p->state == WAITING) p->state = READY;
        }
        e = n;
    }
}

// 5. reap: dead letters, faults, stacks
static void reap(void) {
    for (proc_t **pp = &live; *pp;) {
        proc_t *p = *pp;
        if (p->state != STOPPED) {
            pp = &p->next_live;
            continue;
        }
        *pp = p->next_live;
        if (live_tail == &p->next_live) live_tail = pp;

        if (p->fault) {
            printf("fault: %s: %s\n", who(p->pid), p->fault);
            stats.faults++;
            free(p->fault);
        }
        for (env_t *e = p->mbox, *n; e; e = n) { n = e->next;       bury(e); }
        for (env_t *e = p->frames, *n; e; e = n) { n = e->frame_next; free(e); }
        munmap(p->stack, p->stack_size);
        procs[p->pid] = NULL;
        free(p);
    }
}

void rt_run(void) {
    while (live) {
        stats.ticks++;
        deliver();

        // 2. wake AWAITs whose callee has been reaped (the AWAIT will now fail)
        size_t ready = 0;
        for (proc_t *p = live; p; p = p->next_live) {
            if (p->state == WAITING && p->awaiting && !procs[p->awaiting]) p->state = READY;
            if (p->state == READY) ready++;
        }
        if (!ready) break;                      // live, but nothing can ever run

        // 3. run each READY process in pid order; anything spawned during
        //    this tick waits for the next one
        for (proc_t *p = live; p; p = p->next_live) {
            if (p->state != READY || p->born == stats.ticks) continue;
            current       = p;
            p->reductions = quota;
            stats.activations++;
            rt_switch(&sched, &p->ctx);
        }
        current = NULL;

        // 4. collect: nothing to do, sends went straight onto the bus
        reap();
    }
}

void rt_report(void) {
    if (live) {
        size_t n = 0;
        for (proc_t *p = live; p; p = p->next_live) n++;
        printf("deadlock: %zu waiting:", n);
        for (proc_t *p = live; p; p = p->next_live) printf(" %s", who(p->pid));
        printf("\n");
    }
    for (env_t *e = dead; e; e = e->next) {
        char t[96];
        tagname(t, sizeof t, e->tag);
        printf("dead letter: %s(", t);
        for (uint64_t i = 0; i < e->argc; i++)
            printf("%s%llu", i ? ", " : "", (unsigned long long)e->args[i]);
        printf(") from %s to %s\n", who(e->from), who(e->to));
    }
    printf("done: %llu processes, %llu ticks, %llu faults\n",
           (unsigned long long)stats.spawned, (unsigned long long)stats.ticks,
           (unsigned long long)stats.faults);
}

rt_stats_t rt_get_stats(void) {
    rt_stats_t s = stats;
    for (proc_t *p = live; p; p = p->next_live) s.waiting++;
    return s;
}
