// json.c -- JSON (D154, D160, D161). Two parts. The validator takes text a
// piece at a time, as a device reads it, and says where a value ends, or
// where the text goes wrong: at the first byte that no valid text could
// have there, which is the length of its longest valid prefix. The builder
// makes the slight value of text the validator has passed, so it never
// has to check anything. json/parse is the two in turn; a device runs the
// validator as bytes come, and the builder once a value is whole.
//
// JSON in slight: null is :null, true and false are #true and #false, a
// number is an integer if it's written as one and fits in 63 bits and a
// float otherwise, a string is its bytes, an array is a list, and an
// object is (:object (key value) ...), its keys in order, duplicates kept.
// Strings are bytes, so bytes that aren't UTF-8 pass through, and a \u
// escape of a lone surrogate, which has no UTF-8, becomes U+FFFD.

#include "rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    J_VALUE,                    // a value comes next
    J_FIRST,                    // after [: a value, or ]
    J_KEY,                      // after {: a key, or }
    J_NEXT_KEY,                 // after a , in an object: a key
    J_COLON,                    // after a key
    J_AFTER,                    // after a value inside a container: , or the close
    J_STRING, J_ESCAPE, J_HEX,
    J_MINUS, J_ZERO, J_INT, J_POINT, J_FRAC, J_E, J_E_SIGN, J_EXP,
    J_WORD,                     // in true, false or null
};

static int is_digit(int c) { return c >= '0' && c <= '9'; }

void rt_json_reset(rt_json_t *j) {
    j->depth = 0;
    j->state = J_VALUE;
}

void rt_json_free(rt_json_t *j) {
    free(j->open);
    j->open = NULL;
    j->cap  = 0;
    rt_json_reset(j);
}

static void open_container(rt_json_t *j, unsigned char c) {
    if (j->depth == j->cap) {
        j->cap  = j->cap ? j->cap * 2 : 64;
        j->open = rt_must(realloc(j->open, j->cap));
    }
    j->open[j->depth++] = c;
}

