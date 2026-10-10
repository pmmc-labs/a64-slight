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

#include <math.h>
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

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int hex_value(int c) {
    return is_digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

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
        j->open = realloc(j->open, j->cap);
        if (!j->open) abort();
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
            if (is_space(c))                         break;
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
            if (is_space(c))                         break;
            if (c != '"')                            goto bad;
            j->key   = 1;
            j->state = J_STRING;
            break;
        case J_COLON:
            if (is_space(c))                         break;
            if (c != ':')                            goto bad;
            j->state = J_VALUE;
            break;
        case J_AFTER:
            if (is_space(c))                         break;
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
            if (hex_value(c) < 0)                    goto bad;
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
                return RT_JSON_DONE;
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
                return RT_JSON_DONE;
            }
            j->state = J_AFTER;
        }
    }
    if (!eof) return RT_JSON_MORE;
    if (j->depth == 0 && (j->state == J_ZERO || j->state == J_INT || j->state == J_FRAC || j->state == J_EXP)) {
        *used = len;
        return RT_JSON_DONE;
    }
    if (j->depth == 0 && j->state == J_VALUE) return RT_JSON_NONE;
bad:
    *used = i;
    return RT_JSON_BAD;
}

// --- the builder --------------------------------------------------------------

static rt_value_t *cell(rt_json_alloc_t alloc, void *cx, rt_value_t car) {
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
    for (int k = 0; k < 4; k++) u = u << 4 | (uint32_t)hex_value((unsigned char)p[k]);
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

static rt_value_t string(const char **p, rt_json_alloc_t alloc, void *cx) {
    const char *end;
    size_t      n   = decode(*p + 1, NULL, &end);
    uint64_t   *box = alloc(cx, 8 + n + 1);
    if (!box) return 0;
    box[0] = (uint64_t)n << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
    decode(*p + 1, (char *)(box + 1), &end);
    ((char *)(box + 1))[n] = '\0';
    *p = end;
    return (rt_value_t)box | RT_TAG_BOXED;
}

// An integer if it's written as one and fits in 63 bits; otherwise the
// nearest double, as strtod rounds (inf if it's too big for one, as in
// JavaScript and Python). 0 if alloc gave out. A number is the one value
// that can end the text, so it's the one that has to look for the end.
static int number(const char **p, const char *end, rt_json_alloc_t alloc, void *cx, rt_value_t *out) {
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
    char   local[64], *text = len < sizeof local ? local : malloc(len + 1);
    if (!text) abort();
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
typedef struct {
    rt_value_t  head, key;
    rt_value_t *last;
    int         object;
} frame_t;

int rt_json_build(const char *text, size_t len, rt_json_alloc_t alloc, void *cx, rt_value_t *out) {
    frame_t    *frames = NULL;
    size_t      depth = 0, cap = 0;
    const char *p = text;
    rt_value_t  v;
    for (;;) {
        while (is_space(*p) || *p == ',' || *p == ':') p++;     // separators, in valid text
        if (*p == '[' || *p == '{') {
            if (depth == cap) {
                cap    = cap ? cap * 2 : 16;
                frames = realloc(frames, cap * sizeof *frames);
                if (!frames) abort();
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
    free(frames);
    *out = v;
    return 0;
too_big:
    free(frames);
    return -1;
}

// --- the builtins -------------------------------------------------------------

static void *heap_alloc(void *site, size_t bytes) {
    return rt_alloc(bytes, site);
}

// (:ok value), or (:error (:bad-json at)), at being where the text went
// wrong: a byte no valid text could have there, or the end.
rt_value_t rt_json_parse(rt_value_t s, const char *site) {
    if (!rt_is_string(s)) rt_fault(RT_FAULT_NOT_STRING, s, site);
    const char *text = rt_string_bytes(s);
    size_t      len  = rt_string_len(s), used = 0;
    rt_json_t   j    = { 0 };
    int         r    = rt_json_scan(&j, text, len, 1, &used);
    rt_json_free(&j);
    if (r == RT_JSON_DONE) {
        size_t rest = used;
        while (rest < len && is_space((unsigned char)text[rest])) rest++;
        if (rest < len) r = RT_JSON_BAD, used = rest;
    }
    if (r == RT_JSON_NONE) r = RT_JSON_BAD, used = len;
    rt_value_t *outer = rt_alloc(32, site), v;
    if (r == RT_JSON_DONE) {
        rt_json_build(text, used, heap_alloc, (void *)site, &v);     // the heap faults before it gives out
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
// has been written in it yet.
#define IN_ARRAY  0
#define IN_OBJECT 1
#define WRITTEN   2

static void put(rt_buf_t *b, const char *s) {
    rt_buf_add(b, s, strlen(s));
}

static void put_string(rt_buf_t *b, rt_value_t s) {
    const unsigned char *p   = (const unsigned char *)rt_string_bytes(s);
    size_t               len = rt_string_len(s), from = 0;
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

__attribute__((noreturn))
static void not_json(rt_buf_t *b, rt_work_t *w, rt_value_t v, const char *site) {
    rt_buf_free(b);
    rt_work_free(w);
    rt_fault(RT_FAULT_NOT_JSON, v, site);
}

// Compact JSON, on one line (D161). What JSON can't hold faults.
rt_value_t rt_json_print(rt_value_t v, const char *site) {
    rt_buf_t  b = { 0 };
    rt_work_t w;
    rt_work_init(&w);
    for (;;) {
        if (rt_is_cons(v)) {
            int object = rt_car(v) == rt_symbol(RT_SYM_JSON + RT_JSON_OBJECT);
            put(&b, object ? "{" : "[");
            rt_work_push(&w, object ? rt_cdr(v) : v);
            rt_work_push(&w, object ? IN_OBJECT : IN_ARRAY);
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
            if (rest == RT_NIL) {
                put(&b, how & IN_OBJECT ? "}" : "]");
                w.n -= 2;
                continue;
            }
            if (!rt_is_cons(rest)) not_json(&b, &w, rest, site);
            if (how & WRITTEN) put(&b, ",");
            w.items[w.n - 2] = rt_cdr(rest);
            w.items[w.n - 1] = how | WRITTEN;
            v = rt_car(rest);
            if (how & IN_OBJECT) {              // (key value)
                if (!rt_is_cons(v) || !rt_is_string(rt_car(v)) || !rt_is_cons(rt_cdr(v)) || rt_cdr(rt_cdr(v)) != RT_NIL) {
                    not_json(&b, &w, v, site);
                }
                put_string(&b, rt_car(v));
                put(&b, ":");
                v = rt_car(rt_cdr(v));
            }
            break;
        }
    }
}
