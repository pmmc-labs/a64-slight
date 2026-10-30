// numbers.c -- arithmetic that isn't on two integers, and the math
// builtins. Integers and floats mix: if either side is a float, so is the
// result. Integer results that don't fit in 63 bits fault.

#include "rt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
// CORE-MATH's (vendor/core-math, D253): each the double nearest the exact
// result, so the same on every machine, where libms differ in the last
// bit (D76). sqrt is IEEE's, correctly rounded already.
double cr_sin(double), cr_cos(double), cr_tan(double), cr_asin(double), cr_acos(double), cr_atan(double);
double cr_sinh(double), cr_cosh(double), cr_tanh(double), cr_asinh(double), cr_acosh(double), cr_atanh(double);
double cr_exp(double), cr_expm1(double), cr_log(double), cr_log2(double), cr_log10(double), cr_log1p(double);
double cr_cbrt(double), cr_pow(double, double), cr_atan2(double, double), cr_hypot(double, double);

static rt_value_t math(rt_value_t x, double (*f)(double), const char *site) {
    return rt_new_float(f(as_double(x, site)), site);
}

// Two integers give an integer, as * does, by squaring: a result past 63
// bits overflows, and a negative exponent, whose result isn't an integer,
// is out of range. A float on either side gives a float (D221).
rt_value_t rt_pow(rt_value_t a, rt_value_t b, const char *site) {
    if (!is_int(a) || !is_int(b)) return rt_new_float(cr_pow(as_double(a, site), as_double(b, site)), site);
    int64_t base = rt_int_value(a), e = rt_int_value(b), r = 1;
    if (e < 0) rt_fault(RT_FAULT_RANGE, b, site);
    // Once the base's square overflows, so does the result: what's left of
    // the exponent is at least 2.
    for (; e > 0; e >>= 1) {
        if ((e & 1) && (__builtin_mul_overflow(r, base, &r) || r < INT_MIN63 || r > INT_MAX63)) rt_fault(RT_FAULT_OVERFLOW, 0, site);
        if (e > 1 && (__builtin_mul_overflow(base, base, &base) || base > INT_MAX63)) rt_fault(RT_FAULT_OVERFLOW, 0, site);
    }
    return rt_int(r);
}

rt_value_t rt_sqrt(rt_value_t x, const char *site) { return math(x, sqrt, site); }
rt_value_t rt_sin(rt_value_t x, const char *site)  { return math(x, cr_sin, site); }
rt_value_t rt_cos(rt_value_t x, const char *site)  { return math(x, cr_cos, site); }
rt_value_t rt_tan(rt_value_t x, const char *site)  { return math(x, cr_tan, site); }
rt_value_t rt_exp(rt_value_t x, const char *site)  { return math(x, cr_exp, site); }
rt_value_t rt_asin(rt_value_t x, const char *site)  { return math(x, cr_asin, site); }
rt_value_t rt_acos(rt_value_t x, const char *site)  { return math(x, cr_acos, site); }
rt_value_t rt_atan(rt_value_t x, const char *site)  { return math(x, cr_atan, site); }
rt_value_t rt_sinh(rt_value_t x, const char *site)  { return math(x, cr_sinh, site); }
rt_value_t rt_cosh(rt_value_t x, const char *site)  { return math(x, cr_cosh, site); }
rt_value_t rt_tanh(rt_value_t x, const char *site)  { return math(x, cr_tanh, site); }
rt_value_t rt_asinh(rt_value_t x, const char *site) { return math(x, cr_asinh, site); }
rt_value_t rt_acosh(rt_value_t x, const char *site) { return math(x, cr_acosh, site); }
rt_value_t rt_atanh(rt_value_t x, const char *site) { return math(x, cr_atanh, site); }
rt_value_t rt_log(rt_value_t x, const char *site)   { return math(x, cr_log, site); }
rt_value_t rt_log2(rt_value_t x, const char *site)  { return math(x, cr_log2, site); }
rt_value_t rt_log10(rt_value_t x, const char *site) { return math(x, cr_log10, site); }
rt_value_t rt_log1p(rt_value_t x, const char *site) { return math(x, cr_log1p, site); }
rt_value_t rt_expm1(rt_value_t x, const char *site) { return math(x, cr_expm1, site); }
rt_value_t rt_cbrt(rt_value_t x, const char *site)  { return math(x, cr_cbrt, site); }

rt_value_t rt_atan2(rt_value_t y, rt_value_t x, const char *site) {
    return rt_new_float(cr_atan2(as_double(y, site), as_double(x, site)), site);
}

rt_value_t rt_hypot(rt_value_t x, rt_value_t y, const char *site) {
    return rt_new_float(cr_hypot(as_double(x, site), as_double(y, site)), site);
}

// The remainder of a floored division: its sign is b's, so (mod -1 5) is 4,
// where (% -1 5) is -1. For integers, as % is (D221).
rt_value_t rt_mod(rt_value_t a, rt_value_t b, const char *site) {
    int64_t x = rt_need_int(a, site), y = rt_need_int(b, site);
    if (y == 0) rt_fault(RT_FAULT_DIV_ZERO, 0, site);
    int64_t r = x % y;
    return rt_int(r != 0 && (r < 0) != (y < 0) ? r + y : r);
}

