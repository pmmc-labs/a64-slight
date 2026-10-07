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
#define RT_FAULT_NOT_DEVICE 18    // :not-a-device    connect was handed something that isn't an open device
#define RT_FAULT_COUNT      18

// Symbols the runtime makes: every program has them, after #false and
// #true, in this order (values.ts, RUNTIME_SYMBOLS). The fault kinds
// follow: RT_FAULT_x is symbol RT_SYM_FAULTS - 1 + x.
#define RT_SYM_OK            2
#define RT_SYM_ERROR         3
#define RT_SYM_EXIT          4
#define RT_SYM_KILLED        5
#define RT_SYM_FAULTS        6

// A key from :keypress is (key mods...) (tty.c): key is a string for a
// printable key, or one of these names, and mods are the modifiers held,
// in the order :ctrl :alt :shift. Their symbols follow the fault kinds:
// RT_KEY_x is symbol RT_SYM_KEYS + x. compiler/tests/values.test.ts reads
// the names from here.
#define RT_SYM_KEYS         24    // RT_SYM_FAULTS + RT_FAULT_COUNT
#define RT_KEY_UP            0    // :ArrowUp       the arrows are in the order of
#define RT_KEY_DOWN          1    // :ArrowDown     their escape sequences, ESC [ A
#define RT_KEY_RIGHT         2    // :ArrowRight    to ESC [ D
#define RT_KEY_LEFT          3    // :ArrowLeft
#define RT_KEY_HOME          4    // :Home
#define RT_KEY_END           5    // :End
#define RT_KEY_INSERT        6    // :Insert
#define RT_KEY_DELETE        7    // :Delete
#define RT_KEY_PAGE_UP       8    // :PageUp
#define RT_KEY_PAGE_DOWN     9    // :PageDown
#define RT_KEY_ENTER        10    // :Enter
#define RT_KEY_ESCAPE       11    // :Escape
#define RT_KEY_BACKSPACE    12    // :Backspace
#define RT_KEY_TAB          13    // :Tab
#define RT_KEY_F1           14    // :F1            F1 to F12 in order
#define RT_KEY_F2           15    // :F2
#define RT_KEY_F3           16    // :F3
#define RT_KEY_F4           17    // :F4
#define RT_KEY_F5           18    // :F5
#define RT_KEY_F6           19    // :F6
#define RT_KEY_F7           20    // :F7
#define RT_KEY_F8           21    // :F8
#define RT_KEY_F9           22    // :F9
#define RT_KEY_F10          23    // :F10
#define RT_KEY_F11          24    // :F11
#define RT_KEY_F12          25    // :F12
#define RT_KEY_UNIDENTIFIED 26    // :Unidentified  a key it can't name
#define RT_KEY_CTRL         27    // :ctrl          the modifiers
#define RT_KEY_ALT          28    // :alt
#define RT_KEY_SHIFT        29    // :shift
#define RT_KEY_COUNT        30

// A device (process.c: a file or a socket, from connect) talks to its
// owner with these: (:open f) first (a listener's is (:open l port)), a
// reader's (:line f s) and (:eof f), a listener's (:accept l conn), and
// the (:write x ...) a writer takes. RT_DEV_x is symbol RT_SYM_DEVICE + x.
#define RT_SYM_DEVICE       54    // RT_SYM_KEYS + RT_KEY_COUNT
#define RT_DEV_OPEN          0    // :open
#define RT_DEV_LINE          1    // :line
#define RT_DEV_EOF           2    // :eof
#define RT_DEV_WRITE         3    // :write
#define RT_DEV_ACCEPT        4    // :accept
#define RT_DEV_COUNT         5

