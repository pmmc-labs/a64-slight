# slight

slight is a small Lisp built on actors. Data never changes once it's
made; what would be mutable state elsewhere lives in a process that keeps
it in the arguments of a tail-recursive loop and answers messages about
it. This compiler turns a whole program into native code for AArch64 or
x86-64, linked with a small runtime that schedules the processes.

This document is the reference: what you can write and what it does.
DESIGN.md says how it's built, and DECISIONS.md says why (the D-numbers
below point there).


## Running a program

    node bin/slightc.ts -o hello hello.slight
    ./hello

`slightc` compiles for AArch64 unless given `--target x86_64`. Several
files can be given, and together they're one program, read in order. On
Linux the program is a static binary, which runs on any Linux of its
machine's kind, in an empty container (`FROM scratch`) too (D227).

A program is a sequence of top-level forms:

- A `defun` defines a function. Every function is global, and they can
  call each other whatever order they're defined in.
- Every other top-level form is part of the root process's body, run in
  order. A top-level `let` binds a name for the rest of the top level;
  functions can't see it.

The root process is pid 1. Its value is that of its last form, or `()`
if there are none.

When nothing can run any more, and nothing more can come (no timer is
pending, no socket can be waited for, and no process is waiting for keys from a
stdin that's still open), the program prints the root's value and a
newline, and exits. That isn't the moment the root ends: other processes
keep running after it, and its value comes out last.

    (fork (pprint :child))
    (pprint :root)
    :done

prints `root`, then `child`, then `done`. (Symbols print without their
colon.)

The exit status is 0, or 1 if the root ended with an error (and then
no value is printed), or if it never ended. A process waiting for a
message or for another process, with nothing left that could wake it,
never ends; when that's the root, the program says so on stderr and
exits 1:

    deadlock: the root process is waiting in the top level for #<pid 2>
    to end, which is waiting in wait-forever for a message, and nothing
    else can run

(on one line). The root is a task, so it waits in `join` or `join/race`
(`... for the first of #<pid 2>, #<pid 3> to end, ...`); the message
follows a `join` to the process it waits for, and so on, till it comes
to one waiting for a message or racing, or after eight, `..., and so
on`. It names the function each waits in, as `:system` does (under
Devices).

These also go to stderr, as they happen:

- `fault: not a cons: () (car at prog.slight:3:5)`: a fault in the root.
  In any other process it's `fault in #<pid 4>: ...`.
- `error: reason`: the root ended with an error that wasn't a fault
  (`raise` or `kill`).
- `dead letter: message (recv at prog.slight:9:5)`: a message that no
  clause of a `recv` matched (Processes, below).

SIGINT, SIGTERM and SIGHUP end the program at once, as they end most
programs, killing its commands (`:exec`) and giving back a terminal in
raw mode. As PID 1, a container's first process, which the system lets
ignore a signal it hasn't asked for, they still end it, with status 128
and the signal: 143 for SIGTERM, which `docker stop` sends (D227). As
PID 1 the program also waits for whatever its commands leave running
when it ends, as an init does, so no zombie stays (D229).

What `pprint` writes goes out whenever the program has nothing to run
and waits (for a message, a timer, a socket, a key), and when it ends;
`tty/write` writes at once. So a log read through a pipe, as a
container's is, has a program's output as soon as it waits, and a
program stopped while it waits has lost none of it (D229).

A compile error is `prog.slight:line:col: message`, and `slightc` exits
1 without writing a program.

Some environment variables matter when running a program, mainly for
tests: `SLIGHT_CLOCK=virtual` swaps the real clock for one that starts at
0 and moves only when nothing can run, straight to the next timer;
`SLIGHT_POISON=1` makes the collector fill what it frees with garbage;
and `SLIGHT_GC_STRESS=n` makes it collect at the entry of every nth
function called (n from 1, every call, which is slow, to 1,000), and at
every `recv`.
`SLIGHT_HOSTS` and `SLIGHT_RESOLV_CONF` are in "Sockets", and
`SLIGHT_SEED` in "Random numbers", under Builtins.

### Replaying a run, and other orders

A run is decided by what it takes from outside: its arguments and
environment, the keys typed, the clock, what files, sockets and commands
give. Everything else the runtime decides itself, counting calls rather
than time. So (D170):

- `SLIGHT_RECORD=file` writes all it takes from outside, as it takes it,
  to `file`, a record of the run. That includes the environment, keys and
  all, so keep a record as you would the environment itself: the file is
  its owner's alone (0600, even if it was there), and isn't written
  through a symbolic link (D205). A program that's set-user-ID or
  set-group-ID ignores `SLIGHT_RECORD` and `SLIGHT_REPLAY`, and
  `SLIGHT_HOSTS` and `SLIGHT_RESOLV_CONF` too.
- `SLIGHT_REPLAY=file` runs the program again from that record: the
  arguments, environment, keys, times, file contents and network traffic
  are the record's, and the run goes exactly as the recorded one did,
  printing what it printed. Nothing is read from the world, and nothing
  written to it but stdout and stderr: no file is written, no socket
  opened, no command run, and the terminal is left as it is. So a bug
  that came from the world can be seen again, as often as it takes. The
  record must be of the same program; where a run goes another way, it
  stops with `replay: the run goes another way at entry N` and exits 2.
- `SLIGHT_SCHED=n` runs processes in another order than first in, first
  out: a generator seeded with the number `n` picks which ready process
  runs next and how many calls each slice has. Each seed gives the same
  order every time, so a program that goes wrong under one seed goes
  wrong the same way again. What the language promises holds in any
  order: messages from one process to another arrive in the order they
  were sent, a process takes one message at a time, and timers due
  together fire in the order they were set. A program that relies on
  more than that (that one process's message comes before another's, say)
  can be caught out by trying a few seeds:

      for n in $(seq 1 100); do SLIGHT_SCHED=$n ./prog || echo "seed $n"; done

  A record keeps its run's seed and its clock (real or virtual), so a
  replay uses them, and the seed its random numbers started from,
  whether `SLIGHT_SEED` gave it or the system did.

### @ARGV

`@ARGV` is the program's arguments after its name, as a list of strings,
`()` if there are none:

    ./prog 10 fast          ; @ARGV is ("10" "fast")

Only the top level sees it: the top-level forms are the body of a
function, and `@ARGV` is that function's parameter. So a lambda or a
`fork` at the top level captures it like any other local, and a `defun`
that names it is a compile error; pass it to the functions that need it.
A number arrives as a string: `(str->int (car @ARGV))`. `@ARGV` can't
be bound or defined (D144).

### %ENV

`%ENV` is the program's environment, as a list of `(name value)` lists
of strings, in the environment's order, which `lookup` reads:

    OLLAMA_HOST=127.0.0.1:11434 ./prog

    (lookup "OLLAMA_HOST" %ENV)     ; "127.0.0.1:11434"
    (lookup "NO_SUCH" %ENV)         ; :not-found

A variable set to nothing is `""`. An entry is cut at its first `=`, so
a value can have `=` in it. It's the top level's second parameter, as
`@ARGV` is its first, and only the top level sees it in the same way:
pass it, or what's needed from it, to the functions that need it, so a
key reaches only the code it's given to. A command (`:exec`) gets the
same environment. `%ENV` can't be bound or defined (D186).

### @include

    (@include "util/strings.slight")
    (@include :test)

`(@include "path")` splices in the forms of another file where it
stands. The path is relative to the file the include is in, unless it
starts with `/`. `(@include :name)` is the built-in `lib/name.slight`:
`:test`, `:fs`, `:ds` and `:http` (Libraries, below).

Includes are top-level forms only. A file is included once, however
often it's asked for, and a file that includes itself, directly or not,
is a compile error. Every program starts with an unwritten
`(@include :prelude)`.

It's not a module system: there's one namespace, and the whole program
is compiled at once (D141).


## Syntax

- `( ... )` is a list, and `()` is the empty list, nil.
- An integer is digits, maybe with a `-` in front: `42`, `-7`. It must
  fit in 63 bits, -4611686018427387904 to 4611686018427387903, or it's
  a compile error.
- A float has digits on both sides of the point, and maybe an exponent:
  `3.14`, `-0.5`, `1.5e3`, `2.0E-8`. `1e3`, `.5` and `1.` aren't
  numbers, but names.
- A string is in double quotes. It can run over several lines, and takes
  the escapes `\"`, `\\`, `\n`, `\t`, `\r`, `\e` (ESC), and `\u{hex}`:
  the UTF-8 for a code point, in hex, up to 10FFFF and not a surrogate.
- A name is a run of anything but whitespace, parentheses, `'`, `"`,
  `;`, the backquote and the comma. So `str/len`, `str->int`, `nil?`,
  `+` and `==` are all names.
- `:name` is a keyword: a symbol that stands for itself. The reader makes
  it `(quote name)`, so `:ping` and `'ping` are the same symbol. `:12`,
  `::x`, `:#x` and a lone `:` are errors.
- `#true` and `#false` are the booleans. `#(p ...)` is a tuple pattern,
  which can only be a pattern (Patterns, below). No other `#` word is
  allowed.
- `'x` is `(quote x)`.
- `;` starts a comment, to the end of the line.
- A doc block, between top-level forms: see Documentation, below.

Not in slight: dotted pairs (`(a . b)`), and quasiquote and unquote
(the backquote, `,` and `,@`).

Since `:a` is read as `(quote a)` everywhere, it's that inside a quoted
list too. `'(:inc)` is the list `((quote inc))`, not the message
`(:inc)`. Write `(list :inc)`, or `'(inc)`.

`$$` is the current process, and `^$$` its parent (Processes).

`\n`, `\r`, `\t` and `\e` are also names, for the one-character strings,
and `math/PI` is the float 3.141592653589793. They're constants rather than
reserved words: a local of the same name hides them.

These names can't be bound or defined: `#true`, `#false`, `$$`, `^$$`,
`@ARGV`, `%ENV`, `@include`, the special forms, and the forms that
become `cond` (`match` among them).

### Documentation

Between top-level forms, a line `=doc` begins a doc block, and a line
`=cut` ends it, as in Perl's POD; so does the end of the file. The
compiler skips it, so a doc block never changes what a program means.

    =doc
    # Counters

    A counter is a process that holds a number. *Inc* adds one.
    =cut

    (defun counter (n)
        (recv
            ((:inc) (counter (+ n 1)))))

Inside, write Markdown that reads well as plain text: headings (`#`,
`##`, `###`), paragraphs, `*emphasis*` and `**strong**`, `` `code` ``,
fenced code blocks, lists (`-` and `1.`), and `[links](url)`.

- Only at the start of a line between top-level forms, so nothing inside
  a form (a quoted list, say) is ever taken for one: there, and anywhere
  else, `=doc` is a name.
- `=doc` is alone on its line, and so is `=cut`, but for spaces after
  them; a line like `=cutting` is part of the block.
- `=doc` is the one kind for now. Any other line that starts with `=`
  and a letter there, a stray `=cut` among them, is an error: other
  kinds, such as `=example`, are kept for later.

Reading a file with `(:read :source)` gives its doc blocks and its
forms in order (Files).


## Values

- **Integers**: 63-bit. An integer result that doesn't fit faults
  (`:overflow`); nothing wraps around.
- **Floats**: IEEE doubles. They print in the shortest form that reads
  back as the same double, always with a `.` or an exponent so they
  can't be taken for integers: `3.0`, `0.1`, `1e+21`, `1e-7`, `-0.0`,
  `nan`, `inf`, `-inf`.
- **Symbols**: `:ping`, `'ping`. They print without the colon. Every
  symbol is known when the program is compiled; none are made while it
  runs (`str->sym`, below).
- **Booleans**: `#true` and `#false`, which are symbols too.
- **Lists**: `()`, and cons cells. Every list is proper: `cons` faults
  on anything but a list as its second argument.
- **Strings**: immutable bytes, UTF-8 by convention (Strings, under
  Builtins). `pprint` shows one as the reader takes it back, on one
  line: in double quotes, with `"` and `\` escaped, and the control
  characters, as `\n`, `\t`, `\r`, `\e`, or `\u{hex}` for the rest
  (C0, DEL and C1). Other characters, and stray bytes, are as they are.
- **Tuples**: a fixed number of values, any of which can be had in one
  step, where a list takes a step per element (D169). They print as
  `#(1 "a" b)`. `(tuple 1 "a" :b)` makes one, and a pattern such as
  `#(n s k)` takes one apart.
- **Functions**: a `defun` used as a value, a `lambda`, or a builtin used
  as a value. They print as `#<function name>` or
  `#<function lambda at prog.slight:3:9>`.
- **Pids**: a process, or a device. They print as `#<pid 2>`.

Nothing can be changed once it's made, and nothing has a cycle.

There's no truthiness. A test (in `cond`, and so in `if`, `when`, `and`
and `or`) must be `#true` or `#false`; anything else faults
(`:not-a-bool`).


## Special forms

### defun

    (defun name (params...) body...)

Top level only. A function takes at most 8 parameters. The body is one
or more forms, evaluated in order, and the last one's value is the
function's. A call in tail position is a jump, so a loop written as a
tail call runs in constant stack. Every other call uses the stack, and
recursion too deep for it (8 MB) faults (`:stack`).

