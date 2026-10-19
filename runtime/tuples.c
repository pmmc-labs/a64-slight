// tuples.c -- the tuple builtins (D169). A tuple is a box: the header (how
// many elements, and the tuple type), then the elements, one word each,
// so any of them is one load away. Like everything else, a tuple never
// changes: tuple/set makes a new one.

#include "rt.h"

#include <string.h>

static void need_tuple(rt_value_t v, const char *site) {
    if (!rt_is_tuple(v)) rt_fault(RT_FAULT_NOT_TUPLE, v, site);
}

// A new tuple of n elements, for the caller to fill.
static uint64_t *new_tuple(uint64_t n, const char *site) {
    if (n > (UINT64_MAX >> RT_BOX_SIZE_SHIFT) / 8) rt_fault(RT_FAULT_HEAP, RT_NIL, site);
    uint64_t *box = rt_alloc((8 + 8 * n + 15) & ~(size_t)15, site);
    box[0] = n << RT_BOX_SIZE_SHIFT | RT_BOX_TUPLE;
    return box;
}

static rt_value_t tagged(uint64_t *box) { return (rt_value_t)box | RT_TAG_BOXED; }

// (tuple x ...), whose arguments come as one list, and (list->tuple xs).
rt_value_t rt_tuple(rt_value_t xs, const char *site) {
    if ((xs & RT_TAG_MASK) != RT_TAG_LIST) rt_fault(RT_FAULT_NOT_LIST, xs, site);
    uint64_t n = 0;
    for (rt_value_t v = xs; v != RT_NIL; v = rt_cdr(v)) n++;
    uint64_t *box = new_tuple(n, site);
    for (uint64_t i = 1; xs != RT_NIL; xs = rt_cdr(xs)) box[i++] = rt_car(xs);
    return tagged(box);
}

rt_value_t rt_is_tuple_p(rt_value_t v) {
    return rt_is_tuple(v) ? RT_TRUE : RT_FALSE;
}

rt_value_t rt_tuple_len_of(rt_value_t t, const char *site) {
    need_tuple(t, site);
    return rt_int((int64_t)rt_tuple_len(t));
}

// The element at index i, from 0; any other index faults.
static uint64_t index_of(rt_value_t t, rt_value_t i, const char *site) {
    need_tuple(t, site);
    int64_t k = rt_need_int(i, site);
    if (k < 0 || (uint64_t)k >= rt_tuple_len(t)) rt_fault(RT_FAULT_RANGE, i, site);
    return (uint64_t)k;
}

rt_value_t rt_tuple_ref(rt_value_t t, rt_value_t i, const char *site) {
    return rt_tuple_get(t, index_of(t, i, site));
}

// A copy of t with v at index i.
rt_value_t rt_tuple_set(rt_value_t t, rt_value_t i, rt_value_t v, const char *site) {
    uint64_t  k   = index_of(t, i, site);
    uint64_t  n   = rt_tuple_len(t);
    uint64_t *box = new_tuple(n, site);
    memcpy(box + 1, rt_box(t) + 1, 8 * n);
    box[1 + k] = v;
    return tagged(box);
}

// A copy of t one longer, v at index i and what was there from i on after
// it: i is from 0 to t's length, so v can go at the end. A node of a hash
// trie grows a slot this way (D223).
rt_value_t rt_tuple_insert(rt_value_t t, rt_value_t i, rt_value_t v, const char *site) {
    need_tuple(t, site);
    int64_t  k = rt_need_int(i, site);
    uint64_t n = rt_tuple_len(t);
    if (k < 0 || (uint64_t)k > n) rt_fault(RT_FAULT_RANGE, i, site);
    uint64_t *box = new_tuple(n + 1, site);
    memcpy(box + 1, rt_box(t) + 1, 8 * (uint64_t)k);
    box[1 + k] = v;
    memcpy(box + 2 + k, rt_box(t) + 1 + k, 8 * (n - (uint64_t)k));
    return tagged(box);
}

// A copy of t one shorter, without the element at index i.
rt_value_t rt_tuple_remove(rt_value_t t, rt_value_t i, const char *site) {
    uint64_t  k   = index_of(t, i, site);
    uint64_t  n   = rt_tuple_len(t);
    uint64_t *box = new_tuple(n - 1, site);
    memcpy(box + 1, rt_box(t) + 1, 8 * k);
    memcpy(box + 1 + k, rt_box(t) + 2 + k, 8 * (n - 1 - k));
    return tagged(box);
}

// n elements, each v.
rt_value_t rt_tuple_make(rt_value_t n, rt_value_t v, const char *site) {
    int64_t k = rt_need_int(n, site);
    if (k < 0) rt_fault(RT_FAULT_RANGE, n, site);
    uint64_t *box = new_tuple((uint64_t)k, site);
    for (int64_t i = 0; i < k; i++) box[1 + i] = v;
    return tagged(box);
}

// The elements from start up to but not including end, each clamped to
// the tuple; empty when start isn't before end (JavaScript's slice, but
// for counting from the end).
rt_value_t rt_tuple_slice(rt_value_t t, rt_value_t start, rt_value_t end, const char *site) {
    need_tuple(t, site);
    int64_t n = (int64_t)rt_tuple_len(t);
    int64_t a = rt_need_int(start, site), b = rt_need_int(end, site);
    a = a < 0 ? 0 : a > n ? n : a;
    b = b < 0 ? 0 : b > n ? n : b;
    uint64_t  k   = b > a ? (uint64_t)(b - a) : 0;
    uint64_t *box = new_tuple(k, site);
    memcpy(box + 1, rt_box(t) + 1 + a, 8 * k);
    return tagged(box);
}

rt_value_t rt_tuple_to_list(rt_value_t t, const char *site) {
    need_tuple(t, site);
    uint64_t n = rt_tuple_len(t);
    if (n == 0) return RT_NIL;
    rt_value_t *cells = rt_alloc(16 * n, site);
    for (uint64_t i = 0; i < n; i++) {
        cells[2 * i]     = rt_tuple_get(t, i);
        cells[2 * i + 1] = i + 1 < n ? (rt_value_t)&cells[2 * i + 2] | RT_TAG_LIST : RT_NIL;
    }
    return (rt_value_t)cells | RT_TAG_LIST;
}
