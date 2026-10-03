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
| pid      | a process id; prints as `#<pid N>`, and the root process is 1 |

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

Syntax (D85):

```lisp
(defun counter (n)
    (recv
        ((:inc)        (counter (+ n 1)))
        ((:get from)   (send from (list :count n)) (counter n))
        ((:stop)       n)
        (other         (log-unknown other) (counter n))))
```

- `(:tag a b ...)` matches a list of exactly that length whose head is the
  symbol `:tag`, and binds the rest by position. `_` in a position matches
  anything and binds nothing.
- `:tag` on its own matches exactly that symbol.
- A bare name matches anything and binds the whole message (`_` binds
  nothing).
- Clauses are tried in order. Only the **first** message in the mailbox is
  considered: there's no selective receive. If no clause matches, the
  message goes to the **dead-letter log** (a line on stderr,
  `dead letter: <message> (recv at file:line:col)`) and the function takes
  the next message, or waits.
- Not supported: nested patterns, literals other than the head keyword, a
  rest binding. Match the outer shape, then take the rest apart with
  `cond` in the body.

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
| `(fork expr)` | The compiler turns `expr` into a hidden entry function whose parameters are `expr`'s free variables. Their values are **deep-copied** into the child; at most 8 of them. `expr` runs at the base of the child's stack, so it may tail-call a state function. Inside `expr`, `$$` is the child. |
| `(connect :keypress expr)` | `fork`, plus the new process receives the source's events as messages. |
| `(send pid msg)` | `msg` is any value, conventionally a list headed by a keyword. Deep-copied. Never blocks. Returns `()`. |
| `$$`, `^$$` | self, parent; the root's parent is `()` |
| `(join pid)` | **Blocking** wait for `pid` to end. Returns `(:ok value)` or `(:error reason)`. Works on any pid (not just children), anywhere (including inside lambdas), any number of times, and after the process has already ended. Joiners wake in the order they joined. `(join $$)` faults (`:join-self`). |
| `(monitor pid)` | Opt-in. When `pid` ends, the runtime sends `(:exit pid result)` to the caller; at once, if it already has. Monitoring twice means two messages. There are no automatic messages to the parent. Returns `()`. |
| `(kill pid)` | Ends `pid` with `(:error :killed)` (D87), whatever it's doing: waiting in `recv`, in the run queue, or blocked in `join`. `(kill $$)` ends the caller. Killing a process that has ended does nothing. Returns `()`. |
| `(after ms pid msg)` | A timer: sends `msg` to `pid` after `ms` milliseconds. |
| `(raise reason)` | Ends the current process with `(:error reason)`. |

- A process whose entry expression returns a value ends with `(:ok value)`.
- **Errors are values**: `(:ok v)` and `(:error e)`. There's no `catch`.
  A fault (overflow, a builtin given the wrong type, a `cond` test that
  isn't a boolean, no `cond` clause matching, an arity mismatch, the heap
  limit) ends the process with `(:error (kind value site))` (D87): `kind` a
  keyword such as `:overflow` or `:not-a-list`, `value` the offending value
  (or `()`), `site` a string like `"car at file:line:col"`. The kinds are
  listed in `runtime/rt.h` (`RT_FAULT_...`). `(raise r)` ends it with
  `(:error r)`. `join`, `monitor` and `kill` given something that isn't a
  pid fault (`:not-a-pid`).
- **What gets logged** (D99, D100). A fault is logged to stderr when it
  happens, since it's always a bug: `fault: <what> (<where> at
  file:line:col)` in the root, `fault in #<pid N>: ...` anywhere else.
  `raise` and `kill` aren't logged, except in the root, whose error is the
  program's: `error: <reason>`. The program exits 1 if the root ends with
  an error, and otherwise prints the root's value and exits 0.
- **Preemption.** Every loop is a tail call, so a reduction counter is
  checked at each function entry and tail call. When it runs out, the
  process pauses where it is, *keeping its stack*, and goes to the back of
  the run queue. `yield` is the same pause, chosen by the program.
- **Stacks exist only while needed.** A process that is running, paused
  (preempted or yielded), blocked in `join`, or blocked in a syscall holds a
  stack. A process waiting in `recv` holds none.
- **Deadlock** (nothing can run and nothing is pending: no timers, no event
  sources, no blocked I/O) is detected and reported. So far (step 8): if
  the run queue empties before the root process has ended, the program
  prints `deadlock: the root process is waiting for a message, and nothing
  else can run` (or `... waiting for #<pid N> to end, ...`, in `join`) to
  stderr and exits 1.
