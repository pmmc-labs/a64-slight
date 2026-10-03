# a64-slight design

This is the design as agreed so far. [`DECISIONS.md`](DECISIONS.md) has the
reasons and the alternatives that were turned down, and
[`PLAN.md`](PLAN.md) has the build order. Anything marked **(open)** is a
default I proposed that hasn't been confirmed: it's fine to build on, but
bring it up before baking it in further.

## Goals

- **slight**: an s-expression Lisp, fully immutable, built on actors.
  Mutability comes from "data structure" actors that keep their state in the
  arguments of a tail-recursive loop.
- **A small, tight actor runtime** to build on. The OTP-like layers
  (supervisors, gen-server patterns, pools, the REPL, line editing) are
  written in slight, on top of it.
- **For personal or small-group use.** The typical program has hundreds to
  thousands of processes cooperating like an operating system. A million
  processes should work, but nothing is optimized for it. Erlang's choices
  are not automatically the right ones here.
- **Aggressively simple.** Anything that adds complexity gets cut or changed.
  Practical beats pure.
- **The user surface of ts-slight** (see `reference/ts-slight/examples/`) is
  what we keep. The internals of ts-slight and ts-cpi are not.
- **Native AArch64.** macOS on Apple Silicon first, AArch64 Linux from the
  same code. Development on x86 uses a clang cross-compile plus qemu-aarch64.
- **Ahead-of-time compilation only.** No `eval`, no `slight/parse`, no code
  loading at runtime, no hot reload.

## Language

### Syntax

Same reader as ts-slight (see `reference/ts-slight/src/parser.ts` and
`reference/ts-cpi/src/reader.ts`):

- `( ... )` lists; `()` is nil
- integers (`42`, `-7`) and floats (`3.14`)
- strings in double quotes, with escapes: `\"` `\\` `\n` `\t` `\r` `\e`
  `\u{hex}`
- symbols. `:name` is a keyword, a symbol that evaluates to itself.
  `#true`/`#false` are the booleans.
- `'x` is `(quote x)`
- `;` starts a comment
- `$$` (self) and `^$$` (parent)
- ts-slight's bare constants `\n`, `\r`, `\t`, `\e` (strings)

### Types

| Type     | Notes |
|----------|-------|
| integer  | 63-bit. Overflow faults the process. |
| float    | boxed IEEE double |
| symbol   | an id assigned at compile time. Not interned at runtime, no `gensym`. |
| list     | cons cells and `()` |
| string   | immutable bytes, UTF-8 by convention |
| function | a closure, or a reference to a top-level function |
| pid      | a process id |

Booleans are the reserved symbols `#true` and `#false`.

### Special forms (10)

| Form | Meaning |
|------|---------|
| `(defun name (params...) body...)` | Top level only. No local `defun`. |
| `(lambda (params...) body...)` | A closure. Captures free variables **by value**. Always a *plain* function (see the `recv` rule). Can't refer to itself. |
| `(let name expr)` | Binds `name` for the rest of the enclosing body. Only allowed directly in a body. As the last form of a body, its value is `expr`'s (as in ts-slight). |
| `(cond (test body...) ...)` | The only conditional. `if`, `when` and `case` are gone. Each test must be `#true` or `#false`, or the process faults; so does running out of clauses. |
| `(do form...)` | Evaluates in order. The last form is in tail position. |
| `(quote x)`, `'x` | A constant. `:sym` is self-quoting. |
| `(fork expr)` | Runs `expr` in a new process and returns its pid. |
| `(connect :source expr)` | Same as `fork`, and connects the new process to an event source (`:keypress`). |
| `(recv clause...)` | Takes the next message. Only allowed as the whole body of a receive function. |
| `(yield expr)` | Pauses: goes to the back of the run queue, then evaluates `expr` in tail position. |

Function bodies and clause bodies can hold several forms, as if wrapped in
`do`.

`and`, `or` and `not` are prelude functions on booleans, so `and` and `or`
evaluate all their arguments. `cond` is the short-circuit form.

A function takes at most 8 parameters, one register each (see the
register convention below). More could go on the stack later.

### The `recv` rule

This rule is what makes the runtime simple, so it gets its own section.

- A **receive function** is a top-level `defun` whose body is a single
  `recv` form. `recv` appears nowhere else.
- A **state function** is a receive function, or any function that
  tail-calls a state function. The compiler computes this as a fixpoint over
  the call graph.
- State functions may only be called **in tail position**, or as the body of
  `fork`/`connect`.
