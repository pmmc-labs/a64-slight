# A tour of slight

This is a fifteen-minute look at slight for someone who already
programs. It shows what's ordinary (functions, lists, recursion) and
spends more time on what isn't: nothing is ever mutated, there's no
truthiness, errors are values, and anything that has state over time is
a process. Every program here is a test in the repository, so what it
says it prints is what it prints.

## Building and running

slight compiles ahead of time to a native program. From the repository,
with Node 22.18 or later and clang (see the main [README](../../README.md)):

```
node bin/slightc.ts -o hello hello.slight
./hello
```

`slightc` compiles for AArch64 unless told otherwise. On an x86-64
machine, add `--target x86_64` to get a program you can run directly.

A program is a list of top-level forms, and what it prints at the end is
the value of the last one:

<!-- code: code/tour-hello.slight -->
```lisp
; The program prints the value of its last form.
"hello, world"
```

<!-- output: code/tour-hello.expected -->
```
"hello, world"
```

The string comes out in quotes because it's printed as a value, the way
you'd type it back in. To write text as it is, use `tty/write`.

## Functions and values

<!-- code: code/tour-functions.slight -->
```lisp
; A loop is a tail call: the state is in the arguments.
(defun sum-to (n acc)
    (if (== n 0)
        acc
        (sum-to (- n 1) (+ acc n))))

(defun square (x) (* x x))

(pprint (sum-to 100 0))
(pprint (map square (range 1 6)))
(pprint (filter (lambda (x) (== (% x 2) 0)) (range 1 11)))
(pprint (fold/l 0 + (list 1 2 3 4)))

; Nothing changes once it's made: cons makes a new list.
(let xs (list 1 2 3))
(let ys (cons 0 xs))
(pprint (list xs ys))

(let greet (lambda (name) (str "hello, " name)))
(greet "slight")
```

<!-- output: code/tour-functions.expected -->
```
5050
(1 4 9 16 25)
(2 4 6 8 10)
10
((1 2 3) (0 1 2 3))
"hello, slight"
```

A few things to notice:

- **There are no loops, only calls.** A call in tail position is a jump,
  so `sum-to` runs in constant stack however big `n` is. The values that
  would be loop variables elsewhere are its arguments.
- **Nothing is mutated.** `let` names a value for the rest of the body;
  it doesn't make a variable. `cons` makes a new list and leaves `xs` as
  it was. There's no `set!`, and no way to build a cycle.
- **There's no truthiness.** A test must be `#true` or `#false`.
  `(if xs ...)` doesn't ask whether `xs` is empty; it faults. Ask
  `(nil? xs)`.
- **Integers don't wrap.** They're 63 bits, and a result that doesn't fit
  is an error rather than a surprise.
- **Functions are values.** `lambda` makes one, closing over the locals
  it uses, and a `defun` or a builtin such as `+` can be passed as one.
  The prelude, compiled into every program, has the usual list
  functions: `map`, `filter`, `fold/l`, `range`, `sort` and the rest.

## Taking data apart

Data in slight is mostly lists, with a keyword at the front saying what
kind of thing it is. `match` takes it apart by shape:

<!-- code: code/tour-match.slight -->
```lisp
(defun area (shape)
    (match shape
        ((:square side) (* side side))
        ((:rect w h)    (* w h))
        ((:circle r)    (* math/PI (* r r)))))

(let shapes (list (list :square 3) (list :rect 2 5) (list :circle 1)))
(map area shapes)
```

<!-- output: code/tour-match.expected -->
```
(9 10 3.141592653589793)
```

`:square` is a keyword, a symbol that stands for itself; it prints
without its colon. A pattern matches a list of exactly its length, with
literals where it has them and names bound to the rest. Patterns nest,
and `&` takes what's left of a list. A shape that matches no clause is a
fault. There are no guards: match the shape, then test the rest in the
body.

## Errors

slight has two kinds of failure, and keeps them apart:

<!-- code: code/tour-errors.slight -->
```lisp
; An error you expect is a value.
(defun safe-div (a b)
    (if (== b 0)
        (list :error :div-by-zero)
        (list :ok (div a b))))

(pprint (safe-div 10 2))
(pprint (safe-div 1 0))

; A bug is a fault, which ends only the process it happens in.
(let t (fork (lambda () (car ()))))
(join t)
```

<!-- output: code/tour-errors.expected -->
```
(ok 5)
(error div-by-zero)
fault in #<pid 2>: not a cons: () (car at docs/guide/code/tour-errors.slight:11:25)
(error (not-a-cons () "car at docs/guide/code/tour-errors.slight:11:25"))
```

