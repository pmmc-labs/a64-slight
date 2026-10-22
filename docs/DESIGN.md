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
- **Aggressively simple.** Anything that adds complexity gets cut or
  changed. Practical beats pure.
- **The user surface is kept; the internals are new** (D2).
  [`LANGUAGE.md`](LANGUAGE.md) describes the surface.
- **Native AArch64.** macOS on Apple Silicon first, AArch64 Linux from the
  same code. x86-64 (Linux, and macOS under Rosetta 2) is a second target,
  from the same compiler through a target interface (D145). Development on
  x86 runs x86-64 natively and AArch64 with a clang cross-compile plus
  qemu-aarch64.
- **Ahead-of-time compilation only.** No `eval`, no code loading at
  runtime, no hot reload.

## Language

### Syntax

The reader (`compiler/src/reader.ts`, D37) reads:

- `( ... )` lists; `()` is nil
- integers (`42`, `-7`) and floats (`3.14`)
- strings in double quotes, with escapes: `\"` `\\` `\n` `\t` `\r` `\e`
  `\u{hex}` (a code point up to U+10FFFF that isn't a surrogate)
- symbols. `:name` is a keyword, a symbol that evaluates to itself.
  `#true`/`#false` are the booleans.
- `'x` is `(quote x)`
- `;` starts a comment
- doc blocks (D156, D164): between top-level forms, a line `=doc`
  begins one, which runs to a line `=cut` or the end of the file. The
  compiler skips it. It holds Markdown (a subset: headings, paragraphs,
  emphasis, code, fenced blocks, lists, links); any other `=word` there
  is an error, kept for later kinds.
- `$$` (self) and `^$$` (parent)
- `@ARGV` and `%ENV`, the program's arguments and environment, at the
  top level only (Program structure, below)
- the names `\n`, `\r`, `\t` and `\e`, constants for those one-character
  strings, and `math/PI`

### Types

| Type     | Notes |
|----------|-------|
| integer  | 63-bit. Overflow faults the process. |
| float    | boxed IEEE double |
| symbol   | an id assigned at compile time. Not interned at runtime, no `gensym`. |
| list     | cons cells and `()` |
| string   | immutable bytes, UTF-8 by convention |
| tuple    | a fixed number of values, each one load away; prints as `#(1 2)` (D169) |
| function | a closure, or a reference to a top-level function |
| pid      | a process id; prints as `#<pid N>`, and the root process is 1 |

Booleans are the reserved symbols `#true` and `#false`.

### Special forms (10)

| Form | Meaning |
|------|---------|
| `(defun name (params...) body...)` | Top level only. No local `defun`. |
| `(lambda (params...) body...)` | A closure. Captures free variables **by value**. Always a *plain* function (see the `recv` rule). Can't refer to itself. |
| `(let name expr)` | Binds `name` for the rest of the enclosing body. Only allowed directly in a body. As the last form of a body, its value is `expr`'s (D42). |
| `(cond (test body...) ...)` | The conditional the others become (below). Each test must be `#true` or `#false`, or the process faults; so does running out of clauses. |
| `(do form...)` | Evaluates in order. The last form is in tail position. |
| `(quote x)`, `'x` | A constant. `:sym` is self-quoting. |
| `(fork expr)` | Runs `expr` in a new process and returns its pid. |
| `(connect :source [arg] expr)` | Same as `fork`, and connects the new process to a source: `:keypress`; a file, `(connect :fs/read path expr)` (and `:fs/write`, `:fs/append`); a socket, `(connect :tcp "host:port" expr)` or `(connect :tcp/listen port expr)`, or a Unix socket, `(connect :unix path expr)` or `(connect :unix/listen path expr)`; a command, `(connect :exec (list "ls" "-l") expr)`; or `:system`. `(connect dev expr)` hands the device `dev` to the new process. |
| `(recv clause...)` | Takes the next message. Only allowed as the whole body of a receive function. |
| `(yield expr)` | Pauses: goes to the back of the run queue, then evaluates `expr` in tail position. |

Function bodies and clause bodies can hold several forms, as if wrapped in
`do`.

**Expanded forms** (D142). The expander (`compiler/src/expand.ts`), after
the reader and before the compiler, makes these into `cond`:

| Form | Becomes |
|------|---------|
| `(if test then [else])` | `(cond (test then) (#true else))`; without an else, `()` |
| `(when test body...)` | `(cond (test body...) (#true ()))` |
| `(case topic (value body...) ...)` | the topic, once, compared with each value by `eq?`; a `#true` clause is the default, and without one, `()` when nothing matches |
| `(and a b ... z)` | `(cond (a (and b ... z)) (#true #false))`; `(and z)` is `z`, `(and)` is `#true` |
| `(or a b ... z)` | `(cond (a #true) (b #true) ... (#true z))`; `(or z)` is `z`, `(or)` is `#false` |
| `(match topic (pattern body...) ...)` | the topic, once, then a `cond` whose clauses are each pattern's tests (`cons?`, `nil?`, `eq?` on its parts) and `let`s of its names; a last clause that faults (`:no-match`) unless the last pattern takes anything (D168) |

`and` and `or` stop at the operand that decides. As in Scheme, the last
operand isn't tested, and is what they give if they get to it
(`(and #true 5)` is 5); the others are `cond` tests, so each must be
`#true` or `#false`. They keep tail calls in tail position, to receive
functions too. `not` is a prelude
function. None of these names can be bound or defined.

**Includes** (D141). `(@include "path/file.slight")` splices in another
file's forms where it stands, the path relative to the file it's in;
`(@include :name)` is the built-in `lib/name.slight` (`:fs`, `:test`).
Only at the top level. A file is included once however often it's asked
for, and a file that includes itself, directly or not, is an error. Every
program starts with an unwritten `(@include :prelude)`, whose definitions
the compiler keeps apart (D80). It's not a module system: everything is
still one program, compiled whole.

A function takes at most 8 parameters, one register each (see the
register convention below). More could go on the stack later.

### The `recv` rule

This rule is what makes the runtime simple, so it gets its own section.

- A **receive function** is a top-level `defun` whose body is a single
  `recv` form. In the code the compiler generates, `recv` appears nowhere
  else. In a program it can go anywhere a tail call could (D167), and the
  compiler splits the function there (`compiler/src/split.ts`): the `recv`
  and what follows it become a receive function of their own, which the
  function tail-calls with the locals that are still needed. See below.
- A **state function** is a receive function, or any function that
  tail-calls a state function (so, after the split, any function with a
  `recv` in it). The compiler computes this as a fixpoint over the call
  graph.
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
body is just the `recv`. "All state lives in the loop arguments" is
literally true at every wait.

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
- The patterns are `match`'s (D168, `compiler/src/pattern.ts`): they
  nest, a literal (a number, a string, `#true`, `#false`, `()`, anything
  quoted) can go anywhere, `(p ... & rest)` binds the rest of a
  list, and `#(p ...)` matches a tuple of exactly that many elements
  (D176). One lowering, to tests and bindings, serves both: the expander
  makes `match` into `cond` with it, and the code generator compiles
  `recv`'s clauses with it, branching on each test. No guards: match the
  shape, and test the rest with `cond` in the body. A tuple pattern's test
  is one compare of the box's header with the header a tuple of that many
  elements has, and each element one load, so it costs about what a list
  pattern does (D178).

Send-then-wait is written in one function, and becomes two:

```lisp
(defun pinger (target n)
    (cond
        ((== n 0) (send target (list :stop $$)) :ping-done)
        (#true
            (send target (list :ping $$))
            (recv ((:pong from) (pprint (list :round n))))
            (pinger target (- n 1)))))
```

Where a `recv` can go (D167): a form of a body in tail position (a
function's, a `cond` or `recv` clause's, or that of a `do` or `yield` in
tail position), or a `let`'s value there; or inside a call there, or a
`do` with no `let`s, which is flattened first, in the order things are
evaluated, so what comes before the `recv` still happens first; or a
`cond`'s test, when the `cond` is in tail position. As an expression,
`recv` gives the value of the clause that matched, and `(recv)` takes any
message. Not in a lambda, and not in a `cond`, `do` (with `let`s) or
`yield` that isn't in tail position. A `recv` with several clauses and
something after it gets a generated function for what's after, rather
than a copy of it in each clause; more than 8 locals go across as one
list. Generated names have spaces in them, which a program can't write,
and say where the `recv` is (`ask (recv at f.slight:3:12)`).

A function that waits still can't give a value back to a function that
called it: that's a process and `join` (as `ask` in `lib/ds.slight` does,
and `(join (connect :tcp ...))`), which works anywhere, a lambda included.

