// sexp.c -- s-expressions as data (D155, D160), in two parts as JSON is
// (json.c, D162): a validator that takes text a piece at a time and says
// where a datum ends, or where the text went wrong (the length of its
// longest valid prefix), and a builder that makes the value of text the
// validator has passed.
//
// The text is what the compiler's reader takes (compiler/src/reader.ts),
// without positions: lists, integers and floats, strings with the reader's
// escapes, symbols and keywords, #true and #false, 'x as (quote x), and
// comments. Unlike the compiler, :a reads as the symbol a, as a does
// (D52). A symbol the program mentions reads as itself, and any other as
// (:symbol "name"), so no symbol is made at run time (D14). Numbers read as
// JSON's do: an integer past 63 bits is the nearest float, and a float too
// big for a double is inf.

#include "rt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    S_SPACE,                    // between data: a datum, a ), space or a comment
    S_COMMENT,                  // after a ;, to the end of the line
    S_ATOM,                     // in a token that's a number or a name
    S_DOT,                      // in a token that's only "." so far
    S_HASH,                     // in #true or #false
    S_COLON,                    // just after a keyword's :
    S_KEYWORD,                  // in a keyword's name
    S_STRING, S_ESCAPE, S_BRACE, S_HEX,
};

// Where a token is in the reader's grammar of numbers: -?D+ is an integer,
// and -?D+.D+ a float, with maybe an exponent e[+-]?D+.
enum { N_START, N_MINUS, N_INT, N_POINT, N_FRAC, N_E, N_E_SIGN, N_EXP, N_NOT };

static int number_step(int n, int c) {
    int digit = c >= '0' && c <= '9';
    switch (n) {
        case N_START:  return c == '-' ? N_MINUS : digit ? N_INT : N_NOT;
        case N_MINUS:  return digit ? N_INT : N_NOT;
        case N_INT:    return digit ? N_INT : c == '.' ? N_POINT : N_NOT;
        case N_POINT:  return digit ? N_FRAC : N_NOT;
        case N_FRAC:   return digit ? N_FRAC : c == 'e' || c == 'E' ? N_E : N_NOT;
        case N_E:      return c == '+' || c == '-' ? N_E_SIGN : digit ? N_EXP : N_NOT;
        case N_E_SIGN:
        case N_EXP:    return digit ? N_EXP : N_NOT;
        default:       return N_NOT;
    }
}

static int number_kind(const char *p, size_t len) {
    int n = N_START;
    for (size_t i = 0; i < len; i++) n = number_step(n, (unsigned char)p[i]);
    return n;
}

