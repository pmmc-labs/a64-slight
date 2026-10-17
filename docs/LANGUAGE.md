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
files can be given, and together they're one program, read in order.

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

    deadlock: the root process is waiting in wait-forever for a
    message, and nothing else can run

(on one line; the other kind is `... waiting in the top level for #<pid
2> to end, ...`). It names the function the root waits in, as `:system`
does (under Devices).

These also go to stderr, as they happen:

- `fault: not a cons: () (car at prog.slight:3:5)`: a fault in the root.
  In any other process it's `fault in #<pid 4>: ...`.
- `error: reason`: the root ended with an error that wasn't a fault
  (`raise` or `kill`).
- `dead letter: message (recv at prog.slight:9:5)`: a message that no
  clause of a `recv` matched (Processes, below).

A compile error is `prog.slight:line:col: message`, and `slightc` exits
1 without writing a program.

Some environment variables matter when running a program, mainly for
tests: `SLIGHT_CLOCK=virtual` swaps the real clock for one that starts at
0 and moves only when nothing can run, straight to the next timer;
`SLIGHT_POISON=1` makes the collector fill what it frees with garbage;
and `SLIGHT_GC_STRESS=n` makes it collect at the entry of every nth
function called (n from 1, every call, which is slow, to 1,000), and at
every `recv`.
`SLIGHT_HOSTS` and `SLIGHT_RESOLV_CONF` are in "Sockets".

### Replaying a run, and other orders

A run is decided by what it takes from outside: its arguments and
environment, the keys typed, the clock, what files, sockets and commands
give. Everything else the runtime decides itself, counting calls rather
than time. So (D170):

- `SLIGHT_RECORD=file` writes all it takes from outside, as it takes it,
  to `file`, a record of the run. That includes the environment, keys and
  all, so keep a record as you would the environment itself.
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
  replay uses them.

### @ARGV

`@ARGV` is the program's arguments after its name, as a list of strings,
`()` if there are none:

    ./prog 10 fast          ; @ARGV is ("10" "fast")

Only the top level sees it: the top-level forms are the body of a
function, and `@ARGV` is that function's parameter. So a lambda or a
`fork` at the top level captures it like any other local, and a `defun`
that names it is a compile error; pass it to the functions that need it.
A number arrives as a string: `(string->int (car @ARGV))`. `@ARGV` can't
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
  `;`, the backquote and the comma. So `str-len`, `utf8/len`,
  `string->int`, `nil?`, `+` and `==` are all names.
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
and `PI` is the float 3.141592653589793. They're constants rather than
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
  runs (`string->symbol`, below).
- **Booleans**: `#true` and `#false`, which are symbols too.
- **Lists**: `()`, and cons cells. Every list is proper: `cons` faults
  on anything but a list as its second argument.
- **Strings**: immutable bytes, UTF-8 by convention. `pprint` shows them
  in double quotes, with nothing inside escaped.
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
        (sqrt (+ a2 b2)))

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

Starts a new process that evaluates `expr`, and returns its pid. The
locals `expr` uses are deep-copied into the new process, at most 8 of
them. Inside `expr`, `$$` is the new process. `expr` may tail-call a
function that waits in `recv`, which is the usual way to start an actor:

    (let c (fork (counter 0)))

### connect

    (connect :keypress expr)
    (connect :fs/read path expr)
    (connect :tcp "host:port" expr)
    (connect :exec (list "ls" "-l") expr)
    (connect :system expr)
    (connect device expr)

A `fork` whose new process is also connected to a source of messages.
See Devices.

### recv

    (recv
        (pattern body...)
        ...)
    (recv)

Takes the next message, and gives the value of the clause whose pattern
it fits (Patterns, under "The forms that become cond"); `(recv)` gives
the message itself. A `recv` can go anywhere a tail call could, and
inside a call there. See Processes.

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

`not` is a function, in the prelude, on booleans only.

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

A `defun` name, a lambda, or a builtin that takes a fixed number of
arguments can be used as a value:

    (map car pairs)
    (fold/l 0 + xs)

A builtin that takes a varying number (`list`, `tuple`, `concat`,
`tty/write`, `format-num`) can't, so `(apply list xs)` is a compile
error. Nor can a
function that waits in `recv`.

`(apply f xs)` calls `f` with the elements of the list `xs` as its
arguments, at most 8. In tail position it's a tail call.

A name that isn't a local, a function, a builtin or a constant is a
compile error.


## Processes

Everything runs in processes, on one core. Each has its own heap and a
mailbox. Processes share nothing; they talk by sending messages, which
are copied.

