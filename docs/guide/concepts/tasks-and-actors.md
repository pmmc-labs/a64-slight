# Tasks and actors

slight has one kind of process (D277). Every process has a mailbox and
a value: it can be sent to, and take messages with `recv`; it can be
joined, for the value it ends with; and it can make addresses, open
devices, monitor other processes and set timers. What differs is how a
process is used, and two shapes cover nearly everything:

- A **task** computes a value: `(fork expr)` starts one, and `join`
  waits for it. It takes no messages, and ends when it's done.
- An **actor** keeps state: it loops through a function that waits for
  a message with `recv`, handles it, and calls itself with the next
  state. It gives no value back; it's there to be sent to.

They're two answers to two different questions. A task answers "work
this out". An actor answers "keep this, and answer questions about it".

## What a process can do

Any process can:

- `join` another, which waits for it to end and gives `(:ok value)` or
  `(:error reason)`; race several with `join/race`, or wait for the
  first to succeed or for all of them with `join/any` and `join/all`;
- be sent to, and take its messages with `recv`, one at a time, in
  order, anywhere in its code (see [Waiting for a message](recv-rule.md));
- make an **address**, `(address)`, that others can send to, and wait
  there with `read`, or for the first of several things with
  `read/any`. What's sent to an address is kept apart from the rest of
  the mail: `recv` leaves it. `ask` is built on this;
- open a device of its own with `open`, and `read` its messages, or
  start a process connected to one with `connect`;
- `monitor` another process, and be sent `(:exit pid result)` when it
  ends, in its mailbox or at an address it names;
- set timers with `after`, for itself or for an address;
- wait as deep inside ordinary functions as it likes, since `join`,
  `recv`, `read` and `ask` all keep its stack.

The top level of a program is a process like the others.

## Tasks for work

Tasks are how slight runs things at once and gathers the results:

<!-- code: ../code/con-tasks.slight -->
```lisp
(defun slow-square (n ms)
    (sleep ms)
    (* n n))

; The result of pid, a task, or (:error :timeout) if it takes longer
; than ms.
(defun within (ms pid)
    (let timer (fork (sleep ms)))
    (match (join/race (list pid timer))
        ((p result) (if (eq? p pid)
                        (do (kill timer) result)
                        (do (kill pid) (list :error :timeout))))))

(pprint (join/all (map (lambda (n) (fork (slow-square n (* n 100)))) (list 1 2 3))))
(pprint (within 250 (fork (slow-square 5 100))))
(pprint (within 250 (fork (slow-square 6 400))))
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
the task, and `join` gives `(:error reason)`. So `(join (fork (risky)))`
runs `risky` with its failures turned into a value.

## Actors for state

An actor owns state that outlives any one computation: a table, a
connection, a game, a cache. Its loop takes one message at a time, so
whatever it does with one message happens whole, before the next. That
makes it the only place a piece of shared state can change, which is
what makes it safe.

Anything gets an answer from an actor with `ask`: it sends a message
with an address added at the end, and reads the answer there. The actor
replies with `(reply to v)`. An actor with nothing else to hear can
also give its own pid to answer at, and take the reply with `recv`.

## Choosing

Ask: **should anything be able to send to it?** If so, write it as an
actor, a function that loops through `recv`. If it just has a job to do
and an answer to give, fork it and join it.

Some patterns that come up:

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
it yet.

---

Previous: [Waiting for a message](recv-rule.md) · Next: [Failure](failure.md)
