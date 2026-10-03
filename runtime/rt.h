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
#define RT_TAG_PID           7    // 111    process id << 3
#define RT_PID_SHIFT         3
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

// What rt_fault is told went wrong, and the keyword that names it in the
// process's (:error (kind value site)). compiler/tests/values.test.ts
// reads the keywords from here.
#define RT_FAULT_NOT_INT     1    // :not-an-int      an integer operation was given something else
#define RT_FAULT_OVERFLOW    2    // :overflow        an integer result doesn't fit in 63 bits
#define RT_FAULT_NOT_BOOL    3    // :not-a-bool      a cond test was neither #true nor #false
#define RT_FAULT_NO_CLAUSE   4    // :no-clause       no cond clause matched
#define RT_FAULT_STACK       5    // :stack           a function was called with the stack nearly full
#define RT_FAULT_NOT_CONS    6    // :not-a-cons      car or cdr of something that isn't a cons
#define RT_FAULT_NOT_LIST    7    // :not-a-list      cons onto something that isn't a list
#define RT_FAULT_HEAP        8    // :heap            the heap is full
#define RT_FAULT_NOT_STRING  9    // :not-a-string    a string operation was given something else
#define RT_FAULT_NOT_SYMBOL 10    // :not-a-symbol    a symbol operation was given something else
#define RT_FAULT_RANGE      11    // :out-of-range    an index, a byte, or a rounded float out of range
#define RT_FAULT_NOT_NUMBER 12    // :not-a-number    arithmetic on something that isn't a number
#define RT_FAULT_DIV_ZERO   13    // :div-by-zero     division by zero
#define RT_FAULT_NOT_FUNC   14    // :not-a-function  a call to something that isn't a function
#define RT_FAULT_ARITY      15    // :arity           a function called with the wrong number of arguments
#define RT_FAULT_NOT_PID    16    // :not-a-pid       a process operation was given something else
#define RT_FAULT_JOIN_SELF  17    // :join-self       (join $$), which would wait forever
#define RT_FAULT_COUNT      17

// Symbols the runtime makes: every program has them, after #false and
// #true, in this order (values.ts, RUNTIME_SYMBOLS). The fault kinds
// follow: RT_FAULT_x is symbol RT_SYM_FAULTS - 1 + x.
#define RT_SYM_OK            2
#define RT_SYM_ERROR         3
#define RT_SYM_EXIT          4
#define RT_SYM_KILLED        5
#define RT_SYM_FAULTS        6

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
#define RT_PROC_HEAP_LIMIT  24    // rt_proc_t.heap_limit: the end of the current chunk
#define RT_PROC_PID         32    // rt_proc_t.pid: $$
#define RT_PROC_PARENT      40    // rt_proc_t.parent: ^$$
#define RT_PROC_CODE        48    // rt_proc_t.code: what the trampoline calls...
#define RT_PROC_ARGS        56    // rt_proc_t.args[8]: ...with these in x0..x7
#define RT_PROC_CTX        120    // rt_proc_t.ctx: the registers, while it's switched out

#define RT_CTX_X19           0    // rt_ctx_t: x19..x28, x29, x30, sp, d8..d15
#define RT_CTX_FP           80
#define RT_CTX_LR           88
#define RT_CTX_SP           96
#define RT_CTX_D8          104
#define RT_CTX_SIZE        168

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

// Give a C symbol the exact name the assembly uses, on Linux and on macOS
// (where C names would otherwise get a leading underscore).
#define RT_ASM(name) __asm__(#name)

typedef uint64_t rt_value_t;

// A process. Compiled code finds the current one in x28.
// Compiled code, as the runtime holds it: the trampoline calls it with its
// arguments in x0..x7, whatever its C type says.
typedef void (*rt_code_t)(void);

// The registers that survive a switch (AAPCS64's callee-saved ones).
typedef struct rt_ctx {
    uint64_t x19_x28[10];
    uint64_t fp, lr, sp;
    uint64_t d8_d15[8];
} rt_ctx_t;

// A chunk of a process's heap; the chunks are a list, newest first. A
// message arrives as a chunk of its own, and joins the list.
typedef struct rt_chunk {
    struct rt_chunk *next;
    size_t           size;      // bytes after this header
} rt_chunk_t;

typedef struct rt_msg {
    struct rt_msg *next;
    rt_value_t     value;
    rt_chunk_t    *chunk;       // what value lives in; NULL if it needed none
} rt_msg_t;

// READY: in the run queue. WAITING: in recv, with no stack. JOINING:
// blocked in join, keeping its stack. DONE: ended, about to be freed.
typedef enum { RT_READY, RT_RUNNING, RT_WAITING, RT_JOINING, RT_DONE } rt_state_t;

// Someone to tell when a process ends: monitor's list.
typedef struct rt_watch {
    struct rt_watch *next;
    rt_value_t       pid;
} rt_watch_t;

