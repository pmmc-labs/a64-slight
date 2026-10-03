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

// A boxed value points at a header word: the box's type in the low byte,
// its size above. A string's size is its length in bytes; the bytes follow
// the header, then a NUL that the length doesn't count.
#define RT_BOX_TYPE_MASK  0xff
#define RT_BOX_SIZE_SHIFT    8
#define RT_BOX_STRING        1
#define RT_BOX_FLOAT         2    // size 8: an IEEE double
#define RT_BOX_CLOSURE       3    // size: how many captured values

// A closure's words after the header: the code, its arity, its name (a C
// string, for printing), then the captured values. Offsets are from a
// closure *value* (tagged), as compiled code uses them.
#define RT_CLOSURE_CODE      5    //  8 - RT_TAG_BOXED
#define RT_CLOSURE_ARITY    13    // 16 - RT_TAG_BOXED
#define RT_CLOSURE_NAME     21    // 24 - RT_TAG_BOXED
#define RT_CLOSURE_FREE     29    // 32 - RT_TAG_BOXED: the first captured value

#define RT_NIL               1    // the list tag on a null pointer
#define RT_FALSE             5    // symbol 0
#define RT_TRUE             13    // symbol 1

// What rt_fault is told went wrong.
#define RT_FAULT_NOT_INT     1    // an integer operation was given something else
#define RT_FAULT_OVERFLOW    2    // an integer result doesn't fit in 63 bits
#define RT_FAULT_NOT_BOOL    3    // a cond test was neither #true nor #false
#define RT_FAULT_NO_CLAUSE   4    // no cond clause matched
#define RT_FAULT_STACK       5    // a function was called with the stack nearly full
#define RT_FAULT_NOT_CONS    6    // car or cdr of something that isn't a cons
#define RT_FAULT_NOT_LIST    7    // cons onto something that isn't a list
#define RT_FAULT_HEAP        8    // the heap is full
#define RT_FAULT_NOT_STRING  9    // a string operation was given something else
#define RT_FAULT_NOT_SYMBOL 10    // a symbol operation was given something else
#define RT_FAULT_RANGE      11    // an index, a byte, or a rounded float out of range
#define RT_FAULT_NOT_NUMBER 12    // arithmetic on something that isn't a number
#define RT_FAULT_DIV_ZERO   13    // division by zero
#define RT_FAULT_NOT_FUNC   14    // a call to something that isn't a function
#define RT_FAULT_ARITY      15    // a function called with the wrong number of arguments

// What rt_compare is asked.
#define RT_CMP_EQ            0
#define RT_CMP_NE            1
#define RT_CMP_LT            2
#define RT_CMP_LE            3
#define RT_CMP_GT            4
#define RT_CMP_GE            5

#define RT_PROC_REDUCTIONS   0    // rt_proc_t.reductions: the reduction check at function entry
#define RT_PROC_STACK_LIMIT  8    // rt_proc_t.stack_limit: the stack check at function entry
#define RT_PROC_HEAP_PTR    16    // rt_proc_t.heap_ptr: where the next allocation goes
#define RT_PROC_HEAP_LIMIT  24    // rt_proc_t.heap_limit: the end of the heap

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

// Give a C symbol the exact name the assembly uses, on Linux and on macOS
// (where C names would otherwise get a leading underscore).
#define RT_ASM(name) __asm__(#name)

typedef uint64_t rt_value_t;

// A process. Compiled code finds the current one in x28.
typedef struct rt_proc {
    int64_t   reductions;   // calls left before rt_preempt
    uintptr_t stack_limit;  // a function entered with sp below this faults
    uintptr_t heap_ptr;     // bump allocation: compiled code adds to this...
    uintptr_t heap_limit;   // ...and faults when it would pass this
} rt_proc_t;

// A cons cell is two words with no header; a list value points at it,
// plus RT_TAG_LIST.
static inline rt_value_t rt_car(rt_value_t v) { return ((const rt_value_t *)(v - RT_TAG_LIST))[0]; }
static inline rt_value_t rt_cdr(rt_value_t v) { return ((const rt_value_t *)(v - RT_TAG_LIST))[1]; }
static inline int        rt_is_cons(rt_value_t v) { return (v & RT_TAG_MASK) == RT_TAG_LIST && v != RT_NIL; }

static inline const uint64_t *rt_box(rt_value_t v) { return (const uint64_t *)(v - RT_TAG_BOXED); }
static inline int rt_is_box(rt_value_t v, uint64_t type) {
    return (v & RT_TAG_MASK) == RT_TAG_BOXED && (rt_box(v)[0] & RT_BOX_TYPE_MASK) == type;
}
static inline int         rt_is_string(rt_value_t v)    { return rt_is_box(v, RT_BOX_STRING); }
static inline int         rt_is_float(rt_value_t v)     { return rt_is_box(v, RT_BOX_FLOAT); }
static inline int         rt_is_closure(rt_value_t v)   { return rt_is_box(v, RT_BOX_CLOSURE); }
static inline double      rt_float_value(rt_value_t v) {
    double d;
    __builtin_memcpy(&d, rt_box(v) + 1, sizeof d);
    return d;
}
static inline uint64_t    rt_string_len(rt_value_t v)   { return rt_box(v)[0] >> RT_BOX_SIZE_SHIFT; }
static inline const char *rt_string_bytes(rt_value_t v) { return (const char *)(rt_box(v) + 1); }

static inline rt_value_t rt_int(int64_t n)        { return (rt_value_t)((uint64_t)n << 1); }
static inline int64_t    rt_int_value(rt_value_t v) { return (int64_t)v >> 1; }

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

