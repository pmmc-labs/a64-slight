// rt.c -- the runtime. For now: run the root process, print its result,
// and the few things compiled code calls.

#include "rt.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(offsetof(rt_proc_t, reductions) == RT_PROC_REDUCTIONS, "RT_PROC_REDUCTIONS");
_Static_assert(RT_FALSE == (0 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_FALSE is symbol 0");
_Static_assert(RT_TRUE  == (1 << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL), "RT_TRUE is symbol 1");

static const char *symbol_name(uint64_t id) {
    if (id >= slight_symbol_count) return NULL;
    const char *name = slight_symbol_names;
    for (uint64_t i = 0; i < id; i++) name += strlen(name) + 1;
    return name;
}

static void print_value(FILE *out, rt_value_t v) {
    if ((v & RT_TAG_INT_MASK) == 0) {
        fprintf(out, "%" PRId64, (int64_t)v >> 1);
        return;
    }
    if (v == RT_NIL) {
        fputs("()", out);
        return;
    }
    if ((v & RT_TAG_MASK) == RT_TAG_SYMBOL) {
        const char *name = symbol_name(v >> RT_SYMBOL_SHIFT);
        if (name) {
            fputs(name, out);
            return;
        }
    }
    fprintf(out, "#<value 0x%016" PRIx64 ">", v);
}

rt_value_t rt_pprint(rt_value_t v) {
    print_value(stdout, v);
    putchar('\n');
    return RT_NIL;
}

void rt_fault(uint64_t fault, rt_value_t value, const char *site) {
    fflush(stdout);                             // so a fault lands after the output before it
    fputs("fault: ", stderr);
    switch (fault) {
        case RT_FAULT_NOT_INT:   fputs("not an integer: ", stderr); print_value(stderr, value); break;
        case RT_FAULT_OVERFLOW:  fputs("integer overflow", stderr); break;
        case RT_FAULT_NOT_BOOL:  fputs("not a boolean: ", stderr);  print_value(stderr, value); break;
        case RT_FAULT_NO_CLAUSE: fputs("no cond clause matched", stderr); break;
        default:                 fprintf(stderr, "unknown fault %" PRIu64, fault); break;
    }
    fprintf(stderr, " (%s)\n", site);
    exit(1);
}

int main(void) {
    rt_proc_t  root = { .reductions = INT64_MAX };
    rt_value_t v    = rt_enter(&root, slight_main);
    print_value(stdout, v);
    putchar('\n');
    return 0;
}