int rt_json_scan(rt_json_t *j, const char *bytes, size_t len, int eof, size_t *used) {
    size_t i = 0;
    for (; i < len; i++) {
        int c = (unsigned char)bytes[i], ended = 0;   // ended: a value ends after byte i
        switch (j->state) {
        case J_FIRST:
            if (c == ']') {
                j->depth--;
                ended = 1;
                break;
            }
            // fall through
        case J_VALUE:
            if (rt_is_space(c))                         break;
            if (c == '{')                            open_container(j, '{'), j->state = J_KEY;
            else if (c == '[')                       open_container(j, '['), j->state = J_FIRST;
            else if (c == '"')                       j->key = 0, j->state = J_STRING;
            else if (c == '-')                       j->state = J_MINUS;
            else if (c == '0')                       j->state = J_ZERO;
            else if (is_digit(c))                    j->state = J_INT;
            else if (c == 't')                       j->word = "rue", j->state = J_WORD;
            else if (c == 'f')                       j->word = "alse", j->state = J_WORD;
            else if (c == 'n')                       j->word = "ull", j->state = J_WORD;
            else                                     goto bad;
            break;
        case J_KEY:
            if (c == '}') {
                j->depth--;
                ended = 1;
                break;
            }
            // fall through
        case J_NEXT_KEY:
            if (rt_is_space(c))                         break;
            if (c != '"')                            goto bad;
            j->key   = 1;
            j->state = J_STRING;
            break;
        case J_COLON:
            if (rt_is_space(c))                         break;
            if (c != ':')                            goto bad;
            j->state = J_VALUE;
            break;
        case J_AFTER:
            if (rt_is_space(c))                         break;
            if (c == ',')                            j->state = j->open[j->depth - 1] == '[' ? J_VALUE : J_NEXT_KEY;
            else if (c == (j->open[j->depth - 1] == '[' ? ']' : '}')) j->depth--, ended = 1;
            else                                     goto bad;
            break;
        case J_STRING:
            if (c == '"') {
                if (j->key) j->state = J_COLON;
                else        ended = 1;
            } else if (c == '\\') {
                j->state = J_ESCAPE;
            } else if (c < 0x20) {
                goto bad;
            }
            break;
        case J_ESCAPE:
            if (c == 'u')                            j->hex = 4, j->state = J_HEX;
            else if (strchr("\"\\/bfnrt", c) && c)   j->state = J_STRING;
            else                                     goto bad;
            break;
        case J_HEX:
            if (rt_hex_value(c) < 0)                    goto bad;
            if (--j->hex == 0)                       j->state = J_STRING;
            break;
        case J_MINUS:
            if (c == '0')                            j->state = J_ZERO;
            else if (is_digit(c))                    j->state = J_INT;
            else                                     goto bad;
            break;
        case J_ZERO:
        case J_INT:
        case J_FRAC:
        case J_EXP:
            if (is_digit(c) && j->state != J_ZERO)   break;
            if (c == '.' && (j->state == J_ZERO || j->state == J_INT)) j->state = J_POINT;
            else if ((c == 'e' || c == 'E') && j->state != J_EXP) j->state = J_E;
            else if (j->depth == 0) {                // the number ended before byte i
                *used = i;
                return RT_SCAN_DONE;
            } else {
                j->state = J_AFTER;
                i--;                                 // byte i is the container's again
            }
            break;
        case J_POINT:
            if (!is_digit(c))                        goto bad;
            j->state = J_FRAC;
            break;
        case J_E:
            if (c == '+' || c == '-')                j->state = J_E_SIGN;
            else if (is_digit(c))                    j->state = J_EXP;
            else                                     goto bad;
            break;
        case J_E_SIGN:
            if (!is_digit(c))                        goto bad;
            j->state = J_EXP;
            break;
        case J_WORD:
            if (c != *j->word)                       goto bad;
            if (*++j->word == '\0')                  ended = 1;
            break;
        }
        if (ended) {
            if (j->depth == 0) {
                *used = i + 1;
                return RT_SCAN_DONE;
            }
            j->state = J_AFTER;
        }
    }
    if (!eof) return RT_SCAN_MORE;
    if (j->depth == 0 && (j->state == J_ZERO || j->state == J_INT || j->state == J_FRAC || j->state == J_EXP)) {
        *used = len;
        return RT_SCAN_DONE;
    }
    if (j->depth == 0 && j->state == J_VALUE) return RT_SCAN_NONE;
bad:
    *used = i;
    return RT_SCAN_BAD;
}

// --- the builder --------------------------------------------------------------

static rt_value_t *cell(rt_build_alloc_t alloc, void *cx, rt_value_t car) {
    rt_value_t *c = alloc(cx, 16);
    if (c) {
        c[0] = car;
        c[1] = RT_NIL;
    }
    return c;
}

static int is_high(uint32_t u) { return u >= 0xd800 && u <= 0xdbff; }
static int is_low(uint32_t u)  { return u >= 0xdc00 && u <= 0xdfff; }

static uint32_t hex4(const char *p) {
    uint32_t u = 0;
    for (int k = 0; k < 4; k++) u = u << 4 | (uint32_t)rt_hex_value((unsigned char)p[k]);
    return u;
}

