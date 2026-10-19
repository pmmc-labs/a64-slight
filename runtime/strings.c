// strings.c -- the string builtins (D219). Strings are immutable bytes,
// UTF-8 by convention. The str/ builtins count characters: a character is
// a well-formed UTF-8 sequence or, failing that, a stray byte on its own,
// whose code point is U+FFFD, as in Go, so nothing faults on bad bytes
// and nothing is lost (D148). The bytes/ builtins count bytes, in one step
// each, for parsers and protocols. Where a builtin's behavior isn't
// obvious, it follows JavaScript's string methods, with characters in
// place of UTF-16 units.

#include "rt.h"
#include "unicode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void need_string(rt_value_t v, const char *site) {
    if (!rt_is_string(v)) rt_fault(RT_FAULT_NOT_STRING, v, site);
}

static rt_value_t need_list(rt_value_t v, const char *site) {
    if ((v & RT_TAG_MASK) != RT_TAG_LIST) rt_fault(RT_FAULT_NOT_LIST, v, site);
    return v;
}

static rt_value_t cons(rt_value_t car, rt_value_t cdr, const char *site) {
    rt_value_t *cell = rt_alloc(16, site);
    cell[0] = car;
    cell[1] = cdr;
    return (rt_value_t)cell | RT_TAG_LIST;
}

// A list built front to back: `last` is where the next cell goes.
typedef struct {
    rt_value_t  list;
    rt_value_t *last;
} builder_t;

static void builder_init(builder_t *b) {
    b->list = RT_NIL;
    b->last = &b->list;
}

static void builder_add(builder_t *b, rt_value_t x, const char *site) {
    rt_value_t cell = cons(x, RT_NIL, site);
    *b->last = cell;
    b->last  = (rt_value_t *)(cell - RT_TAG_LIST) + 1;
}

static rt_value_t buf_to_string(rt_buf_t *b, const char *site) {
    rt_value_t s = rt_new_string(b->bytes, b->len, site);
    rt_buf_free(b);
    return s;
}

static const char *bytes_of(rt_value_t s) { return rt_string_bytes(s); }
static size_t      len_of(rt_value_t s)   { return rt_string_len(s); }

// An optional integer argument, () when it isn't given.
static int64_t opt_int(rt_value_t v, int64_t otherwise, const char *site) {
    return v == RT_NIL ? otherwise : rt_need_int(v, site);
}

// An index counted from the end when it's negative, then clamped to
// [0, n], as JavaScript's slice takes its arguments.
static int64_t relative(int64_t i, int64_t n) {
    return i < 0 ? (i + n < 0 ? 0 : i + n) : (i > n ? n : i);
}

// --- characters ---------------------------------------------------------------

// The length of the well-formed sequence at the start of the n > 0 bytes
// at p, with its code point in *code; 0 if they don't start one. The
// second byte's range rules out overlong forms, surrogates and what's
// past U+10FFFF (Unicode's table 3-7).
size_t rt_utf8_sequence(const unsigned char *p, size_t n, uint32_t *code) {
    unsigned char b = p[0];
    if (b < 0x80) {
        *code = b;
        return 1;
    }
    size_t        len = b >= 0xc2 && b <= 0xdf ? 2 : b >= 0xe0 && b <= 0xef ? 3 : b >= 0xf0 && b <= 0xf4 ? 4 : 0;
    unsigned char lo  = b == 0xe0 ? 0xa0 : b == 0xf0 ? 0x90 : 0x80;
    unsigned char hi  = b == 0xed ? 0x9f : b == 0xf4 ? 0x8f : 0xbf;
    if (len == 0 || len > n || p[1] < lo || p[1] > hi) return 0;
    uint32_t c = b & (0x7f >> len);
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xc0) != 0x80) return 0;
        c = c << 6 | (p[i] & 0x3f);
    }
    *code = c;
    return len;
}

// The character at p, of the n > 0 bytes left: how many bytes it takes,
// with its code point in *code (U+FFFD for a stray byte) and *stray set if
// it's one.
static size_t char_at(const char *p, size_t n, uint32_t *code, int *stray) {
    size_t len = rt_utf8_sequence((const unsigned char *)p, n, code);
    *stray = len == 0;
    if (len) return len;
    *code = 0xfffd;
    return 1;
}