An error you expect, such as a file that isn't there or a division you
were asked to do by zero, is a value. By convention it's `(:ok value)`
or `(:error reason)`, and the caller matches on it.

A bug, such as `car` of an empty list, is a **fault**. It ends the
process it happens in, and only that one. The runtime logs it to stderr
as it happens, and the process's result is
`(:error (kind value site))`. There's no `try` or `catch`. The process
that cares about a failure finds out by waiting for the one that failed,
as the top level does here with `join`. Since processes are cheap, the
way to contain something that might go wrong is to run it in a process
of its own.

A bug as plain as that one is found before the program runs, too. The
compiler knows what it can of each value's type, and warns of what's
sure to go wrong if that code runs:

```
docs/guide/code/tour-errors.slight:11:25: warning: car takes a non-empty list, and this is (): it faults :not-a-cons
```

It compiles the program anyway: the fault would end only its own
process. Where it can't tell what a value is, it says nothing, and a
program can tell it more, with types of its own (LANGUAGE.md, "Types").

## Tasks

A **task** is a process started to compute a value. `fork` starts one
and gives its pid, and `join` waits for it to end and gives its result:

<!-- code: code/tour-tasks.slight -->
```lisp
(defun fib (n)
    (if (< n 2)
        n
        (+ (fib (- n 1)) (fib (- n 2)))))

; A task computes a value; join waits for it.
(let tasks (map (lambda (n) (fork fib n)) (list 20 21 22)))
(map join tasks)
```

<!-- output: code/tour-tasks.expected -->
```
((ok 6765) (ok 10946) (ok 17711))
```

Processes share nothing. A `fork` copies the arguments it passes into
the new process, and each process has a heap of its own, collected
on its own. The runtime runs every process on one core and preempts
them, so no process can hold up the rest. Processes are about
structure, not parallel speed: a million of them is fine. Speed comes
from placing processes on the other cores, islands, with `fork/on`
([Use every core](howto/cores.md)).

The top level of a program is a process like the others.

## Actors

An **actor** is a process that waits for messages. It's how slight holds state
that lives on: in the arguments of a function that waits for a message,
handles it, and calls itself with the next state.

<!-- code: code/tour-counter.slight -->
```lisp
; An actor holds state in the arguments of a function that waits for
; messages, and calls itself with the next state.
(defun counter (n)
    (recv
        ((:inc)    (counter (+ n 1)))
        ((:get to) (reply to n) (counter n))))

(let c (fork counter 0))
(send c (list :inc))
(send c (list :inc))
(send c (list :inc))
(ask c (list :get))
```

<!-- output: code/tour-counter.expected -->
```
3
```

- `fork` starts it, as it starts any process, and gives its pid.
- `send` copies a message into its mailbox, and never waits. Messages
  from one process to another arrive in the order they were sent.
- `recv` takes the next message and matches it against its clauses,
  with the same patterns as `match`. A message that matches none is
  dropped and logged as a dead letter.
- `ask`, in the prelude, is how anything gets an answer from an actor: it
  adds an address to the message, sends it, and waits there for the
  `reply`.

When nothing can run any more and nothing more can come, the program
prints the top level's value and ends. Here that's while the counter is
still waiting for its next message, which is fine.

## Waiting

A `recv` goes anywhere in a process's code, a helper or a lambda
included, the top level too, and the process keeps its stack while it
waits, as it does in `join`. So a function that waits gives its value
back to whoever called it.

Two things show up early:

- **A `recv` takes the next message, whatever it is.** There's no
  selective receive, so an actor that serves requests shouldn't stop in
  the middle of one to `recv` a reply: the next message may be another
  request.
- **An address takes only what's sent to it.** `ask` makes one for
  each question, so the reply can't be taken for anything else, and
  `recv` leaves it alone. That's how a process asks from the middle of
  anything, the top level included.

## Where next

- The [tutorial](tutorial/01-values.md) goes over the same ground more
  slowly, building a key-value store from a list to a server.
- The [guide](README.md) lists what else there is to read, and what's
  coming.
- [`docs/LANGUAGE.md`](../LANGUAGE.md) is the reference: every form and
  builtin, and how a program runs. Its sections on Processes and
  Devices go further than this tour: timers, monitors, files, sockets,
  commands.
- [`examples/`](../../examples) has whole programs. `ping-pong`,
  `pub-sub` and `simple-db-server` are small; `game-of-life-actors` is
  a process per cell; `airline` is big.
