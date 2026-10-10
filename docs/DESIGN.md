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
- planned (D156): a line starting with `=` and a letter, between
  top-level forms, begins a doc block, which runs to a line `=cut`. The
  compiler skips it. `=doc` holds Markdown (a subset: headings,
  paragraphs, emphasis, code, fenced blocks, lists, links); other
  `=word`s are kept for later.
- `$$` (self) and `^$$` (parent)
- `@ARGV`, the program's arguments, at the top level only (Program
  structure, below)
- the names `\n`, `\r`, `\t` and `\e`, constants for those one-character
  strings, and `PI`

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
| `(let name expr)` | Binds `name` for the rest of the enclosing body. Only allowed directly in a body. As the last form of a body, its value is `expr`'s (D42). |
| `(cond (test body...) ...)` | The conditional the others become (below). Each test must be `#true` or `#false`, or the process faults; so does running out of clauses. |
| `(do form...)` | Evaluates in order. The last form is in tail position. |
| `(quote x)`, `'x` | A constant. `:sym` is self-quoting. |
| `(fork expr)` | Runs `expr` in a new process and returns its pid. |
| `(connect :source [arg] expr)` | Same as `fork`, and connects the new process to a source: `:keypress`; a file, `(connect :fs/read path expr)` (and `:fs/write`, `:fs/append`); or a socket, `(connect :tcp "host:port" expr)` or `(connect :tcp/listen port expr)`. `(connect dev expr)` hands the device `dev` to the new process. |
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
| `(connect :keypress expr)` | `fork`, plus the new process gets every key typed, as a message (see `:keypress`, under Scheduler). |
| `(connect :fs/read path expr)`, `:fs/write`, `:fs/append` | `fork`, plus the new process owns the file at `path`, opened on a device whose first message to it is `(:open f)` (see Files, under Scheduler). `path` is evaluated in the caller, and must be a string. |
| `(connect :tcp "host:port" expr)`, `(connect :tcp/listen port expr)` | `fork`, plus the new process owns a connection, or a listener on `port` (0 lets the system pick), on a device (see Sockets, under Scheduler). |
| `(connect dev expr)` | `fork`, plus the new process is handed the open device `dev` (anything that isn't a `:keyword` is taken to be one), which then tells it `(:open f)`. Anything that isn't an open device faults (`:not-a-device`). |
| `(disconnect f)` | Closes the device `f`. Anything that isn't an open device it ignores. Returns `()`. |
| `(send pid msg)` | `msg` is any value, conventionally a list headed by a keyword. Deep-copied. Never blocks. Returns `()`. |
| `$$`, `^$$` | self, parent; the root's parent is `()` |
| `(join pid)` | **Blocking** wait for `pid` to end. Returns `(:ok value)` or `(:error reason)`. Works on any pid (not just children), anywhere (including inside lambdas), any number of times, and after the process has already ended. Joiners wake in the order they joined. `(join $$)` faults (`:join-self`). |
| `(monitor pid)` | Opt-in. When `pid` ends, the runtime sends `(:exit pid result)` to the caller; at once, if it already has. Monitoring twice means two messages. There are no automatic messages to the parent. Returns `()`. |
| `(kill pid)` | Ends `pid` with `(:error :killed)` (D87), whatever it's doing: waiting in `recv`, in the run queue, blocked in `join`, or asleep. `(kill $$)` ends the caller. Killing a process that has ended does nothing. Returns `()`. |
| `(after ms pid msg)` | A timer: sends `msg` to `pid` once `ms` milliseconds have passed. `msg` is copied now, as `send` copies it. Returns `()`. |
| `(sleep ms)` | **Blocking** wait for `ms` milliseconds, keeping the stack. Works anywhere. Returns `()`. |
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
  sources, no blocked I/O) is detected and reported. So far (step 10d): if
  the run queue empties, no timer is pending, no process is connected to
  `:keypress` with stdin still open, no socket is open, and the root
  process hasn't ended,
  the program prints `deadlock: the root process is waiting for a
  message, and nothing else can run` (or `... waiting for #<pid N> to end,
  ...`, in `join`) to stderr and exits 1.
- **The program ends when nothing can run** and nothing more can come (no
  timer is pending, no socket is open, and no process is connected to
  `:keypress` or stdin has ended), not when the root ends: other
  processes keep running after the root has its value (or its error), and
  the root's value is printed last. Processes
  still waiting in `recv` or `join` at that point are dropped (D93).
- **Blocking calls.** Since a process can block with its stack, `sleep`
  blocks only the calling process; it can't fail, and returns `()`
  (D111). Files are devices instead (Files, under Scheduler), and `slurp`
  and `spew` are written in slight with `connect` and `join`
  (`lib/fs.slight`), so they block only their caller too, and give
  Results: `(:ok lines)` and `(:ok ())`, or `(:error reason)` (D129).