// Why a device failed, from errno: a device's owner ends with
// (:error (name path)), path being the file's, or the socket's "host:port"
// (a listener's port). RT_ERR_x is symbol RT_SYM_ERRS + x.
#define RT_SYM_ERRS         59    // RT_SYM_DEVICE + RT_DEV_COUNT
#define RT_ERR_ENOENT        0    // :enoent        no such file or directory
#define RT_ERR_EACCES        1    // :eacces        permission denied
#define RT_ERR_EPERM         2    // :eperm         operation not permitted
#define RT_ERR_EEXIST        3    // :eexist
#define RT_ERR_EISDIR        4    // :eisdir        reading a directory
#define RT_ERR_ENOTDIR       5    // :enotdir       a path through something that isn't one
#define RT_ERR_ENAMETOOLONG  6    // :enametoolong
#define RT_ERR_ELOOP         7    // :eloop         too many symbolic links
#define RT_ERR_EROFS         8    // :erofs         a read-only file system
#define RT_ERR_ENOSPC        9    // :enospc        the disk is full
#define RT_ERR_EFBIG        10    // :efbig
#define RT_ERR_EMFILE       11    // :emfile        too many open files
#define RT_ERR_ENFILE       12    // :enfile
#define RT_ERR_EIO          13    // :eio
#define RT_ERR_ECONNREFUSED 14    // :econnrefused  nothing is listening there
#define RT_ERR_ECONNRESET   15    // :econnreset    the other end went away
#define RT_ERR_EPIPE        16    // :epipe         writing to a connection the other end has closed
#define RT_ERR_ETIMEDOUT    17    // :etimedout
#define RT_ERR_EADDRINUSE   18    // :eaddrinuse    listening on a port that's taken
#define RT_ERR_EADDRNOTAVAIL 19   // :eaddrnotavail
#define RT_ERR_EHOSTUNREACH 20    // :ehostunreach
#define RT_ERR_ENETUNREACH  21    // :enetunreach
#define RT_ERR_ENOTFOUND    22    // :enotfound     no such host (Node's name: it isn't an errno)
#define RT_ERR_OTHER        23    // :io-error      anything else
#define RT_ERR_COUNT        24

// How connect :fs/... opens its file, and what connect :tcp... does.
#define RT_FS_READ           0
#define RT_FS_WRITE          1    // creating it, or emptying it
#define RT_FS_APPEND         2    // creating it
#define RT_TCP_CONNECT       0    // to "host:port"
#define RT_TCP_LISTEN        1    // on a port, 0 for one the system picks

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
#define RT_PROC_ARGS        56    // rt_proc_t.args[8]: ...with these in the argument registers
#define RT_PROC_CTX        120    // rt_proc_t.ctx: the registers, while it's switched out

// rt_ctx_t: the callee-saved registers, which are all rt_switch keeps.
#if defined(__x86_64__)
#define RT_CTX_RBX           0    // rbx, rbp, r12..r15, rsp
#define RT_CTX_RBP           8
#define RT_CTX_R12          16
#define RT_CTX_R13          24
#define RT_CTX_R14          32
#define RT_CTX_R15          40
#define RT_CTX_RSP          48
#define RT_CTX_SIZE         56
#elif defined(__aarch64__)
#define RT_CTX_X19           0    // x19..x28, x29, x30, sp, d8..d15
#define RT_CTX_FP           80
#define RT_CTX_LR           88
#define RT_CTX_SP           96
#define RT_CTX_D8          104
#define RT_CTX_SIZE        168
#else
#error "the runtime is for AArch64 and x86-64"
#endif

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

// Give a C symbol the exact name the assembly uses, on Linux and on macOS
// (where C names would otherwise get a leading underscore).
#define RT_ASM(name) __asm__(#name)

typedef uint64_t rt_value_t;

// Compiled code, as the runtime holds it: the trampoline calls it with its
// arguments in the eight argument registers (x0..x7; rdi, rsi, rdx, rcx,
// r8, r9, r10, r11 on x86-64), whatever its C type says.
typedef void (*rt_code_t)(void);

