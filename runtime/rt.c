// rt.c -- the runtime. For now: run the root process, print its result,
// and the few things compiled code calls.

#include "rt.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

_Static_assert(offsetof(rt_proc_t, reductions)  == RT_PROC_REDUCTIONS,  "RT_PROC_REDUCTIONS");
_Static_assert(offsetof(rt_proc_t, stack_limit) == RT_PROC_STACK_LIMIT, "RT_PROC_STACK_LIMIT");
_Static_assert(offsetof(rt_proc_t, heap_ptr)    == RT_PROC_HEAP_PTR,    "RT_PROC_HEAP_PTR");
_Static_assert(offsetof(rt_proc_t, heap_limit)  == RT_PROC_HEAP_LIMIT,  "RT_PROC_HEAP_LIMIT");

#define QUOTA          1000             // reductions between preemptions
#define STACK_BYTES    (8 << 20)        // the root process's stack
#define STACK_HEADROOM (64 << 10)       // room left below the limit for one frame and a C call
#define HEAP_BYTES     (64 << 20)       // the root process's heap; there's no GC yet
_Static_assert(RT_FALSE == (0 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_FALSE is symbol 0");
_Static_assert(RT_TRUE  == (1 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_TRUE is symbol 1");

static const char *symbol_name(uint64_t id) {
    if (id >= slight_symbol_count) return NULL;
    const char *name = slight_symbol_names;
    for (uint64_t i = 0; i < id; i++) name += strlen(name) + 1;
    return name;
}

static void print_value(FILE *out, rt_value_t v) {
    if ((v & RT_TAG_INT_MASK) == 0) {
        fprintf(out, "%" PRId64, (int64_t)v >> 1);
        return;
    }
    if (v == RT_NIL) {
        fputs("()", out);
        return;
    }
    if (rt_is_cons(v)) {
        fputc('(', out);
        for (;;) {
            print_value(out, rt_car(v));
            v = rt_cdr(v);
            if (v == RT_NIL) break;
            fputc(' ', out);
        }
        fputc(')', out);
        return;
    }
    if ((v & RT_TAG_MASK) == RT_TAG_SYMBOL) {
        const char *name = symbol_name(v >> RT_SYMBOL_SHIFT);
        if (name) {
            fputs(name, out);
            return;
        }
    }
    fprintf(out, "#<value 0x%016" PRIx64 ">", v);
}

rt_value_t rt_pprint(rt_value_t v) {
    print_value(stdout, v);
    putchar('\n');
    return RT_NIL;
}

// Structural: recursive down the cars, a loop along the cdrs.
static int equal(rt_value_t a, rt_value_t b) {
    for (;;) {
        if (a == b) return 1;
        if (!rt_is_cons(a) || !rt_is_cons(b) || !equal(rt_car(a), rt_car(b))) return 0;
        a = rt_cdr(a);
        b = rt_cdr(b);
    }
}

rt_value_t rt_equal(rt_value_t a, rt_value_t b) {
    return equal(a, b) ? RT_TRUE : RT_FALSE;
}

void rt_fault(uint64_t fault, rt_value_t value, const char *site) {
    fflush(stdout);                             // so a fault lands after the output before it
    fputs("fault: ", stderr);
    switch (fault) {
        case RT_FAULT_NOT_INT:   fputs("not an integer: ", stderr); print_value(stderr, value); break;
        case RT_FAULT_OVERFLOW:  fputs("integer overflow", stderr); break;
        case RT_FAULT_NOT_BOOL:  fputs("not a boolean: ", stderr);  print_value(stderr, value); break;
        case RT_FAULT_NO_CLAUSE: fputs("no cond clause matched", stderr); break;
        case RT_FAULT_STACK:     fputs("stack overflow", stderr); break;
        case RT_FAULT_NOT_CONS:  fputs("not a cons: ", stderr);     print_value(stderr, value); break;
        case RT_FAULT_NOT_LIST:  fputs("not a list: ", stderr);     print_value(stderr, value); break;
        case RT_FAULT_HEAP:      fputs("heap exhausted", stderr); break;
        default:                 fprintf(stderr, "unknown fault %" PRIu64, fault); break;
    }
    fprintf(stderr, " (%s)\n", site);
    exit(1);
}

void rt_preempt(rt_proc_t *proc) {
    proc->reductions = QUOTA;
}

// A stack of `bytes` with a guard page under it, so running off the end
// crashes instead of corrupting memory. Returns its top, and sets the
// process's limit far enough above the bottom that the function that
// trips the check still has room to call rt_fault.
static void *new_stack(rt_proc_t *proc, size_t bytes) {
    size_t guard = (size_t)sysconf(_SC_PAGESIZE);
    char  *base  = mmap(NULL, guard + bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED || mprotect(base, guard, PROT_NONE) != 0) {
        perror("rt: can't allocate a stack");
        exit(2);
    }
    proc->stack_limit = (uintptr_t)(base + guard + STACK_HEADROOM);
    return base + guard + bytes;
}

// One chunk for now, mapped lazily; collection comes in step 9.
static void new_heap(rt_proc_t *proc, size_t bytes) {
    char *base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) {
        perror("rt: can't allocate a heap");
        exit(2);
    }
    proc->heap_ptr   = (uintptr_t)base;
    proc->heap_limit = (uintptr_t)(base + bytes);
}

int main(void) {
    rt_proc_t  root = { .reductions = QUOTA };
    void      *top  = new_stack(&root, STACK_BYTES);
    new_heap(&root, HEAP_BYTES);
    rt_value_t v    = rt_enter(&root, slight_main, top);
    print_value(stdout, v);
    putchar('\n');
    return 0;
}