// The string whose bytes, escapes and all, start at p: decoded into out if
// it isn't NULL; its decoded length either way. p is past the opening
// quote; *end comes back past the closing one.
static size_t decode(const char *p, char *out, const char **end) {
    size_t n = 0;
    while (*p != '"') {
        if (*p != '\\') {
            if (out) out[n] = *p;
            n++, p++;
            continue;
        }
        char e = p[1];
        if (e != 'u') {
            char c = e == 'b' ? '\b' : e == 'f' ? '\f' : e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e;
            if (out) out[n] = c;
            n++, p += 2;
            continue;
        }
        uint32_t u = hex4(p + 2);
        p += 6;
        if (is_high(u) && p[0] == '\\' && p[1] == 'u' && is_low(hex4(p + 2))) {
            u  = 0x10000 + ((u - 0xd800) << 10) + (hex4(p + 2) - 0xdc00);
            p += 6;
        } else if (is_high(u) || is_low(u)) {
            u = 0xfffd;
        }
        unsigned char b[4];
        size_t        k = u < 0x80 ? 1 : u < 0x800 ? 2 : u < 0x10000 ? 3 : 4;
        if (k == 1) {
            b[0] = (unsigned char)u;
        } else {
            for (size_t m = k - 1; m > 0; m--, u >>= 6) b[m] = (unsigned char)(0x80 | (u & 0x3f));
            b[0] = (unsigned char)((k == 2 ? 0xc0 : k == 3 ? 0xe0 : 0xf0) | u);
        }
        if (out) memcpy(out + n, b, k);
        n += k;
    }
    *end = p + 1;
    return n;
}

static rt_value_t string(const char **p, rt_build_alloc_t alloc, void *cx) {
    const char *end;
    size_t      n   = decode(*p + 1, NULL, &end);
    uint64_t   *box = alloc(cx, 8 + n + 1);
    if (!box) return 0;
    decode(*p + 1, (char *)(box + 1), &end);
    ((char *)(box + 1))[n] = '\0';
    box[0] = rt_string_header((const char *)(box + 1), n);
    *p = end;
    return (rt_value_t)box | RT_TAG_BOXED;
}

// An integer if it's written as one and fits in 63 bits; otherwise the
// nearest double, as strtod rounds (inf if it's too big for one, as in
// JavaScript and Python). 0 if alloc gave out. A number is the one value
// that can end the text, so it's the one that has to look for the end.
static int number(const char **p, const char *end, rt_build_alloc_t alloc, void *cx, rt_value_t *out) {
    const char *s = *p, *q = s + (*s == '-');
    int         integral = 1;
    for (; q < end && (is_digit(*q) || *q == '.' || *q == 'e' || *q == 'E' || *q == '+' || *q == '-'); q++) {
        if (!is_digit(*q)) integral = 0;
    }
    *p = q;
    if (integral) {                             // as rt_string_to_int: negative has the room
        const int64_t min = -((int64_t)1 << 62);
        int64_t       n   = 0;
        int           fits = 1;
        for (const char *d = s + (*s == '-'); d < q && fits; d++) {
            int digit = *d - '0';
            if (n < (min + digit) / 10) fits = 0;
            else                        n = n * 10 - digit;
        }
        if (fits && (*s == '-' || n != min)) {
            *out = rt_int(*s == '-' ? n : -n);
            return 1;
        }
    }
    size_t len = (size_t)(q - s);
    char   local[64], *text = len < sizeof local ? local : rt_must(malloc(len + 1));
    memcpy(text, s, len);
    text[len] = '\0';
    double d = strtod(text, NULL);
    if (text != local) free(text);
    uint64_t *box = alloc(cx, 16);
    if (!box) return 0;
    box[0] = (uint64_t)sizeof d << RT_BOX_SIZE_SHIFT | RT_BOX_FLOAT;
    memcpy(box + 1, &d, sizeof d);
    *out = (rt_value_t)box | RT_TAG_BOXED;
    return 1;
}

// A container being built: its list so far, its last cell (whose cdr the
// next element goes in), and in an object the key waiting for its value.
// The frames are allocated as the value is, so they count against the
// same limit: a value nested a million deep takes more to build than to
// hold (D205).
typedef struct {
    rt_value_t  head, key;
    rt_value_t *last;
    int         object;
} frame_t;

