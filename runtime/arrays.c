// arrays.c -- arrays of numbers (D253). An array is a box of one kind of
// number, unboxed: :float, IEEE doubles, or :int, slight's integers, each
// in an int64 (so in 63 bits, and an element always reads back). The
// header has its type and how many elements, which follow it; or, at
// RT_SHARED_MIN bytes or more, they're a shared buffer's (process.c), so
// a big array is free to send. Like everything else, an array never
// changes: array/set makes a new one. It holds no pointers, so neither
// the collector nor a message's copy walks it.
//
// Element by element, the operands of a call are all of one kind, an
// array's or a number: an :int array's are integers, and a :float
// array's any numbers, an integer made a float as + makes one; an :int
// array never mixes with a float. Reductions go left to right, so the
// answer is the obvious loop's: clang doesn't reorder a float sum unless
// it's told it may.

#include "rt.h"

#include <string.h>

#define INT_MIN63 (-((int64_t)1 << 62))
#define INT_MAX63 (((int64_t)1 << 62) - 1)

static rt_value_t KIND_FLOAT(void) { return rt_symbol(RT_SYM_ARRAY + RT_ARRAY_FLOAT); }
static rt_value_t KIND_INT(void)   { return rt_symbol(RT_SYM_EXEC + RT_EXEC_INT); }

static uint64_t type_of(rt_value_t a) { return rt_box(a)[0] & RT_BOX_TYPE_MASK; }

static void need_array(rt_value_t v, const char *site) {
    if (!rt_is_array(v)) rt_fault(RT_FAULT_NOT_ARRAY, v, site);
}

rt_value_t rt_new_array(uint64_t type, uint64_t n, const char *site) {
    if (n > (UINT64_MAX >> RT_BOX_SIZE_SHIFT) / 8) rt_fault(RT_FAULT_HEAP, RT_NIL, site);
    uint64_t header = n << RT_BOX_SIZE_SHIFT | type;
    if (8 * n >= RT_SHARED_MIN) {
        char *data;
        return rt_new_shared(header, 8 * n, &data, site);
    }
    uint64_t *box = rt_alloc((8 + 8 * n + 15) & ~(size_t)15, site);
    box[0] = header;
    return (rt_value_t)box | RT_TAG_BOXED;
}

// A fresh array's elements, to fill before anything else runs.
static int64_t *ints(rt_value_t a)  { return (int64_t *)rt_array_data(a); }
static double  *floats(rt_value_t a) { return (double *)rt_array_data(a); }

// The type a kind names: :float or :int; anything else is out of range.
static uint64_t type_named(rt_value_t kind, const char *site) {
    if (kind == KIND_FLOAT()) return RT_BOX_FLOATS;
    if (kind == KIND_INT())   return RT_BOX_INTS;
    rt_fault(RT_FAULT_RANGE, kind, site);
}

// x as an element of an array of type: an integer for :int, which
// faults :not-an-int otherwise; any number for :float, made a float.
static int64_t int_of(rt_value_t x, const char *site) {
    if (rt_is_array(x)) rt_fault(RT_FAULT_NOT_INT, x, site);
    return rt_need_int(x, site);
}

static double float_of(rt_value_t x, const char *site) {
    if ((x & RT_TAG_INT_MASK) == 0) return (double)rt_int_value(x);
    if (!rt_is_float(x)) rt_fault(RT_FAULT_NOT_NUMBER, x, site);
    return rt_float_value(x);
}

static void put(rt_value_t a, uint64_t i, rt_value_t x, const char *site) {
    if (type_of(a) == RT_BOX_INTS) ints(a)[i] = int_of(x, site);
    else                           floats(a)[i] = float_of(x, site);
}

static rt_value_t get(rt_value_t a, uint64_t i, const char *site) {
    if (type_of(a) == RT_BOX_INTS) return rt_int(ints(a)[i]);
    return rt_new_float(floats(a)[i], site);
}

// An integer result, which faults if it doesn't fit in 63 bits.
static int64_t fits(int64_t r, int overflowed, const char *site) {
    if (overflowed || r < INT_MIN63 || r > INT_MAX63) rt_fault(RT_FAULT_OVERFLOW, 0, site);
    return r;
}