// A process. Compiled code finds the current one in x28, and uses the
// fields up to ctx; the rest are the runtime's.
typedef struct rt_proc {
    int64_t     reductions;     // calls left before rt_preempt
    uintptr_t   stack_limit;    // a function entered with sp below this faults
    uintptr_t   heap_ptr;       // bump allocation: compiled code adds to this...
    uintptr_t   heap_limit;     // ...and calls rt_heap_grow when it would pass this
    rt_value_t  pid;            // as a value
    rt_value_t  parent;         // as a value; () for the root
    rt_code_t   code;           // where it starts, or restarts after waiting in recv
    rt_value_t  args[8];
    rt_ctx_t    ctx;

    rt_state_t      state;
    void           *stack;      // the bottom of its stack (above the guard page), or NULL
    rt_chunk_t     *chunks;
    size_t          heap_bytes; // in all its chunks
    size_t          next_chunk; // the size of the next chunk it allocates in
    size_t          gc_at;      // collect at recv once this much of the heap is in use
    rt_msg_t       *mail, *mail_last;
    struct rt_proc *next_ready;
    struct rt_proc *joiners;    // blocked in join on this one, in the order they joined
    struct rt_proc *next_joiner;
    rt_value_t      joining;    // while JOINING: whom
    rt_watch_t     *watchers;   // monitoring this one, in order
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
static inline rt_value_t rt_symbol(uint64_t id)    { return id << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL; }

// --- emitted by the compiler -------------------------------------------------

// The body of the root process.
rt_value_t slight_main(void) RT_ASM(slight_main);

// Every symbol's name, in id order, as consecutive NUL-terminated strings.
extern const uint64_t slight_symbol_count   RT_ASM(slight_symbol_count);
extern const char     slight_symbol_names[] RT_ASM(slight_symbol_names);

// --- called by compiled code --------------------------------------------------

// Processes (process.c). rt_recv returns the next message, or, when there
// is none, remembers code(args...) and gives up the stack; the process
// starts again at code when a message comes. First, it may collect
// garbage, with args (the receive function's, in its frame) as the roots,
// which it updates. rt_fork starts code(values...)
// in a new process, with the values deep-copied into its heap.
rt_value_t rt_fork(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) RT_ASM(rt_fork);
rt_value_t rt_send(rt_value_t pid, rt_value_t msg, const char *site) RT_ASM(rt_send);
rt_value_t rt_recv(rt_value_t *args, uint64_t n, rt_code_t code) RT_ASM(rt_recv);
void       rt_dead_letter(rt_value_t msg, const char *site) RT_ASM(rt_dead_letter);
void       rt_yield(void) RT_ASM(rt_yield);

// The lifecycle (process.c). rt_join waits for pid to end, keeping the
// stack, and returns (:ok value) or (:error reason). rt_monitor has
// (:exit pid result) sent to the caller when pid ends, or at once if it
// has. rt_kill ends pid with (:error :killed). rt_raise ends the caller
// with (:error reason). Each returns ().
rt_value_t rt_join(rt_value_t pid, const char *site) RT_ASM(rt_join);
rt_value_t rt_monitor(rt_value_t pid, const char *site) RT_ASM(rt_monitor);
rt_value_t rt_kill(rt_value_t pid, const char *site) RT_ASM(rt_kill);
__attribute__((noreturn))
rt_value_t rt_raise(rt_value_t reason, const char *site) RT_ASM(rt_raise);

// Makes room for `bytes` in the current process's heap: a new chunk.
// Faults at `site` when the heap would pass its limit.
void rt_heap_grow(uint64_t bytes, const char *site) RT_ASM(rt_heap_grow);

// Something went wrong at `site` (a description and a source position);
// `value` is the offending value, where there is one. Logs a line to
// stderr and ends the current process with (:error (kind value site)).
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

// Called when proc's reductions run out: refills them, and lets the next
// process run, if one is waiting to.
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

// Saves the callee-saved registers in `from` and loads them from `to`
// (rt_asm.S): this is what switching processes is.
void rt_switch(rt_ctx_t *from, rt_ctx_t *to) RT_ASM(rt_switch);

// Where a process's first switch lands: calls code(args...), then rt_exit.
void rt_trampoline(void) RT_ASM(rt_trampoline);

// The end of a process, with its result. Doesn't return.
__attribute__((noreturn))
void rt_exit(rt_proc_t *proc, rt_value_t result) RT_ASM(rt_exit);

// Ends the current process with (:ok value) or (:error value). An error
// in the root process is logged to stderr, unless `logged` says the
// caller (rt_fault) already has.
__attribute__((noreturn))
void rt_end(int ok, rt_value_t value, int logged);

// Static data: the compiler brackets its read-only and constant data with
// these, so the runtime can tell values it doesn't have to copy.
extern const char slight_rodata_start[] RT_ASM(slight_rodata_start);
extern const char slight_rodata_end[]   RT_ASM(slight_rodata_end);
extern const char slight_const_start[]  RT_ASM(slight_const_start);
extern const char slight_const_end[]    RT_ASM(slight_const_end);

// The process table, and the scheduler (process.c).
rt_proc_t *rt_new_process(rt_code_t code, rt_value_t parent);
void       rt_run(void);

#endif // __ASSEMBLER__
#endif // RT_H