int rt_json_build(const char *text, size_t len, rt_build_alloc_t alloc, void *cx, rt_value_t *out) {
    frame_t    *frames = NULL;
    size_t      depth = 0, cap = 0;
    const char *p = text;
    rt_value_t  v;
    for (;;) {
        while (rt_is_space(*p) || *p == ',' || *p == ':') p++;     // separators, in valid text
        if (*p == '[' || *p == '{') {
            if (depth == cap) {
                frame_t *more = alloc(cx, (cap ? cap * 2 : 16) * sizeof *frames);
                if (!more) goto too_big;
                if (depth) memcpy(more, frames, depth * sizeof *frames);
                frames = more;
                cap    = cap ? cap * 2 : 16;
            }
            frame_t *f = &frames[depth++];
            f->object  = *p++ == '{';
            f->key     = 0;
            f->last    = NULL;
            f->head    = RT_NIL;
            if (f->object) {
                if (!(f->last = cell(alloc, cx, rt_symbol(RT_SYM_JSON + RT_JSON_OBJECT)))) goto too_big;
                f->head = (rt_value_t)f->last | RT_TAG_LIST;
            }
            continue;
        }
        if (*p == ']' || *p == '}') {
            v = frames[--depth].head;
            p++;
        } else if (*p == '"') {
            if (!(v = string(&p, alloc, cx))) goto too_big;
            frame_t *f = depth ? &frames[depth - 1] : NULL;
            if (f && f->object && !f->key) {
                f->key = v;
                continue;
            }
        } else if (*p == 't' || *p == 'f' || *p == 'n') {
            v  = *p == 't' ? RT_TRUE : *p == 'f' ? RT_FALSE : rt_symbol(RT_SYM_JSON + RT_JSON_NULL);
            p += *p == 'f' ? 5 : 4;
        } else if (!number(&p, text + len, alloc, cx, &v)) {
            goto too_big;
        }
        if (depth == 0) break;
        frame_t *f = &frames[depth - 1];
        if (f->object) {                        // (key value)
            rt_value_t *value = cell(alloc, cx, v), *pair = value ? cell(alloc, cx, f->key) : NULL;
            if (!pair) goto too_big;
            pair[1] = (rt_value_t)value | RT_TAG_LIST;
            v       = (rt_value_t)pair | RT_TAG_LIST;
            f->key  = 0;
        }
        rt_value_t *c = cell(alloc, cx, v);
        if (!c) goto too_big;
        if (f->last) f->last[1] = (rt_value_t)c | RT_TAG_LIST;
        else         f->head    = (rt_value_t)c | RT_TAG_LIST;
        f->last = c;
    }
    *out = v;
    return 0;
too_big:
    return -1;
}

// --- the builtins -------------------------------------------------------------

// (:ok value), or (:error (:bad-json at)), at being where the text went
// wrong: a byte no valid text could have there, or the end.
rt_value_t rt_json_parse(rt_value_t s, const char *site) {
    if (!rt_is_string(s)) rt_fault(RT_FAULT_NOT_STRING, s, site);
    const char *text = rt_string_bytes(s);
    size_t      len  = rt_string_len(s), used = 0;
    rt_json_t   j    = { 0 };
    int         r    = rt_json_scan(&j, text, len, 1, &used);
    rt_json_free(&j);
    if (r == RT_SCAN_DONE) {
        size_t rest = used;
        while (rest < len && rt_is_space((unsigned char)text[rest])) rest++;
        if (rest < len) r = RT_SCAN_BAD, used = rest;
    }
    if (r == RT_SCAN_NONE) r = RT_SCAN_BAD, used = len;
    rt_value_t *outer = rt_alloc(32, site), v;
    if (r == RT_SCAN_DONE) {
        rt_json_build(text, used, rt_heap_alloc, (void *)site, &v);     // the heap faults before it gives out
        outer[0] = rt_symbol(RT_SYM_OK);
    } else {
        rt_value_t *why = rt_alloc(32, site);
        why[0] = rt_symbol(RT_SYM_ERRS + RT_ERR_BADJSON);
        why[1] = (rt_value_t)(why + 2) | RT_TAG_LIST;
        why[2] = rt_int((int64_t)used);
        why[3] = RT_NIL;
        v      = (rt_value_t)why | RT_TAG_LIST;
        outer[0] = rt_symbol(RT_SYM_ERROR);
    }
    outer[1] = (rt_value_t)(outer + 2) | RT_TAG_LIST;
    outer[2] = v;
    outer[3] = RT_NIL;
    return (rt_value_t)outer | RT_TAG_LIST;
}