static size_t char_len(const char *p, size_t n) {
    uint32_t code;
    int      stray;
    return char_at(p, n, &code, &stray);
}

static int64_t count_chars(rt_value_t s) {
    if (rt_string_ascii(s)) return (int64_t)len_of(s);
    const char *p   = bytes_of(s);
    size_t      len = len_of(s);
    int64_t     n   = 0;
    for (size_t at = 0; at < len; n++) at += char_len(p + at, len - at);
    return n;
}

// Where character i starts, counting from byte `at`, or the end if it's
// past the last.
static size_t offset_from(rt_value_t s, size_t at, int64_t i) {
    size_t len = len_of(s);
    if (rt_string_ascii(s)) return at + (size_t)i < len ? at + (size_t)i : len;
    for (const char *p = bytes_of(s); i > 0 && at < len; i--) at += char_len(p + at, len - at);
    return at;
}

static size_t offset_of(rt_value_t s, int64_t i) { return offset_from(s, 0, i); }

// The UTF-8 of code point c, which the caller has checked has some.
static void put_code(rt_buf_t *b, uint32_t c) {
    static const unsigned char lead[] = { 0, 0, 0xc0, 0xe0, 0xf0 };
    char   u[4];
    size_t len = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    for (size_t i = len - 1; i > 0; i--, c >>= 6) u[i] = (char)(0x80 | (c & 0x3f));
    u[0] = (char)(lead[len] | c);
    rt_buf_add(b, u, len);
}

static int is_scalar(int64_t c) { return c >= 0 && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff); }

static int continuation(char c) { return ((unsigned char)c & 0xc0) == 0x80; }

// Whether m's last character is a stray byte, which may be the start of a
// longer character in a string m is matched in.
static int stray_end(const char *m, size_t mlen) {
    uint32_t code;
    int      stray = 0;
    for (size_t at = 0; at < mlen;) at += char_at(m + at, mlen - at, &code, &stray);
    return stray;
}

// Whether, from a character's start at `at`, the characters of the len
// bytes at p land on `to`.
static int lands(const char *p, size_t len, size_t at, size_t to) {
    while (at < to) at += char_len(p + at, len - at);
    return at == to;
}

// The byte offset of the first m in the len bytes at p at or after byte
// `from` (a character's start), or -1. A match is whole characters: it
// starts at one, which it does anyway unless m starts with a continuation
// byte, and it ends at one, which it does anyway unless m ends with a
// stray byte. Only a stray byte in m makes either matter.
static int64_t find(const char *p, size_t len, const char *m, size_t mlen, size_t from) {
    if (mlen == 0) return from <= len ? (int64_t)from : -1;
    int any = !continuation(m[0]), open = stray_end(m, mlen);
    for (size_t at = from; at + mlen <= len; at += any ? 1 : char_len(p + at, len - at)) {
        if (p[at] == m[0] && memcmp(p + at, m, mlen) == 0 && (!open || lands(p, len, at, at + mlen))) return (int64_t)at;
    }
    return -1;
}

// How many characters are in the bytes [from, to) of s.
static int64_t chars_between(rt_value_t s, size_t from, size_t to) {
    if (rt_string_ascii(s)) return (int64_t)(to - from);
    const char *p = bytes_of(s);
    int64_t     n = 0;
    for (size_t at = from; at < to; n++) at += char_len(p + at, to - at);
    return n;
}

// --- making strings -------------------------------------------------------------

rt_value_t rt_is_str(rt_value_t v) {
    return rt_is_string(v) ? RT_TRUE : RT_FALSE;
}

// Strings as their bytes, anything else as pprint shows it.
rt_value_t rt_str(rt_value_t args, const char *site) {
    rt_buf_t b = { 0 };
    for (; rt_is_cons(args); args = rt_cdr(args)) rt_render(&b, rt_car(args), 1);
    return buf_to_string(&b, site);
}

// `~`: strings, and only strings.
rt_value_t rt_cat(rt_value_t args, const char *site) {
    rt_buf_t b = { 0 };
    for (; rt_is_cons(args); args = rt_cdr(args)) {
        rt_value_t s = rt_car(args);
        if (!rt_is_string(s)) {
            rt_buf_free(&b);
            rt_fault(RT_FAULT_NOT_STRING, s, site);
        }
        rt_buf_add(&b, bytes_of(s), len_of(s));
    }
    return buf_to_string(&b, site);
}

