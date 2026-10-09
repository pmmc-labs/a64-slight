// numbers.c -- arithmetic that isn't on two integers, and the math
// builtins. Integers and floats mix: if either side is a float, so is the
// result. Integer results that don't fit in 63 bits fault.

#include "rt.h"

#include <math.h>

#define INT_MIN63 (-((int64_t)1 << 62))
#define INT_MAX63 (((int64_t)1 << 62) - 1)

static int is_int(rt_value_t v) { return (v & RT_TAG_INT_MASK) == 0; }

static double as_double(rt_value_t v, const char *site) {
    if (is_int(v)) return (double)rt_int_value(v);
    if (!rt_is_float(v)) rt_fault(RT_FAULT_NOT_NUMBER, v, site);
    return rt_float_value(v);
}

// Tagged integers are n << 1, so the words add, subtract, and (with one
// side untagged) multiply like the integers, and 64-bit overflow is 63-bit
// overflow.
rt_value_t rt_add(rt_value_t a, rt_value_t b, const char *site) {
    if (is_int(a) && is_int(b)) {
        int64_t r;
        if (__builtin_add_overflow((int64_t)a, (int64_t)b, &r)) rt_fault(RT_FAULT_OVERFLOW, 0, site);
        return (rt_value_t)r;
    }
    return rt_new_float(as_double(a, site) + as_double(b, site), site);
}

rt_value_t rt_sub(rt_value_t a, rt_value_t b, const char *site) {
    if (is_int(a) && is_int(b)) {
        int64_t r;
        if (__builtin_sub_overflow((int64_t)a, (int64_t)b, &r)) rt_fault(RT_FAULT_OVERFLOW, 0, site);
        return (rt_value_t)r;
    }
    return rt_new_float(as_double(a, site) - as_double(b, site), site);
}

rt_value_t rt_mul(rt_value_t a, rt_value_t b, const char *site) {
    if (is_int(a) && is_int(b)) {
        int64_t r;
        if (__builtin_mul_overflow((int64_t)a, rt_int_value(b), &r)) rt_fault(RT_FAULT_OVERFLOW, 0, site);
        return (rt_value_t)r;
    }
    return rt_new_float(as_double(a, site) * as_double(b, site), site);
}

// `/` always gives a float.
rt_value_t rt_divide(rt_value_t a, rt_value_t b, const char *site) {
    double x = as_double(a, site), y = as_double(b, site);
    if (y == 0) rt_fault(RT_FAULT_DIV_ZERO, 0, site);
    return rt_new_float(x / y, site);
}

rt_value_t rt_compare(rt_value_t a, rt_value_t b, uint64_t op, const char *site) {
    // all three, separately: with a NaN, none of them holds
    int lt, eq, gt;
    if (is_int(a) && is_int(b)) {
        lt = (int64_t)a < (int64_t)b;
        eq = a == b;
        gt = (int64_t)a > (int64_t)b;
    } else {
        double x = as_double(a, site), y = as_double(b, site);
        lt = x < y;
        eq = x == y;
        gt = x > y;
    }
    int yes = 0;
    switch (op) {
        case RT_CMP_EQ: yes = eq; break;
        case RT_CMP_NE: yes = !eq; break;
        case RT_CMP_LT: yes = lt; break;
        case RT_CMP_LE: yes = lt || eq; break;
        case RT_CMP_GT: yes = gt; break;
        case RT_CMP_GE: yes = gt || eq; break;
    }
    return yes ? RT_TRUE : RT_FALSE;
}

rt_value_t rt_is_flt(rt_value_t v) { return rt_is_float(v) ? RT_TRUE : RT_FALSE; }
rt_value_t rt_is_num(rt_value_t v) { return is_int(v) || rt_is_float(v) ? RT_TRUE : RT_FALSE; }

// --- rounding: an integer stays itself; a float becomes the integer -------

static rt_value_t to_int(double d, rt_value_t original, const char *site) {
    if (!(d >= (double)INT_MIN63 && d < -(double)INT_MIN63)) rt_fault(RT_FAULT_RANGE, original, site);
    return rt_int((int64_t)d);
}

static rt_value_t rounded(rt_value_t x, double (*f)(double), const char *site) {
    if (is_int(x)) return x;
    return to_int(f(as_double(x, site)), x, site);
}

// Halves go up, as JavaScript's Math.round does (D72):
// (round 2.5) is 3 and (round -2.5) is -2.
static double round_half_up(double d) {
    double r = floor(d);
    return d - r >= 0.5 ? r + 1 : r;
}

rt_value_t rt_ceil(rt_value_t x, const char *site)  { return rounded(x, ceil, site); }
rt_value_t rt_floor(rt_value_t x, const char *site) { return rounded(x, floor, site); }
rt_value_t rt_round(rt_value_t x, const char *site) { return rounded(x, round_half_up, site); }
rt_value_t rt_trunc(rt_value_t x, const char *site) { return rounded(x, trunc, site); }

// --- the rest -----------------------------------------------------------------

rt_value_t rt_abs(rt_value_t x, const char *site) {
    if (is_int(x)) {
        int64_t n = rt_int_value(x);
        if (n == INT_MIN63) rt_fault(RT_FAULT_OVERFLOW, 0, site);
        return rt_int(n < 0 ? -n : n);
    }
    return rt_new_float(fabs(as_double(x, site)), site);
}

// The smaller (larger) argument, as it was given; the first one on a tie.
// Two integers compare as integers: as doubles, those near 2^62 that
// round to the same one would tie.
rt_value_t rt_min(rt_value_t a, rt_value_t b, const char *site) {
    if (is_int(a) && is_int(b)) return (int64_t)b < (int64_t)a ? b : a;
    return as_double(b, site) < as_double(a, site) ? b : a;
}

rt_value_t rt_max(rt_value_t a, rt_value_t b, const char *site) {
    if (is_int(a) && is_int(b)) return (int64_t)b > (int64_t)a ? b : a;
    return as_double(b, site) > as_double(a, site) ? b : a;
}

// These always give floats, as `/` does.
static rt_value_t math(rt_value_t x, double (*f)(double), const char *site) {
    return rt_new_float(f(as_double(x, site)), site);
}

rt_value_t rt_pow(rt_value_t a, rt_value_t b, const char *site) {
    return rt_new_float(pow(as_double(a, site), as_double(b, site)), site);
}

rt_value_t rt_sqrt(rt_value_t x, const char *site) { return math(x, sqrt, site); }
rt_value_t rt_sin(rt_value_t x, const char *site)  { return math(x, sin, site); }
rt_value_t rt_cos(rt_value_t x, const char *site)  { return math(x, cos, site); }
rt_value_t rt_tan(rt_value_t x, const char *site)  { return math(x, tan, site); }
rt_value_t rt_exp(rt_value_t x, const char *site)  { return math(x, exp, site); }
