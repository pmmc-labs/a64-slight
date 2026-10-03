// rt.c -- the runtime. For now: run the root process and print its result.

#include "rt.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

_Static_assert(offsetof(rt_proc_t, reductions) == RT_PROC_REDUCTIONS, "RT_PROC_REDUCTIONS");

static void print_value(FILE *out, rt_value_t v) {
    if ((v & RT_TAG_INT_MASK) == 0) fprintf(out, "%" PRId64, (int64_t)v >> 1);
    else                            fprintf(out, "#<value 0x%016" PRIx64 ">", v);
}

int main(void) {
    rt_proc_t  root = { .reductions = INT64_MAX };
    rt_value_t v    = rt_enter(&root, slight_main);
    print_value(stdout, v);
    putchar('\n');
    return 0;
}
