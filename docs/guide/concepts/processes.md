# Processes and messages

slight has one kind of process (D277). `fork` starts one to run an
expression, and gives its pid; the process ends with the expression's
value. Every process has a mailbox and a value, and can:

- be sent to, and take its messages with `recv`, one at a time, in the
  order they were sent, anywhere in its code;
- `join` another, which waits for it to end and gives `(:ok value)` or
  `(:error reason)`; race several with `join/race`, or wait for the
  first to succeed or for all of them with `join/any` and `join/all`;
- make an **address**, `(address)`, a place of its own that others can
  send to, and wait there with `read`, or for the first of several
  things with `read/any`. `ask` is built on this;
- open a device of its own with `open`, a file, a socket or a command,
  and `read` what it sends, or start a process connected to one with
  `connect`;
- `monitor` another process, and be sent `(:exit pid result)` when it
  ends, in its mailbox or at an address it names;
- set timers with `after`, for itself or for an address;
- wait as deep inside ordinary functions as it likes, since every wait,
  `recv`, `join`, `read`, `ask` and `sleep`, keeps its stack.

The top level of a program is a process like the others. (Till D276 a
`recv` could go only where a tail call could, the recv rule, D8; and
till D277 a process was a task or an actor, D243, and only an actor
could wait for a message. Both are gone, and this page is slight as it
is.)

## Sending and waiting

`send` copies a message into a process's mailbox, and never waits.
Messages from one process to another arrive in the order they were
sent. `recv` takes the next message and matches it against its clauses,
with the same patterns as `match`; a message that matches none is
dropped and logged as a dead letter. A `recv` can go anywhere: as the
last form of a body, as a `let`'s value, inside a call, in a helper the
function calls, in a lambda it runs. Everything before it runs first,
and a function that waits gives its value back to its caller:

```
(defun ask-store (s cmd)
    (send s (list :do cmd $$))
    (recv ((:reply r) r)))
```

There's no selective receive: whatever `recv` a process is in takes the
next message, whatever it is. So `ask-store` is right only while
nothing else sends to the process. If another request arrives before
the reply, the `recv` doesn't match it, and it's a dead letter.

## Addresses

That's what an address is for. `(address)` makes an inbox of the
process's own, which only it reads, and `recv` leaves what's sent there
alone. A message carries the address its answer should go to, and the
answer can't be taken for anything else:

```
(let to (address))
(send s (list :do cmd to))
(read to)
```

`ask`, in the prelude, does this: it makes an address, adds it to the
end of the message, sends it, and reads the reply there, which the
other side gives with `(reply to v)`. It also monitors the process it
asks, to the same address, so if that process ends first, the end comes
where the reply would have, and `ask` raises `(:ended pid result)`
rather than waiting for ever. Any process can ask, an actor in the
middle of handling a message included: its other mail waits in the
mailbox. What a process opens with `open` is read the same way, with
`read`, and `read/any` waits for the first of several addresses and
devices.

A process reading what will never come is a deadlock, and if it's the
top level the program says so and ends ([Find a
deadlock](../howto/deadlocks.md)).

## Tasks and actors

Two shapes of process cover nearly everything:

- A **task** computes a value: `(fork f arg ...)` starts one, and `join`
  waits for it. It takes no messages, and ends when it's done.
- An **actor** keeps state: it loops through a function that waits for
  a message with `recv`, handles it, and calls itself with the next
  state. It gives no value back; it's there to be sent to.

They're two answers to two different questions. A task answers "work
this out". An actor answers "keep this, and answer questions about it".

Tasks are how slight runs things at once and gathers the results:

<!-- code: ../code/con-tasks.slight -->
```lisp
(defun slow-square (n ms)
    (sleep ms)
    (* n n))

; The result of pid, a task, or (:error :timeout) if it takes longer
; than ms.
(defun within (ms pid)
    (let timer (fork sleep ms))
    (match (join/race (list pid timer))
        ((p result) (if (eq? p pid)
                        (do (kill timer) result)
                        (do (kill pid) (list :error :timeout))))))

(pprint (join/all (map (lambda (n) (fork slow-square n (* n 100))) (list 1 2 3))))
(pprint (within 250 (fork slow-square 5 100)))
(pprint (within 250 (fork slow-square 6 400)))
(clock/us)
```

<!-- output: ../code/con-tasks.expected -->
```
(ok (1 4 9))
(ok 25)
(error timeout)
650000
```

The three squares take 100, 200 and 300 ms, and `join/all` has them
all at 300 ms, since they ran at once. Then 100 ms for the first
`within`, and 250 for the one that times out: 650 ms in all. (That's
the virtual clock's reading, which counts only the sleeps. See
[Time](time.md).)