// Anything but a float that's NaN is #false, as JavaScript's Number.isNaN has it.
rt_value_t rt_is_nan(rt_value_t v) {
    return rt_is_float(v) && isnan(rt_float_value(v)) ? RT_TRUE : RT_FALSE;
}

// The bits set in n, as 63 bits of two's complement: (bit/count -1) is 63.
// The other bit operations are compiled inline (D221).
rt_value_t rt_bit_count(rt_value_t n, const char *site) {
    return rt_int(__builtin_popcountll((uint64_t)rt_need_int(n, site) & ~((uint64_t)1 << 63)));
}

// The nearest float: exact up to 2^53.
rt_value_t rt_int_to_float(rt_value_t n, const char *site) {
    return rt_new_float((double)rt_need_int(n, site), site);
}

// x as a float, as str shows one; or, with digits (0 to 100), in fixed
// notation with that many after the point, as JavaScript's toFixed: the
// nearest such decimal to x's exact value, a tie going away from zero
// (printf's goes to even, so it's rounded here, from the exact digits,
// which "%.1100f" gives, a double's expansion ending well before that).
// Past 1e21, nan and inf, as str shows them, as toFixed does.
rt_value_t rt_float_to_str(rt_value_t x, rt_value_t digits, const char *site) {
    double d = as_double(x, site);
    rt_buf_t b = { 0 };
    if (digits == RT_NIL || !(fabs(d) < 1e21)) {
        rt_render(&b, rt_new_float(d, site), 1);
    } else {
        int64_t f = rt_need_int(digits, site);
        if (f < 0 || f > 100) rt_fault(RT_FAULT_RANGE, digits, site);
        char  exact[1500];
        snprintf(exact, sizeof exact, "%.1100f", fabs(d));
        char  *point = strchr(exact, '.');
        size_t keep = (size_t)(point - exact) + (f ? 1 + (size_t)f : 0);   // the digits kept, and the point
        char   out[1500];
        out[0] = '0';                               // room for a carry out of the top digit
        memcpy(out + 1, exact, keep);
        size_t len = keep + 1;
        if (point[f + 1] >= '5') {                  // the rest is at least half of the last kept digit
            for (size_t i = len - 1;; i--) {
                if (out[i] == '.') continue;
                if (out[i] != '9') {
                    out[i]++;
                    break;
                }
                out[i] = '0';
            }
        }
        if (d < 0) rt_buf_add(&b, "-", 1);
        size_t from = out[0] == '0' ? 1 : 0;
        rt_buf_add(&b, out + from, len - from);
    }
    rt_value_t s = rt_new_string(b.bytes, b.len, site);
    rt_buf_free(&b);
    return s;
}

// --- random numbers -------------------------------------------------------------
//
// Java's SplittableRandom (Steele, Lea and Flood): SplitMix64, a state that
// steps by an odd gamma, each step mixed. Splitting draws a new state and
// gamma from the parent's stream, so a process's numbers depend on the run's
// seed and its place in the tree of forks, never on how processes
// interleave (D222). Not for keys or anything that mustn't be guessed.

#define GOLDEN_GAMMA 0x9e3779b97f4a7c15u

static uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

// An odd gamma with enough bit changes in it, which a mixed number may not
// have.
static uint64_t mix_gamma(uint64_t z) {
    z = (z ^ (z >> 33)) * 0xff51afd7ed558ccdu;
    z = (z ^ (z >> 33)) * 0xc4ceb9fe1a85ec53u;
    z = (z ^ (z >> 33)) | 1;
    return __builtin_popcountll(z ^ (z >> 1)) < 24 ? z ^ 0xaaaaaaaaaaaaaaaau : z;
}

static uint64_t next_seed(rt_proc_t *p) { return p->rand_seed += p->rand_gamma; }
static uint64_t next64(rt_proc_t *p)    { return mix64(next_seed(p)); }

void rt_rand_start(rt_proc_t *p, uint64_t seed) {
    p->rand_seed  = seed;
    p->rand_gamma = GOLDEN_GAMMA;
}

void rt_rand_split(rt_proc_t *parent, rt_proc_t *child) {
    child->rand_seed  = next64(parent);
    child->rand_gamma = mix_gamma(next_seed(parent));
}

// 0 to n - 1, each as likely: a draw in the top part of the range that
// would make some come up more often than others is drawn again, as Java's
// nextLong(bound) has it.
rt_value_t rt_rand_int(rt_value_t n, const char *site) {
    int64_t bound = rt_need_int(n, site);
    if (bound < 1) rt_fault(RT_FAULT_RANGE, n, site);
    uint64_t m = (uint64_t)bound - 1, r = next64(rt_current);
    if (((uint64_t)bound & m) == 0) return rt_int((int64_t)(r & m));
    for (uint64_t u = r >> 1; (int64_t)(u + m - (r = u % (uint64_t)bound)) < 0; u = next64(rt_current) >> 1) {}
    return rt_int((int64_t)r);
}

// [0, 1), in steps of 2^-53.
rt_value_t rt_rand_float(const char *site) {
    return rt_new_float((double)(next64(rt_current) >> 11) * 0x1.0p-53, site);
}

rt_value_t rt_rand_seed(rt_value_t n, const char *site) {
    rt_rand_start(rt_current, (uint64_t)rt_need_int(n, site));
    return RT_NIL;
}
