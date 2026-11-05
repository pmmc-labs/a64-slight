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

## What slight is for, and isn't (D250)

What it's meant to be when PLAN.md's "Toward general purpose" is done,
for checking later decisions against: **a small language for networked
services, tools and simulations on one machine, which can be tested
deterministically.** A service, a command-line tool, a daemon, an agent
or a simulation, written end to end in it, one static binary, without
shelling out for the basics. Simulations were added by D253: actors as
agents, the virtual clock as simulated time, seeded orders as Monte
Carlo over schedules, and replay as reproducibility, with arrays of
numbers and floats that come out the same on every machine (PLAN.md,
"Numbers in bulk").

What sets it apart is that last part. Everything a program takes from
the world goes through `replay.c` (D170), so any run, a production one
included, can be recorded and replayed exactly, run on the virtual clock,
and tried under seeded orders to find races. Other languages bolt that
on (FoundationDB's simulation, Rust's madsim and turmoil, Antithesis
from outside the program); in slight it's how the language works. With
actors whose isolation comes from immutability rather than a type
system (Pony), Erlang's model in Go's deployment shape (no VM, a static
binary, `FROM scratch`), and a checker that warns only of what's sure to
go wrong (D247), that's the reason to choose it.

So: **nothing goes in that a run can't be replayed through.** A device,
a builtin or a vendored library takes from the world only through
`replay.c`; that's why C libraries are vendored, and there's no FFI,
which would let a call read the world unrecorded (D252).

And: **vendor only what never needs a security fix** (D251): math, a
hash, Unicode's tables, pinned once and left. What does need them, TLS,
encryption between machines, a model's runtime, is left to programs the
system keeps up to date, run as processes at the program's edges, as
actors are inside it: a proxy (nginx, Caddy) in front of a server, curl
for a client's `https://`, ssh between machines (`rpc/exec`),
`llama-server` for a model. Each is updated by the system's package
manager, a crash in one ends only it, and what crosses comes through
`:exec`, so it's recorded.