// An operand of the kind of type, for a call on arrays: an array of that
// type, as long as n, or a number of the kind.
static void need_operand(uint64_t type, uint64_t n, rt_value_t x, const char *site) {
    if (rt_is_array(x)) {
        if (type_of(x) != type) rt_fault(type == RT_BOX_INTS ? RT_FAULT_NOT_INT : RT_FAULT_NOT_NUMBER, x, site);
        if (rt_array_len(x) != n) rt_fault(RT_FAULT_RANGE, rt_int((int64_t)rt_array_len(x)), site);
    } else if (type == RT_BOX_INTS) {
        int_of(x, site);
    } else {
        float_of(x, site);
    }
}

// --- making and reading ---------------------------------------------------------

rt_value_t rt_is_array_p(rt_value_t v) {
    return rt_is_array(v) ? RT_TRUE : RT_FALSE;
}

// (array/make kind n x): n elements, each x.
rt_value_t rt_array_make(rt_value_t kind, rt_value_t n, rt_value_t x, const char *site) {
    uint64_t type = type_named(kind, site);
    int64_t  k    = rt_need_int(n, site);
    if (k < 0) rt_fault(RT_FAULT_RANGE, n, site);
    need_operand(type, 0, x, site);
    rt_value_t a = rt_new_array(type, (uint64_t)k, site);
    for (int64_t i = 0; i < k; i++) put(a, (uint64_t)i, x, site);
    return a;
}

rt_value_t rt_list_to_array(rt_value_t kind, rt_value_t xs, const char *site) {
    uint64_t type = type_named(kind, site);
    uint64_t n    = 0;
    for (rt_value_t v = xs; v != RT_NIL; v = rt_cdr(v)) {
        if (!rt_is_cons(v)) rt_fault(RT_FAULT_NOT_LIST, xs, site);
        n++;
    }
    rt_value_t a = rt_new_array(type, n, site);
    for (uint64_t i = 0; xs != RT_NIL; xs = rt_cdr(xs)) put(a, i++, rt_car(xs), site);
    return a;
}

rt_value_t rt_array_to_list(rt_value_t a, const char *site) {
    need_array(a, site);
    uint64_t n = rt_array_len(a);
    if (n == 0) return RT_NIL;
    rt_value_t *cells = rt_alloc(16 * n, site);
    for (uint64_t i = 0; i < n; i++) {
        cells[2 * i]     = get(a, i, site);
        cells[2 * i + 1] = i + 1 < n ? (rt_value_t)(cells + 2 * i + 2) | RT_TAG_LIST : RT_NIL;
    }
    return (rt_value_t)cells | RT_TAG_LIST;
}

rt_value_t rt_array_kind(rt_value_t a, const char *site) {
    need_array(a, site);
    return type_of(a) == RT_BOX_INTS ? KIND_INT() : KIND_FLOAT();
}

rt_value_t rt_array_len_of(rt_value_t a, const char *site) {
    need_array(a, site);
    return rt_int((int64_t)rt_array_len(a));
}

// The index i, from 0; any other faults, as tuple/get's does.
static uint64_t index_of(rt_value_t a, rt_value_t i, const char *site) {
    need_array(a, site);
    int64_t k = rt_need_int(i, site);
    if (k < 0 || (uint64_t)k >= rt_array_len(a)) rt_fault(RT_FAULT_RANGE, i, site);
    return (uint64_t)k;
}

rt_value_t rt_array_ref(rt_value_t a, rt_value_t i, const char *site) {
    return get(a, index_of(a, i, site), site);
}

// A copy of a with x at index i: O(n), as tuple/set is.
rt_value_t rt_array_set(rt_value_t a, rt_value_t i, rt_value_t x, const char *site) {
    uint64_t k = index_of(a, i, site), n = rt_array_len(a);
    need_operand(type_of(a), n, x, site);
    rt_value_t b = rt_new_array(type_of(a), n, site);
    memcpy(ints(b), rt_array_data(a), 8 * n);
    put(b, k, x, site);
    return b;
}

// The elements from start up to but not including end, each clamped, as
// tuple/slice has it.
rt_value_t rt_array_slice(rt_value_t a, rt_value_t start, rt_value_t end, const char *site) {
    need_array(a, site);
    int64_t n = (int64_t)rt_array_len(a);
    int64_t s = rt_need_int(start, site), e = rt_need_int(end, site);
    s = s < 0 ? 0 : s > n ? n : s;
    e = e < 0 ? 0 : e > n ? n : e;
    uint64_t   k = e > s ? (uint64_t)(e - s) : 0;
    rt_value_t b = rt_new_array(type_of(a), k, site);
    memcpy(ints(b), rt_array_data(a) + s, 8 * k);
    return b;
}