- **Timers** (D112, D114). `after` and `sleep` take an integer number of
  milliseconds; a negative one counts as 0. Timers due at the same time
  fire in the order they were set, at the scheduler's next decision (so
  `(sleep 0)` lets the processes already waiting go first). A timer whose
  process has ended by the time it's due is dropped then, as a message to
  it would be.

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
defined. A number comes as a string: `(string->int (car @ARGV))`.

### Builtins and the prelude

[`LANGUAGE.md`](LANGUAGE.md) describes each one as a program sees it.

**In C (or assembly):**
- arithmetic: `+ - *` on any two numbers; an integer and a float give a
  float, and an integer result that doesn't fit in 63 bits faults. `/`
  always returns a float. `div` and `%` take integers only and truncate
  toward zero (`(% -7 2)` is -1). Dividing by zero faults, with `/` too.
  `ceil`, `floor`, `round` and `trunc` return integers (`round` sends
  halves up, D72, so `(round -2.5)` is -2), and fault if the result
  doesn't fit. `sqrt pow sin cos tan exp` always return floats.
  `abs`, `min` and `max` keep their argument's type. `PI` is a float.
  `float?` and `num?` alongside `int?`. Floats print in the shortest form
  that reads back as the same double, laid out as JavaScript does, but
  always with a `.` or an exponent: `3.0`, `0.1`, `1e+21`, `nan`, `inf`.
- comparison: `== != < <= > >=` on any two numbers (`(== 1 1.0)` is
  `#true`), structural `eq?`/`ne?` on any values (`(eq? 1 1.0)` is
  `#false`)