`join/all` is JavaScript's `Promise.all`, and `join/race` its
`Promise.race`. A task is a promise you can kill.

A task is also slight's `try`. A fault, a `raise` or a `kill` ends only
the task, and `join` gives `(:error reason)`. So `(join (fork risky))`
runs `risky` with its failures turned into a value.

An actor owns state that outlives any one computation: a table, a
connection, a game, a cache. Its loop takes one message at a time, so
whatever it does with one message happens whole, before the next. That
makes it the only place a piece of shared state can change, which is
what makes it safe. Anything gets an answer from it with `ask`.

Ask: **should anything be able to send to it?** If so, write it as an
actor, a function that loops through `recv`. If it just has a job to do
and an answer to give, fork it and join it. Some patterns that come up:

- **An actor that needs a conversation** (ask one actor, then another,
  then decide) can `ask` in the middle of handling a message: the
  replies come to addresses of its own, and its other mail waits. If
  the conversation shouldn't hold it up, it forks a task to have it,
  and the task sends the actor the result.
- **A task that needs to receive several messages** (a stream, say)
  makes an address and reads it in a loop, or takes them with `recv`.
- **A server** is a process per connection, started by a listener.
  The handlers in `(@include :http)` and `(@include :rpc)` run each
  request in a process of its own, so a handler can call anything.

## Each state is a function

An actor with modes (logged in or not, open or closed, playing or
paused) is a function per mode. Each waits for what makes sense in
that mode, and moves to another by calling it:

<!-- code: ../code/con-states.slight -->
```lisp
(defun locked (code tries)
    (recv
        ((:open guess to)
            (cond
                ((eq? guess code) (reply to :opened) (opened code))
                ((>= tries 2)     (reply to :alarm) (alarm))
                (#true            (reply to :wrong) (locked code (+ tries 1)))))))

(defun opened (code)
    (recv
        ((:open _ to) (reply to :already-open) (opened code))
        ((:close to)  (reply to :closed) (locked code 0))))

; Nothing opens it now.
(defun alarm ()
    (recv
        ((:open _ to) (reply to :alarm) (alarm))))
```

<!-- code: ../code/con-states.slight -->
```lisp
(let door (fork locked "1234" 0))
(list
    (ask door (list :open "0000"))
    (ask door (list :open "1234"))
    (ask door (list :open "1234"))
    (ask door (list :close))
    (ask door (list :open "1111"))
    (ask door (list :open "2222"))
    (ask door (list :open "3333"))
    (ask door (list :open "1234")))
```

<!-- output: ../code/con-states.expected -->
```
(wrong opened already-open closed wrong wrong alarm alarm)
```

This is a state machine with the states as code. No `state` field
needs checking, and a message that a state has no clause for is a
dead letter, logged, rather than handled as if it were another
state's.

## What a wait costs

A process takes a stack when it first runs, and keeps it till it ends,
through every wait: `recv`, `join`, `read` and `sleep` alike. A stack
is 8 MB of address space, of which only the pages the process has
touched are memory, a few KB for most actors. So an idle actor is its
stack's pages, its mailbox and its heap, and how many can wait at once
is how many stacks the system will map: about 32,000 on Linux as it
comes (`vm.max_map_count`). Past that, a new process ends with `:stack`
(D267), and the rest go on. Ten thousand actors are fine; a million
aren't, where before D276 they were.

A process's heap is collected when it waits in `recv`, if it has grown
enough, and at a function's entry, with its frames as roots. See
[Memory](memory.md).

## When they end

A process ends with a value, `(:ok value)`, or an error,
`(:error reason)`. Who hears of it:

- Its result goes to whoever `join`s it, and to its monitors as
  `(:exit pid result)`, and is kept until its **parent** (the process
  that forked it) has had it, by joining it, by a monitor's notice, or
  by ending itself (D244). After that, joining it gives `:ended`. So
  the usual `(join (fork ...))` keeps nothing.
- An actor's end is usually heard through `monitor`: a supervisor
  watches what it started that way (tutorial, chapter 5), and `ask`
  monitors the process it asks, to the address it waits at, so it
  hears when no answer is coming.

A program that runs for good, starting processes as it goes, holds only
those running, open, or keeping a value for a parent that hasn't taken
it yet. So a process that starts others for good, a server accepting
connections, say, joins or monitors what it starts: a monitor's notice
is the parent having it, and nothing is kept.

---

Previous: [Nothing changes](immutability.md) · Next: [Failure](failure.md)