// eq? for two values that aren't the same word: RT_TRUE if they're
// structurally equal, RT_FALSE if not.
rt_value_t rt_equal(rt_value_t a, rt_value_t b) RT_ASM(rt_equal);

// Builtins written in C (strings.c). Each takes the call's site last, for
// its faults. The variadic ones (concat, tty/write) take their arguments
// as a list.
rt_value_t rt_is_str(rt_value_t v) RT_ASM(rt_is_str);
rt_value_t rt_str_len(rt_value_t s, const char *site) RT_ASM(rt_str_len);
rt_value_t rt_substring(rt_value_t s, rt_value_t start, rt_value_t end, const char *site) RT_ASM(rt_substring);
rt_value_t rt_concat(rt_value_t args, const char *site) RT_ASM(rt_concat);
rt_value_t rt_concat2(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_concat2);
rt_value_t rt_index_of(rt_value_t s, rt_value_t m, const char *site) RT_ASM(rt_index_of);
rt_value_t rt_str_split(rt_value_t s, rt_value_t sep, const char *site) RT_ASM(rt_str_split);
rt_value_t rt_str_join(rt_value_t sep, rt_value_t xs, const char *site) RT_ASM(rt_str_join);
rt_value_t rt_string_to_int(rt_value_t s, const char *site) RT_ASM(rt_string_to_int);
rt_value_t rt_symbol_to_string(rt_value_t sym, const char *site) RT_ASM(rt_symbol_to_string);
rt_value_t rt_string_to_symbol(rt_value_t s, const char *site) RT_ASM(rt_string_to_symbol);
rt_value_t rt_byte_at(rt_value_t s, rt_value_t i, const char *site) RT_ASM(rt_byte_at);
rt_value_t rt_bytes_to_string(rt_value_t xs, const char *site) RT_ASM(rt_bytes_to_string);
rt_value_t rt_format_num(rt_value_t n, rt_value_t width, rt_value_t fill, const char *site) RT_ASM(rt_format_num);
rt_value_t rt_tty_write(rt_value_t args, const char *site) RT_ASM(rt_tty_write);

// Functions. rt_apply calls f with the elements of args as its arguments,
// by jumping to it, so f returns straight to apply's caller (rt_asm.S).
rt_value_t rt_apply(rt_value_t f, rt_value_t args, const char *site) RT_ASM(rt_apply);
rt_value_t rt_is_lambda(rt_value_t v) RT_ASM(rt_is_lambda);

// Numbers (numbers.c). The compiler does arithmetic on two integers
// inline, and calls these for anything else.
rt_value_t rt_add(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_add);
rt_value_t rt_sub(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_sub);
rt_value_t rt_mul(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_mul);
rt_value_t rt_divide(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_divide);
rt_value_t rt_compare(rt_value_t a, rt_value_t b, uint64_t op, const char *site) RT_ASM(rt_compare);
rt_value_t rt_is_flt(rt_value_t v) RT_ASM(rt_is_flt);
rt_value_t rt_is_num(rt_value_t v) RT_ASM(rt_is_num);
rt_value_t rt_ceil(rt_value_t x, const char *site) RT_ASM(rt_ceil);
rt_value_t rt_floor(rt_value_t x, const char *site) RT_ASM(rt_floor);
rt_value_t rt_round(rt_value_t x, const char *site) RT_ASM(rt_round);
rt_value_t rt_trunc(rt_value_t x, const char *site) RT_ASM(rt_trunc);
rt_value_t rt_abs(rt_value_t x, const char *site) RT_ASM(rt_abs);
rt_value_t rt_min(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_min);
rt_value_t rt_max(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_max);
rt_value_t rt_pow(rt_value_t a, rt_value_t b, const char *site) RT_ASM(rt_pow);
rt_value_t rt_sqrt(rt_value_t x, const char *site) RT_ASM(rt_sqrt);
rt_value_t rt_sin(rt_value_t x, const char *site) RT_ASM(rt_sin);
rt_value_t rt_cos(rt_value_t x, const char *site) RT_ASM(rt_cos);
rt_value_t rt_tan(rt_value_t x, const char *site) RT_ASM(rt_tan);
rt_value_t rt_exp(rt_value_t x, const char *site) RT_ASM(rt_exp);

// Called when proc's reductions run out. For now it just refills them;
// once there are processes, it's where a process gets preempted.
void rt_preempt(rt_proc_t *proc) RT_ASM(rt_preempt);

// --- the runtime itself -------------------------------------------------------

// The process that is running.
extern rt_proc_t *rt_current;

// Allocates from the current process's heap, in 16-byte units; faults at
// `site` when the heap is full.
void *rt_alloc(size_t bytes, const char *site);

// A new string holding a copy of len bytes.
rt_value_t rt_new_string(const char *bytes, size_t len, const char *site);

// A new float.
rt_value_t rt_new_float(double d, const char *site);

// Text, for printing values and building strings: a growable buffer.
typedef struct rt_buf {
    char  *bytes;
    size_t len, cap;
} rt_buf_t;

void rt_buf_add(rt_buf_t *b, const char *bytes, size_t len);
void rt_buf_free(rt_buf_t *b);

// Appends v as pprint shows it. With raw, a string is its bytes, without
// quotes (concat and tty/write); strings inside lists are always quoted.
void rt_render(rt_buf_t *b, rt_value_t v, int raw);

// The name of symbol id, or NULL.
const char *rt_symbol_name(uint64_t id);

// Runs fn on the stack that ends at stack_top, with x28 = proc, and
// returns its result (rt_asm.S).
rt_value_t rt_enter(rt_proc_t *proc, rt_value_t (*fn)(void), void *stack_top) RT_ASM(rt_enter);

#endif // __ASSEMBLER__
#endif // RT_H