// The printer walks with a work stack (D147): for each container it's in,
// the rest of its list and whether it's an object, and whether anything
// has been written in it yet; or for a tuple (an array too), the tuple,
// and the index of its next element above those flags.
#define IN_ARRAY  0
#define IN_OBJECT 1
#define WRITTEN   2
#define IN_TUPLE  4
#define INDEX     3                         // the shift of a tuple's index

static void put(rt_buf_t *b, const char *s) {
    rt_buf_add(b, s, strlen(s));
}

// len bytes at p, as a JSON string.
static void put_bytes(rt_buf_t *b, const unsigned char *p, size_t len) {
    size_t from = 0;
    put(b, "\"");
    for (size_t i = 0; i < len; i++) {
        const char *e = p[i] == '"' ? "\\\"" : p[i] == '\\' ? "\\\\" : p[i] == '\b' ? "\\b" : p[i] == '\f' ? "\\f"
                      : p[i] == '\n' ? "\\n" : p[i] == '\r' ? "\\r" : p[i] == '\t' ? "\\t" : NULL;
        char u[8];
        if (!e && p[i] < 0x20) {
            static const char digits[] = "0123456789abcdef";
            memcpy(u, "\\u00", 4);
            u[4] = digits[p[i] >> 4];
            u[5] = digits[p[i] & 15];
            u[6] = '\0';
            e    = u;
        }
        if (!e) continue;
        rt_buf_add(b, (const char *)p + from, i - from);
        put(b, e);
        from = i + 1;
    }
    rt_buf_add(b, (const char *)p + from, len - from);
    put(b, "\"");
}

static void put_string(rt_buf_t *b, rt_value_t s) {
    put_bytes(b, (const unsigned char *)rt_string_bytes(s), rt_string_len(s));
}

__attribute__((noreturn))
static void not_json(rt_buf_t *b, rt_work_t *w, rt_value_t v, const char *site) {
    rt_buf_free(b);
    rt_work_free(w);
    rt_fault(RT_FAULT_NOT_JSON, v, site);
}

// v as str writes it, as a JSON string.
static void put_rendered(rt_buf_t *b, rt_value_t v) {
    rt_buf_t r = { 0 };
    rt_render(&r, v, 1);
    put_bytes(b, (const unsigned char *)r.bytes, r.len);
    rt_buf_free(&r);
}

// An array of numbers as a JSON array (D253); a float JSON can't hold,
// NaN or infinite, as a string if lenient, and otherwise 0, for a fault.
static int put_array(rt_buf_t *b, rt_value_t v, int lenient) {
    put(b, "[");
    for (uint64_t i = 0; i < rt_array_len(v); i++) {
        if (i) put(b, ",");
        if (rt_is_box(v, RT_BOX_INTS)) {
            char num[24];
            snprintf(num, sizeof num, "%" PRId64, (int64_t)rt_array_data(v)[i]);
            put(b, num);
            continue;
        }
        double d = rt_array_float(v, i);
        if (isfinite(d)) {
            rt_render_float(b, d);
        } else if (lenient) {
            put(b, isnan(d) ? "\"nan\"" : d < 0 ? "\"-inf\"" : "\"inf\"");
        } else {
            return 0;
        }
    }
    put(b, "]");
    return 1;
}