// The elements of xs, rendered as str does, with sep between them.
rt_value_t rt_str_join(rt_value_t sep, rt_value_t xs, const char *site) {
    need_string(sep, site);
    need_list(xs, site);
    rt_buf_t b = { 0 };
    for (; rt_is_cons(xs); xs = rt_cdr(xs)) {
        rt_render(&b, rt_car(xs), 1);
        if (rt_cdr(xs) != RT_NIL) rt_buf_add(&b, bytes_of(sep), len_of(sep));
    }
    return buf_to_string(&b, site);
}

// A string of len bytes, made whole before it's filled, so one past what
// the heap holds faults at once (D205).
static rt_value_t blank(size_t len, const char *site) {
    return rt_new_string(NULL, len, site);
}

static size_t size_times(size_t a, size_t b, const char *site) {
    size_t n;
    if (__builtin_mul_overflow(a, b, &n)) rt_fault(RT_FAULT_HEAP, RT_NIL, site);
    return n;
}

rt_value_t rt_str_repeat(rt_value_t s, rt_value_t n, const char *site) {
    need_string(s, site);
    int64_t k = rt_need_int(n, site);
    if (k < 0) rt_fault(RT_FAULT_RANGE, n, site);
    size_t     len = len_of(s);
    rt_value_t out = blank(size_times(len, (size_t)k, site), site);
    char      *p   = (char *)bytes_of(out);
    for (int64_t i = 0; i < k; i++) memcpy(p + (size_t)i * len, bytes_of(s), len);
    rt_string_mark(out);
    return out;
}

// s padded to n characters with fill, repeated and cut to fit, at the
// start or the end, as JavaScript's padStart and padEnd do.
static rt_value_t pad(rt_value_t s, rt_value_t n, rt_value_t fill, int at_start, const char *site) {
    need_string(s, site);
    int64_t want = rt_need_int(n, site);
    if (fill == RT_NIL) fill = rt_new_string(" ", 1, site);
    need_string(fill, site);
    int64_t have = count_chars(s), flen = count_chars(fill);
    if (want <= have || flen == 0) return s;
    size_t whole = (size_t)((want - have) / flen), part = offset_of(fill, (want - have) % flen);
    size_t extra = size_times(whole, len_of(fill), site) + part;
    rt_value_t out = blank(len_of(s) + extra, site);
    char      *p   = (char *)bytes_of(out), *q = p + (at_start ? 0 : len_of(s));
    for (size_t i = 0; i < whole; i++) memcpy(q + i * len_of(fill), bytes_of(fill), len_of(fill));
    memcpy(q + whole * len_of(fill), bytes_of(fill), part);
    memcpy(at_start ? p + extra : p, bytes_of(s), len_of(s));
    rt_string_mark(out);
    return out;
}

rt_value_t rt_str_pad_start(rt_value_t s, rt_value_t n, rt_value_t fill, const char *site) {
    return pad(s, n, fill, 1, site);
}

rt_value_t rt_str_pad_end(rt_value_t s, rt_value_t n, rt_value_t fill, const char *site) {
    return pad(s, n, fill, 0, site);
}

// --- characters by index --------------------------------------------------------

rt_value_t rt_str_len(rt_value_t s, const char *site) {
    need_string(s, site);
    return rt_int(count_chars(s));
}

// Where character i is, counting from the end when it's negative, as
// JavaScript's at does; out of range faults.
static size_t char_index(rt_value_t s, rt_value_t i, const char *site) {
    int64_t n = count_chars(s), k = rt_need_int(i, site);
    if (k < 0) k += n;
    if (k < 0 || k >= n) rt_fault(RT_FAULT_RANGE, i, site);
    return offset_of(s, k);
}

rt_value_t rt_str_at(rt_value_t s, rt_value_t i, const char *site) {
    need_string(s, site);
    size_t at = char_index(s, i, site);
    return rt_new_string(bytes_of(s) + at, char_len(bytes_of(s) + at, len_of(s) - at), site);
}

rt_value_t rt_str_code_at(rt_value_t s, rt_value_t i, const char *site) {
    need_string(s, site);
    size_t   at = char_index(s, i, site);
    uint32_t code;
    int      stray;
    char_at(bytes_of(s) + at, len_of(s) - at, &code, &stray);
    return rt_int(code);
}