- **The program ends when nothing can run**, not when the root ends: other
  processes keep running after the root has its value (or its error), and
  the root's value is printed last. Processes still waiting in `recv` or
  `join` at that point are dropped (D93).
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
- arithmetic: `+ - *` on any two numbers; an integer and a float give a
  float, and an integer result that doesn't fit in 63 bits faults. `/`
  always returns a float. `div` and `%` take integers only and truncate
  toward zero (`(% -7 2)` is -1). Dividing by zero faults, with `/` too.
  `ceil`, `floor`, `round` and `trunc` return integers (`round` sends
  halves up, as ts-slight's did: `(round -2.5)` is -2), and fault if the
  result doesn't fit. `sqrt pow sin cos tan exp` always return floats.
  `abs`, `min` and `max` keep their argument's type. `PI` is a float.
  `float?` and `num?` alongside `int?`. Floats print in the shortest form
  that reads back as the same double, laid out as JavaScript does, but
  always with a `.` or an exponent: `3.0`, `0.1`, `1e+21`, `nan`, `inf`.
- comparison: `== != < <= > >=` on any two numbers (`(== 1 1.0)` is
  `#true`), structural `eq?`/`ne?` on any values (`(eq? 1 1.0)` is
  `#false`)
- type predicates: `nil? cons? sym? str? num? int? float? lambda? pid? bool?`
  (`sym?` is true for `#true` and `#false`: they're symbols)
- lists: `cons car cdr list`, and `c[ad]r` with up to four letters
  (`cadr`, `cddr`, `caddr`, ...). `car`/`cdr` of anything but a cons
  faults, and so does `cons` onto anything but a list, so every list is
  proper (both as in ts-slight). Inside a quoted list, `:a` reads as
  `(quote a)`, as it did in ts-slight; write `'(a b)`, not `'(:a :b)`.
- strings: `str?`, `str-len` (bytes), `substring`, `concat`/`~`,
  `index-of`, `str-split`, `str-join`, `string->int`, `symbol->string`,
  `string->symbol`, `byte-at`, `bytes->string`, `format-num`. See
  Strings below for how each behaves.
- processes: `send join monitor kill after raise`
- I/O: `tty/write`, `tty/screen/rows`, `tty/screen/cols`, `pprint`
  (prints its argument and a newline, returns `()`, as in ts-slight;
  symbols print without the colon, so `:ping` prints as `ping`),
  `sleep`, `slurp`, `spew`
- functions: `apply` (`(apply f xs)`, at most 8 elements; a tail call in
  tail position), `lambda?`

**In slight (the prelude, `lib/prelude.slight`):** compiled with every
program; it can only define functions.

| | |
|---|---|
| numbers, booleans | `inc dec`; `not and or` (booleans only, both sides always evaluated) |
| folds | `(fold/l init f xs)` with `(f acc x)`; `(fold/r init f xs)` with `(f x acc)` |
| lists | `reverse length append sum product map`; `(filter f xs)` keeps what `f` says `#true` to, `(remove f xs)` drops it; `(take n xs)`, `(skip n xs)` stop at the end of the list; `(nth i xs)` and `(find f xs)` give `()` when there's nothing; `member?`; `(range start end)` is `start` up to but not including `end`; `(dotimes start end f)` |
| association lists | `(assoc k v table)` adds `(k v)`; `(lookup k table)` gives the value or `:not-found` |
| strings | `uc lc` (ASCII), `(pad-start s n fill)`, `(pad-end s n fill)`, `(str-repeat s n)`, `starts-with?`, `ends-with?` |

These are ts-slight's, adjusted where they were confusing (D78): its
`filter` dropped what matched and `grep` kept it; its `range` took a step
and always ended with `end`; its `take` faulted past the end of the list;
`starts-with`/`ends-with` had no `?`; `concat-list` was `str-join`.

A program can define a function with a prelude name. Its own code then
uses its definition, and the prelude keeps using the prelude's (D80).

**Functions as values.** A `defun` name, a `lambda`, or a builtin with a
fixed number of arguments (`(map car xs)`, `(fold/l 0 + xs)`) can be used
as a value. Builtins with a varying number (`list`, `concat`,
`format-num`) can't.

**Tests in slight:** `lib/test.slight` is a TAP library after ts-slight's
`lib/Test.slight`: `(run-tests (list (ok test msg) (is got expected msg)
(diag msg)))`. It isn't part of the prelude; a golden test whose first line
is `; with: lib/test.slight` gets it compiled in.

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
- `\n`, `\r`, `\t` and `\e` are names for one-character strings, as in
  ts-slight; string literals also take those escapes, plus `\"`, `\\` and
  `\u{hex}`.

**The builtins (step 5a).** Where ts-slight's behaviour (which was
JavaScript's) differs from what you might guess, it wins (D62):

| Builtin | |
|---|---|
| `(str? x)` | |
| `(str-len s)` | length in bytes |
| `(substring s start end)` | bytes `[start, end)`; each index clamped to the string, and swapped if `start > end` |
| `(concat x ...)` | any values: strings as their bytes, anything else as `pprint` shows it |
| `(~ a b)` | two strings only |
| `(index-of s m)` | byte index of the first `m`, or -1; an empty `m` is at 0 |
| `(str-split s sep)` | the pieces between the `sep`s; `""` gives `()`; an empty `sep` splits into bytes |
| `(str-join sep xs)` | `xs` rendered as `concat` does, with `sep` between |
| `(string->int s)` | a decimal integer that fits in 63 bits, or `#false` |
| `(symbol->string sym)`, `(string->symbol s)` | the latter is `#false` unless the program mentions that symbol (D14) |
| `(byte-at s i)` | a byte as an integer; an index out of range faults |
| `(bytes->string xs)` | integers 0–255 to a string |
| `(format-num n width [fill])` | `n` padded at the start to `width`, with `fill` (default `" "`) repeated as JavaScript's `padStart` does |
| `(tty/write x ...)` | writes its arguments, rendered as `concat` does, and flushes; returns `()` |

`pprint` shows a string in double quotes with nothing escaped inside, as
ts-slight did. `eq?` compares strings byte by byte.

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
| `111` | pid     | immediate: the process's index in the process table, `id << 3 \| 7` |

- **Cons cells** are 16 bytes. The tag folds into the load offset:
  `car` is `ldur x0, [x1, #-1]` and `cdr` is `ldur x0, [x1, #7]`.
- **Strings**: header (byte length), the bytes, then a NUL that the length
  doesn't count, so a string can go straight to C.
- **Floats**: header plus 8 bytes.
- **Closures**: header (the number of captured values, and the closure
  type), code pointer, arity, name (a C string, for printing:
  `#<function square>`, `#<function lambda at t/x.slight:3:9>`), then the
  captured values. A lambda's body is a function of its own; on entry it
  copies its captured values from the closure (in `x9`) into its frame, so
  inside, they're ordinary locals (D79). A lambda that captures nothing,
  and every `defun` used as a value, is a static closure.
- **Static data**: string literals, quoted constants, and the static
  closures for top-level functions live in the binary. Every process shares
  them. They're never collected and are sent without copying. The collector
  skips them with one address-range check.
- **The heap is acyclic**: data is immutable, lambdas capture by value and
  can't refer to themselves, and there's no local `defun`.

### Process heaps and GC

**So far (step 7):** a chain of `malloc`ed chunks per process (D89). The
first, 4 KB, comes with the first allocation (so a process that allocates
nothing has no heap); each new one is twice the last, up to 1 MB. Message
chunks join the chain when received. All of a process's chunks together
are capped at 64 MB, and past that the allocation faults. With no GC yet,
that includes every message the process has ever received. The heap
pointer and limit live in the process struct
(`[x28, #RT_PROC_HEAP_PTR]`), so C builtins that allocate can use them with
nothing to sync (D57). Compiled code allocates inline, and calls the
runtime only to start a new chunk:

```
Lalloc_N:
    ldr  x2, [x28, #RT_PROC_HEAP_PTR]
    ldr  x3, [x28, #RT_PROC_HEAP_LIMIT]
    add  x4, x2, #16                        // bytes
    cmp  x4, x3
    b.hi Lalloc_N_grow                      // out of line: rt_heap_grow, then retry
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
- Mailboxes are unbounded (D88). (Bounded ones would have to drop the
  message or fault the sender, since `send` can't block.)
- A message to a process that has ended disappears, as in Erlang (D92).

### Process lifecycle

- `fork`: allocate the process, deep-copy the free variables into its heap,
  queue it to run the entry function.
- Ending: the Result is copied into a small per-pid **exit record**.
  Processes blocked in `join` on it wake with a copy; later `join`s read
  the record; monitors get `(:exit pid result)`. Then the heap, the
  mailbox, the stack and the process struct are freed (D102). Nothing
  on a stack needs cleaning up, so `kill` frees a process wherever it is
  (D103).
- The record is the pid's 32-byte entry in the process table: the value
  (or the error's reason), and a chunk holding a copy of it if it needs
  memory. `(:ok v)` and `(:exit pid result)` are built when asked for.
- Exit records are kept forever (D88). That doesn't matter at hundreds or
  thousands of processes, and a million cost 32 MB. A system that churns
  through many millions will need a retention policy, such as dropping
  the record once the parent has seen it.

### Scheduler

- Single core. A **FIFO run queue** (D86): the spike used deterministic
  ticks, but a plain run queue is simpler and still deterministic on one
  core. A preempted process goes to the back of the queue, but only if
  another process is ready; otherwise it keeps running (D91). `yield`
  always goes to the back. A **virtual clock** for tests (from ts-cpi)
  keeps timer tests exact.
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

### Builtins in C

Arithmetic and comparisons on two integers are inline: one `orr` and one
`tst` check both tag bits, and anything else branches out of line to the
runtime (`rt_add`, `rt_compare`, ...), which promotes to float or faults.

A builtin written in C is an ordinary AAPCS64 function. The compiler
passes the call's site (`"str-len at t/x.slight:2:1"`) in the register
after the arguments, so faults raised inside C still say where in the
program they happened (D63). A variadic builtin (`concat`, `tty/write`)
gets its arguments as one list. C code allocates with `rt_alloc`, which
bumps the heap pointer of `rt_current`, the running process.

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

1. Program structure: top-level forms as the root process (built this way).
2. GC at base-of-stack tail calls outside state functions.

Settled in step 7: `recv` syntax (D85), the run queue (D86), the fault,
`raise` and `kill` reasons (D87), exit records and mailboxes (D88). In
step 8: the fault kinds (D98), and what gets logged (D99, D100).
