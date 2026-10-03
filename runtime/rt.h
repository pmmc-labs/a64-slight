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
#define RT_SYMBOL_SHIFT      3    // a symbol is id << 3 | RT_TAG_SYMBOL

#define RT_NIL               1    // the list tag on a null pointer
#define RT_FALSE             5    // symbol 0
#define RT_TRUE             13    // symbol 1

// What rt_fault is told went wrong.
#define RT_FAULT_NOT_INT     1    // an integer operation was given something else
#define RT_FAULT_OVERFLOW    2    // an integer result doesn't fit in 63 bits
#define RT_FAULT_NOT_BOOL    3    // a cond test was neither #true nor #false
#define RT_FAULT_NO_CLAUSE   4    // no cond clause matched

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

// --- emitted by the compiler -------------------------------------------------

// The body of the root process.
rt_value_t slight_main(void) RT_ASM(slight_main);

// Every symbol's name, in id order, as consecutive NUL-terminated strings.
extern const uint64_t slight_symbol_count   RT_ASM(slight_symbol_count);
extern const char     slight_symbol_names[] RT_ASM(slight_symbol_names);

// --- called by compiled code --------------------------------------------------

// Something went wrong at `site` (a description and a source position);
// `value` is the offending value, where there is one. Ends the program for
// now; it will end only the process once there are processes.
__attribute__((noreturn))
void rt_fault(uint64_t fault, rt_value_t value, const char *site) RT_ASM(rt_fault);

// Prints a value and a newline. Returns nil.
rt_value_t rt_pprint(rt_value_t v) RT_ASM(rt_pprint);

// --- the runtime itself -------------------------------------------------------

// Runs fn with x28 = proc, and returns its result (rt_asm.S).
rt_value_t rt_enter(rt_proc_t *proc, rt_value_t (*fn)(void)) RT_ASM(rt_enter);

#endif // __ASSEMBLER__
#endif // RT_H
