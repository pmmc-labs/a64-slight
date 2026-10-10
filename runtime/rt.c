// rt.c -- the runtime's core: faults, allocation, printing values, and
// equality. Processes and main are in process.c.

#include "rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(offsetof(rt_proc_t, reductions)  == RT_PROC_REDUCTIONS,  "RT_PROC_REDUCTIONS");
_Static_assert(offsetof(rt_proc_t, stack_limit) == RT_PROC_STACK_LIMIT, "RT_PROC_STACK_LIMIT");
_Static_assert(offsetof(rt_proc_t, heap_ptr)    == RT_PROC_HEAP_PTR,    "RT_PROC_HEAP_PTR");
_Static_assert(offsetof(rt_proc_t, heap_limit)  == RT_PROC_HEAP_LIMIT,  "RT_PROC_HEAP_LIMIT");
_Static_assert(offsetof(rt_proc_t, pid)         == RT_PROC_PID,         "RT_PROC_PID");
_Static_assert(offsetof(rt_proc_t, parent)      == RT_PROC_PARENT,      "RT_PROC_PARENT");
_Static_assert(offsetof(rt_proc_t, code)        == RT_PROC_CODE,        "RT_PROC_CODE");
_Static_assert(offsetof(rt_proc_t, args)        == RT_PROC_ARGS,        "RT_PROC_ARGS");
_Static_assert(offsetof(rt_proc_t, ctx)         == RT_PROC_CTX,         "RT_PROC_CTX");
#if defined(__x86_64__)
_Static_assert(offsetof(rt_ctx_t, rbx)          == RT_CTX_RBX,          "RT_CTX_RBX");
_Static_assert(offsetof(rt_ctx_t, rbp)          == RT_CTX_RBP,          "RT_CTX_RBP");
_Static_assert(offsetof(rt_ctx_t, r12)          == RT_CTX_R12,          "RT_CTX_R12");
_Static_assert(offsetof(rt_ctx_t, r13)          == RT_CTX_R13,          "RT_CTX_R13");
_Static_assert(offsetof(rt_ctx_t, r14)          == RT_CTX_R14,          "RT_CTX_R14");
_Static_assert(offsetof(rt_ctx_t, r15)          == RT_CTX_R15,          "RT_CTX_R15");
_Static_assert(offsetof(rt_ctx_t, rsp)          == RT_CTX_RSP,          "RT_CTX_RSP");
#else
_Static_assert(offsetof(rt_ctx_t, fp)           == RT_CTX_FP,           "RT_CTX_FP");
_Static_assert(offsetof(rt_ctx_t, lr)           == RT_CTX_LR,           "RT_CTX_LR");
_Static_assert(offsetof(rt_ctx_t, sp)           == RT_CTX_SP,           "RT_CTX_SP");
_Static_assert(offsetof(rt_ctx_t, d8_d15)       == RT_CTX_D8,           "RT_CTX_D8");
#endif
_Static_assert(sizeof(rt_ctx_t)                 == RT_CTX_SIZE,         "RT_CTX_SIZE");
_Static_assert(RT_FALSE == (0 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_FALSE is symbol 0");
_Static_assert(RT_TRUE  == (1 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_TRUE is symbol 1");
_Static_assert(RT_SYM_KEYS == RT_SYM_FAULTS + RT_FAULT_COUNT, "the keys follow the fault kinds");

rt_proc_t *rt_current;

// --- text ---------------------------------------------------------------------

void rt_buf_add(rt_buf_t *b, const char *bytes, size_t len) {
    if (!len) return;                           // so an empty buffer's NULL is never copied to
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

void rt_work_grow(rt_work_t *w) {
    size_t      cap   = 2 * w->cap;
    rt_value_t *items = malloc(cap * sizeof *items);
    if (!items) {
        perror("rt: out of memory");
        exit(2);
    }
    memcpy(items, w->items, w->n * sizeof *items);
    rt_work_free(w);
    w->items = items;
    w->cap   = cap;
}

void rt_work_free(rt_work_t *w) {
    if (w->items != w->local) free(w->items);
}

// The symbols' names, by id, and a table from names to ids (open
// addressing, ids + 1, 0 for an empty slot), made the first time either is
// wanted: the names are packed one after another in the program's data.
static const char **symbol_names;
static uint64_t    *symbol_table;
static uint64_t     symbol_slots;

static uint64_t name_hash(const char *bytes, size_t len) {
    uint64_t h = 14695981039346656037u;         // FNV-1a
    for (size_t i = 0; i < len; i++) h = (h ^ (unsigned char)bytes[i]) * 1099511628211u;
    return h;
}

static void index_symbols(void) {
    symbol_names = malloc((slight_symbol_count + 1) * sizeof *symbol_names);
    for (symbol_slots = 16; symbol_slots < 2 * slight_symbol_count; symbol_slots *= 2) {}
    symbol_table = calloc(symbol_slots, sizeof *symbol_table);
    if (!symbol_names || !symbol_table) abort();
    const char *name = slight_symbol_names;
    for (uint64_t id = 0; id < slight_symbol_count; id++, name += strlen(name) + 1) {
        symbol_names[id] = name;
        uint64_t slot = name_hash(name, strlen(name)) & (symbol_slots - 1);
        while (symbol_table[slot]) slot = (slot + 1) & (symbol_slots - 1);
        symbol_table[slot] = id + 1;
    }
}

const char *rt_symbol_name(uint64_t id) {
    if (id >= slight_symbol_count) return NULL;
    if (!symbol_names) index_symbols();
    return symbol_names[id];
}

int64_t rt_symbol_find(const char *bytes, size_t len) {
    if (!symbol_names) index_symbols();
    for (uint64_t slot = name_hash(bytes, len) & (symbol_slots - 1); symbol_table[slot]; slot = (slot + 1) & (symbol_slots - 1)) {
        const char *name = symbol_names[symbol_table[slot] - 1];
        if (strlen(name) == len && memcmp(name, bytes, len) == 0) return (int64_t)symbol_table[slot] - 1;
    }
    return -1;
}

// The shortest text that reads back as the same double, laid out as
// JavaScript lays out numbers, except that it always has a "." or an
// exponent, so it can't be mistaken for an integer: 3.0, 0.1, 1e+21.
static void render_float(rt_buf_t *b, double d) {
    const char *special = isnan(d) ? "nan" : isinf(d) ? (d < 0 ? "-inf" : "inf")
                        : d == 0 ? (signbit(d) ? "-0.0" : "0.0") : NULL;
    if (special) {
        buf_str(b, special);
        return;
    }

    // the fewest significant digits that round-trip, as d.ddde±x
    char e[40];
    for (int p = 1; p <= 17; p++) {
        snprintf(e, sizeof e, "%.*e", p - 1, d);
        if (strtod(e, NULL) == d) break;
    }
    const char *s = e;
    if (*s == '-') {
        buf_str(b, "-");
        s++;
    }
    char digits[20];
    int  k = 0;
    for (; *s != 'e'; s++) if (*s != '.') digits[k++] = *s;
    int n = atoi(s + 1) + 1;                    // the decimal point goes after n digits

    if (k <= n && n <= 21) {                    // 300.0
        rt_buf_add(b, digits, (size_t)k);
        for (int i = k; i < n; i++) buf_str(b, "0");
        buf_str(b, ".0");
    } else if (0 < n && n <= 21) {              // 3.25
        rt_buf_add(b, digits, (size_t)n);
        buf_str(b, ".");
        rt_buf_add(b, digits + n, (size_t)(k - n));
    } else if (-6 < n && n <= 0) {              // 0.000325
        buf_str(b, "0.");
        for (int i = n; i < 0; i++) buf_str(b, "0");
        rt_buf_add(b, digits, (size_t)k);
    } else {                                    // 3.25e+21
        char exp[16];
        rt_buf_add(b, digits, 1);
        if (k > 1) {
            buf_str(b, ".");
            rt_buf_add(b, digits + 1, (size_t)(k - 1));
        }
        snprintf(exp, sizeof exp, "e%c%d", n - 1 < 0 ? '-' : '+', abs(n - 1));
        buf_str(b, exp);
    }
}

// Anything but a cons.
static void render_atom(rt_buf_t *b, rt_value_t v, int raw) {
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
    if (rt_is_float(v)) {
        render_float(b, rt_float_value(v));
        return;
    }
    if ((v & RT_TAG_MASK) == RT_TAG_PID) {
        snprintf(num, sizeof num, "#<pid %" PRIu64 ">", v >> RT_PID_SHIFT);
        buf_str(b, num);
        return;
    }
    if (rt_is_closure(v)) {
        buf_str(b, "#<function ");
        buf_str(b, (const char *)rt_box(v)[3]);
        buf_str(b, ">");
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

// A list, along its cdrs, going down into an element that's a list with
// the rest of the outer list on the work stack: the stack holds the rests
// of the lists open around the one being printed. Elements are never raw.
static void render_list(rt_buf_t *b, rt_value_t v) {
    rt_work_t w;
    rt_work_init(&w);
    buf_str(b, "(");
    for (;;) {
        rt_value_t x = rt_car(v);
        if (rt_is_cons(x)) {
            rt_work_push(&w, rt_cdr(v));
            buf_str(b, "(");
            v = x;
            continue;
        }
        render_atom(b, x, 0);
        v = rt_cdr(v);
        while (v == RT_NIL) {                   // the end of this list, and of any it ends
            buf_str(b, ")");
            if (!w.n) {
                rt_work_free(&w);
                return;
            }
            v = rt_work_pop(&w);
        }
        buf_str(b, " ");
    }
}

void rt_render(rt_buf_t *b, rt_value_t v, int raw) {
    if (rt_is_cons(v)) render_list(b, v);
    else               render_atom(b, v, raw);
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

// Two values that aren't both conses.
static int equal_atoms(rt_value_t a, rt_value_t b) {
    if (a == b) return 1;
    if (rt_is_float(a) && rt_is_float(b)) return rt_float_value(a) == rt_float_value(b);
    if (rt_is_string(a) && rt_is_string(b)) {
        return rt_string_len(a) == rt_string_len(b)
            && memcmp(rt_string_bytes(a), rt_string_bytes(b), rt_string_len(a)) == 0;
    }
    return 0;
}

// Structural: a loop along the cdrs, and down the cars when both are
// lists, with the rest of the lists on the work stack, so the stack grows
// only as deep as the values nest in their cars.
static int equal(rt_value_t a, rt_value_t b) {
    rt_work_t w;
    rt_work_init(&w);
    int same = 1;
    for (;;) {
        if (a != b && rt_is_cons(a) && rt_is_cons(b)) {
            rt_value_t x = rt_car(a), y = rt_car(b);
            if (x != y && rt_is_cons(x) && rt_is_cons(y)) {
                rt_work_push(&w, rt_cdr(a));
                rt_work_push(&w, rt_cdr(b));
            } else if (equal_atoms(x, y)) {
                x = rt_cdr(a);
                y = rt_cdr(b);
            } else {
                same = 0;
                break;
            }
            a = x;
            b = y;
            continue;
        }
        if (!equal_atoms(a, b)) {
            same = 0;
            break;
        }
        if (!w.n) break;
        b = rt_work_pop(&w);
        a = rt_work_pop(&w);
    }
    rt_work_free(&w);
    return same;
}

rt_value_t rt_equal(rt_value_t a, rt_value_t b) {
    return equal(a, b) ? RT_TRUE : RT_FALSE;
}

rt_value_t rt_is_lambda(rt_value_t v) {
    return rt_is_closure(v) ? RT_TRUE : RT_FALSE;
}

// --- faults -------------------------------------------------------------------

// What each fault says in the log, and whether it shows the value.
static const struct {
    const char *what;
    int         shows;
} faults[RT_FAULT_COUNT + 1] = {
    [RT_FAULT_NOT_INT]    = { "not an integer: ", 1 },
    [RT_FAULT_OVERFLOW]   = { "integer overflow", 0 },
    [RT_FAULT_NOT_BOOL]   = { "not a boolean: ", 1 },
    [RT_FAULT_NO_CLAUSE]  = { "no cond clause matched", 0 },
    [RT_FAULT_STACK]      = { "stack overflow", 0 },
    [RT_FAULT_NOT_CONS]   = { "not a cons: ", 1 },
    [RT_FAULT_NOT_LIST]   = { "not a list: ", 1 },
    [RT_FAULT_HEAP]       = { "heap exhausted", 0 },
    [RT_FAULT_NOT_STRING] = { "not a string: ", 1 },
    [RT_FAULT_NOT_SYMBOL] = { "not a symbol: ", 1 },
    [RT_FAULT_RANGE]      = { "out of range: ", 1 },
    [RT_FAULT_NOT_NUMBER] = { "not a number: ", 1 },
    [RT_FAULT_DIV_ZERO]   = { "division by zero", 0 },
    [RT_FAULT_NOT_FUNC]   = { "not a function: ", 1 },
    [RT_FAULT_ARITY]      = { "wrong number of arguments for ", 1 },
    [RT_FAULT_NOT_PID]    = { "not a pid: ", 1 },
    [RT_FAULT_JOIN_SELF]  = { "a process can't join itself: ", 1 },
    [RT_FAULT_NOT_DEVICE] = { "not a device: ", 1 },
    [RT_FAULT_NOT_JSON]   = { "not JSON: ", 1 },
    [RT_FAULT_NOT_SEXP]   = { "not data: ", 1 },
};

void rt_fault(uint64_t fault, rt_value_t value, const char *site) {
    if (fault < 1 || fault > RT_FAULT_COUNT) abort();
    if (!faults[fault].shows) value = RT_NIL;   // whatever the caller had in the register

    rt_buf_t b = { 0 };
    buf_str(&b, faults[fault].what);
    if (faults[fault].shows) rt_render(&b, value, 0);
    uint64_t id = rt_current->pid >> RT_PID_SHIFT;
    fflush(stdout);                             // so a fault lands after the output before it
    if (id == 1) fprintf(stderr, "fault: %.*s (%s)\n", (int)b.len, b.bytes, site);
    else         fprintf(stderr, "fault in #<pid %" PRIu64 ">: %.*s (%s)\n", id, (int)b.len, b.bytes, site);
    rt_buf_free(&b);

    // The reason, (kind value site), goes on this stack: rt_end copies it
    // out, and the heap may be what ran out. The string is a box like any
    // other, padded as the copier expects.
    size_t     len = strlen(site);
    uint64_t   space[len / 8 + 6];
    uint64_t  *str = (uint64_t *)(((uintptr_t)space + 15) & ~(uintptr_t)15);
    str[0] = (uint64_t)len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
    memcpy(str + 1, site, len + 1);
    _Alignas(16) rt_value_t cells[6] = {
        rt_symbol(RT_SYM_FAULTS - 1 + fault), (rt_value_t)&cells[2] | RT_TAG_LIST,
        value,                                (rt_value_t)&cells[4] | RT_TAG_LIST,
        (rt_value_t)str | RT_TAG_BOXED,       RT_NIL,
    };
    rt_end(0, (rt_value_t)cells | RT_TAG_LIST, 1);
}

// --- the heap -----------------------------------------------------------------

void *rt_alloc(size_t bytes, const char *site) {
    bytes = (bytes + 15) & ~(size_t)15;
    if (rt_current->heap_limit - rt_current->heap_ptr < bytes) rt_heap_grow(bytes, site);
    void *p = (void *)rt_current->heap_ptr;
    rt_current->heap_ptr += bytes;
    return p;
}

rt_value_t rt_new_string(const char *bytes, size_t len, const char *site) {
    uint64_t *box = rt_alloc(8 + len + 1, site);
    box[0] = (uint64_t)len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
    if (len) memcpy(box + 1, bytes, len);       // bytes may be an empty buffer's NULL
    ((char *)(box + 1))[len] = '\0';
    return (rt_value_t)box | RT_TAG_BOXED;
}

rt_value_t rt_new_float(double d, const char *site) {
    uint64_t *box = rt_alloc(16, site);
    box[0] = (uint64_t)sizeof d << RT_BOX_SIZE_SHIFT | RT_BOX_FLOAT;
    memcpy(box + 1, &d, sizeof d);
    return (rt_value_t)box | RT_TAG_BOXED;
}