rt_value_t rt_array_concat(rt_value_t a, rt_value_t b, const char *site) {
    need_array(a, site);
    need_array(b, site);
    uint64_t n = rt_array_len(a), m = rt_array_len(b);
    need_operand(type_of(a), m, b, site);
    rt_value_t c = rt_new_array(type_of(a), n + m, site);
    memcpy(ints(c), rt_array_data(a), 8 * n);
    memcpy(ints(c) + n, rt_array_data(b), 8 * m);
    return c;
}

// --- element by element -----------------------------------------------------------

enum { ADD, SUB, MUL, DIV };

// The ith operand of x: its element, or x itself if it's a number.
static int64_t int_at(rt_value_t x, uint64_t i) {
    return rt_is_array(x) ? ints(x)[i] : rt_int_value(x);
}

static double float_at(rt_value_t x, uint64_t i) {
    if (rt_is_array(x)) return type_of(x) == RT_BOX_INTS ? (double)ints(x)[i] : floats(x)[i];
    return (x & RT_TAG_INT_MASK) == 0 ? (double)rt_int_value(x) : rt_float_value(x);
}

// a op b, element by element, either one a number; the kind is the
// arrays', and division's result is always :float, as / gives a float.
static rt_value_t elementwise(rt_value_t a, rt_value_t b, int op, const char *site) {
    rt_value_t first = rt_is_array(a) ? a : b;
    if (!rt_is_array(first)) rt_fault(RT_FAULT_NOT_ARRAY, a, site);
    uint64_t type = type_of(first), n = rt_array_len(first);
    need_operand(type, n, a, site);
    need_operand(type, n, b, site);
    if (op == DIV) {
        for (uint64_t i = 0; i < (rt_is_array(b) ? n : 1); i++) {
            if (float_at(b, i) == 0) rt_fault(RT_FAULT_DIV_ZERO, 0, site);
        }
        rt_value_t c = rt_new_array(RT_BOX_FLOATS, n, site);
        double    *r = floats(c);
        for (uint64_t i = 0; i < n; i++) r[i] = float_at(a, i) / float_at(b, i);
        return c;
    }
    rt_value_t c = rt_new_array(type, n, site);
    if (type == RT_BOX_INTS) {
        int64_t *r = ints(c);
        for (uint64_t i = 0; i < n; i++) {
            int64_t x = int_at(a, i), y = int_at(b, i), z;
            int     o = op == ADD ? __builtin_add_overflow(x, y, &z)
                      : op == SUB ? __builtin_sub_overflow(x, y, &z)
                      :             __builtin_mul_overflow(x, y, &z);
            r[i] = fits(z, o, site);
        }
    } else {
        double *r = floats(c);
        for (uint64_t i = 0; i < n; i++) {
            double x = float_at(a, i), y = float_at(b, i);
            r[i] = op == ADD ? x + y : op == SUB ? x - y : x * y;
        }
    }
    return c;
}

rt_value_t rt_array_add(rt_value_t a, rt_value_t b, const char *site) { return elementwise(a, b, ADD, site); }
rt_value_t rt_array_sub(rt_value_t a, rt_value_t b, const char *site) { return elementwise(a, b, SUB, site); }
rt_value_t rt_array_mul(rt_value_t a, rt_value_t b, const char *site) { return elementwise(a, b, MUL, site); }
rt_value_t rt_array_div(rt_value_t a, rt_value_t b, const char *site) { return elementwise(a, b, DIV, site); }

// --- reductions, left to right --------------------------------------------------------

rt_value_t rt_array_sum(rt_value_t a, const char *site) {
    need_array(a, site);
    uint64_t n = rt_array_len(a);
    if (type_of(a) == RT_BOX_INTS) {
        int64_t s = 0;
        for (uint64_t i = 0; i < n; i++) s = fits(s + ints(a)[i], 0, site);
        return rt_int(s);
    }
    double s = 0.0;
    for (uint64_t i = 0; i < n; i++) s += floats(a)[i];
    return rt_new_float(s, site);
}