- Every other function is **plain**. Plain functions can be called anywhere,
  and can never reach a `recv`. Lambdas are always plain: a lambda body
  can't contain `recv` or call a state function. Indirect calls would hide
  the call graph otherwise.

The result: **the stack is empty at every `recv`.** A process waiting for a
message is fully described by (function, arguments, mailbox). Its stack goes
back to a pool. When a message arrives, the runtime calls the function again
from the top with the same arguments. That's exact, because the function's
body is just the `recv`. "All state lives in the loop arguments" is literally
true at every wait.

Syntax **(open; this is the proposal)**:

```lisp
(defun counter (n)
    (recv
        ((:inc)        (counter (+ n 1)))
        ((:get from)   (send from (list :count n)) (counter n))
        ((:stop)       n)
        (other         (log-unknown other) (counter n))))
```

- `(:tag a b ...)` matches a list of exactly that length whose head is the
  symbol `:tag`, and binds the rest by position.
- A bare symbol matches anything and binds the whole message.
- Clauses are tried in order. Only the **first** message in the mailbox is
  considered: there's no selective receive. If no clause matches, the
  message goes to the **dead-letter log** and the function waits again.
- Open details: nested patterns, literals in other positions, a rest
  binding, `_`.

Send-then-wait becomes two functions:

```lisp
(defun pinger (target n)
    (cond
        ((== n 0) (send target (list :stop $$)) :ping-done)
        (#true    (send target (list :ping $$)) (pinger-wait target n))))

(defun pinger-wait (target n)
    (recv
        ((:pong from) (pprint (list :round n)) (pinger target (- n 1)))))
```

Request/reply is correlated by the actor's own state: put a counter or the
request's details in the arguments and match the reply when it arrives
(run-to-completion style). There's no AWAIT/REQUEST/REPLY machinery and no
reply refs.

### Processes

| | |
|---|---|
| `(fork expr)` | The compiler turns `expr` into a hidden entry function whose parameters are `expr`'s free variables. Their values are **deep-copied** into the child. `expr` runs at the base of the child's stack, so it may tail-call a state function. |
| `(connect :keypress expr)` | `fork`, plus the new process receives the source's events as messages. |
| `(send pid msg)` | `msg` is any value, conventionally a list headed by a keyword. Deep-copied. Never blocks. Returns `()`. |
| `$$`, `^$$` | self, parent |
| `(join pid)` | **Blocking** wait for `pid` to end. Returns `(:ok value)` or `(:error reason)`. Works on any pid (not just children), anywhere (including inside lambdas), and after the process has already ended. `(join $$)` is an error. |
| `(monitor pid)` | Opt-in. When `pid` ends, the runtime sends `(:exit pid result)` to the caller. There are no automatic messages to the parent. |
| `(kill pid)` | Ends `pid` with `(:error :killed)`. **(open)** exact reason |
| `(after ms pid msg)` | A timer: sends `msg` to `pid` after `ms` milliseconds. |
| `(raise reason)` | Ends the current process with `(:error reason)`. |

- A process whose entry expression returns a value ends with `(:ok value)`.
- **Errors are values**: `(:ok v)` and `(:error e)`. There's no `catch`.
  A fault (overflow, a builtin given the wrong type, a `cond` test that
  isn't a boolean, no `cond` clause matching, an arity mismatch, the heap
  limit) ends the process with `(:error ...)`. **(open)** The shape of a
  fault's reason. Until there are processes and lists (steps 4 and 7), a
  fault prints `fault: <what> (<where> at file:line:col)` to stderr and
  exits with status 1.
- **Preemption.** Every loop is a tail call, so a reduction counter is
  checked at each function entry and tail call. When it runs out, the
  process pauses where it is, *keeping its stack*, and goes to the back of
  the run queue. `yield` is the same pause, chosen by the program.
- **Stacks exist only while needed.** A process that is running, paused
  (preempted or yielded), blocked in `join`, or blocked in a syscall holds a
  stack. A process waiting in `recv` holds none.
- **Deadlock** (nothing can run and nothing is pending: no timers, no event
  sources, no blocked I/O) is detected and reported.
- **Blocking syscalls.** Since a process can block with its stack, `sleep`,
  `slurp` and `spew` block only the calling process and return a Result.
  This follows from the `join` decision, but wasn't discussed on its own.

### Program structure (open)