// Characters [start, end), as JavaScript's slice: an index counts from
// the end when it's negative, each is clamped, and the slice is empty
// unless start is before end. end is the end of the string if it's ().
rt_value_t rt_str_slice(rt_value_t s, rt_value_t start, rt_value_t end, const char *site) {
    need_string(s, site);
    int64_t n = count_chars(s);
    int64_t a = relative(rt_need_int(start, site), n);
    int64_t b = end == RT_NIL ? n : relative(rt_need_int(end, site), n);
    if (a >= b) return rt_new_string("", 0, site);
    size_t from = offset_of(s, a), to = offset_from(s, from, b - a);
    return rt_new_string(bytes_of(s) + from, to - from, site);
}

// The character index of the first m at or after character `from`
// (clamped to the string), or -1; an empty m is at `from`.
rt_value_t rt_str_index_of(rt_value_t s, rt_value_t m, rt_value_t from, const char *site) {
    need_string(s, site);
    need_string(m, site);
    int64_t f = opt_int(from, 0, site);
    if (f < 0) f = 0;
    size_t  start = offset_of(s, f);
    int64_t found = find(bytes_of(s), len_of(s), bytes_of(m), len_of(m), start);
    if (found < 0) return rt_int(-1);
    return rt_int(chars_between(s, 0, start) + chars_between(s, start, (size_t)found));
}

// The character index of the last m that starts at or before character
// `from` (the end, if it's ()), or -1.
rt_value_t rt_str_last_index_of(rt_value_t s, rt_value_t m, rt_value_t from, const char *site) {
    need_string(s, site);
    need_string(m, site);
    int64_t n = count_chars(s), f = opt_int(from, n, site);
    f = f < 0 ? 0 : f > n ? n : f;
    size_t  limit = offset_of(s, f), at = 0;
    int64_t i = 0, last = -1;
    for (int64_t found; (found = find(bytes_of(s), len_of(s), bytes_of(m), len_of(m), at)) >= 0 && (size_t)found <= limit;) {
        i   += chars_between(s, at, (size_t)found);
        last = i;
        at   = (size_t)found;
        if (at == len_of(s)) break;
        at += char_len(bytes_of(s) + at, len_of(s) - at);
        i++;
    }
    return rt_int(last);
}

rt_value_t rt_str_includes(rt_value_t s, rt_value_t m, const char *site) {
    need_string(s, site);
    need_string(m, site);
    return find(bytes_of(s), len_of(s), bytes_of(m), len_of(m), 0) >= 0 ? RT_TRUE : RT_FALSE;
}

// Whole characters, as find's matches are.
rt_value_t rt_str_starts_with(rt_value_t s, rt_value_t m, const char *site) {
    need_string(s, site);
    need_string(m, site);
    const char *p = bytes_of(s), *q = bytes_of(m);
    size_t      ls = len_of(s), lm = len_of(m);
    int         yes = lm <= ls && memcmp(p, q, lm) == 0 && (!stray_end(q, lm) || lands(p, ls, 0, lm));
    return yes ? RT_TRUE : RT_FALSE;
}

rt_value_t rt_str_ends_with(rt_value_t s, rt_value_t m, const char *site) {
    need_string(s, site);
    need_string(m, site);
    const char *p = bytes_of(s), *q = bytes_of(m);
    size_t      ls = len_of(s), lm = len_of(m);
    int         yes = lm <= ls && memcmp(p + ls - lm, q, lm) == 0 && (lm == 0 || !continuation(q[0]) || lands(p, ls, 0, ls - lm));
    return yes ? RT_TRUE : RT_FALSE;
}

// --- splitting and changing -------------------------------------------------------

