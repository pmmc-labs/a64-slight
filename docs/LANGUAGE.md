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
pending, no socket is open, and no process is waiting for keys from a
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

    deadlock: the root process is waiting for a message, and nothing
    else can run

(on one line; the other kind is `... waiting for #<pid 2> to end, ...`).

These also go to stderr, as they happen:

- `fault: not a cons: () (car at prog.slight:3:5)`: a fault in the root.
  In any other process it's `fault in #<pid 4>: ...`.
- `error: reason`: the root ended with an error that wasn't a fault
  (`raise` or `kill`).
- `dead letter: message (recv at prog.slight:9:5)`: a message that no
  clause of a `recv` matched (Processes, below).

A compile error is `prog.slight:line:col: message`, and `slightc` exits
1 without writing a program.

Two environment variables matter when running a program, mainly for
tests: `SLIGHT_CLOCK=virtual` swaps the real clock for one that starts at
0 and moves only when nothing can run, straight to the next timer; and
`SLIGHT_POISON=1` makes the collector fill what it frees with garbage.

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

### @include

    (@include "util/strings.slight")
    (@include :test)

`(@include "path")` splices in the forms of another file where it
stands. The path is relative to the file the include is in, unless it
starts with `/`. `(@include :name)` is the built-in `lib/name.slight`:
`:test` and `:fs` (Libraries, below).

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
- `#true` and `#false` are the booleans. No other `#` word is allowed.
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
`@ARGV`, `@include`, the special forms, and the forms that become
`cond`.

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
    (connect device expr)

A `fork` whose new process is also connected to a source of messages.
See Devices.

### recv

    (recv
        (pattern body...)
        ...)

Takes the next message. `recv` can only be the whole body of a `defun`.
See Processes.

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

A builtin that takes a varying number (`list`, `concat`, `tty/write`,
`format-num`) can't, so `(apply list xs)` is a compile error. Nor can a
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
the clauses in order:

- `(:tag a b ...)` matches a list of exactly that length headed by the
  symbol `:tag`, and binds the rest of it, by position, to the names.
  `_` in a position matches anything and binds nothing.
- `:tag` matches that symbol.
- A bare name matches any message, and binds it. `_` matches any message
  and binds nothing.

The matching clause's body runs, in tail position. To keep receiving,
tail-call the function again, with the new state as its arguments. When
it returns instead, that's the value the call gives back.

Only the first message is ever looked at; there's no selective receive.
If no clause matches it, it's dropped and logged as a dead letter
(`dead letter: (bogus 1) (recv at prog.slight:2:5)`), and the function
takes the next. Patterns don't nest, and the head keyword is the only
literal: match the outer shape, and take the rest apart in the body.

### The recv rule

A **state function** is a receive function, or any function that
tail-calls one. The compiler works out which functions they are, and a
state function can only be called in tail position, or as the body of a
`fork` or `connect`. It can't be used as a value, and a lambda can't
call it.

So when a process waits, there's nothing on its stack: it's just the
receive function, its arguments and the mailbox. A waiting process holds
no stack at all, and that's when its heap is collected (D8, D105).

The cost is that you can't wait in the middle of a function. To send a
request and wait for the answer, split the function in two:

    (defun ask (c)
        (send c (list :get $$))
        (answer))

    (defun answer ()
        (recv
            ((:count n) n)))

and since `ask` tail-calls `answer`, it's a state function too: it can
only be called in tail position.

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

### Waiting for a process

- `(join pid)` waits until `pid` ends, and gives its result, `(:ok
  value)` or `(:error reason)`. It works on any pid, any number of
  times, from anywhere (a lambda included), and after the process has
  ended. Several joiners wake in the order they joined.
- `(monitor pid)` asks to be told when `pid` ends: the caller is sent
  `(:exit pid result)` then, or at once if it already has. Monitoring
  twice means two messages. Nothing is sent to a parent unless it asks.
  Returns `()`.
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
process counts a reduction at every call, tail calls included; after
1,000, it goes to the back of the queue if another process is ready.
Every loop is a tail call, so no process can hold up the rest.

### The heap

Each process's heap holds up to 64 MB; past that, an allocation faults
(`:heap`). It's collected only when a receive function asks for its next
message, and only once the heap in use is at least 256 KB and twice
what survived the last collection.

So a process that allocates without waiting for messages never collects:
a long loop inside one handler, a process that never calls `recv`, or
the root (unless it ends in a receive function). Each can run into the
limit. Split long work across messages.


## Devices

`connect` is `fork` plus a connection: it starts a process to evaluate
`expr`, as `fork` does, connects it to a source of messages, and returns
the new process's pid.

### The keyboard

    (connect :keypress expr)

The new process is sent every key typed, as `(key mods...)`:

- `key` is a one-character string for a printable key (any UTF-8
  character), or a name: `:ArrowUp`, `:ArrowDown`, `:ArrowLeft`,
  `:ArrowRight`, `:Home`, `:End`, `:Insert`, `:Delete`, `:PageUp`,
  `:PageDown`, `:Enter`, `:Escape`, `:Backspace`, `:Tab`, `:F1` to
  `:F12`, or `:Unidentified`.
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
line, a count, a JSON value or a datum of 64 MB or more, too big for a
process's heap, with `(:error (:too-big path))`.

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
`select()`). Connecting doesn't hold up the other processes, but looking
up a host name does, briefly, for now (D153).

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
that can't be found. Open sockets keep the program running.

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

An HTTP request is lines up to a blank one, then a body of
Content-Length bytes. The server asks for the body while it handles the
blank line, so none of the body is cut as lines:

    (defun headers (c hs)
        (recv
            ((:line c s)
                (cond
                    ((eq? s "\r") (send c (list :read (content-length hs))) (body c hs))
                    (#true        (headers c (cons s hs)))))))

    (defun body (c hs)
        (recv
            ((:chunk c s) (respond c hs s) (headers c ()))))

To `join`, `monitor` and `kill`, a device is a process that has ended
with `(:ok ())`.


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
  `"[1,\"a\",null]"`. A float keeps its point or exponent, and a string
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
  and `#false`, `'x` as `(quote x)`, comments, and doc blocks, which it
  skips. But `:a` reads as
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
  name, without a colon; `(:symbol "name")` is `name`. Anything that
  isn't data faults (`:not-sexp`): a pid, a function, `nan` or `inf`.
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

Its helpers (`take-loop`, `range-loop`, `str-repeat-loop`, `padding` and
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