static int is_number(int n)    { return n == N_INT || n == N_FRAC || n == N_EXP; }
static int is_space(int c)     { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_delimiter(int c) { return is_space(c) || (c && strchr("()';\"`,", c)); }

static int hex_value(int c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

void rt_sexp_reset(rt_sexp_t *s) {
    s->depth = 0;
    s->state = S_SPACE;
    if (s->quoted) s->quoted[0] = 0;
}

void rt_sexp_free(rt_sexp_t *s) {
    free(s->quoted);
    s->quoted = NULL;
    s->cap    = 0;
    rt_sexp_reset(s);
}

// A datum has just ended at the current level, which takes any ' waiting
// there: 1 if that was the top level.
static int ended(rt_sexp_t *s) {
    s->quoted[s->depth] = 0;
    return s->depth == 0;
}

// Whether the token being read is one, now that it's over.
static int token_ok(const rt_sexp_t *s) {
    return s->state == S_ATOM || (s->state == S_HASH && s->word && !*s->word) || (s->state == S_KEYWORD && !is_number(s->num));
}

int rt_sexp_scan(rt_sexp_t *s, const char *bytes, size_t len, int eof, size_t *used) {
    if (!s->quoted) {
        s->cap    = 16;
        s->quoted = calloc(s->cap, 1);
        if (!s->quoted) abort();
    }
    size_t i = 0;
    for (; i < len; i++) {
        int c = (unsigned char)bytes[i];
        switch (s->state) {
        case S_COMMENT:
            if (c == '\n') s->state = S_SPACE;
            break;
        case S_ATOM:
        case S_DOT:
        case S_HASH:
        case S_COLON:
        case S_KEYWORD:
            if (!is_delimiter(c)) {
                if (s->state == S_DOT)          s->state = S_ATOM;
                else if (s->state == S_KEYWORD) s->num = number_step(s->num, c);
                else if (s->state == S_COLON) {
                    if (c == '#' || c == ':')   goto bad;
                    s->state = S_KEYWORD;
                    s->num   = number_step(N_START, c);
                } else if (s->state == S_HASH) {
                    if (!s->word)               s->word = c == 't' ? "rue" : c == 'f' ? "alse" : NULL;
                    else if (*s->word && c == *s->word) s->word++;
                    else                        goto bad;
                    if (!s->word)               goto bad;
                }
                break;
            }
            if (!token_ok(s)) goto bad;          // the token ended before byte i
            s->state = S_SPACE;
            if (ended(s)) {
                *used = i;
                return RT_SCAN_DONE;
            }
            i--;                                 // byte i is the list's again
            break;
        case S_SPACE:
            if (is_space(c))                    break;
            if (c == ';')                       s->state = S_COMMENT;
            else if (c == '\'')                 s->quoted[s->depth] = 1;
            else if (c == '"')                  s->state = S_STRING;
            else if (c == '#')                  s->state = S_HASH, s->word = NULL;
            else if (c == ':')                  s->state = S_COLON;
            else if (c == '.')                  s->state = S_DOT;
            else if (c == '`' || c == ',')      goto bad;
            else if (c == '(') {
                if (s->depth + 1 == s->cap) {
                    s->cap   *= 2;
                    s->quoted = realloc(s->quoted, s->cap);
                    if (!s->quoted) abort();
                }
                s->quoted[++s->depth] = 0;
            } else if (c == ')') {
                if (s->depth == 0 || s->quoted[s->depth]) goto bad;
                s->depth--;
                if (ended(s)) {
                    *used = i + 1;
                    return RT_SCAN_DONE;
                }
            } else {
                s->state = S_ATOM;
            }
            break;
        case S_STRING:
            if (c == '\\') {
                s->state = S_ESCAPE;
            } else if (c == '"') {
                s->state = S_SPACE;
                if (ended(s)) {
                    *used = i + 1;
                    return RT_SCAN_DONE;
                }
            }
            break;
        case S_ESCAPE:
            if (c == 'u')                       s->state = S_BRACE;
            else if (c && strchr("\"\\ntre", c)) s->state = S_STRING;
            else                                goto bad;
            break;
        case S_BRACE:
            if (c != '{')                       goto bad;
            s->state = S_HEX;
            s->hex   = 0;
            s->code  = 0;
            break;
        case S_HEX:
            if (c == '}') {                     // \u{hex}: 1 to 6 digits, no surrogate (D148)
                if (s->hex == 0 || (s->code >= 0xd800 && s->code <= 0xdfff)) goto bad;
                s->state = S_STRING;
                break;
            }
            if (hex_value(c) < 0 || s->hex == 6) goto bad;
            s->code = s->code << 4 | (uint32_t)hex_value(c);
            s->hex++;
            if (s->code > 0x10ffff)             goto bad;
            break;
        }
    }
    if (!eof) return RT_SCAN_MORE;
    if (s->state >= S_ATOM && s->state <= S_KEYWORD && token_ok(s) && s->depth == 0) {
        *used = len;
        return RT_SCAN_DONE;
    }
    if ((s->state == S_SPACE || s->state == S_COMMENT) && s->depth == 0 && !s->quoted[0]) return RT_SCAN_NONE;
bad:
    *used = i;
    return RT_SCAN_BAD;
}

// --- the builder --------------------------------------------------------------

static rt_value_t *cell(rt_build_alloc_t alloc, void *cx, rt_value_t car, rt_value_t cdr) {
    rt_value_t *c = alloc(cx, 16);
    if (c) {
        c[0] = car;
        c[1] = cdr;
    }
    return c;
}

// A string's bytes, escapes and all, from p (past the opening quote) to the
// closing quote: decoded into out if it isn't NULL; the decoded length
// either way, and *end past the closing quote.
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
            char c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'e' ? '\x1b' : e;
            if (out) out[n] = c;
            n++, p += 2;
            continue;
        }
        uint32_t u = 0;
        for (p += 3; *p != '}'; p++) u = u << 4 | (uint32_t)hex_value((unsigned char)*p);
        p++;
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

static rt_value_t new_string(const char *bytes, size_t n, rt_build_alloc_t alloc, void *cx) {
    uint64_t *box = alloc(cx, 8 + n + 1);
    if (!box) return 0;
    box[0] = (uint64_t)n << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
    if (bytes) memcpy(box + 1, bytes, n);
    ((char *)(box + 1))[n] = '\0';
    return (rt_value_t)box | RT_TAG_BOXED;
}

// The symbol named by len bytes, or (:symbol "name") if the program has none.
static rt_value_t symbol(const char *name, size_t len, rt_build_alloc_t alloc, void *cx) {
    int64_t id = rt_symbol_find(name, len);
    if (id >= 0) return rt_symbol((uint64_t)id);
    rt_value_t  s    = new_string(name, len, alloc, cx);
    rt_value_t *rest = s ? cell(alloc, cx, s, RT_NIL) : NULL;
    rt_value_t *head = rest ? cell(alloc, cx, rt_symbol(RT_SYM_SEXP + RT_SEXP_SYMBOL), (rt_value_t)rest | RT_TAG_LIST) : NULL;
    return head ? (rt_value_t)head | RT_TAG_LIST : 0;
}

// A token's value, in *out: 0 if alloc gave out (0 itself is a value,
// the integer).
static int token(const char *p, size_t len, rt_build_alloc_t alloc, void *cx, rt_value_t *out) {
    int kind = number_kind(p, len);
    if (*p == '#')            *out = len == 5 ? RT_TRUE : RT_FALSE;
    else if (*p == ':')       *out = symbol(p + 1, len - 1, alloc, cx);
    else if (!is_number(kind)) *out = symbol(p, len, alloc, cx);
    if (*p == '#' || *p == ':' || !is_number(kind)) return *out != 0;
    if (kind == N_INT) {                        // as rt_string_to_int: negative has the room
        const int64_t min = -((int64_t)1 << 62);
        int64_t       n   = 0;
        int           fits = 1;
        for (size_t i = *p == '-'; i < len && fits; i++) {
            int digit = p[i] - '0';
            if (n < (min + digit) / 10) fits = 0;
            else                        n = n * 10 - digit;
        }
        if (fits && (*p == '-' || n != min)) {
            *out = rt_int(*p == '-' ? n : -n);
            return 1;
        }
    }
    char local[64], *text = len < sizeof local ? local : malloc(len + 1);
    if (!text) abort();
    memcpy(text, p, len);
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

// A list being built: its cells so far, the quotes waiting inside it for a
// datum, and the quotes around the list itself.
typedef struct {
    rt_value_t  head;
    rt_value_t *last;
    size_t      quotes, wrap;
} frame_t;

int rt_sexp_build(const char *text, size_t len, rt_build_alloc_t alloc, void *cx, rt_value_t *out) {
    frame_t    *frames = NULL;
    size_t      depth = 0, cap = 0, top_quotes = 0;
    const char *p = text, *end = text + len;
    rt_value_t  v;
    for (;;) {
        while (is_space((unsigned char)*p) || *p == ';') {
            if (*p == ';') while (p < end && *p != '\n') p++;
            else           p++;
        }
        size_t *quotes = depth ? &frames[depth - 1].quotes : &top_quotes, wrap;
        if (*p == '\'') {
            ++*quotes;
            p++;
            continue;
        }
        if (*p == '(') {
            if (depth == cap) {
                cap    = cap ? cap * 2 : 16;
                frames = realloc(frames, cap * sizeof *frames);
                if (!frames) abort();
                quotes = depth ? &frames[depth - 1].quotes : &top_quotes;
            }
            frames[depth++] = (frame_t){ .head = RT_NIL, .last = NULL, .quotes = 0, .wrap = *quotes };
            *quotes = 0;
            p++;
            continue;
        }
        if (*p == ')') {
            frame_t *f = &frames[--depth];
            v    = f->head;
            wrap = f->wrap;
            p++;
        } else {
            if (*p == '"') {
                const char *after;
                size_t      n = decode(p + 1, NULL, &after);
                uint64_t   *box = alloc(cx, 8 + n + 1);
                if (!box) goto too_big;
                box[0] = (uint64_t)n << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
                decode(p + 1, (char *)(box + 1), &after);
                ((char *)(box + 1))[n] = '\0';
                v = (rt_value_t)box | RT_TAG_BOXED;
                p = after;
            } else {
                const char *start = p;
                while (p < end && !is_delimiter((unsigned char)*p)) p++;
                if (!token(start, (size_t)(p - start), alloc, cx, &v)) goto too_big;
            }
            wrap    = *quotes;
            *quotes = 0;
        }
        for (; wrap > 0; wrap--) {              // (quote v)
            rt_value_t *rest = cell(alloc, cx, v, RT_NIL), *q = rest ? cell(alloc, cx, rt_symbol(RT_SYM_SEXP + RT_SEXP_QUOTE), (rt_value_t)rest | RT_TAG_LIST) : NULL;
            if (!q) goto too_big;
            v = (rt_value_t)q | RT_TAG_LIST;
        }
        if (depth == 0) break;
        frame_t    *f = &frames[depth - 1];
        rt_value_t *c = cell(alloc, cx, v, RT_NIL);
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

// (:ok datum), or (:error (:bad-sexp at)), at being where the text went
// wrong: a byte no valid text could have there, or the end.
rt_value_t rt_sexp_parse(rt_value_t s, const char *site) {
    if (!rt_is_string(s)) rt_fault(RT_FAULT_NOT_STRING, s, site);
    const char *text = rt_string_bytes(s);
    size_t      len  = rt_string_len(s), used = 0;
    rt_sexp_t   x    = { 0 };
    int         r    = rt_sexp_scan(&x, text, len, 1, &used);
    rt_sexp_free(&x);
    if (r == RT_SCAN_DONE) {                    // only space and comments may follow
        size_t k = used;
        while (k < len && (is_space((unsigned char)text[k]) || text[k] == ';')) {
            if (text[k] == ';') while (k < len && text[k] != '\n') k++;
            else                k++;
        }
        if (k < len) r = RT_SCAN_BAD, used = k;
    }
    if (r == RT_SCAN_NONE) r = RT_SCAN_BAD, used = len;
    rt_value_t *outer = rt_alloc(32, site), v;
    if (r == RT_SCAN_DONE) {
        rt_sexp_build(text, used, heap_alloc, (void *)site, &v);     // the heap faults before it gives out
        outer[0] = rt_symbol(RT_SYM_OK);
    } else {
        rt_value_t *why = rt_alloc(32, site);
        why[0] = rt_symbol(RT_SYM_ERRS + RT_ERR_BADSEXP);
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

static void put(rt_buf_t *b, const char *s) {
    rt_buf_add(b, s, strlen(s));
}

// A string as the reader takes it back: " and \ escaped, and the control
// characters, so what's printed stays on one line.
static void put_string(rt_buf_t *b, const char *p, size_t len) {
    size_t from = 0;
    put(b, "\"");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        const char   *e = c == '"' ? "\\\"" : c == '\\' ? "\\\\" : c == '\n' ? "\\n" : c == '\t' ? "\\t"
                        : c == '\r' ? "\\r" : c == 0x1b ? "\\e" : NULL;
        char u[12];
        if (!e && (c < 0x20 || c == 0x7f)) {
            static const char digits[] = "0123456789abcdef";
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
        put(b, e);
        from = i + 1;
    }
    rt_buf_add(b, p + from, len - from);
    put(b, "\"");
}

// Whether len bytes read back as a name: not a number, not ".", no
// delimiter, and not a # literal or a keyword.
static int is_name(const char *p, size_t len) {
    if (len == 0 || (len == 1 && *p == '.') || *p == '#' || *p == ':' || is_number(number_kind(p, len))) return 0;
    for (size_t i = 0; i < len; i++) {
        if (is_delimiter((unsigned char)p[i])) return 0;
    }
    return 1;
}

// (:symbol "name"), as the reader makes it of a name the program doesn't
// mention.
static int is_foreign(rt_value_t v) {
    return rt_car(v) == rt_symbol(RT_SYM_SEXP + RT_SEXP_SYMBOL) && rt_is_cons(rt_cdr(v)) && rt_is_string(rt_car(rt_cdr(v)))
        && rt_cdr(rt_cdr(v)) == RT_NIL && is_name(rt_string_bytes(rt_car(rt_cdr(v))), rt_string_len(rt_car(rt_cdr(v))));
}

__attribute__((noreturn))
static void not_sexp(rt_buf_t *b, rt_work_t *w, rt_value_t v, const char *site) {
    rt_buf_free(b);
    rt_work_free(w);
    rt_fault(RT_FAULT_NOT_SEXP, v, site);
}

// Data as the reader reads it back, on one line (D155). The printer walks
// with a work stack (D147): for each list it's in, the rest of it, and
// whether anything has been written in it yet.
rt_value_t rt_sexp_print(rt_value_t v, const char *site) {
    rt_buf_t  b = { 0 };
    rt_work_t w;
    rt_work_init(&w);
    for (;;) {
        if (rt_is_cons(v) && is_foreign(v)) {
            rt_value_t name = rt_car(rt_cdr(v));
            rt_buf_add(&b, rt_string_bytes(name), rt_string_len(name));
        } else if (rt_is_cons(v)) {
            put(&b, "(");
            rt_work_push(&w, v);
            rt_work_push(&w, 0);
        } else if (v == RT_NIL) {
            put(&b, "()");
        } else if (rt_is_string(v)) {
            put_string(&b, rt_string_bytes(v), rt_string_len(v));
        } else if ((v & RT_TAG_MASK) == RT_TAG_SYMBOL) {
            put(&b, rt_symbol_name(v >> RT_SYMBOL_SHIFT));
        } else if (!(v & RT_TAG_INT_MASK)) {
            rt_render(&b, v, 0);
        } else if (rt_is_float(v) && isfinite(rt_float_value(v))) {
            // as pprint has it, but with digits after a point before an
            // exponent, as the reader wants: 1.0e+21, not 1e+21
            size_t from = b.len;
            rt_render(&b, v, 0);
            if (!memchr(b.bytes + from, '.', b.len - from)) {
                char  *e = memchr(b.bytes + from, 'e', b.len - from);
                size_t k = (size_t)(e - b.bytes), rest = b.len - k;
                rt_buf_add(&b, "..", 2);
                memmove(b.bytes + k + 2, b.bytes + k, rest);
                memcpy(b.bytes + k, ".0", 2);
            }
        } else {
            not_sexp(&b, &w, v, site);
        }
        // on to the next element of the innermost list not yet done
        for (;;) {
            if (w.n == 0) {
                rt_work_free(&w);
                rt_value_t s = rt_new_string(b.bytes, b.len, site);
                rt_buf_free(&b);
                return s;
            }
            rt_value_t rest = w.items[w.n - 2];
            if (rest == RT_NIL) {
                put(&b, ")");
                w.n -= 2;
                continue;
            }
            if (w.items[w.n - 1]) put(&b, " ");
            w.items[w.n - 2] = rt_cdr(rest);
            w.items[w.n - 1] = 1;
            v = rt_car(rest);
            break;
        }
    }
}