// Compact JSON, on one line (D161). What JSON can't hold faults; or, if
// lenient, as log has it (D254), is written as a string, as str writes it.
static rt_value_t print_json(rt_value_t v, int lenient, const char *site) {
    rt_buf_t  b = { 0 };
    rt_work_t w;
    rt_work_init(&w);
    for (;;) {
        if (rt_is_cons(v)) {
            int object = rt_car(v) == rt_symbol(RT_SYM_JSON + RT_JSON_OBJECT);
            put(&b, object ? "{" : "[");
            rt_work_push(&w, object ? rt_cdr(v) : v);
            rt_work_push(&w, object ? IN_OBJECT : IN_ARRAY);
        } else if (rt_is_tuple(v)) {
            put(&b, "[");
            rt_work_push(&w, v);
            rt_work_push(&w, IN_TUPLE);
        } else if (v == RT_NIL) {
            put(&b, "[]");
        } else if (v == RT_TRUE || v == RT_FALSE) {
            put(&b, v == RT_TRUE ? "true" : "false");
        } else if (v == rt_symbol(RT_SYM_JSON + RT_JSON_NULL)) {
            put(&b, "null");
        } else if (rt_is_string(v)) {
            put_string(&b, v);
        } else if (!(v & RT_TAG_INT_MASK) || (rt_is_float(v) && isfinite(rt_float_value(v)))) {
            rt_render(&b, v, 0);
        } else if (rt_is_array(v)) {
            if (!put_array(&b, v, lenient)) not_json(&b, &w, v, site);
        } else if (lenient) {
            put_rendered(&b, v);
        } else {
            not_json(&b, &w, v, site);
        }
        // on to the next element of the innermost container not yet done
        for (;;) {
            if (w.n == 0) {
                rt_work_free(&w);
                rt_value_t s = rt_new_string(b.bytes, b.len, site);
                rt_buf_free(&b);
                return s;
            }
            rt_value_t rest = w.items[w.n - 2], how = w.items[w.n - 1];
            if (how & IN_TUPLE) {
                uint64_t i = how >> INDEX;
                if (i == rt_tuple_len(rest)) {
                    put(&b, "]");
                    w.n -= 2;
                    continue;
                }
                if (how & WRITTEN) put(&b, ",");
                w.items[w.n - 1] = (i + 1) << INDEX | IN_TUPLE | WRITTEN;
                v = rt_tuple_get(rest, i);
                break;
            }
            if (rest == RT_NIL) {
                put(&b, how & IN_OBJECT ? "}" : "]");
                w.n -= 2;
                continue;
            }
            if (!rt_is_cons(rest) && !lenient) not_json(&b, &w, rest, site);
            if (how & WRITTEN) put(&b, ",");
            if (!rt_is_cons(rest)) {            // a list's improper end, lenient: the last element
                put_rendered(&b, rest);
                w.items[w.n - 2] = RT_NIL;
                w.items[w.n - 1] = how | WRITTEN;
                continue;
            }
            w.items[w.n - 2] = rt_cdr(rest);
            w.items[w.n - 1] = how | WRITTEN;
            v = rt_car(rest);
            if (how & IN_OBJECT) {              // (key value)
                if (!rt_is_cons(v) || !rt_is_string(rt_car(v)) || !rt_is_cons(rt_cdr(v)) || rt_cdr(rt_cdr(v)) != RT_NIL) {
                    if (!lenient) not_json(&b, &w, v, site);
                    put_rendered(&b, v);        // lenient: the entry as its key, its value null
                    put(&b, ":null");
                    continue;
                }
                put_string(&b, rt_car(v));
                put(&b, ":");
                v = rt_car(rt_cdr(v));
            }
            break;
        }
    }
}

rt_value_t rt_json_print(rt_value_t v, const char *site) {
    return print_json(v, 0, site);
}

// --- log (D254) ---------------------------------------------------------------
//
// One line of JSON on stderr: {"time":...,"level":...,"pid":...,"msg":...}
// and the fields, what log collectors read, and slight with :json. A
// field's value is written as json/print writes it, but anything JSON
// can't hold (a pid, a function, a keyword but :null, a float that isn't
// finite), at any depth, is a string, as str writes it: logging an error,
// (:error (:enoent "x")), never faults. Lines
// below rt_log_level aren't written. stdout is flushed first, so the two
// come in the order they were written.

