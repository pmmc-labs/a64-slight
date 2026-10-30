// sexp.c -- s-expressions as data (D155, D160), in two parts as JSON is
// (json.c, D162): a validator that takes text a piece at a time and says
// where a datum ends, or where the text went wrong (the length of its
// longest valid prefix), and a builder that makes the value of text the
// validator has passed.
//
// The text is what the compiler's reader takes (compiler/src/reader.ts),
// without positions: lists, integers and floats, strings with the reader's
// escapes, symbols and keywords, #true and #false, 'x as (quote x), and
// comments; and #(...), which the compiler takes only as a pattern, as a
// tuple (D176); and #float(...) and #int(...), arrays of numbers (D253),
// which aren't in source at all, an #int's each in 63 bits. Unlike the compiler, :a reads as the symbol a, as a does
// (D52). A symbol the program mentions reads as itself, and any other as
// (:symbol "name"), so no symbol is made at run time (D14). Numbers read as
// JSON's do: an integer past 63 bits is the nearest float, and a float too
// big for a double is inf.

#include "rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    S_SPACE,                    // between data: a datum, a ), space or a comment
    S_COMMENT,                  // after a ;, to the end of the line
    S_ATOM,                     // in a token that's a number or a name
    S_DOT,                      // in a token that's only "." so far
    S_HASH,                     // in #true or #false, or a # before a tuple's (
    S_COLON,                    // just after a keyword's :
    S_KEYWORD,                  // in a keyword's name
    S_STRING, S_ESCAPE, S_BRACE, S_HEX,
    S_EQUALS,                   // a = to start a line between top-level data: a doc block, or a name
    S_DOC_FIRST,                // in a doc block's first line, =doc and then only space
    S_DOC_BODY,                 // in a doc block, till a line =cut
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
static int is_delimiter(int c) { return rt_is_space(c) || (c && strchr("()';\"`,", c)); }

// Ready for the next datum: where it is in a line, and whether it reads
// doc blocks or turns down a datum, stay as they were.
void rt_sexp_reset(rt_sexp_t *s) {
    s->depth = 0;
    s->state = S_SPACE;
    s->seen  = 0;
    s->lines = 0;
    if (s->quoted) s->quoted[0] = 0;
}

void rt_sexp_free(rt_sexp_t *s) {
    free(s->quoted);
    *s = (rt_sexp_t){ 0 };
}

// What a level's quoted[] says: a ' waits there; and it's an array's,
// #float( or #int(, which holds only numbers, an #int's integers.
enum { Q_WAIT = 1, Q_FLOATS = 2, Q_INTS = 4 };

// A list, a tuple or an array opens a level, with no ' waiting in it yet.
static void open_level(rt_sexp_t *s, int array) {
    if (s->depth + 1 == s->cap) {
        s->cap   *= 2;
        s->quoted = rt_must(realloc(s->quoted, s->cap));
    }
    s->quoted[++s->depth] = (unsigned char)array;
}

// A datum has just ended at the current level, which takes any ' waiting
// there: 1 if that was the top level.
static int ended(rt_sexp_t *s) {
    s->quoted[s->depth] &= ~Q_WAIT;
    return s->depth == 0;
}

static const char ALSE[] = "alse";

// Whether the token being read is one, now that it's over: in an array,
// a number, and in an #int, an integer in 63 bits.
static int token_ok(const rt_sexp_t *s) {
    int in = s->quoted[s->depth];
    if (in & Q_INTS)   return s->state == S_ATOM && s->num == N_INT && s->mag <= ((uint64_t)1 << 62) - !s->neg;
    if (in & Q_FLOATS) return s->state == S_ATOM && is_number(s->num);
    return s->state == S_ATOM || (s->state == S_HASH && s->word && !*s->word && !s->array) || (s->state == S_KEYWORD && !is_number(s->num));
}

// A number's digits, up to just past 63 bits, which is all token_ok needs.
static void count_digit(rt_sexp_t *s, int c) {
    if (c >= '0' && c <= '9' && s->mag <= (uint64_t)1 << 62) s->mag = s->mag * 10 + (uint64_t)(c - '0');
}