// The registers that survive a switch: the ABI's callee-saved ones. On
// x86-64 the return address is on the stack, so rsp is all there is of it.
#if defined(__x86_64__)
typedef struct rt_ctx {
    uint64_t rbx, rbp, r12, r13, r14, r15, rsp;
} rt_ctx_t;
#else
typedef struct rt_ctx {
    uint64_t x19_x28[10];
    uint64_t fp, lr, sp;
    uint64_t d8_d15[8];
} rt_ctx_t;
#endif

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
    rt_value_t     device;      // a reader's pid, if taking this means reading on; or 0
} rt_msg_t;

// READY: in the run queue. WAITING: in recv, with no stack. JOINING:
// blocked in join, keeping its stack. SLEEPING: in sleep, keeping its
// stack, until its timer wakes it. DONE: ended, about to be freed.
typedef enum { RT_READY, RT_RUNNING, RT_WAITING, RT_JOINING, RT_SLEEPING, RT_DONE } rt_state_t;

// Someone to tell when a process ends: monitor's list.
typedef struct rt_watch {
    struct rt_watch *next;
    rt_value_t       pid;
} rt_watch_t;

// A process. Compiled code finds the current one in x28 (r15 on x86-64),
// and uses the fields up to ctx; the rest are the runtime's.
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
    int             keypress;   // connected to :keypress
    struct rt_device *devices;  // the files it has open (process.c)
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
// in a new process, with the values deep-copied into its heap; rt_connect
// does the same, and sends the new process every key from :keypress.
rt_value_t rt_fork(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) RT_ASM(rt_fork);
rt_value_t rt_connect(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site) RT_ASM(rt_connect);

// Files (process.c). rt_connect_fs forks as rt_fork does, and opens path
// (RT_FS_READ, _WRITE or _APPEND) on a device, a new pid that the runtime
// serves; the new process owns it, and hears from it first with
// (:open f). rt_disconnect closes a device; anything else it ignores.
rt_value_t rt_connect_fs(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                         rt_value_t path, uint64_t mode) RT_ASM(rt_connect_fs);

// Sockets (process.c). rt_connect_tcp forks, and connects to "host:port"
// (RT_TCP_CONNECT) or listens on a port (RT_TCP_LISTEN) on a device the
// new process owns. rt_connect_device forks, and hands the new process
// the device dev.
rt_value_t rt_connect_tcp(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                          rt_value_t where, uint64_t mode) RT_ASM(rt_connect_tcp);
rt_value_t rt_connect_device(rt_code_t code, uint64_t n, const rt_value_t *values, const char *site,
                             rt_value_t dev) RT_ASM(rt_connect_device);
rt_value_t rt_disconnect(rt_value_t pid, const char *site) RT_ASM(rt_disconnect);
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

// Timers (process.c). rt_after sends msg to pid in ms milliseconds;
// rt_sleep waits that long, keeping the stack. A negative ms counts as 0.
// Each returns ().
rt_value_t rt_after(rt_value_t ms, rt_value_t pid, rt_value_t msg, const char *site) RT_ASM(rt_after);
rt_value_t rt_sleep(rt_value_t ms, const char *site) RT_ASM(rt_sleep);

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

// The terminal (tty.c). The screen's size, or 24 by 80 when stdout isn't
// a terminal.
rt_value_t rt_screen_rows(const char *site) RT_ASM(rt_screen_rows);
rt_value_t rt_screen_cols(const char *site) RT_ASM(rt_screen_cols);

// Functions. rt_apply calls f with the elements of args as its arguments,
// by jumping to it, so f returns straight to apply's caller
// (rt_asm_aarch64.S, rt_asm_x86_64.S).
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
// (rt_asm_aarch64.S, rt_asm_x86_64.S): this is what switching processes is.
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

// The terminal (tty.c). rt_tty_raw puts it in raw mode, or takes it out;
// it does nothing when stdin isn't a terminal, and the terminal is put
// back at exit. rt_key decodes the key at the start of the n > 0 bytes at
// in, calls emit with it, and returns how many bytes it took; Ctrl-C ends
// the program, with exit status 130, instead.
void   rt_tty_raw(int on);
size_t rt_key(const unsigned char *in, size_t n, void (*emit)(rt_value_t key));

#endif // __ASSEMBLER__
#endif // RT_H
