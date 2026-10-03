// strings.c -- the string builtins. Strings are immutable bytes, UTF-8 by
// convention; lengths and indexes count bytes. Where a builtin's behavior
// isn't obvious, it follows ts-slight's (which followed JavaScript's).

#include "rt.h"

#include <stdio.h>
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
    need_int(n, site);
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