static int is_blank(int c)  { return c == ' ' || c == '\t' || c == '\r'; }
static int is_letter(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

int rt_sexp_scan(rt_sexp_t *s, const char *bytes, size_t len, int eof, size_t *used) {
    if (!s->quoted) {
        s->cap    = 16;
        s->quoted = rt_must(calloc(s->cap, 1));
    }
    size_t i = 0;
    int    r = RT_SCAN_MORE;
    for (; i < len; i++) {
        int    c   = (unsigned char)bytes[i];
        int    end = 0;                         // 1: a datum ends with byte i; 2: a doc block does
        size_t at  = s->seen + i;               // byte i's offset since the reset
        switch (s->state) {
        case S_COMMENT:
            if (c == '\n') s->state = S_SPACE;
            break;
        case S_EQUALS:
            if (is_letter(c)) {                 // a doc block: =doc is the one kind, for now (D164)
                if (c != 'd') goto bad;
                s->state = S_DOC_FIRST;
                s->k     = 1;
                break;
            }
            if (s->closed) goto bad;
            s->state = S_ATOM;                  // a name that starts with =
            // fall through
        case S_ATOM:
        case S_DOT:
        case S_HASH:
        case S_COLON:
        case S_KEYWORD:
            if (!is_delimiter(c)) {
                if (s->state == S_DOT)          s->state = S_ATOM;
                else if (s->state == S_ATOM) {
                    s->num = number_step(s->num, c);
                    count_digit(s, c);
                }
                else if (s->state == S_KEYWORD) s->num = number_step(s->num, c);
                else if (s->state == S_COLON) {
                    if (c == '#' || c == ':')   goto bad;
                    s->state = S_KEYWORD;
                    s->num   = number_step(N_START, c);
                } else if (s->state == S_HASH) {
                    if (!s->word) {
                        s->word  = c == 't' ? "rue" : c == 'f' ? ALSE : c == 'i' ? "nt" : NULL;
                        s->array = c == 'i' ? Q_INTS : 0;
                    } else if (s->word == ALSE && c == 'l') {
                        s->word  = "oat";
                        s->array = Q_FLOATS;
                    } else if (*s->word && c == *s->word) {
                        s->word++;
                    } else {
                        goto bad;
                    }
                    if (!s->word)               goto bad;
                }
                break;
            }
            if (s->state == S_HASH && (!s->word || (s->array && !*s->word)) && c == '(') {
                open_level(s, s->word ? s->array : 0);  // #( : a tuple; #float( or #int( : an array
                s->state = S_SPACE;
                break;
            }
            if (!token_ok(s)) goto bad;          // the token ended before byte i
            s->state = S_SPACE;
            if (ended(s)) {
                *used = i;
                r     = RT_SCAN_DONE;
                goto out;
            }
            i--;                                 // byte i is the list's again
            continue;
        case S_SPACE:
            if (rt_is_space(c))                    break;
            if (c == ';') {
                s->state = S_COMMENT;
                break;
            }
            if (c == '=' && !s->mid_line && s->depth == 0 && !s->quoted[0]) {
                s->state      = S_EQUALS;
                s->from       = at;
                s->first_line = s->lines;
                break;
            }
            if (s->closed || c == '`' || c == ',') goto bad;
            if ((s->quoted[s->depth] & (Q_FLOATS | Q_INTS)) && c != ')' && c != '-' && !(c >= '0' && c <= '9')) goto bad;
            if (s->depth == 0 && !s->quoted[0]) {   // a top-level datum begins
                s->from       = at;
                s->first_line = s->lines;
            }
            if (c == '\'')                      s->quoted[s->depth] |= Q_WAIT;
            else if (c == '"')                  s->state = S_STRING;
            else if (c == '#')                  s->state = S_HASH, s->word = NULL, s->array = 0;
            else if (c == ':')                  s->state = S_COLON;
            else if (c == '.')                  s->state = S_DOT;
            else if (c == '(')                  open_level(s, 0);
            else if (c == ')') {
                if (s->depth == 0 || (s->quoted[s->depth] & Q_WAIT)) goto bad;
                s->depth--;
                end = ended(s);
            } else {
                s->state = S_ATOM;
                s->num   = number_step(N_START, c);
                s->mag   = 0;
                s->neg   = c == '-';
                count_digit(s, c);
            }
            break;
        case S_STRING:
            if (c == '\\') {
                s->state = S_ESCAPE;
            } else if (c == '"') {
                s->state = S_SPACE;
                end      = ended(s);
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
            if (rt_hex_value(c) < 0 || s->hex == 6) goto bad;
            s->code = s->code << 4 | (uint32_t)rt_hex_value(c);
            s->hex++;
            if (s->code > 0x10ffff)             goto bad;
            break;
        case S_DOC_FIRST:                       // k: how much of "doc" there's been
            if (s->k < 3) {
                if (c != "doc"[s->k])           goto bad;
                s->k++;
            } else if (c == '\n') {
                s->state    = S_DOC_BODY;
                s->k        = 0;
                s->doc_from = s->line_from = at + 1;
            } else if (!is_blank(c)) {
                goto bad;
            }
            break;
        case S_DOC_BODY:                        // k: how much of "=cut" the line has been, or -1
            if (c == '\n') {
                if (s->k >= 4) {
                    s->doc_to = s->line_from;
                    end       = 2;
                } else {
                    s->k         = 0;
                    s->line_from = at + 1;
                }
            } else if (s->k >= 4) {
                if (!is_blank(c)) s->k = -1;
            } else if (s->k >= 0) {
                s->k = c == "=cut"[s->k] ? s->k + 1 : -1;
            }
            break;
        }
        if (c == '\n') s->lines++;
        s->mid_line = c != '\n';
        if (end == 1) {
            *used = i + 1;
            r     = RT_SCAN_DONE;
            i++;
            goto out;
        }
        if (end == 2) {
            s->state = S_SPACE;
            if (s->docs) {
                *used = i + 1;
                r     = RT_SCAN_DOC;
                i++;
                goto out;
            }
        }
    }
    if (eof) {
        if (s->state == S_DOC_FIRST || s->state == S_DOC_BODY) {    // the end of the file ends a doc block
            if (s->state == S_DOC_FIRST && s->k < 3) goto bad;
            if (s->state == S_DOC_FIRST) s->doc_from = s->doc_to = s->seen + len;
            else                         s->doc_to = s->k >= 4 ? s->line_from : s->seen + len;
            s->state = S_SPACE;
            if (s->docs) {
                *used = len;
                r     = RT_SCAN_DOC;
                goto out;
            }
        }
        if (s->state >= S_ATOM && s->state <= S_KEYWORD && token_ok(s) && s->depth == 0) {
            *used = len;
            r     = RT_SCAN_DONE;
        } else if (s->state == S_EQUALS && !s->closed) {
            *used = len;
            r     = RT_SCAN_DONE;
        } else if ((s->state == S_SPACE || s->state == S_COMMENT) && s->depth == 0 && !s->quoted[0]) {
            return RT_SCAN_NONE;
        } else {
            goto bad;
        }
    }
out:
    s->seen += i;
    return r;
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
        for (p += 3; *p != '}'; p++) u = u << 4 | (uint32_t)rt_hex_value((unsigned char)*p);
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
    if (bytes) memcpy(box + 1, bytes, n);
    ((char *)(box + 1))[n] = '\0';
    box[0] = rt_string_header((const char *)(box + 1), n);
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
    char local[64], *text = len < sizeof local ? local : rt_must(malloc(len + 1));
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
// datum, the quotes around the list itself, and whether it's a tuple's
// elements, or an array's, to be made one at its ). The frames are allocated as the
// datum is, so they count against the same limit, as JSON's do (D205).
typedef struct {
    rt_value_t  head;
    rt_value_t *last;
    size_t      quotes, wrap;
    int         made;           // F_LIST, or what it's made at its )
} frame_t;

enum { F_LIST, F_TUPLE, F_FLOATS, F_INTS };

static rt_value_t new_tuple(rt_value_t xs, rt_build_alloc_t alloc, void *cx) {
    uint64_t n = 0;
    for (rt_value_t v = xs; v != RT_NIL; v = rt_cdr(v)) n++;
    uint64_t *box = alloc(cx, 8 + 8 * n);
    if (!box) return 0;
    box[0] = n << RT_BOX_SIZE_SHIFT | RT_BOX_TUPLE;
    for (uint64_t i = 1; xs != RT_NIL; xs = rt_cdr(xs)) box[i++] = rt_car(xs);
    return (rt_value_t)box | RT_TAG_BOXED;
}

// An array of the numbers in xs, which the validator has said are all
// numbers, and an #int's all integers.
static rt_value_t new_array(rt_value_t xs, uint64_t type, rt_build_alloc_t alloc, void *cx) {
    uint64_t n = 0;
    for (rt_value_t v = xs; v != RT_NIL; v = rt_cdr(v)) n++;
    uint64_t *box = alloc(cx, 8 + 8 * n);
    if (!box) return 0;
    box[0] = n << RT_BOX_SIZE_SHIFT | type;
    for (uint64_t i = 1; xs != RT_NIL; xs = rt_cdr(xs), i++) {
        rt_value_t x = rt_car(xs);
        double     d = (x & RT_TAG_INT_MASK) == 0 ? (double)rt_int_value(x) : rt_float_value(x);
        if (type == RT_BOX_INTS) box[i] = (uint64_t)rt_int_value(x);
        else                     memcpy(box + i, &d, sizeof d);
    }
    return (rt_value_t)box | RT_TAG_BOXED;
}

int rt_sexp_build(const char *text, size_t len, rt_build_alloc_t alloc, void *cx, rt_value_t *out) {
    frame_t    *frames = NULL;
    size_t      depth = 0, cap = 0, top_quotes = 0;
    const char *p = text, *end = text + len;
    rt_value_t  v;
    for (;;) {
        while (rt_is_space((unsigned char)*p) || *p == ';') {
            if (*p == ';') while (p < end && *p != '\n') p++;
            else           p++;
        }
        size_t *quotes = depth ? &frames[depth - 1].quotes : &top_quotes, wrap;
        if (*p == '\'') {
            ++*quotes;
            p++;
            continue;
        }
        int made = *p == '(' ? F_LIST : *p != '#' ? -1 : p[1] == '(' ? F_TUPLE
                 : strncmp(p, "#float(", 7) == 0 ? F_FLOATS : strncmp(p, "#int(", 5) == 0 ? F_INTS : -1;
        if (made >= 0) {
            if (depth == cap) {
                frame_t *more = alloc(cx, (cap ? cap * 2 : 16) * sizeof *frames);
                if (!more) goto too_big;
                if (depth) memcpy(more, frames, depth * sizeof *frames);
                frames = more;
                cap    = cap ? cap * 2 : 16;
                quotes = depth ? &frames[depth - 1].quotes : &top_quotes;
            }
            frames[depth++] = (frame_t){ .head = RT_NIL, .last = NULL, .quotes = 0, .wrap = *quotes, .made = made };
            *quotes = 0;
            p += made == F_LIST ? 1 : made == F_TUPLE ? 2 : made == F_FLOATS ? 7 : 5;
            continue;
        }
        if (*p == ')') {
            frame_t *f = &frames[--depth];
            v    = f->made == F_TUPLE ? new_tuple(f->head, alloc, cx)
                 : f->made == F_FLOATS ? new_array(f->head, RT_BOX_FLOATS, alloc, cx)
                 : f->made == F_INTS ? new_array(f->head, RT_BOX_INTS, alloc, cx) : f->head;
            wrap = f->wrap;
            if (!v) goto too_big;
            p++;
        } else {
            if (*p == '"') {
                const char *after;
                size_t      n = decode(p + 1, NULL, &after);
                uint64_t   *box = alloc(cx, 8 + n + 1);
                if (!box) goto too_big;
                decode(p + 1, (char *)(box + 1), &after);
                ((char *)(box + 1))[n] = '\0';
                box[0] = rt_string_header((const char *)(box + 1), n);
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
    *out = v;
    return 0;
too_big:
    return -1;
}

// --- the builtins -------------------------------------------------------------

// (:ok datum), or (:error (:bad-sexp at)), at being where the text went
// wrong: a byte no valid text could have there, or the end.
rt_value_t rt_sexp_parse(rt_value_t s, const char *site) {
    if (!rt_is_string(s)) rt_fault(RT_FAULT_NOT_STRING, s, site);
    const char *text = rt_string_bytes(s);
    size_t      len  = rt_string_len(s), used = 0;
    rt_sexp_t   x    = { 0 };
    int         r    = rt_sexp_scan(&x, text, len, 1, &used);
    size_t      from = x.from, rest;
    if (r == RT_SCAN_DONE) {                    // only space, comments and doc blocks may follow
        rt_sexp_reset(&x);
        x.closed = 1;
        if (rt_sexp_scan(&x, text + used, len - used, 1, &rest) == RT_SCAN_BAD) r = RT_SCAN_BAD, used += rest;
    }
    rt_sexp_free(&x);
    if (r == RT_SCAN_NONE) r = RT_SCAN_BAD, used = len;
    rt_value_t *outer = rt_alloc(32, site), v;
    if (r == RT_SCAN_DONE) {
        rt_sexp_build(text + from, used - from, rt_heap_alloc, (void *)site, &v);  // the heap faults before it gives out
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

// A float as pprint has it, but with digits after a point before an
// exponent, as the reader wants: 1.0e+21, not 1e+21.
static void put_float(rt_buf_t *b, double d) {
    size_t from = b->len;
    rt_render_float(b, d);
    if (!memchr(b->bytes + from, '.', b->len - from)) {
        char  *e = memchr(b->bytes + from, 'e', b->len - from);
        size_t k = (size_t)(e - b->bytes), rest = b->len - k;
        rt_buf_add(b, "..", 2);
        memmove(b->bytes + k + 2, b->bytes + k, rest);
        memcpy(b->bytes + k, ".0", 2);
    }
}

// Data as the reader reads it back, on one line (D155). The printer walks
// with a work stack (D147): for each list it's in, the rest of it, and
// whether anything has been written in it yet; for each tuple, the tuple,
// and the index of its next element.
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
        } else if (rt_is_tuple(v)) {
            put(&b, "#(");
            rt_work_push(&w, v);
            rt_work_push(&w, 0);
        } else if (v == RT_NIL) {
            put(&b, "()");
        } else if (rt_is_string(v)) {
            rt_quote(&b, rt_string_bytes(v), rt_string_len(v));
        } else if ((v & RT_TAG_MASK) == RT_TAG_SYMBOL) {
            put(&b, rt_symbol_name(v >> RT_SYMBOL_SHIFT));
        } else if (!(v & RT_TAG_INT_MASK)) {
            rt_render(&b, v, 0);
        } else if (rt_is_float(v) && isfinite(rt_float_value(v))) {
            put_float(&b, rt_float_value(v));
        } else if (rt_is_array(v)) {
            int ints = rt_is_box(v, RT_BOX_INTS);
            put(&b, ints ? "#int(" : "#float(");
            for (uint64_t i = 0; i < rt_array_len(v); i++) {
                if (i) put(&b, " ");
                if (ints) {
                    char num[24];
                    snprintf(num, sizeof num, "%" PRId64, (int64_t)rt_array_data(v)[i]);
                    put(&b, num);
                } else if (isfinite(rt_array_float(v, i))) {
                    put_float(&b, rt_array_float(v, i));
                } else {
                    not_sexp(&b, &w, v, site);
                }
            }
            put(&b, ")");
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
            if (rt_is_tuple(rest)) {
                uint64_t k = w.items[w.n - 1];
                if (k == rt_tuple_len(rest)) {
                    put(&b, ")");
                    w.n -= 2;
                    continue;
                }
                if (k) put(&b, " ");
                w.items[w.n - 1] = k + 1;
                v = rt_tuple_get(rest, k);
                break;
            }
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