Request/reply is correlated by the actor's own state: put a counter or the
request's details in the arguments and match the reply when it arrives
(run-to-completion style). There's no AWAIT/REQUEST/REPLY machinery and no
reply refs.

### Processes

| | |
|---|---|
| `(fork expr)` | The compiler turns `expr` into a hidden entry function whose parameters are `expr`'s free variables. Their values are **deep-copied** into the child; at most 8 of them. `expr` runs at the base of the child's stack, so it may tail-call a state function. Inside `expr`, `$$` is the child. |
| `(connect :keypress expr)` | `fork`, plus the new process gets every key typed, as a message (see `:keypress`, under Scheduler). |
| `(connect :fs/read path expr)`, `:fs/write`, `:fs/append` | `fork`, plus the new process owns the file at `path`, opened on a device whose first message to it is `(:open f)` (see Files, under Scheduler). `path` is evaluated in the caller, and must be a string. |
| `(connect :tcp "host:port" expr)`, `(connect :tcp/listen port expr)` | `fork`, plus the new process owns a connection, or a listener on `port` (0 lets the system pick), on a device (see Sockets, under Scheduler). |
| `(connect :unix path expr)`, `(connect :unix/listen path expr)` | The same, at a Unix socket's path (D231). |
| `(connect :exec cmd expr)` | `fork`, plus the new process owns a command, `cmd` being a list of strings (its name and arguments), run on a device (see Commands, under Scheduler). `cmd` is evaluated in the caller. |
| `(connect :system expr)` | `fork`, plus the new process owns a device that tells about the processes and the runtime (see `:system`, under Scheduler). |
| `(connect dev expr)` | `fork`, plus the new process is handed the open device `dev` (anything that isn't a `:keyword` is taken to be one), which then tells it `(:open f)`. Anything that isn't an open device faults (`:not-a-device`). |
| `(disconnect f)` | Closes the device `f`. Anything that isn't an open device it ignores. Returns `()`. |
| `(send pid msg)` | `msg` is any value, conventionally a list headed by a keyword. Deep-copied. Never blocks. Returns `()`. |
| `$$`, `^$$` | self, parent; the root's parent is `()` |
| `(join pid)` | **Blocking** wait for `pid` to end. Returns `(:ok value)` or `(:error reason)`. Works on any pid (not just children), anywhere (including inside lambdas), any number of times, and after the process has already ended. Joiners wake in the order they joined. `(join $$)` faults (`:join-self`). |
| `(join/race pids)` | **Blocking** wait for the first of `pids` to end, keeping the stack, as `join` does (D220). Returns `(pid result)`, `result` as `join` gives it. If some have ended already, it's the first of those in the list, as JavaScript's `Promise.race` settles. The others are left running. An empty list faults (`:not-a-pid`), and so does the caller's own pid (`:join-self`); every pid is checked before it waits. `join/any` and `join/all`, JavaScript's `Promise.any` and `Promise.all`, are in the prelude, on top of it. |
| `(monitor pid)` | Opt-in. When `pid` ends, the runtime sends `(:exit pid result)` to the caller; at once, if it already has. Monitoring twice means two messages. There are no automatic messages to the parent. Returns `()`. |
| `(kill pid)` | Ends `pid` with `(:error :killed)` (D87), whatever it's doing: waiting in `recv`, in the run queue, blocked in `join`, or asleep. `(kill $$)` ends the caller. Killing a process that has ended does nothing. Returns `()`. |
| `(after ms pid msg)` | A timer: sends `msg` to `pid` once `ms` milliseconds have passed. `msg` is copied now, as `send` copies it. Returns the timer's number (D232). |
| `(timer/cancel t)` | Takes back `after`'s timer `t` if it hasn't fired: `#true`, or `#false` (D232). |
| `(sleep ms)` | **Blocking** wait for `ms` milliseconds, keeping the stack. Works anywhere. Returns `()`. |
| `(clock/us)` | The microseconds since the program started, on the timers' clock (D208). |
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
  checked at each function entry and tail call (a reduction is one entry
  into a compiled function: LANGUAGE.md, under Scheduling). When it runs out, the
  process pauses where it is, *keeping its stack*, and goes to the back of
  the run queue. `yield` is the same pause, chosen by the program.
- **Stacks exist only while needed.** A process that is running, paused
  (preempted or yielded), blocked in `join` or `join/race`, or blocked in a syscall holds a
  stack. A process waiting in `recv` holds none.
- **Deadlock** (nothing can run and nothing is pending: no timers, no event
  sources, no blocked I/O) is detected and reported. So far (step 10d): if
  the run queue empties, no timer is pending, no process is connected to
  `:keypress` with stdin still open, no socket or command can be waited
  for (below), and the root process hasn't ended,
  the program prints `deadlock: the root process is waiting in f for a
  message, and nothing else can run` (or `... waiting in f for #<pid N>
  to end, ...`, in `join`, and `... for the first of #<pid N>, #<pid
  M> to end, ...`, in `join/race`) to stderr and exits 1, `f` being the
  function it waits in (D171).
- **The program ends when nothing can run** and nothing more can come (no
  timer is pending, no socket or command can be waited for, and no
  process is connected to `:keypress` or stdin has ended), not when the
  root ends: other
  processes keep running after the root has its value (or its error), and
  the root's value is printed last. Processes
  still waiting in `recv` or `join` at that point are dropped (D93).
- **Blocking calls.** Since a process can block with its stack, `sleep`
  blocks only the calling process; it can't fail, and returns `()`
  (D111). Files are devices instead (Files, under Scheduler), and `slurp`
  and `spew` are written in slight with `connect` and `join`
  (`lib/fs.slight`), so they block only their caller too, and give
  Results: `(:ok lines)` and `(:ok ())`, or `(:error reason)` (D129).
- **Timers** (D112, D114, D232). `after` and `sleep` take an integer
  number of milliseconds; a negative one counts as 0. Timers due at the
  same time fire in the order they were set, at the scheduler's next
  decision (so `(sleep 0)` lets the processes already waiting go first).
  `after` gives its timer's number, 1 up, and `timer/cancel` takes one
  back by it, found in an index of the pending ones, kept as the heap
  moves them. A timer whose process has ended is dropped when it's due,
  as a message to it would be, or sooner: when nothing can run, those
  next due whose process has ended are dropped, so the program neither
  waits for them nor moves the clock to them.
- **The clock** (D208). `clock/us` reads the timers' clock: the
  system's monotonic one, through `replay.c`, so a replay gives the
  same readings, or the virtual one, which moves only as the timers do.
  Microseconds, since a part of a run worth timing takes tens of them
  or more, and nanoseconds would claim more than macOS's clock gives.

### Program structure

A program is one or more `.slight` files, and whatever they include. All
`defun`s are global and can call each other in any order. Every other
top-level form, in order, becomes the body of the root process; a
top-level `let` binds for the rest of the root process only, and `defun`s
can't see it. The program's exit status follows the root process's Result.
The examples (`examples/`) are all written this way. Built this
way in step 2; a program with no top-level expressions has the value `()`.

**`@ARGV`** (D144) is the program's arguments after its name, as a list of
strings (`()` with none). The top-level forms are the body of a function,
`slight_main`, and `@ARGV` is its one parameter: the top level sees it
like any local (a lambda or a `fork` there captures it), and a `defun`
doesn't, so pass it to the functions that need it. It can't be bound or
defined. A number comes as a string: `(str->int (car @ARGV))`.

**`%ENV`** (D186) is the program's environment, as a list of `(name
value)` lists of strings, in the environment's order, for `lookup`
(`:not-found` for a variable that isn't set). It's `slight_main`'s second
parameter, seen as `@ARGV` is, and for the same reason: the environment is
the program's input, given to the root, and a key reaches only the code
it's passed to. The runtime's `main` builds it in the root's heap; an
entry is cut at its first `=`, and one without an `=` is left out. A
receive function split from the top level (D167) carries both under
names no program can write.

### Builtins and the prelude

[`LANGUAGE.md`](LANGUAGE.md) describes each one as a program sees it.

**In C (or assembly):**
- arithmetic: `+ - *` on any two numbers; an integer and a float give a
  float, and an integer result that doesn't fit in 63 bits faults. `/`
  always returns a float. `div` and `%` take integers only and truncate
  toward zero (`(% -7 2)` is -1). Dividing by zero faults, with `/` too.
  The math builtins are under `math/` (D221). `math/ceil`, `math/floor`,
  `math/round` and `math/trunc` return integers (`math/round` sends
  halves up, D72, so `(math/round -2.5)` is -2), and fault if the result
  doesn't fit. `math/pow` of two integers is an integer, exactly; with a
  float, a float. `math/mod` is the floored remainder, its sign the
  divisor's. The rest of JavaScript's `Math` (`math/sqrt`, `math/log`,
  `math/atan2`, `math/hypot` and so on) is libm's, and always returns
  floats. `math/abs`, `math/min` and `math/max` keep their argument's
  type. `math/PI`, `math/E` and `math/INF` are floats; `math/nan?` tests
  for `nan`. `int->float`, and `float->str`, which with a count of
  digits is JavaScript's `toFixed`, rounding the float's exact value, a
  tie away from zero.
- bits: `bit/and bit/or bit/xor bit/not bit/shl bit/shr` on 63-bit
  two's-complement integers, compiled inline (an integer is `n << 1`, so
  and, or and xor of the tagged words are the tagged results), and
  `bit/count` in C. A shift left wraps, losing what passes the top; a
  shift right fills with the sign; a count of 63 or more gives 0 or -1,
  as in Go, and a negative one faults (D221).
- random numbers: `rand/int` (0 to n - 1), `rand/float` ([0, 1)) and
  `rand/seed`, from a generator each process has, two words in its
  record: SplitMix64, Java's `SplittableRandom` (D222). The root's
  starts from `SLIGHT_SEED`, or from the system's entropy, the seed
  recorded either way; `fork` gives the child a generator split from the
  parent's, so a process's numbers never depend on the order processes
  run in. Not cryptographic.
  `float?` and `num?` alongside `int?`. Floats print in the shortest form
  that reads back as the same double, laid out as JavaScript does, but
  always with a `.` or an exponent: `3.0`, `0.1`, `1e+21`, `nan`, `inf`.
- comparison: `== != < <= > >=` on any two numbers (`(== 1 1.0)` is
  `#true`), structural `eq?`/`ne?` on any values (`(eq? 1 1.0)` is
  `#false`)
