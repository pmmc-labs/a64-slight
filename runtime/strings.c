// strings.c -- the string builtins. Strings are immutable bytes, UTF-8 by
// convention; lengths and indexes count bytes, except in the utf8/
// builtins at the end, which count characters. Where a builtin's behavior
// isn't obvious, it follows ts-slight's (which followed JavaScript's).

#include "rt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void need_string(rt_value_t v, const char *site) {
    if (!rt_is_string(v)) rt_fault(RT_FAULT_NOT_STRING, v, site);
}

static int64_t need_int(rt_value_t v, const char *site) {
    if (v & RT_TAG_INT_MASK) rt_fault(RT_FAULT_NOT_INT, v, site);
    return rt_int_value(v);
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

static rt_value_t buf_to_string(rt_buf_t *b, const char *site) {
    rt_value_t s = rt_new_string(b->bytes, b->len, site);
    rt_buf_free(b);
    return s;
}

// Where m first occurs in s at or after `from`, or -1. An empty m is
// found straight away.
static int64_t find(const char *s, size_t len, const char *m, size_t mlen, size_t from) {
    for (size_t i = from; i + mlen <= len; i++) {
        if (memcmp(s + i, m, mlen) == 0) return (int64_t)i;
    }
    return -1;
}

rt_value_t rt_is_str(rt_value_t v) {
    return rt_is_string(v) ? RT_TRUE : RT_FALSE;
}

rt_value_t rt_str_len(rt_value_t s, const char *site) {
    need_string(s, site);
    return rt_int((int64_t)rt_string_len(s));
}

// Bytes [start, end). As in JavaScript, each index is clamped to the
// string, and the two are swapped if start is after end.
rt_value_t rt_substring(rt_value_t s, rt_value_t start, rt_value_t end, const char *site) {
    need_string(s, site);
    int64_t len = (int64_t)rt_string_len(s);
    int64_t a   = need_int(start, site);
    int64_t b   = need_int(end, site);
    a = a < 0 ? 0 : a > len ? len : a;
    b = b < 0 ? 0 : b > len ? len : b;
    if (a > b) {
        int64_t t = a;
        a = b;
        b = t;
    }
    return rt_new_string(rt_string_bytes(s) + a, (size_t)(b - a), site);
}

// Strings as their bytes, anything else as pprint shows it.
rt_value_t rt_concat(rt_value_t args, const char *site) {
    rt_buf_t b = { 0 };
    for (; rt_is_cons(args); args = rt_cdr(args)) rt_render(&b, rt_car(args), 1);
    return buf_to_string(&b, site);
}

// `~`: two strings, and only strings.
rt_value_t rt_concat2(rt_value_t a, rt_value_t b, const char *site) {
    need_string(a, site);
    need_string(b, site);
    rt_buf_t buf = { 0 };
    rt_buf_add(&buf, rt_string_bytes(a), rt_string_len(a));
    rt_buf_add(&buf, rt_string_bytes(b), rt_string_len(b));
    return buf_to_string(&buf, site);
}

rt_value_t rt_index_of(rt_value_t s, rt_value_t m, const char *site) {
    need_string(s, site);
    need_string(m, site);
    return rt_int(find(rt_string_bytes(s), rt_string_len(s), rt_string_bytes(m), rt_string_len(m), 0));
}

// The pieces between the separators. An empty string has no pieces; an
// empty separator splits into single bytes.
rt_value_t rt_str_split(rt_value_t s, rt_value_t sep, const char *site) {
    need_string(s, site);
    need_string(sep, site);
    const char *bytes = rt_string_bytes(s), *m = rt_string_bytes(sep);
    size_t      len   = rt_string_len(s), mlen = rt_string_len(sep);
    if (len == 0) return RT_NIL;

    // Build the list front to back: `last` is the cell whose cdr comes next.
    rt_value_t  list = RT_NIL;
    rt_value_t *last = &list;
    size_t      at   = 0;
    for (;;) {
        int64_t found = mlen == 0 ? (at + 1 < len ? (int64_t)at + 1 : -1) : find(bytes, len, m, mlen, at);
        size_t  end   = found < 0 ? len : (size_t)found;
        rt_value_t cell = cons(rt_new_string(bytes + at, end - at, site), RT_NIL, site);
        *last = cell;
        last  = (rt_value_t *)(cell - RT_TAG_LIST) + 1;
        if (found < 0) return list;
        at = end + mlen;
    }
}

// The elements of xs, rendered as concat does, with sep between them.
rt_value_t rt_str_join(rt_value_t sep, rt_value_t xs, const char *site) {
    need_string(sep, site);
    need_list(xs, site);
    rt_buf_t b = { 0 };
    for (; rt_is_cons(xs); xs = rt_cdr(xs)) {
        rt_render(&b, rt_car(xs), 1);
        if (rt_cdr(xs) != RT_NIL) rt_buf_add(&b, rt_string_bytes(sep), rt_string_len(sep));
    }
    return buf_to_string(&b, site);
}

// A decimal integer, with an optional '-', or #false if s isn't one that
// fits in 63 bits.
rt_value_t rt_string_to_int(rt_value_t s, const char *site) {
    need_string(s, site);
    const char *p   = rt_string_bytes(s);
    size_t      len = rt_string_len(s), i = p[0] == '-' ? 1 : 0;
    if (i == len) return RT_FALSE;
    // accumulate as a negative number, which has room for the most negative one
    const int64_t min = -((int64_t)1 << 62);
    int64_t n = 0;
    for (; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') return RT_FALSE;
        int digit = p[i] - '0';
        if (n < (min + digit) / 10) return RT_FALSE;
        n = n * 10 - digit;
    }
    if (p[0] != '-') {
        if (n == min) return RT_FALSE;
        n = -n;
    }
    return rt_int(n);
}

static size_t past_digits(const char *p, size_t len, size_t i) {
    while (i < len && p[i] >= '0' && p[i] <= '9') i++;
    return i;
}

// A decimal number, -?D+(.D+)?([eE][+-]?D+)?, as the nearest float, or
// #false if s isn't one or it's too big for a float. strtod rounds
// correctly on glibc and macOS alike, so this agrees with the reader's
// float literals; and the runtime never sets a locale, so the point is ".".
rt_value_t rt_string_to_float(rt_value_t s, const char *site) {
    need_string(s, site);
    const char *p   = rt_string_bytes(s);
    size_t      len = rt_string_len(s), sign = p[0] == '-' ? 1 : 0;
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

rt_value_t rt_symbol_to_string(rt_value_t sym, const char *site) {
    if ((sym & RT_TAG_MASK) != RT_TAG_SYMBOL) rt_fault(RT_FAULT_NOT_SYMBOL, sym, site);
    const char *name = rt_symbol_name(sym >> RT_SYMBOL_SHIFT);
    return rt_new_string(name, strlen(name), site);
}

// The symbol with this name, if the program mentions it anywhere; symbols
// are only made by the compiler. Otherwise #false.
rt_value_t rt_string_to_symbol(rt_value_t s, const char *site) {
    need_string(s, site);
    for (uint64_t id = 0; id < slight_symbol_count; id++) {
        const char *name = rt_symbol_name(id);
        if (strlen(name) == rt_string_len(s) && memcmp(name, rt_string_bytes(s), rt_string_len(s)) == 0) {
            return (rt_value_t)(id << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL);
        }
    }
    return RT_FALSE;
}

rt_value_t rt_byte_at(rt_value_t s, rt_value_t i, const char *site) {
    need_string(s, site);
    int64_t at = need_int(i, site);
    if (at < 0 || (uint64_t)at >= rt_string_len(s)) rt_fault(RT_FAULT_RANGE, i, site);
    return rt_int((unsigned char)rt_string_bytes(s)[at]);
}

rt_value_t rt_bytes_to_string(rt_value_t xs, const char *site) {
    need_list(xs, site);
    rt_buf_t b = { 0 };
    for (; rt_is_cons(xs); xs = rt_cdr(xs)) {
        int64_t byte = need_int(rt_car(xs), site);
        if (byte < 0 || byte > 255) rt_fault(RT_FAULT_RANGE, rt_car(xs), site);
        char c = (char)byte;
        rt_buf_add(&b, &c, 1);
    }
    return buf_to_string(&b, site);
}

// n as text, padded at the start to `width` bytes with fill (default " "),
// repeated and cut to fit, as JavaScript's padStart does.
rt_value_t rt_format_num(rt_value_t n, rt_value_t width, rt_value_t fill, const char *site) {
    if ((n & RT_TAG_INT_MASK) && !rt_is_float(n)) rt_fault(RT_FAULT_NOT_NUMBER, n, site);
    int64_t w = need_int(width, site);
    if (fill == RT_NIL) fill = rt_new_string(" ", 1, site);
    need_string(fill, site);
    rt_buf_t text = { 0 }, b = { 0 };
    rt_render(&text, n, 1);
    size_t flen = rt_string_len(fill);
    for (size_t i = 0; flen > 0 && (int64_t)(text.len + i) < w; i++) rt_buf_add(&b, rt_string_bytes(fill) + i % flen, 1);
    rt_buf_add(&b, text.bytes, text.len);
    rt_buf_free(&text);
    return buf_to_string(&b, site);
}

// Writes its arguments, rendered as concat does, straight to the terminal.
rt_value_t rt_tty_write(rt_value_t args, const char *site) {
    (void)site;
    rt_buf_t b = { 0 };
    for (; rt_is_cons(args); args = rt_cdr(args)) rt_render(&b, rt_car(args), 1);
    fwrite(b.bytes, 1, b.len, stdout);
    fflush(stdout);
    rt_buf_free(&b);
    return RT_NIL;
}

// --- UTF-8 (D148) -------------------------------------------------------------

// The utf8/ builtins count characters. A character is a well-formed UTF-8
// sequence or, failing that, a stray byte on its own, whose code point is
// U+FFFD, as in Go. So any string is a run of characters, nothing faults
// on bad bytes, and utf8/chars loses nothing.

// The length of the well-formed sequence at the start of the n > 0 bytes
// at p, with its code point in *code; 0 if they don't start one. The
// second byte's range rules out overlong forms, surrogates and what's
// past U+10FFFF (Unicode's table 3-7).
static size_t sequence(const unsigned char *p, size_t n, uint32_t *code) {
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

// How many bytes the character at p takes, of the n > 0 left.
static size_t char_len(const char *p, size_t n) {
    uint32_t code;
    size_t   len = sequence((const unsigned char *)p, n, &code);
    return len ? len : 1;
}

// Where character i starts in the len bytes at s, or len if it's past the
// last.
static size_t char_offset(const char *s, size_t len, int64_t i) {
    size_t at = 0;
    for (; i > 0 && at < len; i--) at += char_len(s + at, len - at);
    return at;
}

rt_value_t rt_utf8_len(rt_value_t s, const char *site) {
    need_string(s, site);
    const char *p   = rt_string_bytes(s);
    size_t      len = rt_string_len(s);
    int64_t     n   = 0;
    for (size_t at = 0; at < len; n++) at += char_len(p + at, len - at);
    return rt_int(n);
}

// Characters [start, end), clamped and swapped as substring's bytes are.
// Only the clamp at 0 is needed: char_offset stops at the end anyway.
rt_value_t rt_utf8_substring(rt_value_t s, rt_value_t start, rt_value_t end, const char *site) {
    need_string(s, site);
    int64_t a = need_int(start, site);
    int64_t b = need_int(end, site);
    a = a < 0 ? 0 : a;
    b = b < 0 ? 0 : b;
    if (a > b) {
        int64_t t = a;
        a = b;
        b = t;
    }
    const char *p    = rt_string_bytes(s);
    size_t      len  = rt_string_len(s);
    size_t      from = char_offset(p, len, a);
    size_t      to   = from + char_offset(p + from, len - from, b - a);
    return rt_new_string(p + from, to - from, site);
}

// The character index of the first m, or -1. Only a match that starts a
// character counts, which matters only when m starts with a stray byte.
rt_value_t rt_utf8_index_of(rt_value_t s, rt_value_t m, const char *site) {
    need_string(s, site);
    need_string(m, site);
    const char *p   = rt_string_bytes(s), *q = rt_string_bytes(m);
    size_t      len = rt_string_len(s), mlen = rt_string_len(m);
    int64_t     i   = 0;
    for (size_t at = 0; at + mlen <= len; at += char_len(p + at, len - at), i++) {
        if (memcmp(p + at, q, mlen) == 0) return rt_int(i);
    }
    return rt_int(-1);
}

// The characters, as one-character strings. Joined, they're s again.
rt_value_t rt_utf8_chars(rt_value_t s, const char *site) {
    need_string(s, site);
    const char *p    = rt_string_bytes(s);
    size_t      len  = rt_string_len(s);
    rt_value_t  list = RT_NIL;
    rt_value_t *last = &list;
    for (size_t at = 0; at < len;) {
        size_t     n    = char_len(p + at, len - at);
        rt_value_t cell = cons(rt_new_string(p + at, n, site), RT_NIL, site);
        *last = cell;
        last  = (rt_value_t *)(cell - RT_TAG_LIST) + 1;
        at   += n;
    }
    return list;
}

// The code point of s's first character, as Perl's ord gives (ord is this,
// in the prelude). "" has none.
rt_value_t rt_utf8_code(rt_value_t s, const char *site) {
    need_string(s, site);
    if (rt_string_len(s) == 0) rt_fault(RT_FAULT_RANGE, s, site);
    uint32_t code = 0xfffd;
    sequence((const unsigned char *)rt_string_bytes(s), rt_string_len(s), &code);
    return rt_int(code);
}

// The one-character string for code point n, as Perl's chr gives (chr is
// this, in the prelude). A surrogate, or anything past U+10FFFF, has no
// UTF-8.
rt_value_t rt_utf8_char(rt_value_t n, const char *site) {
    int64_t c = need_int(n, site);
    if (c < 0 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) rt_fault(RT_FAULT_RANGE, n, site);
    static const unsigned char lead[] = { 0, 0, 0xc0, 0xe0, 0xf0 };
    char   b[4];
    size_t len = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    for (size_t i = len - 1; i > 0; i--, c >>= 6) b[i] = (char)(0x80 | (c & 0x3f));
    b[0] = (char)(lead[len] | c);
    return rt_new_string(b, len, site);
}

// Whether s is all well-formed sequences, with no stray bytes.
rt_value_t rt_utf8_valid(rt_value_t s, const char *site) {
    need_string(s, site);
    const unsigned char *p   = (const unsigned char *)rt_string_bytes(s);
    size_t               len = rt_string_len(s);
    for (size_t at = 0; at < len;) {
        uint32_t code;
        size_t   n = sequence(p + at, len - at, &code);
        if (n == 0) return RT_FALSE;
        at += n;
    }
    return RT_TRUE;
}