// The pieces of s between the seps, at most limit of them (all, if it's
// ()), as JavaScript's split: "" is one empty piece, and an empty sep
// splits into characters.
rt_value_t rt_str_split(rt_value_t s, rt_value_t sep, rt_value_t limit, const char *site) {
    need_string(s, site);
    need_string(sep, site);
    int64_t max = opt_int(limit, INT64_MAX, site);
    if (max < 0) rt_fault(RT_FAULT_RANGE, limit, site);
    const char *p   = bytes_of(s), *m = bytes_of(sep);
    size_t      len = len_of(s), mlen = len_of(sep);
    builder_t   out;
    builder_init(&out);
    if (mlen == 0) {
        for (size_t at = 0; at < len && max > 0; max--) {
            size_t n = char_len(p + at, len - at);
            builder_add(&out, rt_new_string(p + at, n, site), site);
            at += n;
        }
        return out.list;
    }
    for (size_t at = 0; max > 0; max--) {
        int64_t found = find(p, len, m, mlen, at);
        size_t  end   = found < 0 ? len : (size_t)found;
        builder_add(&out, rt_new_string(p + at, end - at, site), site);
        if (found < 0) break;
        at = end + mlen;
    }
    return out.list;
}

// White_Space, Unicode's: it hasn't changed since 6.3.
static int white_space(uint32_t c) {
    return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

// s without the white space at its start, its end, or both. A stray byte
// isn't white space.
static rt_value_t trim(rt_value_t s, int start, int end, const char *site) {
    need_string(s, site);
    const char *p    = bytes_of(s);
    size_t      len  = len_of(s), from = 0, to = 0;
    int         seen = 0;
    for (size_t at = 0; at < len;) {
        uint32_t code;
        int      stray;
        size_t   n = char_at(p + at, len - at, &code, &stray);
        if (stray || !white_space(code)) {
            if (!seen) from = at;
            seen = 1;
            to   = at + n;
        }
        at += n;
    }
    if (!seen) from = to = start ? len : 0;
    if (!start) from = 0;
    if (!end) to = len;
    return from == 0 && to == len ? s : rt_new_string(p + from, to - from, site);
}

rt_value_t rt_str_trim(rt_value_t s, const char *site)       { return trim(s, 1, 1, site); }
rt_value_t rt_str_trim_start(rt_value_t s, const char *site) { return trim(s, 1, 0, site); }
rt_value_t rt_str_trim_end(rt_value_t s, const char *site)   { return trim(s, 0, 1, site); }

// Whether the capital sigma at byte `at` ends a word, Unicode's
// Final_Sigma: after a cased letter, and not before one, with any
// case-ignorable characters between skipped.
static int final_sigma(const char *p, size_t len, size_t at, uint32_t before) {
    if (!before) return 0;
    for (at += 2; at < len;) {
        uint32_t code;
        int      stray;
        at += char_at(p + at, len - at, &code, &stray);
        if (!rt_unicode_case_ignorable(code)) return !rt_unicode_cased(code);
    }
    return 1;
}

// Every character mapped to its full uppercase or lowercase, without a
// language; a stray byte is kept as it is. In lowercase, a capital sigma
// at the end of a word is a final sigma.
static rt_value_t change_case(rt_value_t s, int upper, const char *site) {
    need_string(s, site);
    const char *p   = bytes_of(s);
    size_t      len = len_of(s);
    if (rt_string_ascii(s)) {
        rt_value_t out = blank(len, site);
        char      *q   = (char *)bytes_of(out);
        for (size_t i = 0; i < len; i++) {
            char c = p[i];
            q[i]   = upper ? (c >= 'a' && c <= 'z' ? c - 32 : c) : (c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        rt_string_mark(out);
        return out;
    }
    rt_buf_t b      = { 0 };
    uint32_t before = 0;                        // whether the last character not case-ignorable was cased
    for (size_t at = 0; at < len;) {
        uint32_t code, to[3];
        int      stray;
        size_t   n = char_at(p + at, len - at, &code, &stray);
        if (stray) {
            rt_buf_add(&b, p + at, 1);
        } else if (!upper && code == 0x3a3) {
            put_code(&b, final_sigma(p, len, at, before) ? 0x3c2 : 0x3c3);
        } else {
            size_t k = upper ? rt_unicode_upper(code, to) : rt_unicode_lower(code, to);
            for (size_t i = 0; i < k; i++) put_code(&b, to[i]);
        }
        if (stray || !rt_unicode_case_ignorable(code)) before = !stray && rt_unicode_cased(code);
        at += n;
    }
    return buf_to_string(&b, site);
}

rt_value_t rt_str_upper(rt_value_t s, const char *site) { return change_case(s, 1, site); }
rt_value_t rt_str_lower(rt_value_t s, const char *site) { return change_case(s, 0, site); }

// s with its first m, or every m, put r in place of; with an empty m, r
// goes in at the start, or before every character and at the end, as
// JavaScript's replace and replaceAll do.
static rt_value_t replace(rt_value_t s, rt_value_t m, rt_value_t r, int all, const char *site) {
    need_string(s, site);
    need_string(m, site);
    need_string(r, site);
    const char *p   = bytes_of(s), *q = bytes_of(m);
    size_t      len = len_of(s), mlen = len_of(m), at = 0;
    rt_buf_t    b   = { 0 };
    for (;;) {
        int64_t found = find(p, len, q, mlen, at);
        if (found < 0) break;
        rt_buf_add(&b, p + at, (size_t)found - at);
        rt_buf_add(&b, bytes_of(r), len_of(r));
        at = (size_t)found + mlen;
        if (!all) break;
        if (mlen == 0) {
            if (at == len) break;
            size_t n = char_len(p + at, len - at);
            rt_buf_add(&b, p + at, n);
            at += n;
        }
    }
    rt_buf_add(&b, p + at, len - at);
    return buf_to_string(&b, site);
}

rt_value_t rt_str_replace(rt_value_t s, rt_value_t m, rt_value_t r, const char *site) {
    return replace(s, m, r, 0, site);
}

rt_value_t rt_str_replace_all(rt_value_t s, rt_value_t m, rt_value_t r, const char *site) {
    return replace(s, m, r, 1, site);
}

// --- comparing ----------------------------------------------------------------------

// By code point, which for UTF-8 is byte by byte; a string before any it
// starts. Stray bytes compare as the bytes they are.
static int compare(rt_value_t a, rt_value_t b, const char *site) {
    need_string(a, site);
    need_string(b, site);
    size_t la = len_of(a), lb = len_of(b);
    int    c  = memcmp(bytes_of(a), bytes_of(b), la < lb ? la : lb);
    return c ? (c < 0 ? -1 : 1) : la < lb ? -1 : la > lb ? 1 : 0;
}

rt_value_t rt_str_compare(rt_value_t a, rt_value_t b, const char *site) {
    return rt_int(compare(a, b, site));
}

rt_value_t rt_str_less(rt_value_t a, rt_value_t b, const char *site) {
    return compare(a, b, site) < 0 ? RT_TRUE : RT_FALSE;
}

// --- characters and code points -----------------------------------------------------

// The characters, as one-character strings. Joined, they're s again.
rt_value_t rt_str_chars(rt_value_t s, const char *site) {
    need_string(s, site);
    const char *p   = bytes_of(s);
    size_t      len = len_of(s);
    builder_t   out;
    builder_init(&out);
    for (size_t at = 0; at < len;) {
        size_t n = char_len(p + at, len - at);
        builder_add(&out, rt_new_string(p + at, n, site), site);
        at += n;
    }
    return out.list;
}

rt_value_t rt_str_codes(rt_value_t s, const char *site) {
    need_string(s, site);
    const char *p   = bytes_of(s);
    size_t      len = len_of(s);
    builder_t   out;
    builder_init(&out);
    for (size_t at = 0; at < len;) {
        uint32_t code;
        int      stray;
        at += char_at(p + at, len - at, &code, &stray);
        builder_add(&out, rt_int(code), site);
    }
    return out.list;
}

// A string of these code points. A surrogate, or anything past U+10FFFF,
// has no UTF-8.
rt_value_t rt_codes_to_str(rt_value_t xs, const char *site) {
    need_list(xs, site);
    rt_buf_t b = { 0 };
    for (; rt_is_cons(xs); xs = rt_cdr(xs)) {
        int64_t c = rt_need_int(rt_car(xs), site);
        if (!is_scalar(c)) {
            rt_buf_free(&b);
            rt_fault(RT_FAULT_RANGE, rt_car(xs), site);
        }
        put_code(&b, (uint32_t)c);
    }
    return buf_to_string(&b, site);
}

// Whether s is all well-formed sequences, with no stray bytes.
rt_value_t rt_str_well_formed(rt_value_t s, const char *site) {
    need_string(s, site);
    if (rt_string_ascii(s)) return RT_TRUE;
    const char *p   = bytes_of(s);
    size_t      len = len_of(s);
    for (size_t at = 0; at < len;) {
        uint32_t code;
        int      stray;
        at += char_at(p + at, len - at, &code, &stray);
        if (stray) return RT_FALSE;
    }
    return RT_TRUE;
}

// s with each stray byte made U+FFFD, as JavaScript's toWellFormed does
// with a lone surrogate; s itself if it has none.
rt_value_t rt_str_to_well_formed(rt_value_t s, const char *site) {
    if (rt_str_well_formed(s, site) == RT_TRUE) return s;
    const char *p   = bytes_of(s);
    size_t      len = len_of(s);
    rt_buf_t    b   = { 0 };
    for (size_t at = 0; at < len;) {
        uint32_t code;
        int      stray;
        size_t   n = char_at(p + at, len - at, &code, &stray);
        if (stray) put_code(&b, 0xfffd);
        else       rt_buf_add(&b, p + at, n);
        at += n;
    }
    return buf_to_string(&b, site);
}

// --- conversions --------------------------------------------------------------------

static int64_t need_radix(rt_value_t radix, const char *site) {
    int64_t r = opt_int(radix, 10, site);
    if (r < 2 || r > 36) rt_fault(RT_FAULT_RANGE, radix, site);
    return r;
}

static int digit_value(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
}

// An integer in radix 2 to 36 (10 if it's ()), with an optional '-', or
// #false if s isn't one that fits in 63 bits.
rt_value_t rt_str_to_int(rt_value_t s, rt_value_t radix, const char *site) {
    need_string(s, site);
    int64_t     r   = need_radix(radix, site);
    const char *p   = bytes_of(s);
    size_t      len = len_of(s), i = len > 0 && p[0] == '-' ? 1 : 0;
    if (i == len) return RT_FALSE;
    // accumulate as a negative number, which has room for the most negative one
    const int64_t min = -((int64_t)1 << 62);
    int64_t n = 0;
    for (; i < len; i++) {
        int d = digit_value(p[i]);
        if (d >= r) return RT_FALSE;
        if (n < (min + d) / r) return RT_FALSE;
        n = n * r - d;
    }
    if (p[0] != '-') {
        if (n == min) return RT_FALSE;
        n = -n;
    }
    return rt_int(n);
}

// n in radix 2 to 36 (10 if it's ()), its letters lowercase.
rt_value_t rt_int_to_str(rt_value_t n, rt_value_t radix, const char *site) {
    int64_t  v = rt_need_int(n, site), r = need_radix(radix, site);
    char     out[72];
    size_t   at = sizeof out;
    uint64_t u  = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    do {
        out[--at] = "0123456789abcdefghijklmnopqrstuvwxyz"[u % (uint64_t)r];
        u        /= (uint64_t)r;
    } while (u);
    if (v < 0) out[--at] = '-';
    return rt_new_string(out + at, sizeof out - at, site);
}

static size_t past_digits(const char *p, size_t len, size_t i) {
    while (i < len && p[i] >= '0' && p[i] <= '9') i++;
    return i;
}

// A decimal number, -?D+(.D+)?([eE][+-]?D+)?, as the nearest float, or
// #false if s isn't one or it's too big for a float. strtod rounds
// correctly on glibc and macOS alike, so this agrees with the reader's
// float literals; and the runtime never sets a locale, so the point is ".".
rt_value_t rt_str_to_float(rt_value_t s, const char *site) {
    need_string(s, site);
    const char *p   = bytes_of(s);
    size_t      len = len_of(s), sign = len > 0 && p[0] == '-' ? 1 : 0;
    size_t      i   = past_digits(p, len, sign);
    if (i == sign) return RT_FALSE;
    if (i < len && p[i] == '.') {
        size_t j = past_digits(p, len, i + 1);
        if (j == i + 1) return RT_FALSE;
        i = j;
    }
    if (i < len && (p[i] == 'e' || p[i] == 'E')) {
        size_t from = i + 1 < len && (p[i + 1] == '+' || p[i + 1] == '-') ? i + 2 : i + 1;
        size_t j    = past_digits(p, len, from);
        if (j == from) return RT_FALSE;
        i = j;
    }
    if (i != len) return RT_FALSE;
    double d = strtod(p, NULL);
    return isinf(d) ? RT_FALSE : rt_new_float(d, site);
}

rt_value_t rt_sym_to_str(rt_value_t sym, const char *site) {
    if ((sym & RT_TAG_MASK) != RT_TAG_SYMBOL) rt_fault(RT_FAULT_NOT_SYMBOL, sym, site);
    const char *name = rt_symbol_name(sym >> RT_SYMBOL_SHIFT);
    return rt_new_string(name, strlen(name), site);
}

// The symbol with this name, if the program mentions it anywhere; symbols
// are only made by the compiler. Otherwise #false.
rt_value_t rt_str_to_sym(rt_value_t s, const char *site) {
    need_string(s, site);
    int64_t id = rt_symbol_find(bytes_of(s), len_of(s));
    return id < 0 ? RT_FALSE : rt_symbol((uint64_t)id);
}

// --- bytes ---------------------------------------------------------------------------

rt_value_t rt_str_to_bytes(rt_value_t s, const char *site) {
    need_string(s, site);
    const unsigned char *p = (const unsigned char *)bytes_of(s);
    builder_t            out;
    builder_init(&out);
    for (size_t i = 0; i < len_of(s); i++) builder_add(&out, rt_int(p[i]), site);
    return out.list;
}

rt_value_t rt_bytes_to_str(rt_value_t xs, const char *site) {
    need_list(xs, site);
    rt_buf_t b = { 0 };
    for (; rt_is_cons(xs); xs = rt_cdr(xs)) {
        int64_t byte = rt_need_int(rt_car(xs), site);
        if (byte < 0 || byte > 255) {
            rt_buf_free(&b);
            rt_fault(RT_FAULT_RANGE, rt_car(xs), site);
        }
        char c = (char)byte;
        rt_buf_add(&b, &c, 1);
    }
    return buf_to_string(&b, site);
}

rt_value_t rt_bytes_len(rt_value_t s, const char *site) {
    need_string(s, site);
    return rt_int((int64_t)len_of(s));
}

// The byte at index i, counting from the end when it's negative; out of
// range faults.
rt_value_t rt_bytes_at(rt_value_t s, rt_value_t i, const char *site) {
    need_string(s, site);
    int64_t n = (int64_t)len_of(s), k = rt_need_int(i, site);
    if (k < 0) k += n;
    if (k < 0 || k >= n) rt_fault(RT_FAULT_RANGE, i, site);
    return rt_int((unsigned char)bytes_of(s)[k]);
}

// Bytes [start, end), as str/slice takes characters.
rt_value_t rt_bytes_slice(rt_value_t s, rt_value_t start, rt_value_t end, const char *site) {
    need_string(s, site);
    int64_t n = (int64_t)len_of(s);
    int64_t a = relative(rt_need_int(start, site), n);
    int64_t b = end == RT_NIL ? n : relative(rt_need_int(end, site), n);
    return a >= b ? rt_new_string("", 0, site) : rt_new_string(bytes_of(s) + a, (size_t)(b - a), site);
}

// The byte index of the first m at or after byte `from`, or -1; an empty m
// is at `from`, clamped to the string.
rt_value_t rt_bytes_index_of(rt_value_t s, rt_value_t m, rt_value_t from, const char *site) {
    need_string(s, site);
    need_string(m, site);
    int64_t n = (int64_t)len_of(s), f = opt_int(from, 0, site);
    f = f < 0 ? 0 : f > n ? n : f;
    const char *p = bytes_of(s), *q = bytes_of(m);
    size_t      mlen = len_of(m);
    if (mlen == 0) return rt_int(f);
    for (size_t at = (size_t)f; at + mlen <= (size_t)n; at++) {
        if (p[at] == q[0] && memcmp(p + at, q, mlen) == 0) return rt_int((int64_t)at);
    }
    return rt_int(-1);
}

// --- output --------------------------------------------------------------------------

// Writes its arguments, rendered as str does, straight to the terminal.
rt_value_t rt_tty_write(rt_value_t args, const char *site) {
    (void)site;
    rt_buf_t b = { 0 };
    for (; rt_is_cons(args); args = rt_cdr(args)) rt_render(&b, rt_car(args), 1);
    fwrite(b.bytes, 1, b.len, stdout);
    fflush(stdout);
    rt_buf_free(&b);
    return RT_NIL;
}