- `hash` on any value (`runtime/rt.c`, D209): the value as words, walked
  as `eq?` walks it, each word `w` making the hash `h` into `mix(h ^ w)`
  from 0, `mix` being splitmix64's (`+ 0x9e3779b97f4a7c15`, then its
  finalizer); the result is `h >> 2`. An integer is 1 and its value; a
  float 2 and its bits, `-0.0` as `0.0` and every NaN as `0x7ff8...`; a
  string 3, its length, and its bytes eight to a word, little-endian,
  the last padded with zeros; a symbol 4 and its name, and a function 6
  and its name, each as a string's length and bytes; a pid 5 and its
  number; a list 7, its elements and 8, so `()` is 7 8; a tuple 9, its
  length and its elements. No two values that aren't `eq?` have the same
  words, but for functions of one name (two closures of one `lambda`);
  and nothing in them is an address, which differs from run to run and
  moves with the collector. `t/models/hash.py` is the same in
  Python.
- type predicates: `nil? cons? sym? str? num? int? float? lambda? pid?
  bool? tuple?` (`sym?` is true for `#true` and `#false`: they're symbols)
- lists: `cons car cdr list`, and `c[ad]r` with up to four letters
  (`cadr`, `cddr`, `caddr`, ...). `car`/`cdr` of anything but a cons
  faults, and so does `cons` onto anything but a list, so every list is
  proper. Inside a quoted list, `:a` reads as `(quote a)`, as it does
  everywhere; write `'(a b)`, not `'(:a :b)`.
- tuples (`runtime/tuples.c`, D169): `tuple` (variadic), `tuple?`,
  `tuple/len`, `tuple/get`, `tuple/set` (a copy with one element
  changed), `tuple/insert` and `tuple/remove` (a copy one longer or
  shorter, for a hash trie's nodes, D223), `tuple/make`, `tuple/slice`,
  `list->tuple`, `tuple->list`.
  `eq?`, printing, `json/print` (as an array), `sexp/print` and
  `sexp/parse` (as `#(...)`) and copying know them. In a program,
  `#(...)` is only a pattern (D176).
- strings (`runtime/strings.c`, D219): `str?`, `str`, `~`, the `str/`
  builtins, which count characters, the `bytes/` ones, which count
  bytes, and the conversions `str->int`, `int->str`, `str->float`,
  `str->sym`, `sym->str`, `str->bytes`, `bytes->str` and `codes->str`.
  See Strings below for how each behaves.
- processes: `send join join/race monitor kill after raise disconnect`
- I/O: `tty/write`, `tty/screen/rows`, `tty/screen/cols` (the terminal's
  size, asked each time; 24 and 80 when stdout isn't a terminal), `pprint`
  (prints its argument and a newline, and returns `()`: D43;
  symbols print without the colon, so `:ping` prints as `ping`; what it
  writes is flushed whenever the runtime waits, D229),
  `sleep`. Files come through `connect`; `slurp` and `spew` are in
  `lib/fs.slight`.
- functions: `apply` (`(apply f xs)`, at most 8 elements; a tail call in
  tail position), `lambda?`

**In slight (the prelude, `lib/prelude.slight`):** compiled with every
program; it can only define functions.

| | |
|---|---|
| numbers, booleans | `inc dec`; `not` (booleans only; `and` and `or` are expanded forms) |
| folds | `(fold/l init f xs)` with `(f acc x)`; `(fold/r init f xs)` with `(f x acc)` |
| lists | `reverse length append sum product map`; `(filter f xs)` keeps what `f` says `#true` to, `(remove f xs)` drops it; `(take n xs)`, `(skip n xs)` stop at the end of the list; `(nth i xs)` and `(find f xs)` give `()` when there's nothing; `member?`; `(range start end)` is `start` up to but not including `end`; `(dotimes start end f)` |
| association lists | `(assoc k v table)` adds `(k v)`; `(lookup k table)` gives the value or `:not-found` |
| random numbers | `(rand/pick xs)`, an element or `()`; `(rand/shuffle xs)`, each element sorted by a `rand/float` of its own (D222) |
| processes | `(join/any pids)`, the first to succeed as `(:ok v)`, or `(:error reasons)` once all have failed; `(join/all pids)`, `(:ok values)` in the order of `pids`, or the first `(:error r)` as soon as one fails (D220) |

D78 has why `filter`, `remove`, `range`, `take` and `skip` work as they
do. The string functions it had are builtins now (D219).

A program can define a function with a prelude name. Its own code then
uses its definition, and the prelude keeps using the prelude's (D80).

**Functions as values.** A `defun` name, a `lambda`, or a builtin
(`(map car xs)`, `(fold/l 0 + xs)`) can be used as a value; a builtin
with optional arguments then takes the ones it needs (`(map str->int
xs)`, D219). Builtins that take any number (`list`, `str`, `~`) can't.

**Tests in slight:** `lib/test.slight` is a TAP library: `(run-tests
(list (ok test msg) (is got expected msg) (diag msg)))`. It isn't part
of the prelude: a program that wants it says
`(@include :test)`.

**Files in slight:** `lib/fs.slight` has `(slurp path)`, giving
`(:ok lines)` (without their newlines), and `(spew path lines)`, which
writes each line and a newline, replacing what was there, giving
`(:ok ())`; either gives `(:error (name path))` when it fails. They're a
few lines each, on `connect`. Opt-in, like `lib/test.slight`, since not
every program needs files (D129): `(@include :fs)`.

**Data structures in slight:** `lib/ds.slight` (D157) has the goal's
"data structure" actors: a cell, a dictionary, a queue and a channel,
each a process, and `ask` and `reply` for one's own. Writes are sends;
`ask` forks a box for the answer, which watches the structure, and joins
it, raising `(:ended pid result)` if the structure ends first. Opt-in:
`(@include :ds)`.

**A map as a value:** `lib/map.slight` (D223) is a persistent hash trie,
as Clojure's maps are, in slight: 32-way nodes of tuples, a bitmap
saying which slots a node has (`bit/count` finding a slot), 5 bits of
`hash` a level, and a bucket for keys whose 62-bit hashes are the same.
A slot is a leaf only when one key alone is under it, so a map's shape
depends only on its keys and `eq?` compares maps. Opt-in: `(@include
:map)`. Sharing ends at a process's edge: a send copies a map as it
copies any value (D25).

**An agent in slight:** `lib/agent.slight` (D184) is a conversation with a
model as a process, on ollama's `/api/chat` over `http/stream`. Its state
is the conversation; `(agent/say a text)` is a turn, and the caller hears
the answer's pieces as they come, then the whole answer, or an error.
Each turn runs in a process of its own, which the agent monitors. A
tool (D187) is a function of the model's arguments, with a name, a
description and parameters, from which `tool/new` makes the JSON Schema
the model reads; a turn runs each call in a process of its own, all at
once, sends the results back to the model, and goes on, at most 10
rounds, until the model answers. Opt-in: `(@include :agent)`.
`lib/tools.slight` (D188, D189) has tools to read, find and change
files, run commands, fetch URLs with curl (a page made plain text in
slight, its links kept, D193), work out arithmetic (`lib/calc.slight`, D190), and keep notes
in a store, a process that writes them to a file as an s-expression
(D191), and ask other agents (`agent/ask`, D192); those that change
something or reach out ask an `approve`
function first, which the chat makes ask the person at the keyboard,
with the text made printable (`tools/printable`), so the model can't
work the terminal; and a fetch doesn't follow a redirect, which is
another fetch to ask about (D205). A
command ends with the process that ran it, so a call that's killed takes
its command with it, which is how Escape in the chat stops one (D193).

**Calls between programs:** `lib/rpc.slight` (D225) is Matt S Trout's
line protocol, stupidrpc, finished, in s-expressions: `(call id
name arg ...)`, `(next id v)`, `(done id v)`, `(fail id reason)`,
`(stop id)`, `(helo rpc 1)` and `(barf reason)`, a datum each, read with
`(:read :sexp)` and written with `sexp/print`, so a person can type them
at `nc`, and a name or a string argument can be a bare word. The verb
says which way a message goes, so an id is unique only among one end's
calls in flight, and either end can call the other. A connection is a
process that owns the socket; a call from the other end runs in a
process of its own, which the connection monitors, so a fault or a
`raise` is a `fail` and `stop` is `kill`, and which a watcher stops if
the connection ends first, however it ends (D230); a call from here waits in a
process of its own too, which the connection monitors, so a caller that
ends stops its call. A handler has a name, a description and typed
parameters, which `help` gives and the connection checks. No remote
pids: another program is reached only by calls. For fleets (D228): a
handler's `^$$` is its connection, so a hub calls a device over the
connection the device made; `rpc/keep` keeps a connection up, dialing
again with growing waits and running an `on-open` function on each new
one; and `rpc/system` answers with what `:system` says. Where
`"host:port"` goes, a path (a string with a `/` in it) is a Unix
socket's, whose permissions say who may call (D231). Opt-in:
`(@include :rpc)`.

### Strings

Settled in D219, which replaced the byte builtins of step 5a and the
`utf8/` ones of D148 with one set, named after JavaScript's methods:

- **Bytes, counted as characters.** A string is immutable bytes, UTF-8
  by convention, and nothing checks it. A character is a well-formed
  UTF-8 sequence or, failing that, a stray byte on its own, whose code
  point is U+FFFD, as in Go (D148). So nothing faults on bad bytes,
  `str/chars` loses nothing, and `str/well-formed?` says whether a
  string has any stray bytes.
- **Two families.** The `str/` builtins count characters (code points,
  not UTF-16 units, and not graphemes); the `bytes/` builtins count
  bytes, in one step, for protocols and for parsers whose delimiters
  are ASCII. Where an edge case could go either way, a builtin does what
  JavaScript's method of the same name does (D62), with characters in
  place of UTF-16 units.
- **ASCII is marked.** A string whose bytes are all below 0x80 has
  `RT_BOX_ASCII` in its header (the type is the low seven bits), set
  where a string is made: by `rt_new_string`, by `rt_string_mark` after
  a builtin fills a blank one, by the readers and decoders, and by the
  compiler on a literal. There a character is a byte, so the `str/`
  builtins index in one step; elsewhere they walk from the start. A
  string made without the mark is only slower, never wrong.
- **Matches are whole characters.** A match must start and end where
  characters of the string do. A byte search already does that unless
  the pattern starts with a continuation byte or ends with a stray
  byte, which only a stray byte in it can do; then the search checks.
- **Unicode's tables, generated.** `str/upper` and `str/lower` map in
  full and without a language (SpecialCasing's unconditional mappings
  over UnicodeData's, and Final_Sigma), and `str/trim` takes Unicode's
  White_Space. `runtime/gen-unicode.py` makes `runtime/unicode.c` from
  the Unicode Character Database, pinned to the version the tests'
  Python has (15.1). Normalization, graphemes and display width are
  to come, from utf8proc, when they're wanted.
- There's no character type. A character is a one-character string, or
  a code point.
- Build strings by collecting pieces in a list and joining them.
- `\n`, `\r`, `\t` and `\e` are names for one-character strings; string
  literals also take those escapes, plus `\"`, `\\` and `\u{hex}`.

| Builtin | |
|---|---|
| `(str? x)` | |
| `(str x ...)` | any values joined: strings as their bytes, anything else as `pprint` shows it |
| `(~ s ...)` | strings only, joined |
| `(str/join sep xs)` | `xs` rendered as `str` does, with `sep` between |
| `(str/repeat s n)` | a negative `n` faults (`:out-of-range`) |
| `(str/pad-start s n [fill])`, `(str/pad-end s n [fill])` | to `n` characters, `fill` (default `" "`) repeated and cut, as `padStart` and `padEnd` |
| `(str/len s)` | length in characters |
| `(str/at s i)`, `(str/code-at s i)` | the character at `i`, or its code point; negative from the end; out of range faults |
| `(str/slice s start [end])` | characters `[start, end)`, as `slice`: negative from the end, clamped, empty unless `start < end` |
| `(str/chars s)`, `(str/codes s)`, `(codes->str xs)` | characters as one-character strings, or code points, and back; a code point with no UTF-8 faults |
| `(str/index-of s m [from])`, `(str/last-index-of s m [from])` | character index, or -1; `from` clamped |
| `(str/includes? s m)`, `(str/starts-with? s m)`, `(str/ends-with? s m)` | |
| `(str/split s sep [limit])` | as `split`: `""` gives `("")`; an empty `sep` splits into characters; at most `limit` pieces |
| `(str/replace s m r)`, `(str/replace-all s m r)` | as `replace` and `replaceAll` with a string; an empty `m` is at the start, or around each character |
| `(str/trim s)`, `(str/trim-start s)`, `(str/trim-end s)` | White_Space |
| `(str/upper s)`, `(str/lower s)` | full case mapping, no language; a stray byte stays |
| `(str/well-formed? s)`, `(str/to-well-formed s)` | stray bytes, or none, each made U+FFFD |
| `(str/compare a b)`, `(str/< a b)` | code point order, which is byte order |
| `(str->int s [radix])`, `(int->str n [radix])` | radix 2 to 36; `str->int` gives `#false` for what isn't one that fits in 63 bits |
| `(str->float s)` | a decimal number, `-?D+(.D+)?([eE][+-]?D+)?`, as the nearest float (as the reader rounds a literal), or `#false`, also when it's too big for a double |
| `(sym->str sym)`, `(str->sym s)` | the latter is `#false` unless the program mentions that symbol (D14) |
| `(str->bytes s)`, `(bytes->str xs)` | the bytes as integers 0–255, and back |
| `(bytes/len s)`, `(bytes/at s i)`, `(bytes/slice s start [end])`, `(bytes/index-of s m [from])` | as the `str/` ones, on bytes |
| `(tty/write x ...)` | writes its arguments, rendered as `str` does, and flushes; returns `()` |

`pprint` shows a string as the reader takes it back, on one line, as
`sexp/print` does: `"` and `\` escaped, and the control characters (C0,
DEL and C1) as `\n`, `\t`, `\r`, `\e` or `\u{hex}`; other characters,
and stray bytes, as they are (D219, which replaced D64). `eq?` compares
strings byte by byte. Output writes a string's bytes as they are.
`str/chars` takes 32 bytes a character, so a text split into one list
tops out at about 2 MB, all of it live.

### JSON, s-expressions and source (step 12a, done)

Parsed in C, as builtins for text already in hand and as ways a device
can cut its bytes into messages (D154–D156):

- **JSON** (`runtime/json.c`, D162). `(json/parse s)` gives `(:ok v)`,
  or `(:error (:bad-json at))`, `at` being the length of the text's
  longest valid prefix (D160); `(json/print v)` gives compact JSON on
  one line, so NDJSON as it is (D161), and faults (`:not-json`) on what
  JSON can't hold. In slight: `:null`, `#true`, `#false`, integers
  (floats past 63 bits), floats, strings (bytes, passed through; a lone
  surrogate's escape is U+FFFD), lists for arrays, and `(:object (key
  value) ...)` for objects, keys as strings, in order, duplicates kept.
  A device reads JSON with `(:read :json)`, `(:json f v)` for each
  top-level value, or `(:read :json/items)`, one for each element of a
  top-level array (D159). A validator takes the bytes as they come, and
  a builder makes a value once its text is whole; the next is cut only
  when the owner waits again (D158), so a big input goes through a
  bounded heap. Bad text ends the owner with `(:error (:bad-json
  where))`, and a value of 64 MB or more with `:too-big` (D160).
- **S-expressions** (`runtime/sexp.c`, D163), as JSON is: `(sexp/parse
  s)` gives `(:ok datum)` or `(:error (:bad-sexp at))`; `(sexp/print v)`
  prints what `sexp/parse` reads back, on one line, and faults
  (`:not-sexp`) on pids, closures, `nan` and `inf`; `pprint` quotes
  strings the same way. A device reads them with `(:read :sexp)`, `(:sexp f v)` for
  each top-level datum. The text is the compiler's reader's: lists,
  numbers, strings, symbols, keywords, `#true`, `#false`, `'x`,
  comments. A symbol the program mentions reads as itself, any other as
  `(:symbol "name")` (D14 holds: no symbols are made at run time); `:a`
  and `a` are the same symbol (D52).
- **Source** (D156, D164): where `:sexp` skips doc blocks, as the
  compiler and `sexp/parse` do, `:source` gives a file's forms and doc
  blocks in order, `(:form f form line)` and `(:doc f text line)`, a doc
  block as its raw text (the lines between `=doc` and `=cut`), and
  `line` the one each begins on, counted from the top of the file
  through any way of reading. A form's raw text instead, D156's option,
  is left out till something needs it (D159).

### Not in the language

`eval`, hot reload, macros, `catch`, `gensym`, local `defun`, selective
receive, mutable anything. (`if`, `when`, `case` and `match` aren't
macros, but forms the expander makes into `cond`: D142, D168.) readline isn't needed: the
REPL and line editing are slight code over key events.

## Runtime

### Values

64-bit tagged words:

| Low bits | Type | Representation |
|---|---|---|
| `…0`  | integer | 63-bit, immediate. Add is `adds` + `b.vs`. |
| `001` | list    | pointer to a 2-word cons cell, no header. `nil` is this tag with a null pointer. |
| `011` | boxed   | pointer to a header word (type, size) and payload: string, float, closure, tuple |
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
- **Tuples** (D169): header (the number of elements, and the tuple
  type), then the elements, a word each. Closures and tuples are the
  boxes that hold values, which copying and the collector go into
  (`rt_box_holds_values`, `rt_box_first_value` in `rt.h`).
- **Every heap object is a multiple of 16 bytes**, padded at the end: a
  closure with an odd number of captured values, and most strings. Copying
  and the collector copy whole 16-byte units.
- **Static data**: string literals, quoted constants, and the static
  closures for top-level functions live in the binary. Every process shares
  them. They're never collected and are sent without copying. The collector
  skips them with one address-range check.
- **The heap is acyclic**: data is immutable, lambdas capture by value and
  can't refer to themselves, and there's no local `defun`.

### Process heaps and GC

Each process has its own heap, in two generations (D200), each made of
`malloc`ed chunks (D89), with bump allocation and ordinary absolute
pointers. Everything is allocated in the young one, a **nursery**: one
chunk, 4 KB at first, which comes with the first allocation (so a
process that allocates nothing has no heap), and twice the size at each
collection that finds it overflowed, up to 1 MB. Once it's full, what's
allocated goes into overflow chunks, listed after it, till the next
function's entry, where it's collected. The **old generation** is a list
of chunks too: what collections have kept, the messages taken, a fork's
values, joined results, and big strings. All of a process's chunks
together, in both, are capped, at 1 GB unless `SLIGHT_HEAP_MAX` says
otherwise (D202), and past that the allocation faults (`:heap`). The
cap costs nothing till it's used, since chunks come as they're needed;
it keeps a runaway process from taking the program's memory, ending it
instead, where its monitor sees it. The heap pointer and limit live in the process struct
(`[x28, #RT_PROC_HEAP_PTR]`), so C builtins that allocate can use them
with nothing to sync (D57). Compiled code allocates inline (cons cells,
lists, tuples, D195, and closures), and calls the runtime only to start
a new chunk:

```
Lalloc_N:
    ldr  x2, [x28, #RT_PROC_HEAP_PTR]
    ldr  x3, [x28, #RT_PROC_HEAP_LIMIT]
    add  x4, x2, #16                        // bytes
    cmp  x4, x3
    b.hi Lalloc_N_grow                      // out of line: rt_heap_grow, then retry
    str  x4, [x28, #RT_PROC_HEAP_PTR]       // x2 = the new cell
```

**Collection** (step 9, step 12b, and D200):

- **It runs when a receive function asks for its next message** (D105),
  in `rt_recv`, before it takes one. The `recv` rule means the stack is
  empty then, so the roots are just the receive function's arguments, in
  its frame. Nothing outside the process's heap points into it.
- **And at a function's entry** (D166), in `rt_preempt`, so the root and
  a process that never waits collect too, and the cap is on what's
  live. The roots are the frames: the function's parameters and
  captured values, all it has stored yet, and every slot of each frame
  below, found by the frame pointers (the first frame's saved one is 0).
  That works because nothing lives in a register across a call, a slot
  only ever holds a value, and C never calls compiled code, so no C frame
  comes between. A function that calls another other than in tail
  position zeroes its slots at entry, so none holds a stale word; one
  that only tail-calls is never under a collection, and doesn't.
- **Nothing old points at anything young**, which is what the two kinds
  of collection rest on. Elsewhere that's the hard part of a
  generational collector, an old object changed to point at a young one,
  which a write barrier and a remembered set have to catch. In slight
  nothing changes once it's made, so an object only points at objects
  made before it: compiled code fills a cons, a list, a tuple or a
  closure before anything else runs; a C builtin fills what it allocates
  before it returns, and no collection comes between, since one only
  runs at a function's entry or a `recv`; and what comes from outside (a
  message, a fork's values, a joined result) is a chunk of its own,
  pointing only into itself and at static data. So no barrier, no
  remembered set, and nothing in compiled code or the C builtins knows
  there are generations.
- **A minor collection** runs once the nursery has overflowed. It copies
  what's young and reachable from the roots into the old generation, all
  of it (promotion en masse), following only pointers into the young
  generation, so it costs what survives, not what's live; then the
  overflow chunks are freed and the nursery is allocated in again from
  its beginning. Whether a pointer is young is a range check: the
  nursery's, then the overflow chunks' (usually none). What's promoted
  goes into the old generation's first chunk, which collections keep
  for that, and new ones as big as the old generation, up to 1 MB;
  adopted chunks go in behind it.
- **A major collection** runs once the old generation in use (its
  chunks, less what's free in the one promoted into) reaches twice what
  was live after the last major one, and never below 256 KB, so a small
  process's old generation is never collected (D106); but never within
  an eighth of the cap (128 MB at 1 GB, 8 at 64), so a process with more
  than half the cap live still collects before it reaches the cap,
  rather than failing there with a heap of garbage (D196). The eighth is
  room for the nursery and for what's allocated before the next
  function's entry. When more than that is live, the next is halfway from what's live to the cap, so it's
  always above what's live (or every slice's end would collect), and a
  heap that's all live data collects a few times more before it fails.
  A major collection copies everything live, in both generations, into
  fresh chunks, and frees the old ones; it comes right after a minor
  one when what that promoted takes the old generation to its
  threshold.
- **The cap** is checked as the heap grows, in `rt_heap_grow`, and not
  as a collection promotes; so a heap that's past it after a collection
  gets no room in its nursery, and its next allocation faults (D202).
  A major collection with a few hundred MB live takes a quarter of a
  second or so, during which nothing else runs.
- `rt_preempt` checks both at the end of each slice; and when
  `rt_heap_grow` finds the nursery full, or a chunk adopted takes the
  old generation to its threshold, it cuts the slice short, so the next
  call's entry goes to `rt_preempt`, keeping the rest of the slice for
  after: collecting never changes when a process is preempted.
- **Big strings** (64 KB or more), which point at nothing, are allocated
  straight into the old generation, each in a chunk of its own, so no
  minor collection copies them.
- Copying leaves a forwarding pointer, so sharing is kept: a DAG stays a
  DAG. Cons cells have no header, so to-space can't be scanned in order
  as in Cheney's algorithm; a stack of copied objects whose fields still
  need forwarding takes its place (D107).
- **C builtins never see an object move.** No handles, no rooting API, no
  stack maps, no write barrier.
- **A frame keeps everything in its slots** until it returns, so a `let`
  in the root's body lives till the program ends.
- **A waiting process keeps its nursery**, as it keeps its old chunks;
  one that has never allocated has neither. The process struct holds as
  few fields for the generations as will do (the overflow chunks are
  listed after the nursery, one count of bytes covers both generations,
  and only how far the chunk promoted into is filled is kept), since a
  waiting process costs its struct: 352 bytes, as before them.
- `SLIGHT_POISON=1` in the environment fills what the collector frees, a
  nursery started again included, with garbage, so a pointer it missed
  fails at once; `t/run.sh` sets it. `SLIGHT_GC_STRESS=n` makes every nth
  call's entry (n up to 1,000) and every `recv` a minor collection, and
  every eighth of those a major one, without changing when processes are
  preempted. `SLIGHT_GC_VERIFY=1` walks everything reachable before each
  minor collection, and ends the program at an old object pointing at a
  young one: the rule above, checked directly, where poison only hopes
  to trip over a break in it. It's a walk of the whole heap at each minor
  collection, so it's for running the golden tests under, not something
  a test sets.
- **No walk over a value recurses in C** (D147): the collector, printing,
  copying (a message, a fork's values, a result) and `eq?` keep a work
  stack (`rt_work_t`) instead, which grows only as deep as the value nests
  in its cars. So a value can nest as deep as the heap allows, and
  nothing about it depends on the size of a C frame on one target or
  another.

Turned down (D200): aging, keeping survivors in the nursery till they
survive a second minor collection, as the BEAM does (what en masse costs,
objects promoted just before they die, falls on major collections; aging
can follow if a benchmark shows it's wanted); a nursery of a fixed size
(every process that allocates at all would hold it, and on Linux a block
of 256 KB is a mapping of its own, so a million of them run past the
kernel's limit on mappings); growing the heap to three times what's live
(D196, for the memory); mark-sweep or mark-compact for the old
generation (copying it is the code there was); and a pool of nurseries
that waiting processes give back, as they do their stacks (later, if
processes that allocate and then wait turn out to hold too much).

### Messages

- `send` deep-copies the value into a fresh message chunk and appends it to
  the target's FIFO mailbox. Static data is not copied. Sharing within a
  message is duplicated (as in the BEAM); that's acceptable at this scale.
  The copy is one walk over the value, into a scratch buffer, which then
  moves into a chunk of just its size; the message's record (its place in
  the mailbox) is at the front of the same chunk, so a send is one malloc
  (D177).
- `recv` links the chunk into the receiver's heap. Nothing is copied twice.
  A message with nothing to copy (an integer, a keyword, static data) is
  a chunk of just its record, which `recv` frees instead (D199).
- Mailboxes are unbounded (D88). (Bounded ones would have to drop the
  message or fault the sender, since `send` can't block.)
- A message to a process that has ended disappears, as in Erlang (D92).

### Process lifecycle

- `fork`: allocate the process, deep-copy the free variables into its heap,
  queue it to run the entry function.
- Ending: the Result is copied into a small per-pid **exit record**.
  Processes blocked in `join` on it wake with a copy; later `join`s read
  the record; monitors get `(:exit pid result)`, and the monitors it set
  on others are taken off their lists (D183). A `join/race` is a watch
  on each of its pids, as a monitor is, that wakes its process rather
  than sending it a message; the first to end wakes it, and it takes the
  rest off when it runs, so a killed racer, or one whose other pids end
  first, leaves nothing behind (D220). Then the heap, the
  mailbox, the stack and the process struct are freed (D102). Nothing
  on a stack needs cleaning up, so `kill` frees a process wherever it is
  (D103).
- The record is the pid's 32-byte entry in the process table: the value
  (or the error's reason), and a chunk holding a copy of it if it needs
  memory. `(:ok v)` and `(:exit pid result)` are built when asked for.
- Exit records are kept forever (D88). That doesn't matter at hundreds or
  thousands of processes, and a million cost 32 MB. A system that churns
  through many millions will need a retention policy, such as dropping
  the record once the parent has seen it. So will one whose processes
  end with big values, since a record keeps a copy of its value: the
  airline's runs each leave theirs (D216).

### Scheduler

- Single core. A **FIFO run queue** (D86): simple, and deterministic on
  one core. A preempted process goes to the back of the queue, but only
  if another process is ready; otherwise it keeps running (D91). `yield`
  always goes to the back.
- **A seeded schedule** (D170): with `SLIGHT_SCHED=n`, a generator
  (splitmix64) seeded with `n` picks which ready process runs next,
  anywhere in the queue, and how many calls each slice has (1 to
  1,000). It's for testing: each seed is one order, the same every time,
  and every promise the language makes holds in any of them.
- **The clock** is the system's monotonic one. With `SLIGHT_CLOCK=virtual`
  in the environment, it's a **virtual clock** for tests instead: it
  starts at 0, stands still while anything can run, and jumps
  straight to the next timer when nothing can (D110). Timer tests are then
  exact and take no real time; but a process that sleeps while others stay
  busy never wakes.
- **Stack pool**: a process takes a stack when it starts running and gives
  it back when it waits in `recv` or ends.
- **Context switch**: `rt_switch`, 25 instructions on AArch64 (x19–x30,
  sp, d8–d15).
- **Idle**: when nothing is runnable, wait in `select()` (on macOS and
  Linux alike, D113) on stdin, if a process is connected to `:keypress`,
  and on the sockets (a host's lookup among them) and commands, with the
  timeout set to the next timer. Timers are a
  binary heap. While processes are busy, stdin, the sockets and the
  commands are looked at every 10 ms (D122). On the virtual clock
  they're looked at only when nothing can run: the sockets and commands
  first; then a command whose input is closed, which can only be working
  on its own, is waited for, for real (D185); then a key; then the clock
  moves to the next timer; only when nothing else can happen does it
  wait for real (D139). But before it moves to a lookup's timer, it
  waits for real, up to 200 ms, for the sockets (D165).
- **`:keypress`** (`runtime/tty.c`) sends each key as `(key mods...)`
  (D120): the
  key is a string for a printable key (one UTF-8 character) or a DOM-style
  name (`:ArrowUp`, `:Enter`, `:F1`, ..., `:Unidentified`), and the mods
  are those held, in the order `:ctrl :alt :shift`. The runtime decodes
  the terminal's escape sequences (an ESC at the end of a read is the
  Escape key). Every connected process gets every key (D119). stdin is
  read whether or not it's a terminal, so tests can pipe keys in; raw mode
  (Node's, D121) is on, when it is one, while any process is connected.
  Ctrl-C puts the terminal back and exits 130 (D117). When stdin ends, the
  keys stop (D118). On the virtual clock, a key comes each time nothing
  can run, before the clock moves (D122). Keys aren't wrapped as a file's
  messages are: the keyboard can't fail, takes nothing, and is shared
  (D131).
- **Files are devices** (D124–D130). `(connect :fs/read path expr)` forks
  `expr` as `fork` does, opens `path` on a **device** (a pid that the
  runtime serves instead of compiled code), and makes the new process its
  owner. The device's first message to its owner is `(:open f)`, `f`
  being its pid. A reader then sends `(:line f s)` for each line, split
  on `\n` and without it (a last line without one still comes), one at a
  time: the next is cut when the owner, having taken the last, next
  waits in `recv` (D158), so a file never piles up in a mailbox, and
  whatever the owner does with one message applies to the next.
  `(:read how)`, from anyone, changes how it cuts (D159): `:lines`;
  `:chunks`, `(:chunk f s)` of what one read gives, up to 64 KB; or a
  count `n`, one `(:chunk f s)` of the next `n` bytes (fewer at the end),
  and then the way before; or `:json`, `:json/items`, `:sexp` and
  `:source` (under JSON, s-expressions and source, above). Then
  `(:eof f)`, and it closes; nothing comes after it. A line, count,
  value or datum of 64 MB or more ends the owner with `(:error (:too-big
  path))` (D160), whatever the heap's cap: that limit is the reader's,
  so that an endless line can't take a server's memory (D202). `:fs/write`
  (creating the file, or emptying it) and `:fs/append` (creating it)
  take `(:write x ...)` from anyone, render the `x`s as `tty/write` does,
  and write them at once. Anything else sent to a device is a dead
  letter. `(disconnect f)` closes it, and so does its owner ending; what's
  sent after that goes nowhere. A file that can't be opened ends the owner
  before it runs, with `(:error (name path))`, `name` being errno's
  (`:enoent`, `:eacces`, `:eisdir`, ..., or `:io-error`); a read or a
  write that fails ends it the same way then. A file is read when its
  owner waits for its next message, so a reader is never waited for, and
  never counts as something that can still happen; one that can keep a
  read waiting (a FIFO, a terminal) waits with the whole runtime. To
  `join`, `monitor` and `kill`, a device is a process that ended with
  `(:ok ())`.
- **Sockets are devices too** (D133–D139). `(connect :tcp "host:port"
  expr)` connects, without stalling the runtime, after looking up the
  host, which doesn't stall it either (`runtime/dns.c`, D153, D165): the
  hosts file, then a query to the name servers in `resolv.conf`, over UDP
  or TCP, its socket waited for in `select()` and its timeout a timer of
  the device's own. The connection's first message is `(:open c)`, once
  it's connected, or the owner ends with `(:error (econnrefused
  "host:port"))` and the like. It then reads as a file does, one message
  at a time (it's read only while its owner waits for its next one, so a
  fast sender is held back by TCP itself), and `(:eof c)` when the
  other end closes; it can still be written to then. It takes `(:write x
  ...)` as a file does, and keeps what the socket can't take yet, writing
  it when it can, so `send` still never blocks; closing it waits till
  that's written, and meanwhile it keeps the program running. A socket
  counts as something that can still happen only while `select()` can
  wait on it: a listener, a connection connecting or with output
  waiting, a lookup, or a connection read because its owner waits for
  what it has. One whose owner has its last message and isn't waiting,
  or that no process has been handed, can't do anything till a process
  runs, so when none can, that's a deadlock, not a wait.
  `(connect :tcp/listen port expr)` listens on all interfaces, and
  `(connect :tcp/listen "host:port" expr)` at that address alone, the
  host an address, a name in the hosts file or `localhost`, never one a
  name server is asked about (D226); its first message is `(:open l port)`, port being the one it got (0 lets the
  system pick, which tests use, with server and client in one program),
  then `(:accept l conn)` for each connection. `conn` belongs to the
  listener's owner and reads nothing till `(connect conn expr)` hands it to
  a process of its own. Open sockets keep the program running. The error
  names are errno's (`:econnrefused`, `:econnreset`, `:epipe`,
  `:eaddrinuse`, ...), and `:enotfound` (Node's) for a host that can't be
  found, or `:etimedout` when no name server answers; SIGPIPE is ignored, so writing to a closed connection is
  `:epipe`. IPv4 only, and `select()` holds about 1,000 sockets. A Unix
  socket (`:unix`, `:unix/listen`, D231) is the same device at a path:
  a listener makes the socket there, replacing only one that refuses a
  connection, left by a program that was killed, and removes it when it
  closes or the program ends, a signal included; the umask, and so the
  path's permissions, say who may connect. HTTP is a
  slight library on top, `lib/http.slight` (D132, D140, D173, D174), whose
  client can also give a reply's body to its caller as it comes, in
  messages, as a device does (`http/stream`, D181, D182).
- **Commands are devices too** (D185). `(connect :exec (list "ls" "-l")
  expr)` runs a command, a list of strings with no shell unless one is
  asked for, found in `PATH` by the runtime itself, with `posix_spawn`,
  in a process group of its own, with SIGPIPE back to its default; its
  input is a pipe, and its stdout and stderr are another, one pipe for
  both, so its errors come in order with its output. Both are waited for
  in `select()`, on the sockets' list. Its first message is `(:open p)`;
  its output reads as a connection's, then `(:eof p)`; and once it has
  ended too, `(:exited p status)`, the exit code or `(:signal name)`, and
  the device closes (not `(:exit ...)`, which is `monitor`'s). It takes
  `(:write x ...)`, kept till the pipe takes it, and `(:eof)`, which
  closes its input once that's written; input it doesn't take goes
  nowhere. SIGCHLD's handler writes a byte to a pipe that `select()`
  waits on while any command runs, and then each is asked with
  `waitpid(WNOHANG)`; as PID 1, the pipe is waited on from the first
  command on, and any other child that has ended, one a command left,
  is waited for too, a command's own only looked at (`WNOWAIT`), as an
  init would (D229). Closing the device before the command has ended
  (`disconnect`, or the owner ending) kills its group with SIGKILL and
  waits for it; so does the program's end, however it ends (`atexit`,
  and handlers for SIGINT, SIGTERM and SIGHUP, set at the start, which
  then die of the signal, or as PID 1 exit with 128 and it, D227). One
  that can't be started ends the owner with `(:error (:enoent "ls"))`
  and the like. A command counts as something that can still happen while there's
  something to wait for: output its owner waits for, input to write, or
  its end once its output has ended.
- **`:system` is a device too** (D171). `(connect :system expr)`; its
  owner asks `(:processes)`, `(:process pid)` or `(:runtime)`, and hears
  `(:process s info)` for each live process (one at a time, the next
  when it waits again, as a reader's lines, D158) and `(:end s)`;
  `(:process s info)` or `(:ended s pid result)`; or `(:runtime s
  info)`. An `info` is a list of `(key value)`. `(:processes)` copies
  every process's at once, outside any heap; with one core nothing runs
  meanwhile, so they all describe one moment. A process's reductions
  are counted per slice (`calls`, `slice` in `rt_proc_t`), and where it
  is (`:in`) comes from a table the compiler emits of every function's
  entry and name (`slight_functions`): a process holding a stack is at
  `at`, the entry `rt_preempt` was passed or the return address of the
  `join`, `sleep`, `yield` or device `send` it stopped in; one without
  is at its `code`. The deadlock message uses the same table.
- **C libraries** (D151, D152; planned, step 12c). A pure function bounded
  by its input may be a builtin, called on the process's stack; anything
  with state or I/O is a device, its state in the runtime behind a pid,
  serviced in `select()` through a non-blocking API. Nothing blocks, and
  no slight value holds a C pointer. They're vendored as source under
  `vendor/` (as Odin's are), compiled by `slightc` once per target as the
  runtime is, and linked statically into the programs that use them. TLS
  is to be a `:tls` socket device.

### Recording and replaying a run (D170)

Everything a run takes from the world comes through one place,
`runtime/replay.c`, behind `runtime/io.h`: the clock, `select()`'s
answers, what reads and receives give, whether files and sockets open
and what they say, whether commands start and how they end, the hosts
file and `resolv.conf`, a lookup's query ids, the seed for the random
numbers when `SLIGHT_SEED` doesn't give one (D222), and the screen's size. The rest of a run is decided by the runtime
itself (calls are counted, not timed), so those answers, in order, decide
the whole run.

- `SLIGHT_RECORD=file`: each call is made, and its result, `errno`, and
  any bytes it filled in are written to `file`, flushed every time, so a
  crash leaves a record of everything up to it. The record starts with the
  settings that change a run (the clock, the seed), `@ARGV` and the
  environment, keys and all; so the file is its owner's alone (0600),
  and isn't written through a symbolic link, and a set-user-ID or
  set-group-ID program ignores this and `SLIGHT_REPLAY` (D205).
- `SLIGHT_REPLAY=file`: no call is made; each takes the record's next
  entry instead, and the record's settings, arguments and environment are
  the run's.
  Writes to files, sockets and commands aren't made either, no command
  is run or killed, and raw mode isn't set; stdout and stderr, written through stdio, are. An entry of another
  kind than the call asks for means the run has gone another way (another
  program, or something left out of the record): it stops, saying so,
  with exit status 2.
- A record is native-endian and tied to its program: for replaying where
  it was made, under a debugger, as often as it takes.
- Tested by `t/replay.sh` (`make replay`): runs with keys, files, the real
  clock, sockets, a lookup and commands are recorded, then replayed with
  none of them, and must print the same; and `SLIGHT_SCHED` gives several orders,
  each the same every time, and the same random numbers in each from one
  `SLIGHT_SEED`.

### Builtins in C

Arithmetic and comparisons on two integers are inline: one `orr` and one
`tst` check both tag bits, and anything else branches out of line. There,
`+`, `-` and `*` on two floats, or on a float and a literal integer, are
done inline too, the result's box allocated inline (D198), and so is `/`
(whose divisor is checked for zero first); anything else goes to the
runtime (`rt_add`, `rt_divide`, `rt_compare`, ...), which promotes to
float or faults. Comparisons on floats are the runtime's.

A builtin written in C is an ordinary C function (AAPCS64, or System V
on x86-64). The compiler
passes the call's site (`"str/len at t/x.slight:2:1"`) in the register
after the arguments, so faults raised inside C still say where in the
program they happened (D63). A variadic builtin (`str`, `~`, `tty/write`)
gets its arguments as one list. C code allocates with `rt_alloc`, which
bumps the heap pointer of `rt_current`, the running process.

### Register convention

AAPCS64's argument registers carry the arguments (D47), so compiled
functions and the runtime's C are called alike. x86-64's is after
AArch64's.

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
`mmap`ed stack. The quota is 1,000 reductions; when it's used up,
`rt_preempt` refills it, and sends the process to the back of the run
queue if another is ready (D91). The parameters are saved before the
reduction check because `rt_preempt` is a C call and may clobber
`x0`–`x7`.

**On x86-64** (D145; `compiler/src/x86_64.ts`, `runtime/asm_x86_64.h`),
in Intel syntax:

| Register | Role |
|---|---|
| `rax` | the accumulator and the result, and the closure at a lambda's entry (AArch64's `x9`) |
| `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`, `r10`, `r11` | a function's arguments, in order. The first six are System V's, so calls into C line up; the last two are ours (no runtime function takes more than six). |
| `rcx` | a binary operation's left operand (AArch64's `x1`) |
| `rdx` | what was just allocated (AArch64's `x2`) |
| `r15` | the current process (AArch64's `x28`). Never written by compiled code. |
| `rbp` | the frame pointer |
| `rbx`, `r12`–`r14` | never used. Nothing would notice if they were: C enters compiled code only through `rt_trampoline`, and `rt_switch` keeps them per process. |

`rdx`, `rsi` and `r11` are scratch wherever nothing is in them. A frame is
`push rbp; mov rbp, rsp; sub rsp, size` with the size a multiple of 16,
so with the return address it's 16 bytes of linkage, as on AArch64, and
`rsp` is 16-aligned at every call, as System V requires (nothing checks
it; a mistake crashes deep in libc). The entry checks are
`cmp rsp, [r15 + RT_PROC_STACK_LIMIT]` and
`sub qword ptr [r15 + RT_PROC_REDUCTIONS], 1`, each with its branch. The
runtime's out-of-line paths keep the alignment: heap growth pushes two
registers, and `rt_apply`, entered with `rsp` 8 off, realigns before
calling `rt_fault`.

## Compiler

TypeScript, run directly by Node (type stripping is on by default from
Node 22.18). `node --test` for tests, and `typescript` as the only dev
dependency, for `tsc --noEmit` type checks. No runtime npm dependencies.

It compiles the whole program at once: the prelude plus the program's
files go to one `.S` file, which clang assembles and links with the
runtime. The `recv` rule needs the whole call graph, and with one unit
that's just a walk over every function.

### Passes

1. **Read** text into s-expressions with source positions.
2. **Expand** (`expand.ts`): splice in what `@include` asks for, and make
   `if`, `when`, `case`, `and` and `or` into `cond`.
3. **Check and expand** the 10 special forms. `let` becomes nested scopes,
   `quote` becomes constants.
4. **Resolve names** as locals, globals or builtins. Check arity on calls
   to known functions.
5. **Classify** functions as receive, state or plain (fixpoint over the
   call graph), and enforce the `recv` rule.
6. **Convert closures.** Each lambda's free variables become fields of its
   closure. Each `fork`/`connect` expression becomes a hidden entry
   function that takes its free variables as arguments.
7. **Mark tail calls.**
8. **Generate code**, Ghuloum-style: every expression leaves its value
   in the accumulator, temporaries spill to the stack, no register
   allocation. `codegen.ts` decides what to emit and where values live,
   and asks a target (`target.ts`: `aarch64.ts`, `x86_64.ts`) for the
   instructions of each shape of code: the prologue, a call with its
   operands, a test that branches, filling a cons cell (D145). Tagged
   values, a reduction check at entries and tail calls. Collection needs
   nothing from the compiler: `recv` already passes its function's
   arguments to the runtime, to restart it after waiting. A test that
   can only be a boolean (`cons?`, `nil?`, `eq?` with an immediate
   literal, a comparison, and what `and` becomes) branches without making
   one, which is what keeps a pattern's tests cheap (D168, D197); and so
   does a call to the prelude's `not`, as its argument's test with the
   branches swapped, which as a value is the `cond` the prelude says it
   is (D203). An
   operand that needs no code (a local, or a literal integer, `()`,
   `#true` or `#false`) is read straight into its register when it's
   wanted, rather than waiting in a slot; and an integer literal added,
   subtracted or compared with is an immediate where the target has one
   (D197).
9. **Emit data**: string literals, quoted constants, static closures, the
   symbol name table.

Expect 1,500–2,500 lines for a first version.

### Style: "slight-shaped"

Pure functions over immutable s-expressions, recursion instead of loops
(or a loop where slight would tail-recurse: D39), association lists for
environments, no classes. Then porting the compiler to slight is a near
line-by-line translation.

### Bootstrap (plan step 17)

1. The TypeScript compiler (stage 0) compiles the slight port of the
   compiler into a native binary (stage 1).
2. Stage 1 compiles its own source, producing stage 2.
3. Stage 2 compiles the source again. When the output matches stage 2's
   byte for byte, the bootstrap is proven and the TypeScript can retire.

## Testing

- **Golden tests**: compile a `.slight` file, run it (natively, or under
  qemu when it's for another architecture), diff its stdout and stderr
  against a `.expected` file, with a last line `exit: N` when the exit
  status isn't 0. Every plan step adds
  some. The same tests and expected output serve both targets (D146).
- **Compiler unit tests** with `node:test`, per pass.
- **Timing uses the virtual clock** (`t/run.sh` sets
  `SLIGHT_CLOCK=virtual`), never real time, and random numbers a fixed
  seed (`SLIGHT_SEED=1`).
- **Record and replay** (`t/replay.sh`, D170): a few programs recorded on
  the real clock and replayed; each replay is compared with its own
  recording, never with a fixed output, so nothing depends on timing.
- **Keys** come from a `; stdin:` line in the test (D116), in printf `%b`'s
  escapes; without one, stdin is empty.
- `lib/test.slight`, a slight-level test library (TAP: `ok`, `is`,
  `diag`), for programs that want one: `(@include :test)`.

## Open questions

Collected from above:

1. A worker thread for a C library that can only block, such as SQLite
   (D151): the runtime's first thread, if it comes.

Settled in step 2: program structure, the top-level forms as the root
process (with its arguments as `@ARGV` since D144). In step 7: `recv`
syntax (D85), the run queue (D86), the fault, `raise` and `kill`
reasons (D87), exit records and mailboxes (D88). In
step 8: the fault kinds (D98), and what gets logged (D99, D100). In
step 9: collecting only at `recv` (D105). In step 10a: the virtual clock
(D110), what `after` and `sleep` return (D111), timers whose process has
ended (D112), and waiting in `select()` (D113). In step 10b: feeding tests
keys (D116), Ctrl-C (D117), the end of stdin (D118), and several
connected processes (D119). In step 12b: collecting at a function's
entry, with the frames as roots (D166).