A call to a `defun` is checked at compile time: the name must exist and
the number of arguments must match. A `defun` can't take a builtin's
name, but it can take one of the prelude's: the program's code then uses
its own, and the prelude keeps using the prelude's.

### lambda

    (lambda (params...) body...)

A function as a value, closing over the locals its body uses. It
captures them **by value**: a copy, made when the lambda is. Since
nothing changes, that's rarely visible, except that a lambda can't call
itself (the name it's `let` to isn't bound while it's made). Use a
`defun` for that. At most 8 parameters.

A lambda can't contain a `recv`, or call a function that waits in one
(see "The recv rule", below).

### let

    (let name expr)

Binds `name` to `expr`'s value for the rest of the body it's in. It's
only allowed as a form of a body: a function's, a `do`'s, a `cond` or
`recv` clause's, or the top level. As the last form of a body, its value
is `expr`'s. A `let` can rebind a name, hiding the earlier binding for
the rest of the body.

    (defun hypotenuse (a b)
        (let a2 (* a a))
        (let b2 (* b b))
        (math/sqrt (+ a2 b2)))

### cond

    (cond
        (test body...)
        ...
        (#true body...))

The tests are evaluated in order until one is `#true`, and then that
clause's body is. Each test must be `#true` or `#false`, or the process
faults (`:not-a-bool`), and if none is `#true` it faults too
(`:no-clause`). So end with a `(#true ...)` clause when you want a
default. The bodies are in tail position if the `cond` is.

### do

    (do form...)

Evaluates the forms in order, and gives the last one's value, in tail
position. At least one form.

### quote

    (quote x)
    'x

`x` as data, not evaluated: a symbol, a number, a string, or a list of
them, nested or not. A quoted list is a constant in the program, shared
by every process.

### fork

    (fork expr)

Starts a new process, a **task**, that evaluates `expr`, and returns
its pid. The locals `expr` uses are deep-copied into the new process, at
most 8 of them. A local is copied whole, however little of it `expr`
uses, so a big one is worth narrowing first: `(let mine (part-of big
i))`, then `(fork (worker mine))`. Inside `expr`, `$$` is the new
process. A task computes a value, which `join` gives: it can't wait in
`recv`, nor be sent to (Processes, D243).

    (join (fork (+ 1 2)))                (:ok 3)

### spawn

    (spawn expr)

As `fork`, but the new process is an **actor**: it has a mailbox, can be
sent to, and can wait in `recv`. `expr` may tail-call a function that
waits, which is the usual way to start one:

    (let c (spawn (counter 0)))

### connect

    (connect :keypress expr)
    (connect :fs/read path expr)
    (connect :tcp "host:port" expr)
    (connect :exec (list "ls" "-l") expr)
    (connect :system expr)
    (connect device expr)

A `spawn` whose new process, an actor, is also connected to a source of
messages. See Devices.

### recv

    (recv
        (pattern body...)
        ...)
    (recv)

Takes the next message, and gives the value of the clause whose pattern
it fits (Patterns, under "The forms that become cond"); `(recv)` gives
the message itself. A `recv` can go anywhere a tail call could, and
inside a call there, in an actor. See Processes.

### yield

    (yield expr)

Pauses: the process goes to the back of the run queue, and when its turn
comes, evaluates `expr` in tail position. Processes are preempted anyway,
so it's rarely needed.


## The forms that become cond

Before the compiler sees them, these are rewritten as `cond`, so they
follow its rules: each test must be `#true` or `#false`.

- `(if test then else)`. Without the else, `()` when the test is
  `#false`.
- `(when test body...)`: the body when the test is `#true`, `()` when
  it's `#false`.
- `(case topic (value body...) ...)`: evaluates `topic` once, and
  compares it with each value in turn, with `eq?`, which is structural,
  so strings and lists match by content. The values are expressions.
  A clause whose value is `#true` matches anything, so it's the default;
  without one, a case that matches nothing gives `()`. (A clause whose
  value is `#false` never matches.)
- `(and a b ... z)` and `(or a b ... z)`: they go from left to right and
  stop at the first operand that decides. Every operand but the last
  must be a boolean; the last isn't tested, and is what they give if
  they get to it, so `(and #true 5)` is 5 and `(or #false "five")` is
  `"five"`. `(and)` is `#true` and `(or)` is `#false`. The last operand
  is in tail position. (Scheme's rule.)

A `case` on a message's tag:

    (case (car msg)
        (:add  (+ n (cadr msg)))
        (:zero 0)
        (#true n))

`not` is a function, in the prelude, on booleans only; a call to it
compiles inline, as a test where it's one (D203).

### match

    (match topic
        (pattern body...)
        ...)

Evaluates `topic` once, and tries the clauses in order: the first whose
pattern the value fits gives the `match` its value, with the names in
its pattern bound to the parts they matched, in its body only. The
bodies are in tail position if the `match` is, so a loop can go through
one, and so can a `recv`. If no pattern fits, the process faults
(`:no-match`, with the value). (D168)

    (defun area (shape)
        (match shape
            ((:circle r)            (* PI (* r r)))
            ((:rect (x1 y1) (x2 y2)) (* (- x2 x1) (- y2 y1)))
            ((:poly & points)       (polygon-area points))))

#### Patterns

`match` and `recv` take the same patterns:

- `_` matches anything, and binds nothing.
- A name matches anything, and binds it.
- A literal matches a value `eq?` to it: an integer, a float, a string,
  `#true`, `#false`, `()`, or anything quoted, such as `:tag`, `'x` or
  `'(1 2)`. So `1` doesn't match `1.0`.
- `(p ...)` matches a list of exactly as many elements, each matching
  its pattern: `(:point x y)` matches `(:point 3 4)`, and
  `(:line (x1 y1) (x2 y2))` a line of two points.
- `(p ... & rest)` matches a list of at least as many, and `rest` (a
  pattern too, usually a name) matches the list of what's left, maybe
  `()`. `(& all)` matches any list.
- `#(p ...)` matches a tuple of exactly as many elements, each matching
  its pattern: `#(x y)` matches `(tuple 3 4)`, but not `(list 3 4)`, nor
  a tuple of three. A tuple pattern has no `&`. It's only a pattern:
  anywhere else, quoted too, `#(...)` is a compile error, and
  `(tuple ...)` makes a tuple (D176).

A name can be in a pattern only once, and it can't be a name that can't
be bound (`$$`, `cond`, ...). There are no guards: match the shape, and
test the rest in the body, with `cond`.


## Functions and calls

`(f a b)` calls `f`. The arguments are evaluated left to right, and a
call passes at most 8.

When `f` is the name of a `defun`, a builtin or a prelude function, the
call is checked at compile time. When `f` is a local (a parameter or a
`let`), or the head is an expression, as in `((adder 1) 2)`, the call is
through a function value, and it's checked when it happens: anything but
a function faults (`:not-a-function`), and so does the wrong number of
arguments (`:arity`). A local hides a function or builtin of the same
name.

A `defun` name, a lambda, or a builtin can be used as a value. A builtin
with optional arguments then takes just the ones it needs (D219):

    (map car pairs)
    (fold/l 0 + xs)
    (map str->int words)

A builtin that takes any number of arguments (`list`, `tuple`, `str`,
`~`, `tty/write`) can't, so `(apply list xs)` is a compile error. Nor
can a function that waits in `recv`.

`(apply f xs)` calls `f` with the elements of the list `xs` as its
arguments, at most 8. In tail position it's a tail call.

A name that isn't a local, a function, a builtin or a constant is a
compile error.


## Processes

Everything runs in processes, on one core. Each has its own heap and a
mailbox. Processes share nothing; they talk by sending messages, which
are copied.

There are two kinds (D243). A **task**, started with `(fork expr)`,
computes a value: it can't wait in `recv`, nor be sent to, and `join`
gives its result. An **actor**, started with `(spawn expr)` (or
`connect`, under Devices), has a mailbox: it's sent messages, and takes
them with `recv`, as a loop with its state in its arguments. The root is
a task. Each gives its pid. `$$` is the current process, and `^$$` the
one that started it (the root's is `()`).

A program whose top level would wait for messages spawns an actor to do
it, and joins it:

    (defun main ()
        (send $$ :hi)
        (recv (m m)))

    (join (spawn (main)))                (:ok hi)

### Messages

`(send pid msg)` puts a deep copy of `msg` at the end of `pid`'s mailbox,
and returns `()`. It never blocks. A message can be any value, and is
conventionally a list headed by a keyword: `(list :add 5)`. A message to
a process that has ended disappears. Sending to a task that hasn't
faults (`:not-an-actor`), as does setting a timer for one (`after`), so
whatever sends its caller messages (`http/stream`, `agent/say`,
`rpc/stream`) is called from an actor.

A function whose whole body is a `recv` is a **receive function**:

    (defun counter (n)
        (recv
            ((:inc)      (counter (+ n 1)))
            ((:get from) (send from (list :count n)) (counter n))
            (:stop       n)))

It takes the first message in the mailbox, or waits for one, and tries
the clauses in order. Their patterns are `match`'s (Patterns, above):

- `(:tag a b ...)` matches a list of exactly that length headed by the
  symbol `:tag`, and binds the rest of it, by position, to the names.
  `_` in a position matches anything and binds nothing.
- `:tag` matches that symbol.
- A bare name matches any message, and binds it. `_` matches any message
  and binds nothing.
- Patterns nest, a literal can go anywhere, `&` takes the rest, and
  `#(...)` takes a tuple apart: `((:move (x y)) ...)`,
  `((:write & xs) ...)`, `((:at #(x y)) ...)`.

The matching clause's body runs, in tail position when the `recv` is the
last thing the function does, and its value is the `recv`'s. To keep
receiving, tail-call the function again, with the new state as its
arguments. When it returns instead, that's the value the call gives
back.

Only the first message is ever looked at; there's no selective receive.
If no clause matches it, it's dropped and logged as a dead letter
(`dead letter: (bogus 1) (recv at prog.slight:2:5)`), and the function
takes the next.

### The recv rule

A `recv` can go anywhere a tail call could (D167): as a form of a body
in tail position (a function's, a `spawn`'s or `connect`'s, or a `cond`
or `recv` clause's), or as a `let`'s value there:

    (defun ask (c)
        (send c (list :get $$))
        (let n (recv ((:count n) n)))
        (pprint (list :count n)))

It can go inside a call there too, and what's evaluated before it still
comes first: in `(list (send $$ :hi) (recv))` the `send` is done before
the `recv`, which gets `:hi`. And in a `cond`'s test, when the `cond` is
in tail position. Not in a lambda, nor in a `cond`, `yield` or `do` (one
with `let`s) that isn't in tail position: take the message first, with
`(let m (recv))`.

The compiler splits the function at each `recv`: the `recv` and the rest
become a receive function of their own, which the function tail-calls
with the locals the rest uses. So a function with a `recv` in it is a
**state function**, as is any function that tail-calls one. The compiler
works out which functions they are, and a state function can only be
called in tail position, or as the body of a `spawn` or `connect`. It
can't be used as a value, and a lambda can't call it. A task (a `fork`'s
body, or the top level) can't wait at all: a `recv` there, or a call to
a state function, is an error (`a task (a fork's body, or the top
level) can't wait for messages, ...: spawn an actor for it`).

So when a process waits, there's nothing on its stack: it's just the
receive function, its arguments and the mailbox. A waiting process holds
no stack at all (D8), and its heap is collected then if it's grown
enough (D105).

The cost is that a function that waits can't give a value back to the
function that called it. For that, use a process: `join` waits for one
to end and gives its result, from anywhere, a lambda included, and
`ask` (in `lib/ds.slight`) sends a request and waits for the answer that
way.

### How processes end

A process ends when its expression gives a value, and its result is
`(:ok value)`. Or it ends with an error, and its result is
`(:error reason)`:

- A fault: a builtin given the wrong type, an integer overflow, a `cond`
  test that isn't a boolean, and so on. The reason is
  `(kind value site)`: a keyword for the kind, the value at fault (or
  `()`), and a string saying where. A fault is always a bug, so it's
  logged as it happens.

      (join (fork (car ())))
      ; (:error (:not-a-cons () "car at prog.slight:1:13"))

- `(raise reason)` ends the current process with `(:error reason)`.
- `(kill pid)` ends `pid` with `(:error :killed)`, whatever it's doing.

`raise` and `kill` aren't logged, except in the root, whose error is the
program's (`error: reason`). There's no catch: whoever cares finds out
through `join` or `monitor`.

The fault kinds:

- `:not-an-int`: an integer operation given something else
- `:overflow`: an integer result that doesn't fit in 63 bits
- `:not-a-bool`: a `cond` test that's neither `#true` nor `#false`
- `:no-clause`: no `cond` clause matched
- `:stack`: a call with the stack nearly full
- `:not-a-cons`: `car` or `cdr` of something that isn't a cons
- `:not-a-list`: `cons` onto something that isn't a list
- `:heap`: the process's heap is full
- `:not-a-string`: a string operation given something else
- `:not-a-symbol`: a symbol operation given something else
- `:out-of-range`: an index, a byte, a code point, or a rounded float
  out of range
- `:not-a-number`: arithmetic on something that isn't a number
- `:div-by-zero`: division by zero
- `:not-a-function`: a call to something that isn't a function
- `:arity`: a function called with the wrong number of arguments
- `:not-a-pid`: a process operation given something else
- `:join-self`: `(join $$)`, which would wait forever
- `:not-a-device`: `connect` handed something that isn't an open device
- `:not-json`: `json/print` given something JSON can't hold
- `:not-sexp`: `sexp/print` given something that isn't data
- `:no-match`: a `match` that no clause matched
- `:not-a-tuple`: a tuple operation given something else
- `:not-an-actor`: a task sent a message, or a timer, or monitoring
  something, which would tell it with a message it can't take (D243)
- `:not-a-task`: for joining an actor, which D243 will forbid once a task
  can wait for an actor's answer in other ways; nothing gives it yet

### Waiting for a process

- `(join pid)` waits until `pid` ends, and gives its result, `(:ok
  value)` or `(:error reason)`. It works on any pid, any number of
  times, from anywhere (a lambda included), and after the process has
  ended. Several joiners wake in the order they joined.
- `(join/race pids)` waits until the first of the list `pids` ends, and
  gives `(pid result)`, `result` being what `join` would give (D220). If
  some have ended already, it's the first of those in the list. Like
  `join`, it works from anywhere, and the caller keeps its stack while it
  waits, so it needn't be a function that waits in `recv`. An empty list
  faults (`:not-a-pid`), since it would wait forever, and so does
  something in the list that isn't a pid, or the caller's own pid
  (`:join-self`), all found before it waits. The others go on running:
  `kill` them if they're no longer wanted.

      (defun within (ms pid)
          (let timer (fork (sleep ms)))
          (match (join/race (list pid timer))
              ((p result) (if (eq? p pid)
                              (do (kill timer) result)
                              (do (kill pid) (list :error :timeout))))))

  And a pool, at most k jobs at once, is a loop that forks till k are
  running and then waits for the first of them to end.
- `(join/any pids)` gives `(:ok value)` for the first of `pids` to end
  well, or, once all have ended with errors, `(:error reasons)`, the
  reasons in the order of `pids`: JavaScript's `Promise.any`. An empty
  list gives `(:error ())`.
- `(join/all pids)` gives `(:ok values)`, the values in the order of
  `pids`, once all have ended well; or the first `(:error reason)` as
  soon as there is one, without waiting for the rest: JavaScript's
  `Promise.all`. An empty list gives `(:ok ())`. `(map join pids)` is the
  one that waits for every result, ended well or not.

  Both are in the prelude, on `join/race`, and both leave running the
  processes they no longer wait for.
- `(monitor pid)` asks to be told when `pid` ends: the caller is sent
  `(:exit pid result)` then, or at once if it already has. Monitoring
  twice means two messages. Nothing is sent to a parent unless it asks.
  Only an actor can monitor (`:not-an-actor`). Returns `()`. A monitor lasts while both processes do: if the caller
  ends first, its monitor goes with it, so short-lived processes can
  monitor a long-lived one as often as they like, as `ask`'s boxes do.
- `(kill pid)` ends `pid`, wherever it is: running, waiting in `recv` or
  `join`, or asleep. `(kill $$)` ends the caller. Killing a process that
  has ended does nothing. Returns `()`.

A process's result is kept after it ends, for good, so a late `join`
or `monitor` still gets it.

### Addresses

A task can't be sent to, so it makes a place that can be (D243):

- `(address)` makes one, a value of its own, printed `#<address 1.0>`
  (the task's pid and its number among the task's addresses). `send` and
  `after` take it where they take a pid, so it goes in a message for an
  actor to answer at. `pid?` is `#true` of it; `join`, `monitor`, `kill`
  and the rest fault on it (`:not-a-pid`). Only a task makes one: in an
  actor, `address` faults `:not-a-task`, since an actor takes what comes
  to it with `recv`.
- `(read src)` waits, keeping the stack as `join` does, for the next
  message sent to `src`, an address of the caller's, and gives it; or,
  if `src` is a pid, for it to end, and gives its result, as `join`.
  Messages to one address come in the order they were sent, and no
  other message can be taken for one: there's nothing to steal, and no
  dead letters.
- `(read/any srcs)` waits for the first of a list of them to have
  something, and gives `(src what)`. They're looked at in their order,
  as `join/race`'s pids are: an address with a message waiting, or a pid
  that has ended. So with an address and the actor it asked, the answer,
  or the actor's end, whichever comes; and with a timer set for the
  address too, a timeout.
- `(disconnect a)` closes an address: what's waiting there is dropped,
  and anything sent to it after disappears, as to an ended process. So
  does what's sent to an address of a task that has ended. Reading a
  closed address, or another task's, faults (`:not-a-pid`).

`read` and `read/any` are a task's alone (`:not-a-task` in an actor). A
task reading what will never come is waiting for ever: `deadlock: the
root process is waiting in the top level for a message to #<address
1.0>, ...`.

    (defun counter (n)
        (recv
            ((:get to) (reply to n) (counter n))))

    (let c (spawn (counter 5)))
    (let to (address))
    (send c (list :get to))
    (read to)                            (reply 5)

That's what `ask` does, in the prelude: `(ask pid msg)` adds an address
to the end of `msg`, sends it, and reads the answer, which the actor
gives with `(reply to v)` (it sends `(:reply v)`). It gives `v`; or, if
`pid` ends first, raises `(:ended pid result)`. `(ask-within ms pid
msg)` raises `(:timeout pid)` too, if no answer comes in `ms`
milliseconds, and takes its timer back if one does. Since `ask` makes
an address, only a task asks (D243): an actor sends, and takes the
answer as a message, or forks a task to ask.

### Time

- `(after ms pid msg)` sends `msg` to `pid` once `ms` milliseconds have
  passed. `msg` is copied when `after` is called. Returns the timer's
  number, an integer: 1 for the program's first, and one more for each
  after it.
- `(timer/cancel t)` takes back `after`'s timer `t`, its number, if it
  hasn't fired: its message never comes, and it no longer keeps the
  program running. Gives `#true` if it took it back, or `#false` if it
  had fired already (its message may be in the mailbox, or taken), had
  been taken back, or is no timer's number. Any process can take a
  timer back (D232).
- `(sleep ms)` blocks the calling process, and only that one, for `ms`
  milliseconds. Returns `()`.

Both take an integer, and a negative one counts as 0. Timers due at the
same time fire in the order they were set. A pending timer keeps the
program running, but not one for a process that has ended, which is
dropped (D232): a sleeper that's killed, or a timeout sent to a process
that has gone, doesn't hold up the program's end. An `after` is for an
actor, since a task can't be sent to. In one:

    (let t (after 5000 $$ :timeout))
    (send worker (list :job $$))
    (recv
        ((:done v)  (timer/cancel t) v)
        (:timeout   (kill worker) :timed-out))

`timer/cancel` giving `#false` there would mean the `:timeout` came
first, and is still to be taken.

- `(clock/us)`: the microseconds since the program started, an integer,
  on the clock the timers go by. It's for timing a part of a program:
  `(let t0 (clock/us))`, then `(- (clock/us) t0)` after it. It isn't the
  time of day. On the virtual clock (Running a program) it moves only
  as the timers do, so work takes no time on it; and a recorded run
  replays with the same readings.

### Scheduling

Processes take turns from a run queue, first in, first out. A running
process counts **reductions**, slight's measure of a process's work (the
word is Erlang's): one each time it enters a function compiled from
slight, by a call or a tail call alike. That's a `defun`, the prelude's
included; a lambda; a `fork`'s, `spawn`'s or `connect`'s body; and a receive
function, which counts again when it starts over after waiting for a
message (one already in the mailbox it takes without starting over). A
builtin, such as `car`, `+` or `send`, runs inline or in C, and counts
nothing. After 1,000 reductions, a process goes to the back of the queue
if another is ready. Every loop is a tail call, so no process can hold
up the rest.
`SLIGHT_SCHED` changes the order, for testing (under "Replaying a run,
and other orders").

### The heap

Each process's heap holds up to 1 GB, unless `SLIGHT_HEAP_MAX` in the
program's environment says otherwise (a number of bytes, or with `K`,
`M` or `G` after it, and at least `1M`); past that, an allocation faults
(`:heap`), and just that process ends. On Linux, when the program's
memory is limited to less than 4 GB, as in a container (`docker run
--memory`, the cgroup's limit), the cap is a quarter of the limit
instead (D233): a process that grows without end then holds about half
the limit when it faults, as its last collection copies, so it ends on
its own and the kernel doesn't kill the program. That caps each
process, not all of them together. It's collected when a receive function asks
for its next message, or at the entry of the next function called
(D166), in two generations (D200): what's new is allocated in a nursery
(from 4 KB, growing to 1 MB), and once that's full, what's still
reachable in it is moved to the old generation and the nursery starts
again; the old generation is collected once it's at least 256 KB and
twice what survived its last collection. So what dies young costs
little, and what lives long is copied rarely. Every loop is a call, so
any process collects, the root included, whether it waits for messages
or not.

What's live is what the process can still reach, and that includes
whatever a function that hasn't returned has bound with `let`, until it
returns: a `let` at the top level keeps its value till the program ends.


## Devices

`connect` is `fork` plus a connection: it starts a process to evaluate
`expr`, as `fork` does, connects it to a source of messages, and returns
the new process's pid.

### The keyboard

    (connect :keypress expr)

The new process is sent every key typed, as `(key mods...)`:

- `key` is a one-character string for a printable key (any UTF-8
  character, well-formed as the `str/` builtins have it), or a name:
  `:ArrowUp`, `:ArrowDown`, `:ArrowLeft`, `:ArrowRight`, `:Home`, `:End`,
  `:Insert`, `:Delete`, `:PageUp`, `:PageDown`, `:Enter`, `:Escape`,
  `:Backspace`, `:Tab`, `:F1` to `:F12`, or `:Unidentified`, which is
  also what each byte of a malformed character is.
- the mods are those held, in the order `:ctrl :alt :shift`. An
  upper-case letter comes with `:shift`, and Ctrl and a letter is the
  lower-case letter with `:ctrl` (but Ctrl-H, Ctrl-I, Ctrl-J and Ctrl-M
  are Backspace, Tab, Enter and Enter, since terminals send the same
  bytes for them).

So `("a")`, `("Z" shift)`, `("e" ctrl)`, `(ArrowRight ctrl)`,
`(Tab shift)`. Every connected process gets every key. While any is
connected and stdin is a terminal, the terminal is in raw mode (keys
arrive as they're typed, unechoed). Ctrl-C puts the terminal back and
exits with status 130. When stdin ends, the keys stop. stdin is read
whether or not it's a terminal, so keys can be piped in; on the virtual
clock, a piped key arrives each time nothing else can run.

### Files

    (connect :fs/read path expr)
    (connect :fs/write path expr)
    (connect :fs/append path expr)

The file at `path` (a string, relative to the current directory) is
opened on a **device**: a pid that the runtime serves rather than
compiled code. The new process owns it.

- The device's first message to its owner is `(:open f)`, `f` being the
  device's pid.
- A reader then sends each line as `(:line f s)`, without its `\n` (a
  last line without one still comes; a `\r` before it stays), and then
  `(:eof f)`, after which it closes. One message at a time: the next is
  cut only when the owner, having taken the last, waits in `recv` again.
  So a big file never piles up in the mailbox, and whatever the owner
  does while it handles a message (changing how it reads, disconnecting,
  handing the file over) applies from the next one on.
- A writer (`:fs/write` empties the file, or creates it; `:fs/append`
  adds to it, or creates it) takes `(:write x ...)` from any process,
  and writes the `x`s at once, rendered as `tty/write` renders them.

`(send f (list :read how))`, from any process, changes how a reader
cuts what it reads into messages, from the next message on. `how` is
one of:

- `:lines`, the way it starts: `(:line f s)`.
- `:chunks`: `(:chunk f s)`, what one read gives (or what's been read
  and not yet sent), never more than 64 KB, and never empty.
- `:json`: `(:json f v)` for each top-level JSON value, `v` as
  `json/parse` makes it (under Builtins, JSON). Space between values
  doesn't matter, so NDJSON (a value a line) and values one after
  another both work.
- `:json/items`: as `:json`, but a top-level array comes an element at
  a time, a `(:json f v)` for each, so an array too big to hold can be
  read through.
- `:sexp`: `(:sexp f v)` for each top-level datum, `v` as `sexp/parse`
  makes it (under Builtins, S-expressions), past space, comments and doc
  blocks.
- `:source`: slight's source as it is, in order: `(:form f form line)`
  for each top-level form, as `:sexp` reads it, and `(:doc f text line)`
  for each doc block, `text` being the lines between `=doc` and `=cut`.
  `line` is the line each begins on, counting from the top of the file
  whatever was read before.
- A count, an integer `n` from 0: one `(:chunk f s)` of the next `n`
  bytes, and then back to the way before. If the input ends first, `s`
  is shorter, and `(:eof f)` comes next. A count is for the next message
  alone, whatever the way: sent with `:chunks`, say, it comes first,
  whichever was sent first.

Nothing comes after `(:eof f)`. Anything else sent to a device, `:read`
to a writer or a `how` it doesn't know included, is a dead letter. Bad
JSON ends the owner with `(:error (:bad-json path))`, after the values
before it, and a bad s-expression with `(:error (:bad-sexp path))`.

    (defun size (n)
        (recv
            ((:open f)    (send f (list :read :chunks)) (size n))
            ((:chunk f s) (size (+ n (bytes/len s))))
            ((:eof f)     n)))

    (join (connect :fs/read "photo.jpg" (size 0)))
    ; (:ok 1048576)

`(disconnect f)` closes the device `f`, and returns `()`. A pid that
isn't an open device it ignores. A device also closes when its owner
ends, and messages to a closed device go nowhere.

If the file can't be opened, the owner ends before it runs, with
`(:error (name path))`, `name` being errno's: `:enoent`, `:eacces`,
`:eisdir`, `:enotdir`, `:enospc`, and so on, or `:io-error`. A read or a
write that fails ends it the same way, when it happens; and so does a
line, a count, a JSON value or a datum of 64 MB or more, with `(:error
(:too-big path))`, whatever the heap holds, so that an endless line
can't take all the memory. A value is too big if it hasn't ended by 64
MB (D204), or if building it takes that much, the lists and objects
still open in it included (D205).

    (defun reader (lines)
        (recv
            ((:open f)   (reader lines))
            ((:line f s) (reader (cons s lines)))
            ((:eof f)    (reverse lines))))

    (join (connect :fs/read "notes.txt" (reader ())))
    ; (:ok ("first line" "second line"))

`lib/fs.slight` has `slurp` and `spew`, written this way.

### Sockets

    (connect :tcp "host:port" expr)
    (connect :tcp/listen port expr)
    (connect :tcp/listen "host:port" expr)

TCP, IPv4 only, and about 1,000 sockets at once (the limit of
`select()`). The port is a number. Neither connecting nor looking up the
host holds up the other processes: the runtime reads `/etc/hosts`, and
asks the name servers in `/etc/resolv.conf` about any other name itself
(D153, D165). `SLIGHT_HOSTS` and `SLIGHT_RESOLV_CONF` name other files
for it to read, and there a `nameserver` line may give a port
(`nameserver 127.0.0.1:5353`), so a test can serve its own names.

A connection's first message is `(:open c)`, once it's connected. Then
it reads as a file does: lines at first, one message at a time, and
`(:read how)` changes the way; and `(:eof c)` when the other end closes.
It can still be written to after that. It takes `(:write x ...)` as a
file does, and keeps what the socket can't take yet, so `send` still
never blocks. Closing it waits until that's written.

A listener on a port listens on all interfaces; one at `"host:port"`
listens at that address alone, so `"127.0.0.1:7317"` takes connections
from this machine and no other (D226). The host there is an address, a
name in the hosts file, or `localhost`, but never a name the name
servers are asked about, since it must be this machine's: any other is
`:enotfound`, and an address that isn't this machine's is
`:eaddrnotavail`. A listener first sends `(:open l port)`, `port` being
the one it got: port 0 lets the system pick. Then `(:accept l conn)`
for each connection. `conn` reads nothing until
`(connect conn expr)` hands it to a process of its own, which then hears
`(:open conn)`.

`(connect device expr)`, with anything but a keyword as its first
argument, hands an open device to a new process in this way. Anything
that isn't an open device faults (`:not-a-device`).

A connection that fails ends its owner with `(:error (name "host:port"))`
(a listener's, with its port, or the `"host:port"` it was given): errno's names, such as `:econnrefused`,
`:econnreset`, `:epipe` and `:eaddrinuse`, and `:enotfound` for a host
that can't be found, or `:etimedout` when no name server answered. Open
sockets keep the program running, and so does a host being looked up.

    (defun echo ()
        (recv
            ((:open c)   (echo))
            ((:line c s) (send c (list :write s \n)) (echo))
            ((:eof c)    (disconnect c))))

    (defun server ()
        (recv
            ((:open l port) (server))
            ((:accept l c)  (connect c (echo)) (server))))

    (connect :tcp/listen 7000 (server))

A Unix socket, at a path on this machine, is the same but for where it
is (D231):

    (connect :unix "/run/app/ctl.sock" expr)
    (connect :unix/listen "/run/app/ctl.sock" expr)

A connection reads and writes as a TCP one does, and a listener sends
`(:open l path)`, then `(:accept l conn)` for each connection, whose
errors name the path. Listening makes the socket, as a file at the
path, and removes it when the listener closes, and when the program
ends, however it ends, SIGTERM included. If something is there already,
listening is `:eaddrinuse`, unless it's a socket that refuses a
connection: one left by a program that was killed with SIGKILL, or
crashed, which is replaced. Connecting needs write permission on the
socket, which the program's umask sets: with the usual one, 022, only
its owner (and root) can connect, so the path's permissions say who
may call a program there. A path with nothing there is `:enoent`, and
one too long for a socket (more than 103 bytes on macOS, 107 on Linux)
`:enametoolong`.

`(socket/peer c)` says who's at the other end of the Unix connection
`c`, either end of it: `((:uid u) (:gid g) (:pid p))`, as the kernel
had them when it connected, which a program can trust as it can't
trust what the other end says of itself (D235). For any other pid, a
TCP connection or a process, it's `()`; anything that isn't a pid
faults. On macOS the pid can be had only while the other end is still
connected, and is 0 after; the uid and gid stay (D236).

In a container, a socket in a directory shared with the host is how a
program on the host reaches one inside without a port: `docker run -v
/run/app:/run/app`, and `--user` with the host user's ids, so that the
socket is theirs. `docker stop` removes it, and after `docker kill` the
next start replaces it.

HTTP is built on these, in `lib/http.slight` (Libraries, below). A
request's head comes as lines up to a blank one, and its body as a count,
which the reader asks for while it handles that blank line, so none of
the body is cut as lines (D159).

To `join`, `monitor` and `kill`, a device is a process that has ended
with `(:ok ())`.

### Commands

    (connect :exec (list "ls" "-l") expr)

Runs a command: a list of strings, its name and its arguments. There's
no shell unless one is asked for, `(list "sh" "-c" "ls -l | wc -l")`, so
nothing in an argument is expanded, or quoted wrongly. The name is
looked for in `PATH`, unless it has a `/` in it. The command runs with
the program's environment and working directory (`env NAME=value ...`
and `sh -c "cd dir && ..."` change them), in a process group of its own,
with its input and output on pipes. Its stderr goes where its stdout
does, so its errors come where they happened, in order (D185).

- Its first message is `(:open p)`. Then its output reads as a
  socket's does: lines at first, one message at a time, and `(:read
  how)` changes the way. Then `(:eof p)`, when the output has ended;
  and once the command has ended too, `(:exited p status)`, after which
  the device closes. `status` is the exit code, or `(:signal name)` if a
  signal ended it: `:hup`, `:int`, `:quit`, `:ill`, `:trap`, `:abrt`,
  `:bus`, `:fpe`, `:kill`, `:usr1`, `:segv`, `:usr2`, `:pipe`, `:alrm`
  or `:term`, or the number of any other.
- `(:write x ...)`, from any process, writes to its input, as to a
  socket: kept till the pipe takes it, so `send` never blocks.
  `(:eof)` closes the input, once what was written before it has gone,
  while the output still comes. A command that reads its input waits for
  it till then, so send `(:eof)` at once to one that has none. Input
  the command doesn't take, having ended or closed its input, goes
  nowhere, and so does anything written after `(:eof)`.

      (defun run (out)
          (recv
              ((:open p)     (send p (list :eof)) (run out))
              ((:line p s)   (run (cons s out)))
              ((:eof p)      (run out))
              ((:exited p n) (list n (reverse out)))))

      (join (connect :exec (list "sh" "-c" "echo hi; echo oops >&2; exit 3") (run ())))
      ; (:ok (3 ("hi" "oops")))

`(disconnect p)` stops a command that hasn't ended: its process group is
killed (SIGKILL), and it's waited for, so it's gone when `disconnect`
returns, and nothing more comes from it. Its owner ending does the same,
and so does the program's end, however it ends. A timeout is the owner's
own `after`, then `disconnect`:

    (defun within (out)
        (recv
            ((:open p)     (send p (list :eof)) (after 5000 $$ (list :late p)) (within out))
            ((:line p s)   (within (cons s out)))
            ((:eof p)      (within out))
            ((:exited p n) (list n (reverse out)))
            ((:late p)     (disconnect p) :timed-out)))

A command that can't be started ends its owner before it runs, with
`(:error (:enoent "ls"))` for one that isn't there, `:eacces` for one
that can't be run, or `:io-error` for an argument with a NUL in it. A
read that fails ends it as a socket's does, and so does a line, count
or value of 64 MB or more, with `(:error (:too-big "ls"))`. Something
that isn't a list of strings, at least one, faults in the caller. A
command keeps the program running while there's something to wait for:
output its owner waits for, input to write, or its end, once its output
has ended. Each takes two of the 1,000 or so file descriptors `select()`
can wait on.

On the virtual clock, a command whose input is closed can only be
working on its own, so while its owner waits for its output or its end,
the runtime waits for it, for real, and neither keys nor the clock move
meanwhile: a timeout around it doesn't fire first. One whose input is
open may be waiting for the program, so it's looked at as a socket is,
and the clock moves when it has nothing to say: set a timer around it
once it has said all it will.

### The system

    (connect :system expr)

A device that tells about the program's processes and the runtime
(D171). Any process may connect to it, and anyone may ask it; it
answers its owner, whose first message is `(:open s)`. It's asked:

- `(:processes)`: it answers with `(:process s info)` for each process
  that hasn't ended, in the order of their pids, and then `(:end s)`.
  It copies what it tells of them all at once, so that it all describes
  one moment: nothing runs in between. Then it sends them one at a
  time, as a file sends lines: the next when the owner, having taken
  the last, waits in `recv` again; so a million processes never pile up
  in a mailbox. A `(:processes)` asked before the last one's `(:end s)`
  is answered after it.
- `(:process pid)`: it answers at once, with `(:process s info)`, or
  `(:ended s pid result)` if `pid` has ended, `result` being what `join`
  would give. A device is a process that ended with `(:ok ())`.
- `(:runtime)`: it answers at once, with `(:runtime s info)`.

Anything else is a dead letter. An `info` is a list of `(key value)`,
which `lookup` reads; more keys may come. A process's:

- `(:pid p)`, and `(:parent q)`, the process that forked it (`()` for
  the root).
- `(:kind k)`: `:task` or `:actor` (D243).
- `(:state st)`: `:running` (the one asking, unless a timer asked),
  `:ready` (in the run queue), `:recv` (waiting for a message),
  `:joining` (in `join` or `join/race`) or `:sleeping`.
- `(:joining p)`: the process it waits for in `join`, the list of those
  it races in `join/race`, or `()`.
- `(:in name)`: the function it's in, as a string: the receive function
  it waits in; the one that called `join`, `join/race`, `sleep`,
  `yield`, or `send` to a device; the one it was entering when it was
  preempted; or the one it starts in. A `recv` in the middle of a function waits in a receive
  function of its own, named for both: `"server (recv at
  prog.slight:7:12)"`. The top level is `"the top level"`, a `fork`'s
  body `"fork at prog.slight:3:9"` (a `spawn`'s, `"spawn at ..."`, and a
  `connect`'s, `"connect at ..."`),
  and a lambda `"lambda at ..."`.
- `(:mail n)`: the messages waiting for it.
- `(:heap bytes)`: its heap in use, both generations.
- `(:reductions n)`: the reductions it has made, the functions it has
  entered (under Scheduling).
- `(:stack held)`: whether it holds a stack (waiting in `recv`, it gives
  its back).
- `(:devices pids)`: the devices it owns.

The runtime's: `(:processes n)` that haven't ended; `(:pids n)` made,
processes and devices, ended or not; `(:ready n)` in the run queue;
`(:timers n)` pending; `(:devices n)` open; `(:heap bytes)` in use by
all the processes; `(:heap-max bytes)`, the most one process's heap
holds (D233); `(:stacks n)` held; `(:clock ms)` since the program
started; and `(:reductions n)`, made by every process, ended or not.

A timer's message to the device is answered when it's due, so this
shows where every process is, once a second (`examples/top.slight` is a
fuller `top`):

    (defun every-second (s)
        (after 1000 s (list :processes))
        (show s ()))

    (defun show (s ins)
        (recv
            ((:process s i) (show s (cons (lookup :in i) ins)))
            ((:end s)       (pprint (reverse ins)) (every-second s))))

    (connect :system (recv ((:open s) (every-second s))))


## Builtins

The process builtins (`send`, `join`, `monitor`, `kill`, `raise`,
`after`, `sleep`, `disconnect`) are under Processes and Devices. A
builtin given the wrong type of value faults, with the kind that says
what it wanted (`:not-a-number`, `:not-a-string`, ...).

### Numbers

Integers and floats mix: if either operand is a float, so is the result.
The arithmetic is unprefixed; the rest is under `math/`, as JavaScript
keeps it in `Math` (D221).

- `(+ a b)`, `(- a b)`, `(* a b)`: two numbers, exactly. An integer
  result that doesn't fit in 63 bits faults (`:overflow`).
- `(/ a b)`: always a float, so `(/ 6 2)` is `3.0`. Dividing by zero
  faults (`:div-by-zero`), floats included.
- `(div a b)`: integer division of two integers, truncating toward zero,
  so `(div -7 2)` is -3.
- `(% a b)`: the remainder, for integers only, with the sign of `a`:
  `(% -7 2)` is -1 (as in C, Go or JavaScript). By zero, both fault
  (`:div-by-zero`), and so does `div` of the most negative integer by
  -1 (`:overflow`).
- `(math/mod a b)`: the remainder of a floored division, with the sign
  of `b`: `(math/mod -1 10)` is 9, where `(% -1 10)` is -1, so it's the
  one for wrapping an index round. Integers only, as `%` is.
- `(math/ceil x)`, `(math/floor x)`, `(math/round x)`, `(math/trunc
  x)`: an integer. An integer stays as it is. `math/round` sends halves
  up, so `(math/round 2.5)` is 3 and `(math/round -2.5)` is -2
  (JavaScript's `Math.round`). A float too big for an integer faults
  (`:out-of-range`).
- `(math/abs x)`: keeps the type. `math/abs` of the most negative
  integer overflows.
- `(math/min a b)`, `(math/max a b)`: whichever argument it is, as it
  was given, so `(math/min 1 2.0)` is 1; the first one on a tie.
- `(math/pow a b)`: of two integers, an integer, exactly, as `*` gives
  one: past 63 bits it overflows, and a negative exponent faults
  (`:out-of-range`), since the result wouldn't be an integer. With a
  float on either side, a float.
- `(math/sqrt x)`, `(math/cbrt x)`, `(math/exp x)`, `(math/expm1 x)`,
  `(math/log x)` (natural), `(math/log2 x)`, `(math/log10 x)`,
  `(math/log1p x)`, `(math/hypot x y)`; `(math/sin x)`, `(math/cos x)`,
  `(math/tan x)`, `(math/asin x)`, `(math/acos x)`, `(math/atan x)`,
  `(math/atan2 y x)`; `(math/sinh x)`, `(math/cosh x)`, `(math/tanh x)`,
  `(math/asinh x)`, `(math/acosh x)`, `(math/atanh x)`: JavaScript's
  `Math` functions of the same names, always a float. Out of their
  domain they give `nan` or `inf` rather than faulting: `(math/sqrt -1)`
  is `nan`, and `(math/log 0)` is `-inf`. `math/expm1` and
  `math/log1p` keep their precision near 0, where `(math/exp x)` less
  one, or `(math/log (+ 1 x))`, lose it. Beside `math/sqrt`, which is
  exact everywhere, each is only as good as the platform's C library,
  which can be a bit off in the last place, and so differ between
  machines (D76).
- `(math/nan? x)`: whether `x` is the float `nan`; anything else is
  `#false`. `nan` is the one value that isn't `==` to itself.
- `math/PI`, `math/E`: 3.141592653589793 and 2.718281828459045.
  `math/INF`: the float `inf`, which `(- 0 math/INF)` makes `-inf`.
- `(int->float n)`: the float nearest the integer `n`, exactly so up to
  2^53.
- `(float->str x [digits])`: `x`, which may be an integer, as a float:
  as `str` shows one, or with `digits` (0 to 100), in fixed notation
  with that many digits after the point, as JavaScript's `toFixed`:
  `(float->str 3.14159 2)` is `"3.14"`, and `(float->str 7 2)` is
  `"7.00"`. It's the nearest such decimal to the float's exact value,
  so `(float->str 1.005 2)` is `"1.00"`, 1.005 being a little under that
  as a float; and a tie goes away from zero, so `(float->str 2.5 0)` is
  `"3"`. From 1e21 up, and `nan` and `inf`, are as `str` shows them.

### Bits

On integers, as 63 bits of two's complement, compiled inline (D221).
Anything else faults (`:not-an-int`).

- `(bit/and a b)`, `(bit/or a b)`, `(bit/xor a b)`, `(bit/not a)`.
- `(bit/shl a k)`: `a` shifted left `k` bits. Bits shifted past the top
  are lost, where `*` would fault: `(bit/shl 3 61)` keeps only the low
  63 bits, and so is negative.
- `(bit/shr a k)`: `a` shifted right `k` bits, filled with its sign, as
  JavaScript's `>>`, so it's `a` divided by 2^k, rounded down:
  `(bit/shr -7 1)` is -4.
- A count of 63 or more shifts out every bit, giving 0, or -1 for a
  negative `a` shifted right, as Go has it (JavaScript takes it mod 32
  instead). A negative count faults (`:out-of-range`).
- `(bit/count a)`: the bits set in `a`'s 63, so `(bit/count -1)` is 63.

They take a fixed number of arguments, so they can be values:
`(fold/l 0 bit/xor hashes)`.

### Random numbers

Each process has a generator of its own (D222).

- `(rand/int n)`: an integer from 0 to `n` - 1, each as likely. `n` must
  be an integer (`:not-an-int`) of at least 1 (`:out-of-range`).
- `(rand/float)`: a float from 0 up to but not including 1, in steps of
  2^-53, each as likely.
- `(rand/seed n)`: starts the calling process's generator again from
  the integer `n`, so the numbers it draws after are the same every run.
  Returns `()`.

The root's generator starts from the run's seed: `SLIGHT_SEED=n`, a
number of digits, or, without it, one the system makes up, different
every run. A `fork` gives the new process a generator drawn from its
parent's, as a number is drawn. So what a process draws depends only on
the seed and on what was drawn and forked before, in it and in the
processes it was forked from, never on the order processes run in.
Under one `SLIGHT_SEED`, a program draws the same numbers whatever
`SLIGHT_SCHED` says. And a record keeps the seed, even one the system
made up, so a replay draws them again.

    (rand/seed 2026)
    (let die (lambda () (+ 1 (rand/int 6))))
    (list (die) (die) (die))

The prelude has `rand/pick` and `rand/shuffle`. The generator is
SplitMix64, Java's `SplittableRandom`, the same numbers from the same
seed. It's fast and fair, but what it'll draw next can be worked out
from what it drew, so it's not for keys, tokens or anything that
mustn't be guessed.

### Comparison

- `(== a b)`, `(!= a b)`, `(< a b)`, `(<= a b)`, `(> a b)`, `(>= a b)`:
  two numbers, of either kind, so `(== 1 1.0)` is `#true`. Anything else
  faults (`:not-a-number`). With `nan`, only `!=` is `#true`.
- `(eq? a b)`, `(ne? a b)`: any two values, compared by structure:
  lists element by element, strings byte by byte, floats by value.
  An integer is never `eq?` to a float, so `(eq? 1 1.0)` is `#false`.
  Functions are `eq?` only to themselves, and pids to the same pid.
- `(hash v)`: a non-negative integer for any value, the same for values
  that are `eq?` (`0.0` and `-0.0` included), and the same on every run,
  target and machine, so a hash kept in a file means the same value
  later. Two values that aren't `eq?` hash alike only by chance, about
  once in 2^62: a hash can stand in for a big value where that chance
  will do, as git's names for its objects do, to keep and compare
  digests of a simulation's state, say. A symbol and a function hash by
  their names, so two closures of one `lambda` hash alike, and a pid by
  its number. It isn't seeded, so it's no help against keys picked to
  collide.

### Predicates

Each takes one value of any type, and gives `#true` or `#false`.

- `nil?`: `()`.
- `cons?`: a non-empty list.
- `sym?`: a symbol, `#true` and `#false` included.
- `bool?`: `#true` or `#false`.
- `int?`, `float?`, and `num?` for either.
- `str?`: a string.
- `tuple?`: a tuple.
- `lambda?`: a function, of any kind.
- `pid?`: a pid, a device's included.

### Lists

- `(cons x xs)`: a new list, `x` and then the elements of `xs`, which
  must be a list (`:not-a-list`).
- `(car xs)`, `(cdr xs)`: the first element, and the rest. Anything but
  a non-empty list faults (`:not-a-cons`), `()` included.
- `(cadr xs)`, `(cddr xs)`, `(caddr xs)`, ...: every combination of `a`
  and `d` up to four letters, applied right to left, so `cadr` is the
  `car` of the `cdr`.
- `(list x ...)`: a list of its arguments, any number.

The prelude has the rest (`map`, `filter`, `length`, ...).

### Tuples

A tuple is like a list that can't grow, whose elements are each one step
away: `tuple/get` takes the same time for the last element as the
first. Changing one makes a new tuple, copying the rest, so a tuple
suits a record of a few fields, a row, or a node of a tree; something
that grows an element at a time suits a list.

- `(tuple x ...)`: a tuple of its arguments, any number.
- `(tuple/make n x)`: `n` elements, each `x`. A negative `n` faults
  (`:out-of-range`).
- `(tuple/len t)`: how many elements.
- `(tuple/get t i)`: the element at index `i`, counting from 0. An index
  that isn't an integer faults (`:not-an-int`), and so does one out of
  range (`:out-of-range`).
- `(tuple/set t i x)`: a new tuple, `t` with `x` at index `i`; `t` is as
  it was. The index is checked as `tuple/get` checks it.
- `(tuple/insert t i x)`: a new tuple one longer, `x` at index `i` and
  the elements that were there from `i` on after it. `i` can be the
  length, putting `x` at the end; anything past it faults
  (`:out-of-range`).
- `(tuple/remove t i)`: a new tuple one shorter, without the element at
  index `i`, which is checked as `tuple/get` checks it.
- `(tuple/slice t start end)`: a new tuple of the elements from `start`
  up to but not including `end`, each clamped to the tuple; empty when
  `start` isn't before `end`.
- `(list->tuple xs)`, `(tuple->list t)`.

Anything but a tuple faults (`:not-a-tuple`). `eq?` compares tuples
element by element, and a tuple is never `eq?` to a list.

    (let p (tuple 3 4))
    (tuple/get p 1)                      4
    (tuple/set p 0 5)                    #(5 4)
    (match p (#(x y) (+ x y)))           7

### Strings

A string is immutable bytes, UTF-8 by convention (D219). The `str/`
builtins count characters. A character is a well-formed UTF-8 sequence,
or, failing that, a stray byte on its own, whose code point is then
65533 (U+FFFD), as in Go (D148): `(str/len "héllo")` is 5, an emoji
counts 1, nothing faults on bad bytes, and splitting a string into
characters loses nothing. The `bytes/` builtins count bytes, for
protocols and parsers: `(bytes/len "héllo")` is 6. Where a builtin's
behavior isn't obvious, it's that of JavaScript's string method of the
same name, with characters in place of UTF-16 units. A string whose
bytes are all below 128 is marked so when it's made, and there a
character is a byte, so the `str/` builtins index it in one step;
elsewhere they walk from the start.

Making strings:

- `(str x ...)`: any number of values of any type, joined: a string as
  its bytes, anything else as `pprint` shows it. `(str "n=" 5)` is
  `"n=5"`, and `(str)` is `""`.
- `(~ s ...)`: strings, and only strings, joined.
- `(str/join sep xs)`: the elements of the list `xs`, rendered as `str`
  renders them, with `sep` between.
- `(str/repeat s n)`: `s`, `n` times. A negative `n` faults
  (`:out-of-range`).
- `(str/pad-start s n [fill])`, `(str/pad-end s n [fill])`: `s` padded
  to `n` characters with `fill` (`" "` if it isn't given), repeated and
  cut to fit: `(str/pad-start (str 7) 3 "0")` is `"007"`. If `s` has `n`
  characters already, or `fill` is empty, it's `s`.

Characters by index, which counts from 0; where it says so, a negative
one counts from the end, -1 being the last character:

- `(str/len s)`: the length, in characters.
- `(str/at s i)`: the character at `i`, as a one-character string;
  negative from the end. Past either end faults (`:out-of-range`).
- `(str/code-at s i)`: the code point of the character at `i`, as
  `str/at` finds it.
- `(str/slice s start [end])`: the characters from `start` up to but
  not including `end`, or to the end if it isn't given. Either counts
  from the end when it's negative, each is clamped to the string, and
  the slice is empty unless `start` comes before `end`:
  `(str/slice "hello" 1 -1)` is `"ell"`, and `(str/slice s -3)` is the
  last three characters.
- `(str/chars s)`: the characters, as one-character strings.
  `(str/join "" (str/chars s))` is `s` again.
- `(str/codes s)`: the characters' code points.
- `(codes->str xs)`: the string of the code points in the list `xs`. A
  surrogate, or anything past 1114111 (U+10FFFF), faults
  (`:out-of-range`).

Searching. A match is whole characters: it starts and ends where
characters do, which only a stray byte in what's looked for can make a
difference to: a stray byte 195 isn't found in `"é"`, though 195 is the
first of its bytes.

- `(str/index-of s m [from])`: the character index of the first `m` at
  or after `from` (0 if it isn't given; clamped to the string), or -1.
  An empty `m` is at `from`.
- `(str/last-index-of s m [from])`: the index of the last `m` that
  starts at or before `from` (the end if it isn't given), or -1.
- `(str/includes? s m)`, `(str/starts-with? s m)`,
  `(str/ends-with? s m)`.
- `(str/split s sep [limit])`: the pieces of `s` between the `sep`s:
  `(str/split "a,b,,c" ",")` is `("a" "b" "" "c")`, and
  `(str/split "" ",")` is `("")`. An empty `sep` splits into characters.
  With a `limit`, only the first that many pieces; a negative one
  faults (`:out-of-range`).
- `(str/replace s m r)`: `s` with its first `m` made `r`;
  `(str/replace-all s m r)`, with every one. An empty `m` is at the
  start, or, for `str/replace-all`, before each character and at the
  end: `(str/replace-all "ab" "" "-")` is `"-a-b-"`.

Changing:

- `(str/trim s)`, `(str/trim-start s)`, `(str/trim-end s)`: `s` without
  the white space at both ends, at its start, or at its end. White
  space is Unicode's (White_Space), the no-break space and the
  ideographic space among it.
- `(str/upper s)`, `(str/lower s)`: each character mapped to its upper
  or lower case in full, as Unicode does it without a language:
  `(str/upper "straße")` is `"STRASSE"`, and a capital sigma that ends a
  word is a final sigma in lowercase, so `(str/lower "ΟΔΟΣ")` is
  `"οδος"`. A stray byte stays as it is. The mappings are Unicode
  15.1's, made into `runtime/unicode.c` by `runtime/gen-unicode.py`.
- `(str/well-formed? s)`: whether `s` has no stray bytes.
- `(str/to-well-formed s)`: `s` with each stray byte made U+FFFD.

Comparing. `eq?` says whether two strings are the same bytes.

- `(str/compare a b)`: -1, 0 or 1, as `a` comes before `b`, is the
  same, or comes after, in the order of their code points, which for
  UTF-8 is the order of their bytes; a string comes before the longer
  ones it starts. Stray bytes compare as the bytes they are.
- `(str/< a b)`: whether `a` comes before `b`, so `(sort str/< xs)`
  sorts strings.

Converting:

- `(str->int s [radix])`: the integer, if `s` is digits in `radix` (2
  to 36, or 10 if it isn't given; past 9, letters in either case), maybe
  with a `-` in front, and fits in 63 bits; otherwise `#false`.
  `(str->int "ff" 16)` is 255.
- `(int->str n [radix])`: `n` written in `radix`, its letters lowercase:
  `(int->str 255 16)` is `"ff"`. In either, a radix outside 2 to 36
  faults (`:out-of-range`).
- `(str->float s)`: the nearest float, if `s` is digits, maybe a `-` in
  front, maybe a point and more digits, and maybe an exponent; so
  `"1.5"`, `"-2"`, `"1e3"` and `"2.5E-3"`, but not `".5"`, `"1."` or
  `"inf"`. Otherwise, or if it's too big for a float, `#false`.
- `(sym->str sym)`: the symbol's name, without a colon.
- `(str->sym s)`: the symbol with that name, if the program mentions it
  anywhere (as `:name` or `'name`); otherwise `#false`, since symbols
  are made only by the compiler.
- `(str->bytes s)`: the bytes, as a list of integers.
- `(bytes->str xs)`: a list of integers 0 to 255, as a string.

Bytes, for what's made of them: protocols, and parsers whose delimiters
are ASCII, where a byte's index serves as well as a character's and is
found in one step:

- `(bytes/len s)`: the length, in bytes.
- `(bytes/at s i)`: the byte at `i`, as an integer; negative from the
  end. Past either end faults (`:out-of-range`).
- `(bytes/slice s start [end])`: bytes, as `str/slice` takes
  characters.
- `(bytes/index-of s m [from])`: the byte index of the first `m` at or
  after `from` (clamped), or -1.

There's no character type: a character is a one-character string, or a
code point. Build a long string by collecting the pieces in a list and
joining them.

### JSON

JSON in slight: `null` is `:null`, `true` and `false` are `#true` and
`#false`, a number is an integer if it's written as one and fits in 63
bits and a float otherwise, a string is a string, an array is a list,
and an object is `(:object (key value) ...)`, its keys strings, in
order, duplicates kept. `{"a": [1, 2.5], "b": null}` is

    (:object ("a" (1 2.5)) ("b" :null))

- `(json/parse s)`: `(:ok value)` for the JSON text `s`, or
  `(:error (:bad-json at))`, `at` being where it went wrong: the first
  byte no JSON text could have there, or the end of one that stops too
  soon, so `(json/parse "[1,]")` is `(:error (:bad-json 3))`. A `\u`
  escape of a lone surrogate gives U+FFFD, and bytes that aren't UTF-8
  pass through.
- `(json/print v)`: `v` as compact JSON, on one line, so what it gives
  is NDJSON as it is: `(json/print (list 1 "a" :null))` is
  `"[1,\"a\",null]"`. A tuple is an array too. A float keeps its point or exponent, and a string
  escapes `"`, `\` and the control characters. Anything JSON can't
  hold faults (`:not-json`): any other symbol, a pid, a function, `nan`
  or `inf`, or an object's member that isn't `(key value)` with a string
  key.

A file or socket can be read as JSON too: `(:read :json)`, under Files.

### S-expressions

Data written as slight is written, so two programs can talk in printed
forms, and a program can read slight's own source:

- `(sexp/parse s)`: `(:ok datum)` for the text `s`, or
  `(:error (:bad-sexp at))`, `at` being where it went wrong, as for
  `json/parse`. The text is what the compiler reads: lists, integers and
  floats, strings with the same escapes, symbols and keywords, `#true`
  and `#false`, `'x` as `(quote x)`, `#(...)` as a tuple (which in a
  program is a pattern, D176), comments, and doc blocks, which it
  skips. So, as in a file, a line that starts with `=` and a letter is a
  doc block's, or an error: `(sexp/parse "=x")` is `(:error (:bad-sexp
  1))`, but `" =x"` and `"(=x)"` read `=x` as a name. And `:a` reads as
  the symbol `a`, as `a` does; a symbol the program mentions (as `:name`
  or `'name`) reads as itself, and any other as `(:symbol "name")`,
  since symbols are made only by the compiler. An integer past 63 bits
  reads as a float.

      (sexp/parse "(alpha :beta 'zebra 1.5)")
      ; (:ok (alpha beta (quote (symbol "zebra")) 1.5)), if the
      ; program mentions alpha and beta but not zebra

- `(sexp/print v)`: `v` as `sexp/parse` reads it back, on one line.
  A string escapes `"`, `\` and the control characters; a float always
  has digits on both sides of its point (`1.0e+21`); a symbol is its
  name, without a colon; `(:symbol "name")` is `name`; a tuple is
  `#(...)`. Anything that isn't data faults (`:not-sexp`): a pid, a
  function, `nan` or `inf`. `pprint` quotes a string the same way.

A file or socket can be read as s-expressions too: `(:read :sexp)`,
under Files.

### Output

- `(pprint x)`: writes `x` as the program shows values (strings in
  quotes and escaped, symbols without their colon), and a newline, to
  stdout. Returns `()`.
- `(tty/write x ...)`: writes its arguments to stdout, rendered as
  `str` renders them (strings as their bytes), and flushes. Returns
  `()`. `(tty/write "\e[2J")` clears a terminal.
- `(tty/screen/rows)`, `(tty/screen/cols)`: the terminal's size, asked
  each time; 24 and 80 when stdout isn't a terminal.

### Functions

- `(apply f xs)`: calls `f` with the elements of `xs`, at most 8.
- `lambda?`, under Predicates.


## The prelude

`lib/prelude.slight`, compiled into every program. It's slight, and every
loop in it is a tail call, so long lists are fine.

Numbers and booleans:

- `(inc n)`, `(dec n)`: `n` plus or minus 1.
- `(not b)`: on booleans only.

Folds:

- `(fold/l acc f xs)`: calls `(f acc x)` for each `x`, from the left,
  each result being the next `acc`, and gives the last.
- `(fold/r acc f xs)`: the same from the right, calling `(f x acc)`.

Lists:

- `(reverse xs)`, `(length xs)`, `(append xs ys)`.
- `(sum xs)`, `(product xs)`.
- `(map f xs)`: `(f x)` for each `x`, as a list.
- `(filter f xs)`: the elements for which `(f x)` is `#true`.
- `(remove f xs)`: the elements for which `(f x)` is `#false`.
- `(take n xs)`: the first `n` elements, or all of them if there are
  fewer.
- `(skip n xs)`: all but the first `n`, or `()` if there are fewer.
- `(nth i xs)`: the element at index `i`, counting from 0, or `()` if
  there isn't one.
- `(find f xs)`: the first element for which `(f x)` is `#true`, or
  `()`.
- `(member? x xs)`: whether `x` is `eq?` to an element.
- `(range start end)`: the integers from `start` up to but not including
  `end`: `(range 0 3)` is `(0 1 2)`.
- `(dotimes start end f)`: calls `(f i)` for each `i` in
  `(range start end)`, for its effects. Returns `()`.
- `(zip xs ys)`: `(x y)` for each `x` and the `y` in the same place, as
  long as the shorter list: `(zip '(a b c) '(1 2))` is `((a 1) (b 2))`.
  So `(zip keys values)` is an association list, and
  `(zip (range 0 (length xs)) xs)` numbers the elements.

Sorting:

- `(sort less? xs)`: the elements of `xs` in order, `a` before `b` when
  `(less? a b)` is `#true`: `(sort < '(3 1 2))` is `(1 2 3)`, and
  `(sort > xs)` is descending. It's stable: elements neither of which is
  less than the other keep their order. A merge sort, so it calls
  `less?` about n log n times.
- `(sort-by key xs)`: the elements in the order of `(key x)`, which must
  be a number, ascending, and stable too. `key` is called once for each
  element. `(sort-by car pairs)` sorts pairs by their first element.

`(sort str/< xs)` sorts strings.

Association lists, `((key value) ...)`:

- `(assoc k v table)`: `table` with `(k v)` added at the front.
- `(lookup k table)`: the value for the first key `eq?` to `k`, or
  `:not-found`.

Random numbers (D222):

- `(rand/pick xs)`: an element of `xs`, each as likely, or `()` if
  there are none.
- `(rand/shuffle xs)`: the elements of `xs` in an order each of whose
  arrangements is as likely.

Processes: `join/any` and `join/all`, under Waiting for a process.

Its helpers (`fold/r-loop`, `reverse-onto`, `length-loop`, `sum-loop`,
`product-loop`, `map-loop`, `filter-loop`, `remove-loop`, `take-loop`,
`range-loop`, `zip-loop`, `sort-runs`, `sort-pairs`, `sort-merge`,
`join/any-loop` and `join/all-loop`) are visible too, but not meant to
be called. The string functions are
builtins (Strings, under Builtins).


## Libraries

Opt-in: a program that wants one includes it.

### (@include :test)

`lib/test.slight`, a library for tests that print TAP.

    (@include :test)

    (run-tests (list
        (ok (< 1 2) "one is less than two")
        (is (+ 1 1) 2 "addition")
        (diag "a comment")))

prints

    ok 1 - one is less than two
    ok 2 - addition
    # a comment
    1..2

- `(ok test msg)`: passes if `test` is `#true`.
- `(is got expected msg)`: passes if `got` and `expected` are `eq?`. If
  not, it shows both.
- `(diag msg)`: a comment line.
- `(run-tests tests)`: runs a list of them in order, prints the plan
  (`1..n`) at the end, and a line saying how many failed if any did.
  Returns `#true` if all passed.

A test is a function of the counts so far, `(n failed)`, that prints its
line and returns the new counts. The expressions inside `ok` and `is`
are evaluated when the list is built, before `run-tests` prints
anything.

### (@include :fs)

`lib/fs.slight`: whole files at once, on top of `connect`. Each blocks
only its caller.

- `(slurp path)`: `(:ok lines)`, the file's lines without their
  newlines, as a list of strings.
- `(spew path lines)`: writes each line followed by a newline, replacing
  what was in the file. `(:ok ())`.

Either gives `(:error (name path))` if the file can't be opened, read or
written: `(:error (enoent "notes.txt"))`, say.

### (@include :map)

`lib/map.slight`: a persistent map, a hash trie as Clojure's maps are
(D223). A map is a value, as a list is: a change gives a new map, which
shares all but the path it changed with the old one, and the old one is
as it was. Keys are compared with `eq?` and found by their `hash`, so any
value can be a key, and finding one takes a few steps however many there
are. It's for a process's own data; sending a map copies it whole, as
any value is copied.

    (@include :map)

    (let m (map/put (map/put (map/new) "a" 1) "b" 2))
    (map/get m "a")                      1
    (map/get (map/delete m "a") "a")     not-found
    (map->list m)                        (("a" 1) ("b" 2))

- `(map/new)`: a map with nothing in it.
- `(map/get m k)`: `k`'s value, or `:not-found`. `(map/has? m k)`:
  whether `k` has one, for when a value could be `:not-found` itself.
- `(map/put m k v)`: `m` with `v` as `k`'s value, in place of any it had.
- `(map/delete m k)`: `m` without `k`, or `m` itself if it hasn't `k`.
- `(map/update m k f default)`: `m` with `(f v)` as `k`'s value, or
  `(f default)` if it has none, as `dict/update` (under `:ds`).
- `(map/size m)`: how many keys, at once.
- `(map/fold acc f m)`: calls `(f acc k v)` for each entry, each result
  being the next `acc`, as `fold/l`, and gives the last.
- `(map->list m)`: the entries, `((k v) ...)`, an association list.
- `(list->map xs)`: a map of an association list's entries; for a key
  that's there more than once, its first value, as `lookup` gives.

`map/fold` and `map->list` take the entries in an order that depends
only on the keys' hashes: the same every run and on every machine, but
neither the order they were put in nor any order of the keys. Two maps
with the same entries are `eq?`, whatever was put and deleted on the way.
A map is a tuple, so `tuple?` says `#true` of one, and `pprint` shows the
trie; `map->list` is the way to show it. A `nan` key is never found again,
since `nan` isn't `eq?` to itself.

Its helpers (`map/get-hashed`, `map/put-hashed`, `map/delete-hashed`,
which take the key's hash, for tests; `map/bit`, `map/slot`,
`map/bucket-index`, `map/leaf`, `map/put-at`, `map/pair`,
`map/delete-at`, `map/collapse`, `map/walk` and `map/walk-slots`) are
visible too, but not meant to be called.

### (@include :ds)

`lib/ds.slight`: data structures as processes, state that changes made
of values that don't. Each structure is a process holding its state in
the arguments of its receive function, so it takes one message at a
time: no races, and an update is atomic. Writes are sends, and return
at once; reads wait for the answer. Everything goes across by copying,
so ask for pieces, not the whole of something big. A structure lasts
until it's killed.

    (@include :ds)

    (let d (dict/new))
    (dict/put d "name" "slight")
    (dict/update d "count" inc 0)
    (dict/get d "count")                 1

- A cell, one value: `(cell/new v)`, `(cell/get c)`, `(cell/set c v)`,
  `(cell/update c f)` (the value becomes `(f value)`, in the cell, so if
  `f` faults the cell ends), and `(cell/swap c f)`, which updates and
  gives the new value.
- A dictionary, keys compared with `eq?`: `(dict/new)`, `(dict/put d k
  v)`, `(dict/get d k)` (or `:not-found`), `(dict/delete d k)`,
  `(dict/update d k f default)` (`(f default)` when `k` has no value
  yet), `(dict/keys d)` (most recently put first), `(dict/size d)`. It's
  an association list, so a lookup costs the number of keys.
- A queue: `(queue/new)`, `(queue/push q x)`, `(queue/pop q)` (the oldest
  item, or `:empty`), `(queue/size q)`.
- A channel: `(channel/new)`, `(channel/put ch x)`, and `(channel/take
  ch)`, which waits until there's an item.

For structures of one's own: `(ask pid msg)`, in the prelude (under
Addresses), sends `msg` with an address for the answer added at the end,
and reads the answer; the structure answers with `(reply to v)`, `to`
being that address. A task asks; an actor can't.

    (defun counter (n)
        (recv
            (:inc      (counter (+ n 1)))
            ((:get to) (reply to n) (counter n))))

    (let k (spawn (counter 0)))
    (send k :inc)
    (ask k (list :get))                  1

If the structure ends before it answers, `ask` raises `(:ended pid
result)`. `(ask-within ms pid msg)` raises `(:timeout pid)` too, if no
answer comes in time.

### (@include :http)

`lib/http.slight`: HTTP/1.1 on `:tcp`, a server and a client (D173). One
request and one reply to each connection, which then closes. HTTPS
waits for TLS (step 12c).

    (@include :http)

    (defun handler (req)
        (list 200 (list (list "Content-Type" "text/plain"))
            (str "you asked for " (lookup :path req))))

    (let server (http/serve 8080 handler))    ; the listener's pid
    (http/get "http://localhost:8080/hello")  ; (:ok (200 headers body))
    (kill server)

- `(http/serve port handler)` listens on all interfaces and gives the
  listener's pid, which `kill` stops. Port 0 lets the system pick, and
  the listener sends `(:listening port)` to the caller, which is how a
  test learns the port. Each connection gets a process of its own, which
  calls `handler` with the request and writes the response it gives. The
  handler runs in a process of its own too: if it faults, or raises, the
  client gets a 500.
- A request is a keyed list, read with `lookup`: `(:method m)`, `(:path
  p)`, `(:query q)` (`""` when there's none), `(:headers hs)` and `(:body
  b)`. A method is a keyword for GET, HEAD, POST, PUT and DELETE, and a
  string otherwise. Headers are `(name value)` pairs, the names in
  lowercase, in the order they came, repeats kept.
- A response is `(status headers body)`, its headers `(name value)` pairs.
  The library adds Content-Length and Connection: close, so a handler
  leaves them out. The reason phrase comes from a table of the common
  statuses.
- `(http/get url)` and `(http/request method url headers body)` (`""` for
  no body) give `(:ok response)`, the response in the same shape, or
  `(:error reason)`. A reason is the device's, such as `(:econnrefused
  "127.0.0.1:1")`, or one of `(:bad-http what)` for a reply that isn't
  HTTP, `(:bad-url url)`, `:no-tls` for an `https` URL, and `:closed` for
  a connection that closed before its reply. They wait as long as the
  reply takes (D236).
- `(http/request-within ms method url headers body)` is `http/request`
  with `(:error :timeout)` if the reply hasn't all come in `ms`
  milliseconds. Its timer doesn't hold up the program's end once the
  reply has come (D232).
- What a server refuses: a start line that isn't HTTP is a 400, a head
  over 64 KB a 431, and a body over 8 MB a 413, which is answered before
  the body is read. Chunked bodies are read both ways. A reply with no
  length is read to the end of the connection, except for HEAD, 1xx, 204
  and 304, which have no body. `Expect: 100-continue` gets its 100 at
  once.
- `(http/stream method url headers body how)` makes the same request, and
  gives the reply to the caller as it comes, in messages, as a device
  gives a file (D181, D182). It gives the stream's pid, `s`, at once. The
  caller is sent `(:head s status headers)`; then the body, as `(:line s
  text)` for each line when `how` is `:lines`, or `(:chunk s bytes)` when
  it's `:chunks`; then `(:eof s)`. If anything fails, `(:error s reason)`
  comes in place of whatever hadn't come yet, with `http/request`'s
  reasons, or `:line-too-big` for a line over 64 KB. Chunked encoding is
  undone on the way, so the pieces are the body's own bytes. A line is
  cut at `\n`, and one `\r` at its end is dropped, as in the head; an
  empty line is `""`, and a last line with no newline still comes. A
  chunk is never empty, but its size isn't promised: it's what the socket
  gave. A reply with no body is its head and then `(:eof s)`.
- `(kill s)` stops a stream and closes its connection; the caller hears
  nothing more from it, though what it sent before is still in the
  mailbox. A stream whose caller ends is killed too. A timeout is the
  caller's own `after`.

  ollama streams a model's answer as JSON lines, each with a piece of the
  text:

      (defun answer (text)
          (recv
              ((:head _ _ _)     (answer text))
              ((:line _ line)
                  (match (json/parse line)
                      ((:ok (:object & fields)) (answer (~ text (lookup "response" fields))))))
              ((:eof _)          text)
              ((:error _ reason) (list :error reason))))

      (http/stream :post "http://localhost:11434/api/generate" ()
          "{\"model\": \"llama3.2\", \"prompt\": \"Why is the sky blue?\"}" :lines)
      (answer "")

- Not yet: keep-alive, a Date header (slight has no calendar time), and
  HTTPS. A server that answers a request it hasn't fully read (a 431 or
  413) may have its connection reset before the client reads the reply,
  since there's no lingering close yet.

### (@include :agent)

`lib/agent.slight`: a conversation with a model, as a process, which
can call tools (D184, D187). The model is ollama's `/api/chat`, over
plain HTTP; the agent includes `:http` itself.

    (@include :agent)

    (let a (agent/new "http://localhost:11434" "llama3.2" "" ()))
    (agent/say a "Why is the sky blue?")
    ; the caller hears (:piece a text) ..., then (:answer a text)

- `(agent/new url model system tools)` starts an agent and gives its
  pid. `url` is the server's, `"http://host:port"`; `system` is the
  system prompt, `""` for none; `tools` is a list of tools (below), `()`
  for none.
- `(agent/say a text)` asks `a` a question, and returns at once. The
  caller is sent `(:piece a text)` for each piece of the answer as the
  model streams it, then `(:answer a text)`, the whole answer. If the
  turn fails, `(:error a reason)` comes in place of what hadn't come: the
  reasons `http/stream` gives, such as `(:econnrefused "localhost:11434")`;
  `(:http status message)` for a reply that isn't a 200, the message
  being the server's (ollama's 404 says the model isn't there); or
  `(:model message)` for an error in the middle of an answer.
- The agent's state is the conversation, which goes to the model with
  each question. A question and its answer join it once the answer is
  whole, so a turn that fails leaves it as it was.
- One turn at a time: a question asked while another is answered waits
  for it, and its answer comes after.
- `(agent/ask a text)` asks `a` and waits for the whole answer: `(:ok
  answer)`, or `(:error reason)` as `(:error a reason)` would have said,
  or `(:error (:ended a result))` if `a` has ended or ends first. A task
  of its own asks, at an address of its own (D243), so none of the
  pieces or calls reach the caller, and it works anywhere, a lambda, a
  tool or an actor included (D192).
- `(kill a)` ends the agent, and stops an answer on its way, and any
  tool it was running.

A tool is a function the model can call, with a name, a description,
and its parameters, which the model reads to know when to call it and
what with:

    (let weather
        (tool/new "get_weather" "The weather in a city, now."
            (list (list "city" :string "The city, such as Tokyo"))
            (lambda (args) (str "11 degrees in " (lookup "city" args)))))

    (let a (agent/new url "llama3.2" "" (list weather)))
    (agent/say a "What's the weather in Tokyo?")
    ; the caller hears (:calling a "get_weather" (("city" "Tokyo"))),
    ; (:called a "get_weather" "11 degrees in Tokyo"), then the answer

- `(tool/new name description params f)` makes a tool. `params` is a
  list of `(name type description)`, `type` being `:string`, `:number`,
  `:integer` or `:boolean`, every one required (anything else faults
  here). `f` is a function of the arguments the model gives, as a list
  of `(name value)` that `lookup` reads; nothing checks them against the
  parameters, so a tool that finds one missing or wrong can raise, and
  the model is told why.
- When the model calls tools, each call runs in a process of its own,
  all at once. The caller hears `(:calling a name args)` for each call,
  then `(:called a name result)` for each, in the calls' order, once all
  have finished. The result is `f`'s value, rendered as `str` renders
  it; or `"error: reason"` if it raised, faulted or was killed, or for a
  tool the agent doesn't have. A result longer than 16 KB is cut there,
  with `[cut: n more bytes]` at its end. The results go back to the
  model, and the turn goes on until the model answers without calling
  any: `(:answer a text)` is the text of that last reply, though the
  pieces of the replies before it came too.
- `(tool/name tool)` is a tool's name, and `(tool/call tool args)` calls
  it here, as the model would, and gives its value or raises as it does:
  for a test, or a tool built on another.
- A turn has at most 10 rounds with the model; one still calling tools
  in its tenth fails with `(:rounds 10)`. A call has no time limit: a
  tool that might hang sets its own, with `after`.
- The calls and their results join the conversation with the question
  and the answer, once the answer is whole.

`examples/chat.slight` is a chat in the terminal, a line at a time, on
`:keypress`: `chat [model] [url]`. Its model has `lib/tools.slight`'s
tools (below); those that change something do it only once you've said
`y` to what they show you, asking the keyboard's process with `ask`, and
Escape stops those still running (D193). Its notes are kept in `~/.slight-chat-notes`, and with `CHAT_EXPERT` set to
another model, it can ask an agent on that one (`ask_expert`).

### (@include :tools)

`lib/tools.slight`: tools for an agent, to read, find and change files,
run commands, fetch from the web, work out arithmetic, keep notes from
one conversation to the next, and ask other agents (D188–D192). It includes `:agent`,
`:calc`, `:ds`, `:exec` and `:fs` itself.

    (@include :tools)

    (let approve (lambda (what) #true))           ; yes to everything; the chat asks you
    (agent/new url model "" (list (tools/read-file) (tools/list-files) (tools/search) (tools/calc)
                                  (tools/write-file approve) (tools/edit-file approve)
                                  (tools/run approve) (tools/fetch approve)))

Each function makes a tool. Those that change something take `approve`,
a function of what the tool is about to do, as text for a person to
read, made printable first (`tools/printable`, below), so it shows what
will be done whatever the model put in it; the tool does it only if
`approve` gives `#true`, and otherwise tells the model it wasn't let. A tool that can't do what it's asked
raises why, and the model is told that too, as is one called without an
argument it needs, or with one that isn't a string.

- `(tools/read-file)`: `read_file(path)`, the file's bytes as they are.
- `(tools/list-files)`: `list_files(path)`, what's in a directory, one
  name a line, a directory's ending in `/` (`ls -1Ap`); or "the
  directory is empty".
- `(tools/search)`: `search(text, path)`, the lines that contain `text`
  in the file, or in every file under the directory, as
  `file:line:text` (`grep -rnIF`: the text as it is, not a pattern; no
  binary files, and no `.git`); or "no lines contain it".
- `(tools/calc)`: `calc(expression)`, arithmetic as `calc/eval` (below)
  works it out, for a model, which is bad at it.
- `(tools/remember store)` and `(tools/recall store)`: `remember(key,
  text)` keeps a note, replacing one under the same key, and
  `recall(key)` gives it back; for a key with no note, the keys there
  are. They don't ask: the notes are the model's own, in a file the
  program chose. `store` is a store of notes:
  - `(notes/open path)` starts one, a process, which reads its notes
    from the file at `path` (none if it isn't there, or is empty) and
    writes them all back after each change, as an s-expression, a list
    of `(key text)`, newest first. A file that isn't notes ends the
    store, rather than be written over.
  - `(notes/get store key)`, the text or `:not-found`; `(notes/keys
    store)`, newest first; `(notes/put store key text)`, which raises
    why if the file can't be written, the store going on as it was.
- `(tools/ask-agent name description a)`: a tool called `name` that
  asks the agent `a` a question (`question`), with `agent/ask`, and
  gives its answer: an agent on a bigger model, say, or with a system
  prompt of its own. `description` tells the model what it's for. It
  doesn't ask `approve`: `a`'s own tools ask about what they do. An agent
  mustn't be given itself, since it would wait for itself.
- `(tools/write-file approve)`: `write_file(path, content)`, the whole
  file, made if it isn't there. `approve` is shown the content, each
  line after `+ `.
- `(tools/edit-file approve)`: `edit_file(path, old, new)`, `old`
  replaced with `new`. `old` must be in the file once, exactly as it is
  there, or the tool raises, before asking; the rest of the file is kept
  as it was, its last newline included. `approve` is shown the change,
  `old`'s lines after `- ` and `new`'s after `+ `.
- `(tools/run approve)`: `run(command)`, a shell command (`sh -c`), run
  to its end, and what it printed and its exit status; one still running
  after 2 minutes is killed, and the model gets what it printed and
  `[stopped after 120 s, still running]` (D234). `(tools/run-within ms
  approve)` sets another limit. The tool's description tells the model
  its limit. `approve` is shown `run: command`.
- `(tools/fetch approve)`: `fetch(url)`, an `http://` or `https://` URL
  (anything else raises), with curl, for at most 20 seconds and 5 MB.
  `approve` is shown `fetch url`, every time, since a URL can carry
  anything out. A redirect isn't followed: the model is told where it
  goes, `HTTP 302: a redirect to http://...`, and fetching that is
  asked about in turn, so a page can't send a fetch somewhere the person
  didn't agree to (D205). A web page comes as plain text, by
  `tools/html->text` (below), its links resolved against its URL; other
  text (JSON, XML, plain text) as it is; anything else as what it is,
  `not text: image/png, 5120 bytes`. Any other status that isn't 2xx
  comes first, `HTTP 404`, then the text. If curl fails, the tool raises
  with curl's message, such as `curl: (6) Could not resolve host:
  example.invalid`.
- `(tools/printable text)`: `text` with what a terminal would take as a
  command, or would show in another order, written as an escape a person
  can read (D205): control characters but newline and tab, DEL and the
  C1 controls as `\u{1b}`, Unicode's bidirectional embeddings,
  overrides and isolates (U+202A to U+202E, U+2066 to U+2069) the same,
  and a byte that isn't UTF-8 as `\x9b`. So `(tools/printable "a\e[8mb")`
  is `"a\u{1b}[8mb"`. What it gives is printable already. The chat
  writes everything that comes from the model through it.
- `(tools/html->text html base)`: HTML as plain text, for a model.
  Comments, scripts, styles, SVG and `noscript` go, with what's in them;
  other tags go, but one that starts a block (a paragraph, a heading, a
  row, a break, ...) starts a line, a list's item starts one with `- `,
  and a table's cell starts with ` | `; entities are decoded (the named
  ones for `&`, `<`, `>`, quotes, a space, dashes and the like, and
  numbered ones); and space is squeezed, with at most one blank line in a
  row. A link is `[text](url)`, its text on one line, and its URL
  resolved against `base`, the page's URL (`""` leaves it as written):
  `<a href="next">Next</a>` on `https://example.com/docs/a.html` is
  `[Next](https://example.com/docs/next)`. A link to a place on the same
  page (`#top`), by a scheme a fetch can't follow (`mailto:`,
  `javascript:`), or with no text (an image's) is just its text.

The commands these run are given their arguments directly, with no
shell (but `run`'s), so a path or a text can't be taken for anything
else.

### (@include :calc)

`lib/calc.slight`: arithmetic, written as people write it, worked out
(D190).

    (@include :calc)

    (calc/eval "(2 + 3) * 4 ^ 2 / 8")      ; 10
    (calc/eval "7 / 2")                     ; 3.5

- `(calc/eval s)` gives the value of the expression `s`: numbers
  (`42`, `3.5`, `.5`, `1.5e3`), `+ - * / %` and `^` (or `**`) for a
  power, and parentheses. `^` binds first, and from the right (`2^3^2`
  is 512); then a sign (`-2^2` is -4); then `*`, `/` and `%`; then `+`
  and `-`. `×`, `÷` and `−` are `*`, `/` and `-`.
- The functions `sqrt`, `abs`, `floor`, `ceil`, `round`, `sin`, `cos`,
  `tan`, `exp`, and `min` and `max` of one number or more; the
  constants `pi` and `e`. Names in any case.
- Whole numbers stay whole while the answer fits well inside 62 bits
  (`2^61` is exact), and a division that comes out whole is whole (`6/2`
  is 3); anything else is a float (`7/2` is 3.5, `2^62` is
  4611686018427388000.0), rather than an overflow. `%` keeps the sign of
  its left side, as `%` does, and works on floats too.
- An expression that can't be read or worked out raises why, and the
  byte where: `"at byte 3: 3 where an operator or the end should be"`,
  `"division by zero"`, `"at byte 1: there's no log: ..."`.

### (@include :exec)

`lib/exec.slight`: a command run to its end (D187).

    (@include :exec)

    (exec/run (list "sh" "-c" "echo hi; exit 3"))    ; (:ok (3 "hi\n"))

- `(exec/run cmd)` runs `cmd`, a list of strings as `:exec` takes it,
  with its input closed, and waits for it to end. It gives `(:ok (status
  output))`, `output` being all it printed, stdout and stderr together,
  and `status` its exit code or `(:signal name)`; or `(:error reason)` if
  it can't be started, such as `(:error (:enoent "ls"))`. There's no time
  limit: a command that doesn't end keeps it waiting. If the caller ends
  first, killed, say, the command is killed too (D193).
- `(exec/run-within ms cmd)` is `exec/run`, but a command still running
  after `ms` milliseconds is killed, its process group with it, and its
  status is `(:timeout ms)`, its output what it printed till then (D234).
  On the virtual clock, time stands still while a command runs (D185),
  so there it has no limit.

### (@include :rpc)

`lib/rpc.slight`: calls between slight programs, and people, over a
connection (D225). The protocol is Matt S Trout's stupidrpc, finished,
in s-expressions, so a person can take part by typing the messages,
with `nc`.

    (@include :rpc)

    (let add (rpc/handler "add" "The sum of two integers."
        (list (list "a" :integer "One") (list "b" :integer "The other"))
        (lambda (a b) (+ a b))))
    (rpc/serve "127.0.0.1:7317" (list add))     ; the listener's pid

    (match (rpc/connect "localhost:7317" ())
        ((:ok conn) (rpc/call conn "add" (list 2 3))))     ; (:ok 5)

The library keeps the ids: a program makes calls and answers them, and
never sees a message. A connection is a process, `conn`, which owns the
socket. Either end can call the other, whichever connected.

- `(rpc/handler name description params f)` is something the other end
  can call: `name` a string, `description` what it does, `params` a list
  of `(name type description)`, at most 8, and `f` a function of the
  arguments, in that order. `type` is `:string`, `:integer`, `:number`,
  `:boolean` or `:any`, and a `:string` takes a bare word too, as its
  name: `(read-file notes.txt)`. Each call runs `f` in a process of its
  own, so calls run at once. What `f` gives is the answer; if it raises
  or faults, the caller is told why. An answer that isn't data (a pid, a
  function) faults the call.
- `(rpc/peer)` says who called, from a handler: `((:uid u) (:gid g)
  (:pid p))` for a caller at the other end of a Unix socket, as
  `socket/peer` gives it, or `()` over TCP or a command (D235). Only
  from the call's own process, as `rpc/next`.
- `(rpc/next v)` sends `v` to the caller at once, from a handler that
  streams; its answer comes after its values. Only from the call's own
  process, whose parent (`^$$`) is the connection: so a handler calls its
  caller back with `(rpc/call ^$$ name args)`.
- `(rpc/serve where handlers)` listens at `where`, a port or
  `"host:port"`, or a Unix socket's path, and gives the listener's pid,
  which `kill` stops. The caller is sent `(:listening at)`, `at` being
  the port it got, or the path. Each connection gets a process of its
  own, whose other end can call `handlers`. A path is any string with a
  `/` in it, so one here is `"./ctl.sock"`; that holds wherever `rpc`
  takes `"host:port"` (D231).
- `(rpc/connect where handlers)` connects to `"host:port"` or a path,
  and once it's open gives `(:ok conn)`; or `(:error reason)`, the
  socket's, such as `(:econnrefused "localhost:7317")`. The other end
  can call `handlers`.
- `(rpc/exec cmd handlers)` is `rpc/connect` over a command's input and
  output, `cmd` as `:exec` takes it: ssh, say, to a machine whose
  program listens at `127.0.0.1`, `(list "ssh" "-q" "host" "nc"
  "127.0.0.1" "7317")`, or at a path, `(list "ssh" "-q" "host" "nc" "-U"
  "/run/app/ctl.sock")`. Its stderr comes with its stdout, so it mustn't
  write anything else there.
- `(rpc/call conn name args)` calls `name`, a string or a symbol, on the
  other end with the list `args`, and waits for the answer: `(:ok v)`,
  or `(:error reason)`. It waits in `join`, so it works anywhere, a
  lambda or a handler included. A stream's values are dropped. `args`
  that aren't data fault in the caller (`:not-sexp`).
- `(rpc/stream conn name args)` makes the same call, and gives the
  caller what comes as it comes: `(:next s v)` for each value, then
  `(:done s v)` or `(:fail s reason)`. It gives `s`, the stream's pid,
  at once. `(kill s)` stops the call: the other end is told, and kills
  the process running it, and the caller hears nothing more. A call
  whose caller ends, `rpc/call`'s included, is stopped the same way.
- `(rpc/close conn)` closes the connection: calls in flight from here
  fail with `:disconnected`, and those from the other end are stopped.
  The other end hanging up does the same; and so does the connection
  ending with an error, such as a reset, but the calls from here fail
  with that error (D230).
- `(rpc/keep where handlers on-open)` keeps a connection up
  (D228). It gives a keeper's pid, `k`, at once, which `rpc/call`,
  `rpc/stream` and `rpc/close` take as they take a connection. It dials,
  and when the connection drops, or can't be made, dials again after
  100 ms, then twice as long each time, up to 10 s, and from 100 ms again
  once one opens. While it's down, a call fails at once with
  `:disconnected`; a call doesn't wait for it to come back. Each time a
  connection opens, `(on-open conn)` runs in a process of its own, `conn`
  being that connection: to say who this end is, say. `(rpc/close k)`
  closes the connection and stops the keeping; if the keeper was waiting
  to dial again, that wait's timer keeps the program running till it's
  due.
- `(rpc/system)` is a handler, `system`, that answers with what the
  `:system` device says (under Devices): `((runtime info) (processes
  (info ...)))`, both of one moment, a pid in them as its printed form,
  `"#<pid 3>"`, so that it can go as data. A program offers it if it
  wants to be looked into from outside: `(call 1 system)` at `nc`.

A program that serves can call whoever connects to it, over the
connection they made, so a device behind a firewall can still be called:
inside a handler, `^$$` is the connection the call came on, which
`rpc/call` takes. A hub keeps its devices in a registry, a process of
its own, which `monitor`s each connection, so one that hangs up is let
go:

    (rpc/handler "register" "Says who a device is." (list (list "name" :string "Its name"))
        (lambda (name) (send registry (list :add name ^$$)) :registered))

and a device keeps its connection up and registers each time it opens:

    (rpc/keep "hub.example:7317" (list reader)
        (lambda (conn) (rpc/call conn :register (list "kitchen"))))

The reasons a call fails, besides a handler's own: `(:no-such-call
name)`; `(:arity name n)`, `n` being how many arguments it takes;
`(:bad-arg name param type value)`; `:disconnected`; and the
connection's own error, if it ended with one.

A message is a datum, which may run over lines, and comments between
them are skipped:

    (call id name arg ...)    (next id v)    (done id v)    (fail id reason)
    (stop id)                 (helo rpc 1)   (barf reason)

`call` and `stop` go from the caller to the callee, and `next`, `done`
and `fail` back, so an id need only be unique among the calls in flight
that one end has made, and any datum will do. A call gets any number of
`next`s, then one `done` or `fail`; after `stop` it gets nothing more.
Each end says `(helo rpc 1)` first; a person needn't, and a `helo` for
another protocol or version is barfed at, and the connection closed.
`(call id help)`, which every connection answers, gives what can be
called, `(name description params)` for each. Anything that isn't a
message gets `(barf (:bad-message msg))`, and a `call` whose id is in
use `(barf (:id-in-use id))`; a message about a call that isn't in
flight is let be, since one can cross a `stop`. A person at `nc`:

    (helo rpc 1)
    (call 1 add 2 3)
    (done 1 5)
    (call 2 run "make test")
    (call 1 approve "run: make test")       ; the program asks the person
    (done 1 #true)
    (done 2 "ok   000-int ...")

A datum that can't be read, such as a stray `)`, ends the connection
(`:bad-sexp`), and with it the calls on it. There's no flow control: a
stream's values are sent as fast as they're made. Nor any
authentication: listen at `127.0.0.1`, or better at a path, whose
permissions say who may connect, and reach it from elsewhere over ssh.