int rt_log_level = 1;                       // :info

static const char *const level_names[] = { "debug", "info", "warn", "error" };

static int level_of(rt_value_t level, const char *site) {
    if (level == rt_symbol(RT_SYM_ERROR)) return 3;
    for (int i = 0; i < RT_LOG_COUNT; i++) {
        if (level == rt_symbol(RT_SYM_LOG + (uint64_t)i)) return i;
    }
    rt_fault(RT_FAULT_RANGE, level, site);
}

// The day of the days since 1970 (Hinnant's civil_from_days, as
// lib/time.slight has it), floor being their floored division.
static void civil(int64_t days, int64_t *y, int *m, int *d) {
    int64_t z   = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp  = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

// The time as time/iso writes it, for a year from 0 to 9999.
static void put_time(rt_buf_t *b, int64_t us) {
    int64_t day  = us / 86400000000 - (us % 86400000000 < 0);
    int64_t rest = us - day * 86400000000, y;
    int     m, d;
    civil(day, &y, &m, &d);
    char s[40];
    snprintf(s, sizeof s, "%04" PRId64 "-%02d-%02dT%02d:%02d:%02d.%06dZ", y, m, d, (int)(rest / 3600000000),
             (int)(rest / 60000000 % 60), (int)(rest / 1000000 % 60), (int)(rest % 1000000));
    put(b, s);
}


rt_value_t rt_log_line(rt_value_t level, rt_value_t msg, rt_value_t fields, const char *site) {
    int l = level_of(level, site);
    if (!rt_is_string(msg)) rt_fault(RT_FAULT_NOT_STRING, msg, site);
    // each field (name value), a name a string or a keyword, before
    // anything's written
    for (rt_value_t f = fields; f != RT_NIL; f = rt_cdr(f)) {
        if (!rt_is_cons(f)) rt_fault(RT_FAULT_NOT_LIST, fields, site);
        rt_value_t x = rt_car(f);
        if (!rt_is_cons(x) || !rt_is_cons(rt_cdr(x)) || rt_cdr(rt_cdr(x)) != RT_NIL
            || !(rt_is_string(rt_car(x)) || rt_is_symbol(rt_car(x)))) {
            rt_fault(RT_FAULT_RANGE, x, site);
        }
    }
    if (l < rt_log_level) return RT_NIL;
    rt_buf_t b = { 0 };
    char     pid[64];
    put(&b, "{\"time\":\"");
    put_time(&b, rt_int_value(rt_clock_now(site)));
    put(&b, "\",\"level\":\"");
    put(&b, level_names[l]);
    uint64_t k = rt_pid_island(rt_current->pid);   // as a pid prints, island 0's without it (D256)
    if (k) snprintf(pid, sizeof pid, "\",\"pid\":%" PRIu64 ",\"island\":%" PRIu64 ",\"msg\":", rt_pid_number(rt_current->pid), k);
    else   snprintf(pid, sizeof pid, "\",\"pid\":%" PRIu64 ",\"msg\":", rt_pid_number(rt_current->pid));
    put(&b, pid);
    put_string(&b, msg);
    for (rt_value_t f = fields; f != RT_NIL; f = rt_cdr(f)) {
        rt_value_t name = rt_car(rt_car(f)), v = rt_car(rt_cdr(rt_car(f)));
        put(&b, ",");
        if (rt_is_string(name)) put_string(&b, name);
        else                    put_rendered(&b, name);
        put(&b, ":");
        rt_value_t text = print_json(v, 1, site);
        rt_buf_add(&b, rt_string_bytes(text), rt_string_len(text));
    }
    put(&b, "}\n");
    fflush(stdout);
    fwrite(b.bytes, 1, b.len, stderr);
    fflush(stderr);
    rt_buf_free(&b);
    return RT_NIL;
}