A program is one or more `.slight` files. All `defun`s are global and can
call each other in any order. Every other top-level form, in order, becomes
the body of the root process; a top-level `let` binds for the rest of the
root process only, and `defun`s can't see it. The program's exit status
follows the root process's Result. ts-slight's examples (`examples/`) are
all written this way. Built this way in step 2; a program with no
top-level expressions has the value `()`.

### Builtins and the prelude

Keep ts-slight's names where possible
(`reference/ts-slight/docs/NOTES-language.md`, `lib/Builtins.slight`,
`lib/Prelude.slight`).

**In C (or assembly):**
- arithmetic: `+ - * /`, `div` and `%` **(open: the names)**. `/` always
  returns a float. Mixed integer and float arithmetic gives a float.
  `ceil`, `floor`, `round` and `trunc` return integers. `sqrt`, `pow`,
  `sin`, `cos`, `exp`, `abs`, `min`, `max` and friends wrap libm.
- comparison: `== != < <= > >=`, structural `eq?`/`ne?`
- type predicates: `nil? cons? sym? str? num? int? float? lambda? pid? bool?`
  (`sym?` is true for `#true` and `#false`: they're symbols)
- lists: `cons car cdr list`, and `c[ad]r` with up to four letters
  (`cadr`, `cddr`, `caddr`, ...). `car`/`cdr` of anything but a cons
  faults, and so does `cons` onto anything but a list, so every list is
  proper (both as in ts-slight). Inside a quoted list, `:a` reads as
  `(quote a)`, as it did in ts-slight; write `'(a b)`, not `'(:a :b)`.
- strings: `str-len` (bytes), `substring`, `concat`/`~` (renders numbers and
  symbols, as in ts-slight), `index-of`, `str-split`, `str-join`,
  `string->int`, `symbol->string`, `string->symbol` (looks up the
  compile-time table; an unknown name gives `#false`), `byte-at`,
  `bytes->string`, `format-num`
- processes: `send join monitor kill after raise`
- I/O: `tty/write`, `tty/screen/rows`, `tty/screen/cols`, `pprint`
  (prints its argument and a newline, returns `()`, as in ts-slight;
  symbols print without the colon, so `:ping` prints as `ping`),
  `sleep`, `slurp`, `spew`
- **(open)** whether `apply` is a builtin

**In slight (the prelude):** `map filter grep fold/l fold/r reverse append
concat-list length nth range member? find assoc lookup take skip sum product
inc dec dotimes`, `and or not`, and string helpers: `uc lc` (ASCII),
`pad-start pad-end str-repeat starts-with ends-with`.

### Strings

- **Level 1, now:** immutable byte strings. Indexing is by byte; case
  mapping is ASCII only.
- **Level 2, when the editor needs it:** UTF-8 helpers (decode, count and
  walk code points, display width).
- Full Unicode is out. If it's ever needed, a C library comes in as a
  driver.
- There's no character type. A character is an integer or a one-character
  string.
- Build strings by collecting pieces in a list and joining them.

### Not in the language

`eval`, `slight/parse`, hot reload, macros, `if`/`when`/`case`, `catch`,
`gensym`, local `defun`, selective receive, mutable anything. readline isn't
needed: the REPL and line editing are slight code over key events.

## Runtime

### Values

64-bit tagged words:

| Low bits | Type | Representation |
|---|---|---|
| `…0`  | integer | 63-bit, immediate. Add is `adds` + `b.vs`. |
| `001` | list    | pointer to a 2-word cons cell, no header. `nil` is this tag with a null pointer. |
| `011` | boxed   | pointer to a header word (type, size) and payload: string, float, closure |
| `101` | symbol  | compile-time id, immediate |
| `111` | pid     | immediate |

- **Cons cells** are 16 bytes. The tag folds into the load offset:
  `car` is `ldur x0, [x1, #-1]` and `cdr` is `ldur x0, [x1, #7]`.
- **Strings**: header (byte length), the bytes, then a NUL that the length
  doesn't count, so a string can go straight to C.
- **Floats**: header plus 8 bytes.
- **Closures**: header, code pointer, arity, then the captured values.
- **Static data**: string literals, quoted constants, and the static
  closures for top-level functions live in the binary. Every process shares
  them. They're never collected and are sent without copying. The collector
  skips them with one address-range check.
- **The heap is acyclic**: data is immutable, lambdas capture by value and
  can't refer to themselves, and there's no local `defun`.

### Process heaps and GC

**So far (step 4):** one 64 MB chunk per process, mapped lazily, and a
fault when it's full. The heap pointer and limit live in the process
struct (`[x28, #RT_PROC_HEAP_PTR]`), so C builtins that allocate can use
them with nothing to sync (D57). Compiled code allocates inline:

```
    ldr  x2, [x28, #RT_PROC_HEAP_PTR]
    ldr  x3, [x28, #RT_PROC_HEAP_LIMIT]
    add  x4, x2, #16                        // bytes
    cmp  x4, x3
    b.hi Lfault_N                           // fault: heap exhausted
    str  x4, [x28, #RT_PROC_HEAP_PTR]       // x2 = the new cell
```

**The plan (step 9):**

- Each process has its own heap: a chain of chunks with bump allocation and
  ordinary absolute pointers.
- **Collection runs only where the stack is empty**: at `recv` (roots: the
  receive function's arguments, plus the message) and at tail calls made
  from the base of the stack (roots: the tail call's arguments). It's a
  Cheney copy of the live data into one fresh chunk, after which the old
  chunks are freed.
- It runs once the heap has roughly doubled since the last collection, so a
  short-lived process never collects.
- In the middle of a handler, the heap only grows.
- **C builtins never see an object move.** No handles, no rooting API, no
  stack maps, no write barrier.
- **The gotcha:** an allocation-heavy loop called from inside an expression
  (not in tail position) can't collect until it returns. A per-process heap
  limit turns that into a fault instead of exhausting memory. Write long
  loops in tail position, or split the work across messages.
- **(open)** The compiler knows state functions always run at the base of
  the stack. Collecting at other base-of-stack tail calls (say a plain loop
  that is a `fork` body) needs a runtime check of `sp` against the stack
  base. Start with state functions only.

### Messages

- `send` deep-copies the value into a fresh message chunk and appends it to
  the target's FIFO mailbox. Static data is not copied. Sharing within a
  message is duplicated (as in the BEAM); that's acceptable at this scale.
- `recv` links the chunk into the receiver's heap. Nothing is copied twice.
- **(open)** Bounded mailboxes. Since `send` can't block, a full mailbox
  would mean dropping the message or faulting the sender.

### Process lifecycle

- `fork`: allocate the process, deep-copy the free variables into its heap,
  queue it to run the entry function.
- Ending: the Result is copied into a small per-pid **exit record**.
  Processes blocked in `join` on it wake with a copy; later `join`s read
  the record; monitors get `(:exit pid result)`. Then the heap and the
  stack are freed.
- **Known issue (open):** exit records accumulate. That doesn't matter at
  hundreds or thousands of processes. A system that churns through millions
  will need a retention policy, such as dropping the record once the parent
  has seen it.

### Scheduler

- Single core. A **FIFO run queue (open)**: the spike used deterministic
  ticks, but a plain run queue is simpler and still deterministic on one
  core. A **virtual clock** for tests (from ts-cpi) keeps timer tests exact.
- **Stack pool**: a process takes a stack when it starts running and gives
  it back when it waits in `recv` or ends.
- **Context switch**: the spike's `rt_switch`, 25 instructions (x19–x30,
  sp, d8–d15).
- **Idle**: when nothing is runnable, wait in `kqueue` (macOS) or `poll`
  (Linux) with the timeout set to the next timer. Timers are a binary heap.
- **Devices** are runtime event sources that send messages: `:keypress`
  sends ts-slight's key shape `(key mods...)`, where the key is a string for
  printable keys and a DOM-style name symbol for named keys
  (`reference/ts-slight/src/extensions.ts`, `KEY_NAMES`). The runtime
  decodes the terminal's escape sequences.
- C libraries come in later as drivers exposed as processes, never as
  direct calls that could stall the runtime.

### Register convention

Carried over from the spike (`spike/aarch64/actor.h`, `rt.h`), with
AAPCS64's argument registers (D47):

| Register | Role |
|---|---|
| `x0`–`x7` | a function's arguments, in order; the result comes back in `x0`. The same as C, so compiled functions and the runtime's C functions are called the same way. |
| `x9` | the closure, for a call through a closure (step 6) |
| `x16` | scratch for the checks at function entry |
| `x28` | the current process. Set by the runtime, never written by compiled code. |
| `x19`–`x27` | callee-saved. Compiled code never uses them, and every runtime op preserves them *including* the ones that suspend. |
| `x18` | never touched (it belongs to macOS) |
| the rest | as AAPCS64 |

Compiled code keeps nothing in registers across a call: every value that
has to wait is in a frame slot. Where the heap pointer and limit live is
for step 4.

**A function's frame** is `x29`/`x30` on top, then its slots, addressed up
from `sp`; the parameters go to the first slots straight away. A call in
tail position loads the arguments, takes the frame down
(`mov sp, x29; ldp x29, x30, [sp], #16`) and branches, so loops run in
constant stack.

**Every function entry** (so every call and every tail call) runs two
checks:

```
    ldr  x16, [x28, #RT_PROC_STACK_LIMIT]   // room on the stack?
    cmp  sp, x16
    b.lo Lfault_N                           // fault: stack overflow
    str  x0, [sp, #0]                       // save the parameters
    ...
    ldr  x16, [x28, #RT_PROC_REDUCTIONS]    // reductions used up?
    subs x16, x16, #1
    str  x16, [x28, #RT_PROC_REDUCTIONS]
    b.le Lpreempt_N                         // mov x0, x28; bl rt_preempt
```

The stack check is Go's: compare `sp` with a limit in the process, rather
than catch a guard-page `SIGSEGV` on an alternate signal stack (D48). The
limit sits 64 KB above the bottom, which leaves room for the frame that
tripped it and a call to `rt_fault`; a guard page under the stack still
catches anything that slips past. The root process runs on its own 8 MB
`mmap`ed stack. The quota is 1,000 reductions; for now `rt_preempt` just
refills it, and in step 7 it is where a process gets preempted. The
parameters are saved before the reduction check because `rt_preempt` is
a C call and may clobber `x0`–`x7`.

## Compiler

TypeScript, run directly by Node (type stripping is on by default from
Node 22.18; this container has 22.22), as ts-cpi does. Same setup as ts-cpi:
`node --test` for tests, and `typescript` as the only dev dependency, for
`tsc --noEmit` type checks. No runtime npm dependencies.

It compiles the whole program at once: the prelude plus the program's
files go to one `.S` file, which clang assembles and links with the
runtime. The `recv` rule needs the whole call graph, and with one unit
that's just a walk over every function.

### Passes

1. **Read** text into s-expressions with source positions.
2. **Check and expand** the 10 special forms. `let` becomes nested scopes,
   `quote` becomes constants.
3. **Resolve names** as locals, globals or builtins. Check arity on calls
   to known functions.
4. **Classify** functions as receive, state or plain (fixpoint over the
   call graph), and enforce the `recv` rule.
5. **Convert closures.** Each lambda's free variables become fields of its
   closure. Each `fork`/`connect` expression becomes a hidden entry
   function that takes its free variables as arguments.
6. **Mark tail calls.**
7. **Generate code**, Ghuloum-style: the accumulator is `x0`, temporaries
   spill to the stack, no register allocation. Tagged values, a reduction
   check at entries and tail calls, collection points at `recv` and at
   tail calls from state functions.
8. **Emit data**: string literals, quoted constants, static closures, the
   symbol name table.

Expect 1,500–2,500 lines for a first version.

### Style: "slight-shaped"

Pure functions over immutable s-expressions, recursion instead of loops
(or a loop where slight would tail-recurse: D39), association lists for
environments, no classes. Then porting the compiler to slight is a near
line-by-line translation.

### Bootstrap (plan step 12)

1. The TypeScript compiler (stage 0) compiles the slight port of the
   compiler into a native binary (stage 1).
2. Stage 1 compiles its own source, producing stage 2.
3. Stage 2 compiles the source again. When the output matches stage 2's
   byte for byte, the bootstrap is proven and the TypeScript can retire.

## Testing

- **Golden tests**: compile a `.slight` file, run it (under qemu on x86),
  diff its stdout and stderr against a `.expected` file, with a last line
  `exit: N` when the exit status isn't 0. Like `spike/aarch64/t/run.sh`.
  Every plan step adds some.
- **Compiler unit tests** with `node:test`, per pass.
- A slight-level test library in the style of ts-slight's `lib/Test.slight`
  (TAP: `ok`, `is`, `diag`) once enough of the language exists.

## Open questions

Collected from above:

1. Exact `recv` pattern syntax and matching details.
2. Names for integer division and remainder (`div`, `%`?).
3. Exit-record retention policy.
4. Bounded mailboxes.
5. FIFO run queue (proposed) vs. the spike's ticks.
6. Whether `apply` is a builtin.
7. Program structure: top-level forms as the root process (built this way).
8. `kill`'s exit reason.
9. GC at base-of-stack tail calls outside state functions.
10. The shape of a fault's reason.
