// rt.c -- the runtime's core: run the root process and print its result,
// faults, the heap, printing values, and equality.

#include "rt.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

_Static_assert(offsetof(rt_proc_t, reductions)  == RT_PROC_REDUCTIONS,  "RT_PROC_REDUCTIONS");
_Static_assert(offsetof(rt_proc_t, stack_limit) == RT_PROC_STACK_LIMIT, "RT_PROC_STACK_LIMIT");
_Static_assert(offsetof(rt_proc_t, heap_ptr)    == RT_PROC_HEAP_PTR,    "RT_PROC_HEAP_PTR");
_Static_assert(offsetof(rt_proc_t, heap_limit)  == RT_PROC_HEAP_LIMIT,  "RT_PROC_HEAP_LIMIT");
_Static_assert(RT_FALSE == (0 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_FALSE is symbol 0");
_Static_assert(RT_TRUE  == (1 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_TRUE is symbol 1");

#define QUOTA          1000             // reductions between preemptions
#define STACK_BYTES    (8 << 20)        // the root process's stack
#define STACK_HEADROOM (64 << 10)       // room left below the limit for one frame and a C call
#define HEAP_BYTES     (64 << 20)       // the root process's heap; there's no GC yet

rt_proc_t *rt_current;

// --- text ---------------------------------------------------------------------

void rt_buf_add(rt_buf_t *b, const char *bytes, size_t len) {
    if (b->len + len > b->cap) {
        b->cap   = (b->len + len) * 2 + 64;
        b->bytes = realloc(b->bytes, b->cap);
        if (!b->bytes) {
            perror("rt: out of memory");
            exit(2);
        }
    }
    memcpy(b->bytes + b->len, bytes, len);
    b->len += len;
}

static void buf_str(rt_buf_t *b, const char *s) { rt_buf_add(b, s, strlen(s)); }

void rt_buf_free(rt_buf_t *b) {
    free(b->bytes);
    *b = (rt_buf_t){ 0 };
}

const char *rt_symbol_name(uint64_t id) {
    if (id >= slight_symbol_count) return NULL;
    const char *name = slight_symbol_names;
    for (uint64_t i = 0; i < id; i++) name += strlen(name) + 1;
    return name;
}

void rt_render(rt_buf_t *b, rt_value_t v, int raw) {
    char num[32];
    if ((v & RT_TAG_INT_MASK) == 0) {
        snprintf(num, sizeof num, "%" PRId64, rt_int_value(v));
        buf_str(b, num);
        return;
    }
    if (v == RT_NIL) {
        buf_str(b, "()");
        return;
    }
    if (rt_is_cons(v)) {
        buf_str(b, "(");
        for (;;) {
            rt_render(b, rt_car(v), 0);
            v = rt_cdr(v);
            if (v == RT_NIL) break;
            buf_str(b, " ");
        }
        buf_str(b, ")");
        return;
    }
    if (rt_is_string(v)) {
        if (!raw) buf_str(b, "\"");
        rt_buf_add(b, rt_string_bytes(v), rt_string_len(v));
        if (!raw) buf_str(b, "\"");
        return;
    }
    if ((v & RT_TAG_MASK) == RT_TAG_SYMBOL) {
        const char *name = rt_symbol_name(v >> RT_SYMBOL_SHIFT);
        if (name) {
            buf_str(b, name);
            return;
        }
    }
    snprintf(num, sizeof num, "#<value 0x%016" PRIx64 ">", v);
    buf_str(b, num);
}

static void print_value(FILE *out, rt_value_t v) {
    rt_buf_t b = { 0 };
    rt_render(&b, v, 0);
    fwrite(b.bytes, 1, b.len, out);
    rt_buf_free(&b);
}

rt_value_t rt_pprint(rt_value_t v) {
    print_value(stdout, v);
    putchar('\n');
    return RT_NIL;
}

// --- equality -----------------------------------------------------------------

// Structural: recursive down the cars, a loop along the cdrs.
static int equal(rt_value_t a, rt_value_t b) {
    for (;;) {
        if (a == b) return 1;
        if (rt_is_string(a) && rt_is_string(b)) {
            return rt_string_len(a) == rt_string_len(b)
                && memcmp(rt_string_bytes(a), rt_string_bytes(b), rt_string_len(a)) == 0;
        }
        if (!rt_is_cons(a) || !rt_is_cons(b) || !equal(rt_car(a), rt_car(b))) return 0;
        a = rt_cdr(a);
        b = rt_cdr(b);
    }
}

rt_value_t rt_equal(rt_value_t a, rt_value_t b) {
    return equal(a, b) ? RT_TRUE : RT_FALSE;
}

// --- faults -------------------------------------------------------------------

void rt_fault(uint64_t fault, rt_value_t value, const char *site) {
    fflush(stdout);                             // so a fault lands after the output before it
    fputs("fault: ", stderr);
    switch (fault) {
        case RT_FAULT_NOT_INT:    fputs("not an integer: ", stderr); print_value(stderr, value); break;
        case RT_FAULT_OVERFLOW:   fputs("integer overflow", stderr); break;
        case RT_FAULT_NOT_BOOL:   fputs("not a boolean: ", stderr);  print_value(stderr, value); break;
        case RT_FAULT_NO_CLAUSE:  fputs("no cond clause matched", stderr); break;
        case RT_FAULT_STACK:      fputs("stack overflow", stderr); break;
        case RT_FAULT_NOT_CONS:   fputs("not a cons: ", stderr);     print_value(stderr, value); break;
        case RT_FAULT_NOT_LIST:   fputs("not a list: ", stderr);     print_value(stderr, value); break;
        case RT_FAULT_HEAP:       fputs("heap exhausted", stderr); break;
        case RT_FAULT_NOT_STRING: fputs("not a string: ", stderr);   print_value(stderr, value); break;
        case RT_FAULT_NOT_SYMBOL: fputs("not a symbol: ", stderr);   print_value(stderr, value); break;
        case RT_FAULT_RANGE:      fputs("out of range: ", stderr);   print_value(stderr, value); break;
        default:                  fprintf(stderr, "unknown fault %" PRIu64, fault); break;
    }
    fprintf(stderr, " (%s)\n", site);
    exit(1);
}

// --- the heap -----------------------------------------------------------------

void *rt_alloc(size_t bytes, const char *site) {
    bytes = (bytes + 15) & ~(size_t)15;
    if (rt_current->heap_limit - rt_current->heap_ptr < bytes) rt_fault(RT_FAULT_HEAP, 0, site);
    void *p = (void *)rt_current->heap_ptr;
    rt_current->heap_ptr += bytes;
    return p;
}

rt_value_t rt_new_string(const char *bytes, size_t len, const char *site) {
    uint64_t *box = rt_alloc(8 + len + 1, site);
    box[0] = (uint64_t)len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
    memcpy(box + 1, bytes, len);
    ((char *)(box + 1))[len] = '\0';
    return (rt_value_t)box | RT_TAG_BOXED;
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

// --- processes ----------------------------------------------------------------

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

int main(void) {
    rt_proc_t root = { .reductions = QUOTA };
    void     *top  = new_stack(&root, STACK_BYTES);
    new_heap(&root, HEAP_BYTES);
    rt_current = &root;
    rt_value_t v = rt_enter(&root, slight_main, top);
    print_value(stdout, v);
    putchar('\n');
    return 0;
}