Not what it's for: numerical computing at large (no mutable arrays,
tensors, autodiff or GPU: D253's arrays are for simulations), systems
programming (no control of memory, no FFI), a GUI, the browser or a
phone (WebAssembly is parked). It uses more than one core only as a
program places its actors on islands (D256), so a server bound by
throughput spreads its connections' actors itself, or runs a copy per
core behind one port (`airline`'s `--shard` is the same idea). Nor a large ecosystem: that comes from users, not a plan.

## Language

### Syntax

The reader (`compiler/src/reader.ts`, D37) reads:

- `( ... )` lists; `()` is nil
- integers (`42`, `-7`) and floats (`3.14`)
- strings in double quotes, with escapes: `\"` `\\` `\n` `\t` `\r` `\e`
  `\u{hex}` (a code point up to U+10FFFF that isn't a surrogate)
- keywords, `:name`, which evaluate to themselves, and symbols, `'name`,
  names as data, apart (D287). `#true`/`#false` are the booleans.
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

A second syntax, C-style, is read into the same s-expressions: a file
ending in `.sly` goes to `compiler/src/sly.ts` instead (D246,
`docs/SYNTAX.md`), and nothing after the reader knows which syntax a
program was in.

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

Those are what values are, when the program runs. Before it does, the
compiler checks what it can of them (D247, `docs/TYPES.md`): a type is
one of those, or a keyword alone, a list's shape, a union, `any`; it's
inferred from literals, the builtins, the prelude's sigs, patterns and
tests, and said, if a program wants, with `deftype`, `sig` and `the`.
It warns only about what's certain to go wrong, unless `--strict`, and
never changes the code.

### Special forms (10)

| Form | Meaning |
|------|---------|
| `(defun name (params...) body...)` | Top level only. No local `defun`. |
| `(lambda (params...) body...)` | A closure. Captures free variables **by value**. Always a *plain* function (see the `recv` rule). Can't refer to itself. |
| `(let name expr)` | Binds `name` for the rest of the enclosing body. Only allowed directly in a body. As the last form of a body, its value is `expr`'s (D42). |
| `(cond (test body...) ...)` | The conditional the others become (below). Each test must be `#true` or `#false`, or the process faults; so does running out of clauses. |
| `(do form...)` | Evaluates in order. The last form is in tail position. |
| `(quote x)`, `'x` | A constant: a symbol, a number, a string, or a list of them. A keyword, `:name`, is itself (D287). |
| `(fork f arg ...)` | Starts a process that calls `f` with the arguments, worked out by the caller and copied (D286), and returns its pid; `(fork/on k f arg ...)` on island `k` (D256). One kind of process (D277). |
| `(connect :source [x] f arg ...)` | Same as `fork`, and connects the new process to a source: `:keypress`; a file, `(connect :fs/read path f arg ...)` (and `:fs/write`, `:fs/append`); a socket, `(connect :tcp "host:port" f arg ...)` or `(connect :tcp/listen port f arg ...)`, or a Unix socket, `(connect :unix path f arg ...)` or `(connect :unix/listen path f arg ...)`; a command, `(connect :exec (list "ls" "-l") f arg ...)`; or `:system`. `(connect dev expr)` hands the device `dev` to the new process. |
| `(recv clause...)` | Takes the next message, waiting for one with the stack kept (D276), anywhere (D277). |
| `(deftype ...)`, `(sig ...)`, `(the type expr)` | Types (D247): a type's name and a function's types, at the top level, and an expression's. The checker reads them; code generation leaves them out (`(the T e)` is `e`). Not counted among the 10, since none does anything when the program runs. |
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
the compiler keeps apart (D80). A file with a header beside it, `X.sli`
or `X.slyi`, is a module (D248): its includers see what the header
declares (`sig`s and `deftype`s), the rest is its own, and what a header
includes its includers see too. `compiler/src/modules.ts` makes the
units one program again by renaming: a module's names get its file in
them, which no program can write, and each name a unit's code mentions
becomes the one it means there. Everything is still one program,
compiled whole; a header could let a module be compiled apart one day,
but nothing needs that yet.

A function takes at most 8 parameters, one register each (see the
register convention below). More could go on the stack later.

### Waiting, and the stack

A process keeps its stack across every wait: `recv`, `join`,
`join/race`, `read` and `sleep` alike (D276, which retired D8 and D167:
till then the stack was empty at every `recv`, which could go only
where a tail call could, and a waiting process held none). So a `recv`
goes anywhere, a helper or a lambda included, the top level too (D277),
and a function that waits gives its value back to its caller
as any function does. The runtime's `rt_recv` takes the next message,
or parks the process in state `RT_WAITING`, keeping its stack, and
returns the message when one comes; the process gets a new slice then,
and a call is counted, as a receive function started again had, so a
program is preempted where it was before D276.

There's still no selective receive: whatever `recv` a process is in
takes the next message, but what's sent to an address of its own, or
comes from a device it opened, which `read` takes (D277).

What it costs: a waiting process holds a stack, 8 MB of address space
and the pages it touched, and the system maps about 32,000 of them at
once on default Linux (`vm.max_map_count`), so a million processes
can't all wait, where they could before (`million-forks` is
`ten-thousand-forks`). What it buys is the process model's simplicity:
one kind of wait, no split pass, no state functions, and a call to a
function that waits is a call.

### Processes

| | |
|---|---|
| `(fork f arg ...)` | Starts a process (D277: one kind, where D243 had two). `f` and the arguments are evaluated by the caller and **deep-copied** into the child, which calls `f` with them (D286); at most 8 arguments. A literal in `f`'s place is a compile error; what isn't a function of as many arguments faults the caller, as `apply` does, and starts nothing. `f` runs at the base of the child's stack, and may wait in `recv` anywhere (D276). Inside `f`, `$$` is the child; in the fork form, the caller. |
| `(fork/on k f arg ...)` | As `fork`, on island `k` (D256); `fork` stays on its parent's. |
| `(connect :keypress f arg ...)` | `fork`, plus the new process gets every key typed, as a message (see `:keypress`, under Scheduler). |
| `(connect :fs/read path f arg ...)`, `:fs/write`, `:fs/append` | `fork`, plus the new process owns the file at `path`, opened on a device whose first message to it is `(:open f)` (see Files, under Scheduler). `path` is evaluated in the caller, and must be a string. |
| `(connect :tcp "host:port" f arg ...)`, `(connect :tcp/listen port f arg ...)` | `fork`, plus the new process owns a connection, or a listener on `port` (0 lets the system pick), on a device (see Sockets, under Scheduler). |
| `(connect :unix path f arg ...)`, `(connect :unix/listen path f arg ...)` | The same, at a Unix socket's path (D231). |
| `(socket/peer c)` | The uid, gid and pid at the other end of the Unix connection `c`, or `()` (D235). |
| `(connect :exec cmd expr)` | `fork`, plus the new process owns a command, `cmd` being a list of strings (its name and arguments), run on a device (see Commands, under Scheduler). `cmd` is evaluated in the caller. |
| `(connect :system expr)` | `fork`, plus the new process owns a device that tells about the processes and the runtime (see `:system`, under Scheduler). |
| `(connect dev expr)` | `fork`, plus the new process is handed the open device `dev` (anything that isn't a `:keyword` is taken to be one), which then tells it `(:open f)`. Anything that isn't an open device faults (`:not-a-device`). |
| `(disconnect f)` | Closes the device `f`. Anything that isn't an open device it ignores. Returns `()`. |
| `(send pid msg)` | `msg` is any value, conventionally a list headed by a keyword. Deep-copied. Never blocks. Returns `()`. |
| `$$`, `^$$` | self, parent; the root's parent is `()` |
| `(join pid)` | **Blocking** wait for `pid` to end, keeping the stack. Returns `(:ok value)` or `(:error reason)`. Works on any process (not just children), on any island (D277), anywhere (including inside lambdas), and after the process has already ended, till its parent has had the result: joined it, been told by a monitor, or ended; then `:ended` (D244). Joiners wake in the order they joined, each with a copy. `(join $$)` faults (`:join-self`), and so does an address (`:not-a-pid`). |
| `(join/race pids)` | **Blocking** wait for the first of `pids` to end, keeping the stack, as `join` does (D220). Returns `(pid result)`, `result` as `join` gives it. If some have ended already, it's the first of those in the list, as JavaScript's `Promise.race` settles. The others are left running. An empty list faults (`:not-a-pid`), and so does the caller's own pid (`:join-self`); every pid is checked before it waits. `join/any` and `join/all`, JavaScript's `Promise.any` and `Promise.all`, are in the prelude, on top of it. |
| `(address)`, `(read src)`, `(read/any srcs)` | `address` makes an inbox of the caller's own, which only it reads: an immediate under the pid tag (its island's id for it, never given twice, D273), which `send`, `after`, `monitor` and `disconnect` take; `read` waits, keeping the stack, for the next message to one of its open addresses, or from a device it opened; `read/any` for the first of several, in their order, giving `(src what)`. A process is no source: `read` of a pid faults (`:not-a-pid`), its end being a message, from `monitor`, or a join's (D277). The messages wait in the process's mailbox, marked with their address, and `recv` leaves them. `ask`, `ask-within` and `reply` are prelude functions on them. |
| `(open source arg)` | Opens a device the caller owns, with no process started (D243, D277), from `connect`'s sources but the listeners and `:keypress`, and waits till it's open: `(:ok d)`, or `(:error reason)`. `read` gives its messages, which `recv` leaves, its next cut at the `read` after (D158); a failure is `(:error d reason)` there, and the device closes, rather than ending the process (D128's rule is `connect`'s). |
| `(monitor pid)`, `(monitor pid to)` | Opt-in. When `pid` ends, the runtime sends `(:exit pid result)` to the caller, or to its address `to` (D277), so a reply and the end of the process asked come to one place; at once, if it already has, `result` being `:ended` once the value is gone (D244). Monitoring twice means two messages; `disconnect` of `to` drops the monitors told at it. There are no automatic messages to the parent. Returns `()`. |
| `(kill pid)` | Ends `pid` with `(:error :killed)` (D87), whatever it's doing: waiting in `recv`, in the run queue, blocked in `join`, or asleep. `(kill $$)` ends the caller. Killing a process that has ended does nothing. Returns `()`. |
| `(after ms pid msg)` | A timer: sends `msg` to `pid` once `ms` milliseconds have passed. `msg` is copied now, as `send` copies it. Returns the timer's number (D232). |
| `(timer/cancel t)` | Takes back `after`'s timer `t` if it hasn't fired: `#true`, or `#false` (D232). |

**One kind of process (D277; D243's two, tasks and actors, with D245,
are retired).** Every process, started with `(fork f arg ...)`, has a
mailbox and a value: it can be sent to, and takes its messages with
`recv`, wherever it is in its code (D276); it can be joined for its
value, on any island; monitored, timed and killed; and it can make
addresses, `(address)`, which `send`, `after` and `monitor` take where
they take a pid, and open devices, `(open how ...)`, with `(read src)`
for the next message from one, or `(read/any srcs)` for the first of
several; each wait keeps the stack. The root is one too. A pid is its
process's inbox: what's sent to it is what `recv` takes. An address is
an inbox of its own, read only by its maker, an immediate under the
pid tag: its island and an id the island gives it, once, which a table
of the island's open addresses maps to its process (D273); `recv`
leaves what's sent to one, and what comes from a device the process
opened, for `read`. A process's end is a message: `(monitor pid to)`
tells the address `to`, so a reply and the end of the process asked
come to one place, which is how `ask`, a prelude function, waits. A
process's value is kept for its parent till the parent has it (D244,
for every process). `connect` starts a process that owns a device, as
before. Tasks and actors stay as the two shapes a process is written
in: one that computes a value, and one that loops through `recv`.
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
- **A process holds a stack from its first run to its end** (D276),
  whatever it waits in. One that can't have one ends `:stack` (D267);
  the pool keeps a few hundred of those that have ended, for the next.
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
  `after` gives its timer's number, 1 up, with its island above bit 52
  (D274), and `timer/cancel` takes one back by it, found in an index of
  the pending ones, kept as the heap moves them; another island's is
  taken back by that island, the caller waiting for its answer, as
  `fork/on` waits. A timer whose process has ended is dropped when it's due,
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
entry is cut at its first `=`, and one without an `=` is left out.