// The least (greatest) element, the first on a tie, as math/min takes
// them two at a time; so a NaN is the answer only if it's first. An
// empty array has none, and faults.
static rt_value_t extreme(rt_value_t a, int least, const char *site) {
    need_array(a, site);
    uint64_t n = rt_array_len(a);
    if (n == 0) rt_fault(RT_FAULT_RANGE, a, site);
    if (type_of(a) == RT_BOX_INTS) {
        int64_t m = ints(a)[0];
        for (uint64_t i = 1; i < n; i++) if (least ? ints(a)[i] < m : ints(a)[i] > m) m = ints(a)[i];
        return rt_int(m);
    }
    double m = floats(a)[0];
    for (uint64_t i = 1; i < n; i++) if (least ? floats(a)[i] < m : floats(a)[i] > m) m = floats(a)[i];
    return rt_new_float(m, site);
}

rt_value_t rt_array_min(rt_value_t a, const char *site) { return extreme(a, 1, site); }
rt_value_t rt_array_max(rt_value_t a, const char *site) { return extreme(a, 0, site); }

// The sum of the products of n pairs, left to right from 0, each product
// and sum rounded on its own, -ffp-contract=off keeping them apart.
static int64_t mul_add(int64_t s, int64_t x, int64_t y, const char *site) {
    int64_t z;
    int     o = __builtin_mul_overflow(x, y, &z);
    return fits(s + fits(z, o, site), 0, site);
}

rt_value_t rt_array_dot(rt_value_t a, rt_value_t b, const char *site) {
    need_array(a, site);
    need_array(b, site);
    uint64_t n = rt_array_len(a);
    need_operand(type_of(a), n, b, site);
    if (type_of(a) == RT_BOX_INTS) {
        int64_t s = 0;
        for (uint64_t i = 0; i < n; i++) s = mul_add(s, ints(a)[i], ints(b)[i], site);
        return rt_int(s);
    }
    double s = 0.0;
    for (uint64_t i = 0; i < n; i++) s += floats(a)[i] * floats(b)[i];
    return rt_new_float(s, site);
}

// (array/matmul a b n k m): a, n rows of k, times b, k rows of m, row by
// row: an array of n rows of m, each element the dot product of a row of
// a and a column of b, summed in order, as dot sums. Each row of the
// result is summed into a p at a time, which reads b along its rows, and
// changes no element's order. Not BLAS, whose order changes with its
// blocking and threads, and its bits with it.
rt_value_t rt_array_matmul(rt_value_t a, rt_value_t b, rt_value_t rows, rt_value_t inner, rt_value_t cols, const char *site) {
    need_array(a, site);
    need_array(b, site);
    int64_t n = rt_need_int(rows, site), k = rt_need_int(inner, site), m = rt_need_int(cols, site);
    if (n < 0) rt_fault(RT_FAULT_RANGE, rows, site);
    if (k < 0) rt_fault(RT_FAULT_RANGE, inner, site);
    if (m < 0) rt_fault(RT_FAULT_RANGE, cols, site);
    int64_t nk, km, nm;
    if (__builtin_mul_overflow(n, k, &nk) || (uint64_t)nk != rt_array_len(a)) rt_fault(RT_FAULT_RANGE, rt_int((int64_t)rt_array_len(a)), site);
    if (__builtin_mul_overflow(k, m, &km)) rt_fault(RT_FAULT_RANGE, rt_int((int64_t)rt_array_len(b)), site);
    need_operand(type_of(a), (uint64_t)km, b, site);
    if (__builtin_mul_overflow(n, m, &nm)) rt_fault(RT_FAULT_HEAP, RT_NIL, site);
    rt_value_t c = rt_new_array(type_of(a), (uint64_t)nm, site);
    memset(ints(c), 0, 8 * (uint64_t)nm);       // 0 and 0.0 alike
    if (type_of(a) == RT_BOX_INTS) {
        const int64_t *x = ints(a), *y = ints(b);
        int64_t       *z = ints(c);
        for (int64_t i = 0; i < n; i++) {
            for (int64_t p = 0; p < k; p++) {
                for (int64_t j = 0; j < m; j++) z[i * m + j] = mul_add(z[i * m + j], x[i * k + p], y[p * m + j], site);
            }
        }
        return c;
    }
    const double *x = floats(a), *y = floats(b);
    double       *z = floats(c);
    if (m == 1) {                               // a vector: each row's dot product, in one register
        for (int64_t i = 0; i < n; i++) {
            double s = 0.0;
            for (int64_t p = 0; p < k; p++) s += x[i * k + p] * y[p];
            z[i] = s;
        }
        return c;
    }
    for (int64_t i = 0; i < n; i++) {
        for (int64_t p = 0; p < k; p++) {
            for (int64_t j = 0; j < m; j++) z[i * m + j] += x[i * k + p] * y[p * m + j];
        }
    }
    return c;
}
