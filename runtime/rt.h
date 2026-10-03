// rt.h -- the runtime's data structures, shared by C, the runtime's
// assembly, and the compiler's output.
//
// The constants at the top are what the assembly side depends on; rt.c
// checks the struct offsets with _Static_assert, so they can't drift.
// Everything here is prefixed (RT_, rt_) because it's included before the
// system headers: macOS's <stdlib.h> pulls in <sys/wait.h>, whose enum has
// a P_PID that an unprefixed macro would clobber. t/headers.c checks this.

#ifndef RT_H
#define RT_H

// Values are 64-bit tagged words (docs/DESIGN.md, "Values").
#define RT_TAG_INT_MASK      1    // ...0   integer, 63-bit, shifted left by one
#define RT_TAG_MASK          7
#define RT_TAG_LIST          1    // 001    pointer to a cons cell; nil is this tag on a null pointer
#define RT_TAG_BOXED         3    // 011    pointer to a header word and payload
#define RT_TAG_SYMBOL        5    // 101    compile-time id
#define RT_TAG_PID           7    // 111

#define RT_PROC_REDUCTIONS   0    // rt_proc_t.reductions (the reduction check: [x28, #0])

#ifndef __ASSEMBLER__

#include <stdint.h>

// Give a C symbol the exact name the assembly uses, on Linux and on macOS
// (where C names would otherwise get a leading underscore).
#define RT_ASM(name) __asm__(#name)

typedef uint64_t rt_value_t;

// A process. Compiled code finds the current one in x28.
typedef struct rt_proc {
    int64_t reductions;
} rt_proc_t;

// The compiled program: the body of the root process.
rt_value_t slight_main(void) RT_ASM(slight_main);

// Runs fn with x28 = proc, and returns its result (rt_asm.S).
rt_value_t rt_enter(rt_proc_t *proc, rt_value_t (*fn)(void)) RT_ASM(rt_enter);

#endif // __ASSEMBLER__
#endif // RT_H