`(fork expr)` starts one and gives its pid. `$$` is the current process,
and `^$$` the one that started it (the root's is `()`).

### Messages

`(send pid msg)` puts a deep copy of `msg` at the end of `pid`'s mailbox,
and returns `()`. It never blocks. A message can be any value, and is
conventionally a list headed by a keyword: `(list :add 5)`. A message to
a process that has ended disappears.

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
in tail position (a function's, the top level's, a `fork`'s, or a `cond`
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
called in tail position, or as the body of a `fork` or `connect`. It
can't be used as a value, and a lambda can't call it.

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

### Waiting for a process

- `(join pid)` waits until `pid` ends, and gives its result, `(:ok
  value)` or `(:error reason)`. It works on any pid, any number of
  times, from anywhere (a lambda included), and after the process has
  ended. Several joiners wake in the order they joined.
- `(monitor pid)` asks to be told when `pid` ends: the caller is sent
  `(:exit pid result)` then, or at once if it already has. Monitoring
  twice means two messages. Nothing is sent to a parent unless it asks.
  Returns `()`. A monitor lasts while both processes do: if the caller
  ends first, its monitor goes with it, so short-lived processes can
  monitor a long-lived one as often as they like, as `ask`'s boxes do.
- `(kill pid)` ends `pid`, wherever it is: running, waiting in `recv` or
  `join`, or asleep. `(kill $$)` ends the caller. Killing a process that
  has ended does nothing. Returns `()`.

A process's result is kept after it ends, for good, so a late `join`
or `monitor` still gets it.

### Time

- `(after ms pid msg)` sends `msg` to `pid` once `ms` milliseconds have
  passed. `msg` is copied when `after` is called. Returns `()`.
- `(sleep ms)` blocks the calling process, and only that one, for `ms`
  milliseconds. Returns `()`.

Both take an integer, and a negative one counts as 0. Timers due at the
same time fire in the order they were set. A pending timer keeps the
program running, and one for a process that has ended is dropped when
it's due.

### Scheduling

Processes take turns from a run queue, first in, first out. A running
process counts **reductions**, slight's measure of a process's work (the
word is Erlang's): one each time it enters a function compiled from
slight, by a call or a tail call alike. That's a `defun`, the prelude's
included; a lambda; a `fork`'s or `connect`'s body; and a receive
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
(`:heap`), and just that process ends. It's collected when a receive function asks
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
  character, well-formed as the `utf8/` builtins have it), or a name:
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
            ((:chunk f s) (size (+ n (str-len s))))
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
can't take all the memory.

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

A listener (on all interfaces) first sends `(:open l port)`, `port`
being the one it got: port 0 lets the system pick. Then
`(:accept l conn)` for each connection. `conn` reads nothing until
`(connect conn expr)` hands it to a process of its own, which then hears
`(:open conn)`.

`(connect device expr)`, with anything but a keyword as its first
argument, hands an open device to a new process in this way. Anything
that isn't an open device faults (`:not-a-device`).

A connection that fails ends its owner with `(:error (name "host:port"))`
(a listener's, with its port): errno's names, such as `:econnrefused`,
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
- `(:state st)`: `:running` (the one asking, unless a timer asked),
  `:ready` (in the run queue), `:recv` (waiting for a message),
  `:joining` or `:sleeping`.
- `(:joining p)`: the process it waits for in `join`, or `()`.
- `(:in name)`: the function it's in, as a string: the receive function
  it waits in; the one that called `join`, `sleep`, `yield`, or `send`
  to a device; the one it was entering when it was preempted; or the one
  it starts in. A `recv` in the middle of a function waits in a receive
  function of its own, named for both: `"server (recv at
  prog.slight:7:12)"`. The top level is `"the top level"`, a `fork`'s
  body `"fork at prog.slight:3:9"` (a `connect`'s, `"connect at ..."`),
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
all the processes; `(:stacks n)` held; `(:clock ms)` since the program
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
- `(ceil x)`, `(floor x)`, `(round x)`, `(trunc x)`: an integer. An
  integer stays as it is. `round` sends halves up, so `(round 2.5)` is
  3 and `(round -2.5)` is -2 (JavaScript's `Math.round`). A float too
  big for an integer faults (`:out-of-range`).
- `(abs x)`: keeps the type. `abs` of the most negative integer
  overflows.
- `(min a b)`, `(max a b)`: whichever argument it is, as it was given,
  so `(min 1 2.0)` is 1; the first one on a tie.
- `(sqrt x)`, `(pow a b)`, `(sin x)`, `(cos x)`, `(tan x)`, `(exp x)`:
  always a float. Out of their domain they give `nan` or `inf` rather
  than faulting: `(sqrt -1)` is `nan`.
- `PI`: 3.141592653589793.

### Comparison

- `(== a b)`, `(!= a b)`, `(< a b)`, `(<= a b)`, `(> a b)`, `(>= a b)`:
  two numbers, of either kind, so `(== 1 1.0)` is `#true`. Anything else
  faults (`:not-a-number`). With `nan`, only `!=` is `#true`.
- `(eq? a b)`, `(ne? a b)`: any two values, compared by structure:
  lists element by element, strings byte by byte, floats by value.
  An integer is never `eq?` to a float, so `(eq? 1 1.0)` is `#false`.
  Functions are `eq?` only to themselves, and pids to the same pid.

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

Strings are bytes, and these builtins count bytes: `(str-len "héllo")`
is 6. The `utf8/` builtins, after them, count characters.

- `(str-len s)`: the length, in bytes.
- `(substring s start end)`: the bytes from `start` up to but not
  including `end`. Each index is clamped to the string, and the two are
  swapped if `start` is after `end`, so `(substring "hello" 3 1)` is
  `"el"`.
- `(index-of s m)`: the byte index of the first `m` in `s`, or -1. An
  empty `m` is at 0.
- `(concat x ...)`: any number of values of any type, joined: a string
  as its bytes, anything else as `pprint` shows it. `(concat "n=" 5)` is
  `"n=5"`.
- `(~ a b)`: two strings, and only strings, joined.
- `(str-split s sep)`: the pieces of `s` between the `sep`s:
  `(str-split "a,b,,c" ",")` is `("a" "b" "" "c")`. An empty `s` gives
  `()`, and an empty `sep` splits into single bytes.
- `(str-join sep xs)`: the elements of the list `xs`, rendered as
  `concat` renders them, with `sep` between.
- `(string->int s)`: the integer, if `s` is decimal digits with maybe a
  `-` in front, and fits in 63 bits; otherwise `#false`.
- `(string->float s)`: the nearest float, if `s` is digits, maybe a `-`
  in front, maybe a point and more digits, and maybe an exponent; so
  `"1.5"`, `"-2"`, `"1e3"` and `"2.5E-3"`, but not `".5"`, `"1."` or
  `"inf"`. Otherwise, or if it's too big for a float, `#false`.
- `(symbol->string sym)`: the symbol's name, without a colon.
- `(string->symbol s)`: the symbol with that name, if the program
  mentions it anywhere (as `:name` or `'name`); otherwise `#false`,
  since symbols are made only by the compiler.
- `(byte-at s i)`: the byte at index `i`, as an integer. An index out of
  range faults (`:out-of-range`).
- `(bytes->string xs)`: a list of integers 0 to 255, as a string.
- `(format-num n width [fill])`: the number `n` as text, padded at the
  start to `width` bytes with `fill` (a string, `" "` by default),
  repeated and cut to fit: `(format-num 7 3 "0")` is `"007"`.

Counting characters (D148). A character is a well-formed UTF-8
sequence, or, failing that, a stray byte on its own, whose code point is
then 65533 (U+FFFD), as in Go. So nothing faults on bad bytes, and
splitting a string into characters loses nothing.

- `(utf8/len s)`: the length, in characters: `(utf8/len "héllo")` is 5,
  and an emoji counts 1.
- `(utf8/substring s start end)`: characters, clamped and swapped as
  `substring` does.
- `(utf8/index-of s m)`: the character index of the first `m` that
  starts at a character, or -1. An empty `m` is at 0.
- `(utf8/chars s)`: the characters, as one-character strings.
  `(str-join "" (utf8/chars s))` is `s` again.
- `(utf8/code s)`: the code point of the first character. `""` faults
  (`:out-of-range`).
- `(utf8/char n)`: the one-character string for code point `n`. A
  surrogate, or anything past 1114111 (U+10FFFF), faults
  (`:out-of-range`).
- `(utf8/valid? s)`: whether `s` is all well-formed UTF-8.

The byte builtins that don't count are right for UTF-8 as they are:
`concat`, `~`, `str-join`, `eq?`, `starts-with?`, `ends-with?`, and
`str-split` with a separator that isn't empty. There's no character
type: a character is an integer or a one-character string. Build a long
string by collecting the pieces in a list and joining them.

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
  function, `nan` or `inf`.
  `pprint` stays as it is, its strings unescaped.

A file or socket can be read as s-expressions too: `(:read :sexp)`,
under Files.

### Output

- `(pprint x)`: writes `x` as the program shows values (strings in
  quotes, symbols without their colon), and a newline, to stdout.
  Returns `()`.
- `(tty/write x ...)`: writes its arguments to stdout, rendered as
  `concat` renders them (strings as their bytes), and flushes. Returns
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

There's no comparison of strings yet, so sorting strings takes a `less?`
written for it.

Association lists, `((key value) ...)`:

- `(assoc k v table)`: `table` with `(k v)` added at the front.
- `(lookup k table)`: the value for the first key `eq?` to `k`, or
  `:not-found`.

Strings:

- `(str-repeat s n)`: `s`, `n` times.
- `(starts-with? s m)`, `(ends-with? s m)`.
- `(pad-start s n fill)`, `(pad-end s n fill)`: `s` padded to `n` bytes
  with `fill`, repeated and cut to fit (JavaScript's `padStart` and
  `padEnd`).
- `(uc s)`, `(lc s)`: upper and lower case, for ASCII letters only;
  other bytes are left alone.
- `(bytes s)`: the bytes of `s`, as a list of integers.
- `(ord s)`, `(chr n)`: `utf8/code` and `utf8/char`, under Perl's
  names: `(ord "é")` is 233, and `(chr 233)` is `"é"`.

Its helpers (`take-loop`, `range-loop`, `zip-loop`, `sort-runs`,
`sort-pairs`, `sort-merge`, `str-repeat-loop`, `padding` and
`bytes-loop`) are visible too, but not meant to be called.


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

For structures of one's own: `(ask pid msg)` sends `msg` with a box for
the answer added at the end, and waits for the answer; the structure
answers with `(reply to v)`, `to` being that box.

    (defun counter (n)
        (recv
            (:inc      (counter (+ n 1)))
            ((:get to) (reply to n) (counter n))))

    (let k (fork (counter 0)))
    (send k :inc)
    (ask k (list :get))                  1

If the structure ends before it answers, `ask` raises `(:ended pid
result)`. `(ask-within ms pid msg)` raises `(:timeout pid)` too, if no
answer comes in time; its timer stays pending till then, and a pending
timer keeps the program running.

### (@include :http)

`lib/http.slight`: HTTP/1.1 on `:tcp`, a server and a client (D173). One
request and one reply to each connection, which then closes. HTTPS
waits for TLS (step 12c).

    (@include :http)

    (defun handler (req)
        (list 200 (list (list "Content-Type" "text/plain"))
            (concat "you asked for " (lookup :path req))))

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
  a connection that closed before its reply.
- `(http/request-within ms method url headers body)` is `http/request`
  with `(:error :timeout)` if no reply comes in `ms` milliseconds. Its
  timer keeps the program running till it's due, as `ask-within`'s does,
  so `http/request` is the one to use unless a timeout is wanted.
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
  or `(:error (:ended a result))` if `a` has ended or ends first. A
  process of its own asks, so none of the pieces or calls reach the
  caller, and it works anywhere, a lambda or a tool included (D192).
- `(kill a)` ends the agent, and stops an answer on its way, and any
  tool it was running.

A tool is a function the model can call, with a name, a description,
and its parameters, which the model reads to know when to call it and
what with:

    (let weather
        (tool/new "get_weather" "The weather in a city, now."
            (list (list "city" :string "The city, such as Tokyo"))
            (lambda (args) (concat "11 degrees in " (lookup "city" args)))))

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
  have finished. The result is `f`'s value, rendered as `concat` renders
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
read; the tool does it only if `approve` gives `#true`, and otherwise
tells the model it wasn't let. A tool that can't do what it's asked
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
  to its end, and what it printed and its exit status. `approve` is shown
  `run: command`.
- `(tools/fetch approve)`: `fetch(url)`, an `http://` or `https://` URL
  (anything else raises), with curl, following redirects, for at most 20
  seconds and 5 MB. `approve` is shown `fetch url`, every time, since a
  URL can carry anything out. A web page comes as plain text, by
  `tools/html->text` (below), its links resolved against the URL it was
  fetched from, after any redirects; other text (JSON, XML, plain text) as it
  is; anything else as what it is, `not text: image/png, 5120 bytes`. A
  status that isn't 2xx comes first, `HTTP 404`, then the text. If curl
  fails, the tool raises with curl's message, such as `curl: (6) Could
  not resolve host: example.invalid`.
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
