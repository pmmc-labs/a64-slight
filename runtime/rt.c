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

RT_ISLAND rt_proc_t *rt_current;

void *rt_must(void *p) {
    if (!p) {
        perror("rt: out of memory");
        exit(2);
    }
    return p;
}

// --- text ---------------------------------------------------------------------

void rt_buf_add(rt_buf_t *b, const char *bytes, size_t len) {
    if (!len) return;                           // so an empty buffer's NULL is never copied to
    if (b->len + len > b->cap) {
        b->cap   = (b->len + len) * 2 + 64;
        b->bytes = rt_must(realloc(b->bytes, b->cap));
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
    rt_value_t *items = rt_must(malloc(cap * sizeof *items));
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
    symbol_names = rt_must(malloc((slight_symbol_count + 1) * sizeof *symbol_names));
    for (symbol_slots = 16; symbol_slots < 2 * slight_symbol_count; symbol_slots *= 2) {}
    symbol_table = rt_must(calloc(symbol_slots, sizeof *symbol_table));
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
void rt_render_float(rt_buf_t *b, double d) {
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

void rt_pid_text(char *buf, size_t size, rt_value_t v) {
    char     at[8] = "";
    uint64_t island = rt_pid_island(v);
    if (island) snprintf(at, sizeof at, "@%" PRIu64, island);
    if (rt_is_address(v)) snprintf(buf, size, "#<address %" PRIu64 ".%" PRIu64 "%s>", rt_address_task(v), rt_address_number(v), at);
    else                  snprintf(buf, size, "#<pid %" PRIu64 "%s>", rt_pid_number(v), at);
}

// Anything but a cons.
static void render_atom(rt_buf_t *b, rt_value_t v, int raw) {
    char num[48];
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
        rt_render_float(b, rt_float_value(v));
        return;
    }
    if (rt_is_array(v)) {                       // #float(1.0 2.5), #int(1 2)
        int ints = rt_is_box(v, RT_BOX_INTS);
        buf_str(b, ints ? "#int(" : "#float(");
        for (uint64_t i = 0; i < rt_array_len(v); i++) {
            if (i) buf_str(b, " ");
            if (ints) {
                snprintf(num, sizeof num, "%" PRId64, (int64_t)rt_array_data(v)[i]);
                buf_str(b, num);
            } else {
                rt_render_float(b, rt_array_float(v, i));
            }
        }
        buf_str(b, ")");
        return;
    }
    if ((v & RT_TAG_MASK) == RT_TAG_PID) {
        rt_pid_text(num, sizeof num, v);
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
        if (raw) rt_buf_add(b, rt_string_bytes(v), rt_string_len(v));
        else     rt_quote(b, rt_string_bytes(v), rt_string_len(v));
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

// A sequence open in a walk over a value: a list, with what's left of it,
// or a tuple, with the index of its next element. On a work stack it's
// two words, the list or tuple and the index.
typedef struct {
    rt_value_t seq;
    uint64_t   i;
} cursor_t;

static int is_seq(rt_value_t v) { return rt_is_cons(v) || rt_is_tuple(v); }

static int at_end(cursor_t c) {
    return rt_is_tuple(c.seq) ? c.i == rt_tuple_len(c.seq) : c.seq == RT_NIL;
}

static rt_value_t next(cursor_t *c) {
    if (rt_is_tuple(c->seq)) return rt_tuple_get(c->seq, c->i++);
    rt_value_t x = rt_car(c->seq);
    c->seq = rt_cdr(c->seq);
    return x;
}

static void push_cursor(rt_work_t *w, cursor_t c) {
    rt_work_push(w, c.seq);
    rt_work_push(w, c.i);
}

static cursor_t pop_cursor(rt_work_t *w) {
    cursor_t c;
    c.i   = rt_work_pop(w);
    c.seq = rt_work_pop(w);
    return c;
}

// A list or a tuple, element by element, going down into an element
// that's a list or a tuple with the one around it on the work stack: the
// stack holds the sequences open around the one being printed. Elements
// are never raw.
static void render_seq(rt_buf_t *b, rt_value_t v) {
    rt_work_t w;
    rt_work_init(&w);
    cursor_t c = { v, 0 };
    buf_str(b, rt_is_tuple(v) ? "#(" : "(");
    for (;;) {
        if (at_end(c)) {                        // the end of this one, and of any it ends
            buf_str(b, ")");
            if (!w.n) break;
            c = pop_cursor(&w);
            if (!at_end(c)) buf_str(b, " ");
            continue;
        }
        rt_value_t x = next(&c);
        if (is_seq(x)) {
            push_cursor(&w, c);
            c = (cursor_t){ x, 0 };
            buf_str(b, rt_is_tuple(x) ? "#(" : "(");
            continue;
        }
        render_atom(b, x, 0);
        if (!at_end(c)) buf_str(b, " ");
    }
    rt_work_free(&w);
}

void rt_render(rt_buf_t *b, rt_value_t v, int raw) {
    if (is_seq(v)) render_seq(b, v);
    else           render_atom(b, v, raw);
}

// In one write, so a line from another island can't come between the
// value and its newline (D256).
rt_value_t rt_pprint(rt_value_t v) {
    rt_buf_t b = { 0 };
    rt_render(&b, v, 0);
    rt_buf_add(&b, "\n", 1);
    fwrite(b.bytes, 1, b.len, stdout);
    rt_buf_free(&b);
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
    if (rt_is_array(a) && rt_is_array(b)) {     // of one kind and length, an element as eq? has it
        uint64_t n = rt_array_len(a);
        if ((rt_box(a)[0] & RT_BOX_TYPE_MASK) != (rt_box(b)[0] & RT_BOX_TYPE_MASK) || rt_array_len(b) != n) return 0;
        if (rt_is_box(a, RT_BOX_INTS)) return memcmp(rt_array_data(a), rt_array_data(b), 8 * n) == 0;
        for (uint64_t i = 0; i < n; i++) if (rt_array_float(a, i) != rt_array_float(b, i)) return 0;
        return 1;
    }
    return 0;
}

// Two lists, or two tuples of one length: what equal() goes into.
static int same_shape(rt_value_t a, rt_value_t b) {
    return (rt_is_cons(a) && rt_is_cons(b)) || (rt_is_tuple(a) && rt_is_tuple(b) && rt_tuple_len(a) == rt_tuple_len(b));
}

// Structural: the two values element by element, going down into a pair
// of lists or tuples with the pair around them on the work stack, so the
// stack grows only as deep as the values nest. Two lists whose rests are
// the same are equal from there.
static int equal(rt_value_t a, rt_value_t b) {
    if (a == b || !same_shape(a, b)) return equal_atoms(a, b);
    rt_work_t w;
    rt_work_init(&w);
    cursor_t ca = { a, 0 }, cb = { b, 0 };
    int same = 1;
    for (;;) {
        if (at_end(ca) || at_end(cb) || (!rt_is_tuple(ca.seq) && ca.seq == cb.seq)) {
            if (!rt_is_tuple(ca.seq) && ca.seq != cb.seq) {     // one list is longer
                same = 0;
                break;
            }
            if (!w.n) break;
            cb = pop_cursor(&w);
            ca = pop_cursor(&w);
            continue;
        }
        rt_value_t x = next(&ca), y = next(&cb);
        if (x != y && same_shape(x, y)) {
            push_cursor(&w, ca);
            push_cursor(&w, cb);
            ca = (cursor_t){ x, 0 };
            cb = (cursor_t){ y, 0 };
            continue;
        }
        if (!equal_atoms(x, y)) {
            same = 0;
            break;
        }
    }
    rt_work_free(&w);
    return same;
}

rt_value_t rt_equal(rt_value_t a, rt_value_t b) {
    return equal(a, b) ? RT_TRUE : RT_FALSE;
}

// --- hashing ------------------------------------------------------------------

// A value is hashed as equal() walks it, word by word through splitmix64's
// mixer: each atom as its kind and what it holds, a symbol or a function
// by its name, never by an address, which differs from run to run and
// moves when the collector does; a list as its elements and an end, and a
// tuple as its length and its elements. That's a code no two values that
// aren't eq? share, functions of one name aside, so two of them hash
// alike only by chance, about once in 2^62. It isn't seeded, so it's for digests, not for a table whose
// keys come from someone who'd want them to collide (D209).
enum { H_INT = 1, H_FLOAT, H_STRING, H_SYMBOL, H_PID, H_FUNCTION, H_LIST, H_END, H_TUPLE, H_OTHER, H_FLOATS, H_INTS };

static uint64_t mix(uint64_t h, uint64_t w) {
    uint64_t x = (h ^ w) + 0x9e3779b97f4a7c15;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
    x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
    return x ^ (x >> 31);
}

// Byte by byte into words, so the hash doesn't depend on the machine's
// byte order.
static uint64_t mix_bytes(uint64_t h, const char *bytes, uint64_t len) {
    const unsigned char *s = (const unsigned char *)bytes;
    h = mix(h, len);
    for (uint64_t i = 0; i < len; i += 8) {
        uint64_t w = 0;
        for (uint64_t j = 0; j < 8 && i + j < len; j++) w |= (uint64_t)s[i + j] << (8 * j);
        h = mix(h, w);
    }
    return h;
}

// A float's bits, as eq? sees it.
static uint64_t float_bits(double d) {
    uint64_t bits = 0x7ff8000000000000;         // every NaN alike
    if (d == 0) d = 0;                          // -0.0 is eq? to 0.0
    if (d == d) memcpy(&bits, &d, sizeof bits);
    return bits;
}

static uint64_t hash_atom(uint64_t h, rt_value_t v) {
    if (!(v & RT_TAG_INT_MASK)) return mix(mix(h, H_INT), (uint64_t)rt_int_value(v));
    if (v == RT_NIL)            return mix(mix(h, H_LIST), H_END);
    if (rt_is_float(v))         return mix(mix(h, H_FLOAT), float_bits(rt_float_value(v)));
    if (rt_is_array(v)) {                       // its kind, its length and its elements
        int      ints = rt_is_box(v, RT_BOX_INTS);
        uint64_t n    = rt_array_len(v);
        h = mix(mix(h, ints ? H_INTS : H_FLOATS), n);
        for (uint64_t i = 0; i < n; i++) h = mix(h, ints ? rt_array_data(v)[i] : float_bits(rt_array_float(v, i)));
        return h;
    }
    if (rt_is_string(v)) return mix_bytes(mix(h, H_STRING), rt_string_bytes(v), rt_string_len(v));
    if ((v & RT_TAG_MASK) == RT_TAG_SYMBOL) {
        const char *name = rt_symbol_name(v >> RT_SYMBOL_SHIFT);
        return name ? mix_bytes(mix(h, H_SYMBOL), name, strlen(name)) : mix(mix(h, H_SYMBOL), v);
    }
    if ((v & RT_TAG_MASK) == RT_TAG_PID) return mix(mix(h, H_PID), v >> RT_PID_SHIFT);
    if (rt_is_closure(v)) {                     // eq? only to itself, so its name will do
        const char *name = (const char *)rt_box(v)[3];
        return mix_bytes(mix(h, H_FUNCTION), name, strlen(name));
    }
    return mix(mix(h, H_OTHER), v);
}

static uint64_t hash_open(uint64_t h, rt_value_t seq) {
    return rt_is_tuple(seq) ? mix(mix(h, H_TUPLE), rt_tuple_len(seq)) : mix(h, H_LIST);
}

rt_value_t rt_hash(rt_value_t v, const char *site) {
    (void)site;
    uint64_t h = 0;
    if (!is_seq(v)) return rt_int((int64_t)(hash_atom(h, v) >> 2));
    rt_work_t w;
    rt_work_init(&w);
    cursor_t c = { v, 0 };
    h = hash_open(h, v);
    for (;;) {
        if (at_end(c)) {
            if (!rt_is_tuple(c.seq)) h = mix(h, H_END);
            if (!w.n) break;
            c = pop_cursor(&w);
            continue;
        }
        rt_value_t x = next(&c);
        if (is_seq(x)) {
            push_cursor(&w, c);
            c = (cursor_t){ x, 0 };
            h = hash_open(h, x);
            continue;
        }
        h = hash_atom(h, x);
    }
    rt_work_free(&w);
    return rt_int((int64_t)(h >> 2));
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
    [RT_FAULT_NO_MATCH]   = { "no pattern matched: ", 1 },
    [RT_FAULT_NOT_TUPLE]  = { "not a tuple: ", 1 },
    [RT_FAULT_NOT_ACTOR]  = { "not an actor: ", 1 },
    [RT_FAULT_NOT_TASK]   = { "not a task: ", 1 },
    [RT_FAULT_NOT_ARRAY]  = { "not an array: ", 1 },
};

void rt_fault(uint64_t fault, rt_value_t value, const char *site) {
    if (fault < 1 || fault > RT_FAULT_COUNT) abort();
    if (!faults[fault].shows) value = RT_NIL;   // whatever the caller had in the register

    rt_buf_t b = { 0 };
    buf_str(&b, faults[fault].what);
    if (faults[fault].shows) rt_render(&b, value, 0);
    char who[64];
    rt_pid_text(who, sizeof who, rt_current->pid);
    fflush(stdout);                             // so a fault lands after the output before it
    if (rt_current->pid == RT_ROOT) fprintf(stderr, "fault: %.*s (%s)\n", (int)b.len, b.bytes, site);
    else                            fprintf(stderr, "fault in %s: %.*s (%s)\n", who, (int)b.len, b.bytes, site);
    rt_buf_free(&b);

    // The reason, (kind value site), goes on this stack: rt_end copies it
    // out, and the heap may be what ran out. The string is a box like any
    // other, padded as the copier expects.
    size_t     len = strlen(site);
    uint64_t   space[len / 8 + 6];
    uint64_t  *str = (uint64_t *)(((uintptr_t)space + 15) & ~(uintptr_t)15);
    str[0] = rt_string_header(site, len);
    memcpy(str + 1, site, len + 1);
    _Alignas(16) rt_value_t cells[6] = {
        rt_symbol(RT_SYM_FAULTS - 1 + fault), (rt_value_t)&cells[2] | RT_TAG_LIST,
        value,                                (rt_value_t)&cells[4] | RT_TAG_LIST,
        (rt_value_t)str | RT_TAG_BOXED,       RT_NIL,
    };
    rt_end(0, (rt_value_t)cells | RT_TAG_LIST, 1);
}

int64_t rt_need_int(rt_value_t v, const char *site) {
    if (v & RT_TAG_INT_MASK) rt_fault(RT_FAULT_NOT_INT, v, site);
    return rt_int_value(v);
}

// --- the heap -----------------------------------------------------------------

void *rt_alloc(size_t bytes, const char *site) {
    bytes = (bytes + 15) & ~(size_t)15;
    if (rt_current->heap_limit - rt_current->heap_ptr < bytes) rt_heap_grow(bytes, site);
    void *p = (void *)rt_current->heap_ptr;
    rt_current->heap_ptr += bytes;
    return p;
}

void *rt_heap_alloc(void *site, size_t bytes) {
    return rt_alloc(bytes, site);
}

// A string this long is a buffer of its own, off the heap, which no
// collection copies and a send shares (D253).
rt_value_t rt_new_string(const char *bytes, size_t len, const char *site) {
    if (len >= RT_SHARED_MIN) return rt_new_shared_string(bytes, len, site);
    uint64_t *box = rt_alloc(8 + len + 1, site);
    box[0] = bytes ? rt_string_header(bytes, len) : (uint64_t)len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
    if (len && bytes) memcpy(box + 1, bytes, len);
    ((char *)(box + 1))[len] = '\0';
    return (rt_value_t)box | RT_TAG_BOXED;
}

// A word at a time where the bytes are aligned for it, which a box's are.
uint64_t rt_string_header(const char *bytes, size_t len) {
    uint64_t high = 0;
    size_t   i    = 0;
    if (((uintptr_t)bytes & 7) == 0) {
        for (; i + 8 <= len; i += 8) {
            uint64_t w;
            memcpy(&w, bytes + i, 8);
            high |= w;
        }
        high &= 0x8080808080808080u;
    }
    for (; i < len; i++) high |= (unsigned char)bytes[i] & 0x80;
    return (uint64_t)len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING | (high ? 0 : RT_BOX_ASCII);
}

void rt_string_mark(rt_value_t s) {
    uint64_t *box = (uint64_t *)rt_box(s);
    box[0] = rt_string_header(rt_string_bytes(s), rt_string_len(s)) | (box[0] & RT_BOX_SHARED);
}

// The control characters (Unicode's: C0, DEL and C1) as escapes, as the
// reader takes them, those without a letter in \u{hex}; stray bytes as
// they are, since no escape makes one.
void rt_quote(rt_buf_t *b, const char *p, size_t len) {
    static const char digits[] = "0123456789abcdef";
    size_t from = 0;
    buf_str(b, "\"");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i], n = 1;
        if (c == 0xc2 && i + 1 < len && (unsigned char)p[i + 1] <= 0x9f && (unsigned char)p[i + 1] >= 0x80) {
            c = (unsigned char)p[i + 1];                // U+0080 to U+009F, in two bytes
            n = 2;
        }
        const char *e = c == '"' ? "\\\"" : c == '\\' ? "\\\\" : c == '\n' ? "\\n" : c == '\t' ? "\\t"
                      : c == '\r' ? "\\r" : c == 0x1b ? "\\e" : NULL;
        char u[8];
        if (!e && (c < 0x20 || c == 0x7f || n == 2)) {
            int k = 0;
            memcpy(u, "\\u{", 3);
            if (c >= 16) u[3 + k++] = digits[c >> 4];
            u[3 + k++] = digits[c & 15];
            u[3 + k++] = '}';
            u[3 + k]   = '\0';
            e = u;
        }
        if (!e) continue;
        rt_buf_add(b, p + from, i - from);
        buf_str(b, e);
        i   += n - 1;
        from = i + 1;
    }
    rt_buf_add(b, p + from, len - from);
    buf_str(b, "\"");
}

rt_value_t rt_new_float(double d, const char *site) {
    uint64_t *box = rt_alloc(16, site);
    box[0] = (uint64_t)sizeof d << RT_BOX_SIZE_SHIFT | RT_BOX_FLOAT;
    memcpy(box + 1, &d, sizeof d);
    return (rt_value_t)box | RT_TAG_BOXED;
}