### Builtins and the prelude

[`LANGUAGE.md`](LANGUAGE.md) describes each one as a program sees it.
Agreed (D254, PLAN.md's "Step 1, proposed"): `clock/now`, the time of
day, with `lib/time.slight`; `fs/list`, `fs/stat`, `fs/delete`,
`fs/rename` and `fs/mkdir` (`runtime/files.c`); `bytes/pack` and
`bytes/unpack`; and `log`, JSON lines on stderr; all built. And
(D255): `crypto/sha256`, `crypto/sha512`, their HMACs, `crypto/equal?`
and `crypto/random-bytes` (`runtime/crypto.c`); base64, base64url and
hex, `bytes->` and `->bytes`; built.

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
socket's, whose permissions say who may call (D231), and a handler can
ask who called, `(rpc/peer)`, the kernel's word for it (D235). Opt-in:
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
  comments. A name the program mentions reads as the symbol or the
  keyword it was written as, any other as `(:symbol "name")` or
  `(:keyword "name")` (D14 holds: no names are made at run time); `:a`
  is the keyword and `a` the symbol, apart (D287).
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
- **Arrays of numbers** (D253): header (how many elements, and
  `RT_BOX_FLOATS` or `RT_BOX_INTS`), then the elements, a double or an
  int64 each, unboxed; or, at 4 KB or more, a shared buffer's handle
  (below). They hold no values, so nothing walks them.
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
otherwise (D202), or at a quarter of a smaller limit on the program's
memory, its cgroup's, as in a container (D233), and past that the
allocation faults (`:heap`). The
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

- **It runs when a process waits in `recv`** (D105), in `rt_recv`,
  before it takes a message, and again each time it wakes to one. The
  roots are as at a function's entry (below), the frame's slots in use
  so far and the frames below, since the stack is kept (D276). Nothing
  outside the process's heap points into it.
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
  need forwarding takes its place (D107). A list's spine never goes on
  it: along a list the walk copies each cdr as it comes to it and goes on
  from the copy, so only cars that are lists or boxes of values do. The
  walk is compiled twice, for a minor collection and a major one; what
  moves is worked out once for each (the nursery and the chunk it
  overflowed into as two ranges, or all but static data); and what
  changes at every object (where the next copy goes, the stack) is kept
  where the compiler can hold it in registers, out of the struct the
  walk passes to the function that gets it a new chunk (D239).
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
  stack instead: printing and `eq?` an `rt_work_t`, which grows only as
  deep as the value nests in its cars, and the collector and the copy
  one of their own, which holds the cars still to do that are lists or
  boxes of values (D239, D240). So a value can nest as deep as the heap
  allows, and nothing about it depends on the size of a C frame on one
  target or another.

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

### The libm (D253)

`math/`'s functions are CORE-MATH's, vendored (`vendor/core-math/`, the
first vendored code, D152) and compiled with the runtime: each result is
the double nearest the exact one, so the same on every machine, and the
runtime is compiled with `-ffp-contract=off`, so clang fuses no `a * b +
c` into one rounding on AArch64 that x86-64 would round twice. A run that
computes with floats replays on any machine.

### Shared buffers (D253)

A string or an array of numbers of 4 KB or more (`RT_SHARED_MIN`) is an
`rt_buffer_t` off the heap, its references counted, and a handle in the
heap: a box of its header with `RT_BOX_SHARED`, the data's address and
the buffer's. `rt_string_bytes` and `rt_array_data` read through it, so
nothing else knows. A
copy (a message, a fork's values, a kept result) copies the handle,
counts a reference, and lists the handle in its chunk, for whoever adopts
the chunk; a chunk freed unadopted lets its handles go. Each process
keeps its handles in two lists, young and old, on its side; after a
collection a handle that wasn't copied is garbage, and its reference
goes; at a process's end, all of them. It's safe because what's shared
never changes and holds no pointers, so no buffer points into a heap, as
nothing old points at anything young (D200). A process's young handles'
bytes past 1 MB make its next call collect, and all its handles' bytes
count toward its cap. Not yet: a slice that shares its string's or
array's buffer (one is copied, as any slice is), and a device's read made
a buffer as it comes; and an array `sexp/parse` reads is in the heap,
whatever its size, as the reader builds into whatever it's given.

### Arrays of numbers (D253)

`runtime/arrays.c`: `rt_new_array` makes one, in the heap or, at 4 KB or
more, shared, for the builtin to fill before anything else runs. Element
by element, an operation's operands are all its arrays' kind, so its
result's kind is the arrays' (`array/div`'s always `:float`); an `:int`
result is checked against 63 bits. Reductions and `array/matmul` sum in
order, left to right, each product and sum rounded apart
(`-ffp-contract=off`), so a Python loop is their model
(`t/models/arrays.py`); `array/matmul` goes a row of the result at a
time, adding a row of `b` times an element of `a`, which reads `b` along
its rows and keeps each element's order. `array/generate`, `array/map`
and `array/fold` are the prelude's, `array/generate` through lists of up
to 512 at a time, joined in a tree, since C can't call back into slight
while the collector walks its frames (D166).
`lib/matrix.slight` is matrices on arrays, a module. The type is
`(array float)`, `(array int)`, or `array` for either (`types.ts`).
`:int` is the same symbol as `:exec`'s SIGINT, `RT_SYM_EXEC +
RT_EXEC_INT`, since a symbol is its name.

### Messages

- `send` deep-copies the value into a fresh message chunk and appends it to
  the target's FIFO mailbox. Static data is not copied, and nor is a
  string of `RT_SHARED_MIN` (4 KB) or more, a buffer off the heap whose
  handle is copied, its references counted (D253, below). Sharing within a
  message is duplicated (as in the BEAM); that's acceptable at this scale,
  but a copy a process asks for (a send, a fork, its end) is held to the
  most a heap can hold, past which that process faults `:heap` (D265).
  What the runtime says of a value, a fault's or a dead letter's, is cut
  at 64 KB.
  The copy is one walk over the value, into a scratch buffer, which then
  moves into a chunk of just its size; the message's record (its place in
  the mailbox) is at the front of the same chunk, so a send is one malloc
  (D177). The walk is the collector's, with nothing to forward: along a
  list's spine, only cars that are lists and boxes of values going on its
  stack, and what changes at every word kept where the compiler can hold
  it in registers; values with no pointer among them aren't walked
  (D240).
- `recv` links the chunk into the receiver's heap. Nothing is copied twice.
  A message with nothing to copy (an integer, a keyword, static data) is
  a chunk of just its record, which `recv` frees instead (D199).
- Mailboxes are unbounded (D88). (Bounded ones would have to drop the
  message or fault the sender, since `send` can't block.)
- A message to a process that has ended disappears, as in Erlang (D92).

### Process lifecycle

- `fork`: check that the function takes as many arguments as it's given,
  allocate the process, deep-copy the function and its arguments into
  its heap, queue it to start in `rt_apply_start`, which calls the
  function with them (D286).
- Ending: the Result goes, from the process's own heap, to those
  waiting then: each joiner, and a race it wins, is given a copy to
  take when it runs, a joiner on another island as an item to its
  island (D277); and monitors get `(:exit pid result)`, in their inbox
  or at the address they named (D243, D244, D277). Then a Result isn't
  kept if the parent was among them, or has ended: the record says only
  that it ended, and later asks get `:ended`. Else the Result is copied
  into its small per-pid **exit record**, kept for the parent, which
  lists it (D244); the parent's `join` or `monitor` of it, or its end,
  lets it go. The monitors a process set
  on others are taken off their lists (D183). A `join/race` is a watch
  on each of its pids, as a monitor is, that wakes its process rather
  than sending it a message; the first to end wakes it, and it takes the
  rest off when it runs, so a killed racer, or one whose other pids end
  first, leaves nothing behind (D220). Then the heap, the
  mailbox, the stack and the process struct are freed (D102). Nothing
  on a stack needs cleaning up, so `kill` frees a process wherever it is
  (D103).
- The record is the pid's 40-byte entry in the process table (32 till
  devices added a pointer to it): the value (or the error's reason), and
  a chunk holding a copy of it if it needs memory. `(:ok v)` and `(:exit pid result)` are built when asked for.
- An entry is **dead** once nothing more can come of it: no process, no
  open device, and no value kept. The table is in pages of 1,024
  entries, and a page is freed once all its entries are dead and its
  last pid has been made; a pid whose page has gone reads as ended,
  keeping nothing, since pids are never reused (D244). So a program
  that runs for good holds the entries of what's running, open or
  keeping a value, not of every process it started: `bank`'s million
  customers peak at 2 MB where they took 42, an HTTP server's 4,000
  requests at 2.6 where they took 95. A long-lived process holds its
  page's 40 KB; there's no `detach`, since the children nobody takes
  are few and small, and go when their parent ends.

### Islands: more than one core (D256; built)

Each island is today's runtime, single-threaded, on a thread of its
own, with its own process table, run queue, timers, devices, stack pool
and record; nothing is shared between islands but an inbox each and
the shared buffers (D253), which never change. A fixed set, `(islands)`
of them, one per core unless `SLIGHT_ISLANDS` says; island 0 is the
main one, and owns the signals, the terminal and `:system`. A program
places a process with `(fork/on k expr)`; nothing moves on its own, and
`fork` stays on its parent's island. A pid carries its island in 8 bits
below the address bit (`#<pid 5@2>`, island 0's printed as now), each
island numbering its own; a send to another island's pid copies the
message into a chunk and pushes it onto that island's inbox, a queue
many push to and one pops, and the island delivers it into the local
mailbox, so compiled code, the collector and every mailbox stay
single-threaded. Big strings and arrays cross as handles, their counts
atomic. Each island records its own run, its inbox's deliveries among
its inputs, and the islands replay together. `docs/PLAN.md`, "More than
one core: islands", has the steps.

As built (`process.c`, "islands"): an inbox is a list under a mutex,
taken whole at each of the island's scheduler's decisions (`events`),
and a pipe that wakes the island only when it's blocked in `select()`;
an island with nothing to do watches its inbox for 50 µs first, so a
reply that comes back quickly needs no wake (two islands' round trip,
3.4 µs on the cloud machine, where a wake through the pipe took 50).
What crosses is an item: a message, already copied into its chunk by
the sender, the item's head in the same chunk, so that crossing is one
allocation; a start, for `fork/on`, which the island makes and answers
with its pid, the forker waiting, so each island numbers only its own;
and kills, watches and ends. A pid not on this island reads as ended
(`slot`), except where send, kill, monitor and join see to it. A
process's end goes to its parent on another island, and to a joiner
there, and is kept apart from the mail, as a record keeps one here
(D244, D277). The
program ends when `busy`, the islands that may still do something and
the items not yet handled, comes to 0.

`:system` (step 5) asks other islands through their inboxes: an `ASK`
item, answered by a `TOLD` with that island's snapshots (taken in one
moment there), a process's, or its counts, which the device adds to
what it sends. `(:processes)` and `(:runtime)` go to each started island
in turn, in order, the device's own island answered in place; a question
that waits holds up those asked after it, so answers come in order. A
process counts its messages to and from other islands, and their bytes,
in its side record, made the first time one crosses, so a process that
never sends across pays nothing; each island counts its own too. PID 1's
orphans are island 0's to reap, and every island's commands are in one
list under a lock that they're started under, so island 0 can tell them
from orphans.

Replay (step 6) keeps a record per island, `f` and `f.k`, on the
island's thread. An item is named by its island and that island's count
of items sent, which a replay makes again, so a record names what its
inbox took and when, by the count of the scheduler's looks at it
(`events`), and keeps nothing of it: the senders, replaying too, send
the items again, and an island waits for the ones its record says to
take, holding back any that come early. What an island sees of the
others outside a take (whether its inbox has something, as it goes idle
or wakes from `select()`; whether an island has started, for `:system`)
is recorded as a seen value, and the number of the pipe that wakes it,
which `select()`'s recorded sets name, as an entry of its own. So the
islands replay together, each exactly, but not one alone, which would
need every message's contents kept, closures' code and static data
placed again on macOS, whose programs load at a different address each
run (option (b), D256's step 6, put off).
### Scheduler

- Single core (on each island, once there are islands, D256). A **FIFO run queue** (D86): simple, and deterministic on
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
  it back when it ends (D276; till then at `recv` too). An island keeps
  256 given back, and unmaps the rest. With none to be had (about 32,700 held at
  once reach Linux's default `vm.max_map_count`, two mappings a stack),
  the process that wanted one faults `:stack`, and the program goes on
  (D267).
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
  waits for real, up to 200 ms, for the sockets (D165); and the same
  before a program's timer while a connection the program made (`:tcp`,
  `:unix`) has been written to, or is still connecting, with nothing
  come back since, and is waited on: a reply may be crossing macOS's
  loopback, which delivers a moment after a send, and a budget pending
  during a request mustn't beat it (D236, D288). A reply on its way
  ends the wait as it comes; only a server that stays silent costs the
  whole of it, once per move of the clock, and a reply that comes in
  pieces, a byte to a tick, costs nothing after its first. An accepted
  connection isn't waited for, so a server's idle connections never
  hold the clock. With nothing to look at (no key wanted, no socket or command
  open) it doesn't call `select()` to look, so a simulation's clock
  moves at each event without a system call (D242).
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
  write that fails ends it the same way then. A regular file is read
  when its owner waits for its next message, and written where it's
  sent to, so a reader is never waited for, and never counts as
  something that can still happen; the only wait is the disk's. Anything
  else (a FIFO, a pipe, a terminal, a device such as `/dev/zero`) is
  opened without waiting, `O_NONBLOCK`, and then read and written as a
  socket is (D272): read when `select()` says it can be, what it hasn't
  taken kept and written when it can, up to 64 MB waiting (D270),
  closing once that's written, and counted as something that can still
  happen while `select()` waits on it. So no FIFO or terminal holds up
  its island; and a FIFO opened to write that nothing has open to read
  fails at once, `(:error (:enxio path))`, where it would wait. To
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
  `:epipe`. IPv4 only, and `select()` holds about 1,000 sockets (D266).
  Output a socket, a command or a polled file hasn't taken waits in
  memory off every heap: a write while 64 MB waits fails the device
  `:too-big` (D270, D272). A Unix
  socket (`:unix`, `:unix/listen`, D231) is the same device at a path:
  a listener makes the socket there, replacing only one that refuses a
  connection, left by a program that was killed, and removes it when it
  closes or the program ends, a signal included; the umask, and so the
  path's permissions, say who may connect. HTTP is a
  slight library on top, `lib/http.slight` (D132, D140, D173, D174), whose
  client can also give a reply's body to its caller as it comes, in
  messages, as a device does (`http/stream`, D181, D182).
- **Commands are devices too** (D185). `(connect :exec (list "ls" "-l")
  f arg ...)` runs a command, a list of strings with no shell unless one is
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
  every process's on an island at once, outside any heap; nothing runs
  there meanwhile, so each island's describe one moment (D256). A process's reductions
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
  runtime is, and linked statically into the programs that use them.
  Only code that never needs a security fix is vendored (D251); TLS
  isn't: a proxy in front ends it for a server, and curl, through
  `:exec`, for a client.

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
float or faults. Comparisons of two floats, or of a float and a literal
integer, are inline too, out of line from the integers' compare, with
no box to make (D259): a NaN makes every one false but `!=`, as
`rt_compare` has it, which on x86-64 means looking at the parity flag.
A tree of two or more float operations (`+`, `-`, `*`, `/`,
`math/sqrt`, `math/abs`) on locals and literals is worked as doubles in
float registers and boxed once (D261), its locals each checked to be a
float first; if one isn't, or a divisor is ±0, it's worked out as
before, out of line, so faults and results are what they were. One
with an integer literal and nothing only a float can be (a float
literal, `/`, `math/sqrt`) is taken to be integers and left alone.

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
   `if`, `when`, `case`, `and` and `or` into `cond`, keeping `match`.
   **Check the types** (`check.ts`, D247): warnings, on the source's
   forms and places. Then make `match` into `cond` too, and erase the
   types (`deftype`, `sig`, `the`).
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
entry, with the frames as roots (D166). In Oct 2026: tasks and actors (D243), and in Nov 2026 one kind of
process again (D277, below Processes).