- type predicates: `nil? cons? sym? str? num? int? float? lambda? pid?
  bool?` (`sym?` is true for `#true` and `#false`: they're symbols)
- lists: `cons car cdr list`, and `c[ad]r` with up to four letters
  (`cadr`, `cddr`, `caddr`, ...). `car`/`cdr` of anything but a cons
  faults, and so does `cons` onto anything but a list, so every list is
  proper. Inside a quoted list, `:a` reads as `(quote a)`, as it does
  everywhere; write `'(a b)`, not `'(:a :b)`.
- strings: `str?`, `str-len` (bytes), `substring`, `concat`/`~`,
  `index-of`, `str-split`, `str-join`, `string->int`, `string->float`,
  `symbol->string`, `string->symbol`, `byte-at`, `bytes->string`,
  `format-num`; and counting characters, `utf8/len`, `utf8/substring`,
  `utf8/index-of`, `utf8/chars`, `utf8/code`, `utf8/char`, `utf8/valid?`.
  See Strings below for how each behaves.
- processes: `send join monitor kill after raise disconnect`
- I/O: `tty/write`, `tty/screen/rows`, `tty/screen/cols` (the terminal's
  size, asked each time; 24 and 80 when stdout isn't a terminal), `pprint`
  (prints its argument and a newline, and returns `()`: D43;
  symbols print without the colon, so `:ping` prints as `ping`),
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
| strings | `uc lc` (ASCII), `(pad-start s n fill)`, `(pad-end s n fill)`, `(str-repeat s n)`, `starts-with?`, `ends-with?`, `(bytes s)` (a list of its bytes), `ord` and `chr` (`utf8/code` and `utf8/char`, under Perl's names) |

D78 has why `filter`, `remove`, `range`, `take`, `skip`,
`starts-with?` and `ends-with?` work as they do.

A program can define a function with a prelude name. Its own code then
uses its definition, and the prelude keeps using the prelude's (D80).

**Functions as values.** A `defun` name, a `lambda`, or a builtin with a
fixed number of arguments (`(map car xs)`, `(fold/l 0 + xs)`) can be used
as a value. Builtins with a varying number (`list`, `concat`,
`format-num`) can't.

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

### Strings

- **Level 1:** immutable byte strings. The byte builtins index by byte;
  case mapping is ASCII only.
- **Level 2, in part (D148):** a second set of builtins, `utf8/`, that
  count characters, enough for a JSON parser and printer. Display width
  is still to come, when the editor needs it.
- Full Unicode is out. If it's ever needed, a C library comes in as a
  driver.
- There's no character type. A character is an integer or a one-character
  string.
- Build strings by collecting pieces in a list and joining them.
- `\n`, `\r`, `\t` and `\e` are names for one-character strings; string
  literals also take those escapes, plus `\"`, `\\` and `\u{hex}`.

**The builtins (step 5a).** Where an edge case could go either way,
they do what JavaScript's string methods do (D62):

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
| `(string->float s)` | a decimal number, `-?D+(.D+)?([eE][+-]?D+)?`, as the nearest float (as the reader rounds a literal), or `#false`, also when it's too big for a double |
| `(symbol->string sym)`, `(string->symbol s)` | the latter is `#false` unless the program mentions that symbol (D14) |
| `(byte-at s i)` | a byte as an integer; an index out of range faults |
| `(bytes->string xs)` | integers 0–255 to a string |
| `(format-num n width [fill])` | `n` padded at the start to `width`, with `fill` (default `" "`) repeated as JavaScript's `padStart` does |
| `(tty/write x ...)` | writes its arguments, rendered as `concat` does, and flushes; returns `()` |

`pprint` shows a string in double quotes with nothing escaped inside
(D64). `eq?` compares strings byte by byte.

**Counting characters (D148).** A character is a well-formed UTF-8
sequence or, failing that, a stray byte on its own, whose code point is
U+FFFD, as in Go. So nothing faults on bad bytes, `utf8/chars` loses
nothing, and `utf8/valid?` says whether a string has any.

| Builtin | |
|---|---|
| `(utf8/len s)` | length in characters |
| `(utf8/substring s start end)` | characters `[start, end)`, clamped and swapped as `substring` does |
| `(utf8/index-of s m)` | character index of the first `m` that starts a character, or -1; an empty `m` is at 0 |
| `(utf8/chars s)` | the characters, as one-character strings; `(str-join "" (utf8/chars s))` is `s` |
| `(utf8/code s)` | the code point of the first character; `""` faults (`:out-of-range`) |
| `(utf8/char n)` | the one-character string for code point `n`; a surrogate or anything past U+10FFFF faults (`:out-of-range`) |
| `(utf8/valid? s)` | `#true` if `s` is all well-formed sequences |

The byte builtins that don't count are right for UTF-8 as they are:
`concat`, `~`, `str-join`, `eq?`, `starts-with?`, `ends-with?`, and
`str-split` on a separator that isn't empty. Output writes a string's
bytes as they are. `utf8/chars` takes 32 bytes a character, and the root
never collects, so a text split there tops out at about 2 MB.

### JSON, s-expressions and source (step 12a; JSON done)

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
- **S-expressions** (planned): `(sexp/parse s)`, the same Results with
  `:bad-sexp`; `(sexp/print v)`; and `(:read :sexp)`, `(:sexp f v)` for
  each form.
- **S-expressions as data:** lists, numbers, strings, symbols, `#true`,
  `#false`, `'x`, comments. A symbol the program mentions reads as itself,
  any other as `(:symbol "name")` (D14 holds: no symbols are made at run
  time); `:a` and `a` are the same symbol (D52). `sexp/print` prints what
  `sexp/parse` reads back, strings escaped and `(:symbol "name")` as
  `name`, and faults on pids, closures, `nan` and `inf`; `pprint` stays
  unescaped.
- **Source:** where `:sexp` skips doc blocks, `:source` gives a file's
  forms and doc blocks in order, `(:form f form line)` and `(:doc f text
  line)`, a doc block as its raw text. A form's raw text instead, D156's
  option, is left out till something needs it (D159).

### Not in the language

`eval`, hot reload, macros, `catch`, `gensym`, local `defun`, selective
receive, mutable anything. (`if`, `when` and `case` aren't macros, but
forms the expander makes into `cond`: D142.) readline isn't needed: the
REPL and line editing are slight code over key events.

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

Each process has its own heap: a chain of `malloc`ed chunks (D89), with
bump allocation and ordinary absolute pointers. The first chunk, 4 KB,
comes with the first allocation (so a process that allocates nothing has
no heap); each new one is twice the last, up to 1 MB. Message chunks join
the chain when received. All of a process's chunks together are capped at
64 MB, and past that the allocation faults (`:heap`). The heap pointer and
limit live in the process struct
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

**Collection** (step 9):

- **It runs only when a receive function asks for its next message**
  (D105), in `rt_recv`, before it takes one. The `recv` rule means the
  stack is empty then, so the roots are just the receive function's
  arguments, in its frame. Nothing else in the frame is live, and nothing
  outside the process's heap points into it.
- It runs once the heap in use (its chunks, less what's free in the
  current one) reaches twice what survived the last collection, and never
  below 256 KB, so a short-lived or small process never collects (D106).
- The live data is copied into fresh chunks (of up to 1 MB each), and the
  old ones are freed. Copying leaves a forwarding pointer, so sharing is
  kept: a DAG stays a DAG. Cons cells have no header, so to-space can't be
  scanned in order as in Cheney's algorithm; a stack of copied objects
  whose fields still need forwarding takes its place (D107).
- In the middle of a handler, the heap only grows.
- **C builtins never see an object move.** No handles, no rooting API, no
  stack maps, no write barrier.
- **The gotcha:** a process that allocates without waiting for a message
  never collects: an allocation-heavy loop inside a handler, the root
  (unless it ends in a receive function), or a process that never calls
  `recv`. The 64 MB limit turns that into a fault instead of exhausting
  memory. Split long work across messages.
- Collecting at other places where the stack is empty (a plain loop that
  is a `fork` body, say, or tail calls between state functions) would need
  a runtime check of `sp`, or more from the compiler. Left out for now.
- `SLIGHT_POISON=1` in the environment fills what the collector frees with
  garbage, so a pointer it missed fails at once; `t/run.sh` sets it.
- **No walk over a value recurses in C** (D147): the collector, printing,
  copying (a message, a fork's values, a result) and `eq?` keep a work
  stack (`rt_work_t`) instead, which grows only as deep as the value nests
  in its cars. So a value can nest as deep as the heap allows, and
  nothing about it depends on the size of a C frame on one target or
  another.

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

- Single core. A **FIFO run queue** (D86): simple, and deterministic on
  one core. A preempted process goes to the back of the queue, but only
  if another process is ready; otherwise it keeps running (D91). `yield`
  always goes to the back.
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
  and on the sockets, with the timeout set to the next timer. Timers are a
  binary heap. While processes are busy, stdin and the sockets are looked
  at every 10 ms (D122). On the virtual clock they're looked at only when
  nothing can run: the sockets first, then a key, then the clock moves to
  the next timer; only when nothing else can happen does it wait for real
  (D139).
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
  and then the way before; or `:json` and `:json/items` (under JSON,
  above). Then `(:eof f)`, and it closes; nothing comes after it. A
  line, count or JSON value of 64 MB or more ends the owner with
  `(:error (:too-big path))` (D160). `:fs/write`
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
  expr)` connects, without stalling the runtime (looking up the host does
  stall it, briefly, until the runtime looks names up itself: D153); the
  connection's first message is `(:open c)`, once
  it's connected, or the owner ends with `(:error (econnrefused
  "host:port"))` and the like. It then reads as a file does, one message
  at a time (it's read only while its owner waits for its next one, so a
  fast sender is held back by TCP itself), and `(:eof c)` when the
  other end closes; it can still be written to then. It takes `(:write x
  ...)` as a file does, and keeps what the socket can't take yet, writing
  it when it can, so `send` still never blocks; closing it waits till
  that's written, and meanwhile it keeps the program running.
  `(connect :tcp/listen port expr)` listens on all interfaces; its first
  message is `(:open l port)`, port being the one it got (0 lets the
  system pick, which tests use, with server and client in one program),
  then `(:accept l conn)` for each connection. `conn` belongs to the
  listener's owner and reads nothing till `(connect conn expr)` hands it to
  a process of its own. Open sockets keep the program running. The error
  names are errno's (`:econnrefused`, `:econnreset`, `:epipe`,
  `:eaddrinuse`, ...), and `:enotfound` (Node's) for a host that can't be
  found; SIGPIPE is ignored, so writing to a closed connection is
  `:epipe`. IPv4 only, and `select()` holds about 1,000 sockets. HTTP will
  be a slight library on top (D132, D140).
- **C libraries** (D151, D152; planned, step 12c). A pure function bounded
  by its input may be a builtin, called on the process's stack; anything
  with state or I/O is a device, its state in the runtime behind a pid,
  serviced in `select()` through a non-blocking API. Nothing blocks, and
  no slight value holds a C pointer. They're vendored as source under
  `vendor/` (as Odin's are), compiled by `slightc` once per target as the
  runtime is, and linked statically into the programs that use them. TLS
  is to be a `:tls` socket device.

### Builtins in C

Arithmetic and comparisons on two integers are inline: one `orr` and one
`tst` check both tag bits, and anything else branches out of line to the
runtime (`rt_add`, `rt_compare`, ...), which promotes to float or faults.

A builtin written in C is an ordinary C function (AAPCS64, or System V
on x86-64). The compiler
passes the call's site (`"str-len at t/x.slight:2:1"`) in the register
after the arguments, so faults raised inside C still say where in the
program they happened (D63). A variadic builtin (`concat`, `tty/write`)
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
   arguments to the runtime, to restart it after waiting.
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
  `SLIGHT_CLOCK=virtual`), never real time.
- **Keys** come from a `; stdin:` line in the test (D116), in printf `%b`'s
  escapes; without one, stdin is empty.
- `lib/test.slight`, a slight-level test library (TAP: `ok`, `is`,
  `diag`), for programs that want one: `(@include :test)`.

## Open questions

Collected from above:

1. Collecting anywhere but `recv` (see "Process heaps and GC"; plan
   step 12b).
2. A worker thread for a C library that can only block, such as SQLite
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
connected processes (D119).
