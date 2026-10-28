# Tasks and actors

Every slight process is one of two kinds (D243):

- A **task**, started with `(fork expr)`, computes a value. It can wait
  for other processes and keep its stack while it does, but it has no
  mailbox: it can't `recv`, and nothing can `send` to it. The top level
  of a program is a task.
- An **actor**, started with `(spawn expr)` or `connect`, has a mailbox.
  It's sent messages, takes them with `recv`, and keeps its state in the
  arguments of the function it loops through. It doesn't give a value
  back the way a task does.

They're two answers to two different questions. A task answers "work
this out". An actor answers "keep this, and answer questions about it".

## What each can do

A task can:

- `join` another task, which waits for it to end and gives
  `(:ok value)` or `(:error reason)`;
- race several with `join/race`, or wait for the first to succeed or for
  all of them with `join/any` and `join/all`;
- make an **address**, `(address)`, that others can send to, and wait
  there with `read`, or for the first of several things with
  `read/any`. `ask` is built on this;
- open a device of its own with `open`, and `read` its messages;
- wait as deep inside ordinary functions as it likes, since `join`,
  `read` and `ask` keep its stack.

An actor can:

- be sent to, and take its messages with `recv`, one at a time, in
  order;
- `monitor` another process, and be sent `(:exit pid result)` when it
  ends;
- set timers for itself with `after` (a task can set one for an
  address of its own);
- connect to devices (files, sockets, commands, the keyboard) with
  `connect`, and take what they send as messages;
- give its stack back while it waits. See [the recv rule](recv-rule.md).

Both can `send` to actors and addresses, `kill`, `sleep`, and start
either kind.

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

Tasks get answers from actors with `ask`: it sends a message with an
address added at the end, and reads the answer there. The actor replies
with `(reply to v)`. Actors talk to each other by sending, with their
own pid to answer to.

## Choosing

Ask: **should anything be able to send to it?** If so, it's an actor.
If it just has a job to do and an answer to give, it's a task.

Some patterns that come up:

- **An actor that needs a conversation** (ask one actor, then another,
  then decide) forks a task to have it, and the task sends the actor
  the result.
- **A task that needs to receive several messages** (a stream, say)
  spawns an actor to take them, and reads its end; or makes an address
  and reads it in a loop.
- **A server** is an actor per connection, started by a listener actor.
  The handlers in `(@include :http)` and `(@include :rpc)` run each
  request in a task, so a handler can call anything.

## When they end

A process ends with a value, `(:ok value)`, or an error,
`(:error reason)`. Who hears of it:

- A task's result goes to whoever `join`s or `read`s it, and is kept
  until its **parent** (the process that forked it) has had it, by
  joining it, reading it, a monitor's notice, or by ending itself
  (D244). After that, joining it gives `:ended`. So the usual
  `(join (fork ...))` keeps nothing.
- An actor can't be joined. Its end goes to its monitors and to the
  tasks reading its pid. If its parent is a task, the end is kept until
  the parent reads it, so a task can start an actor and read its end
  whenever it likes (D245). Otherwise it isn't kept.

A program that runs for good, starting processes as it goes, holds only
those running, open, or keeping a value for a parent that hasn't taken
it yet.

---

Previous: [The recv rule](recv-rule.md) · Next: [Failure](failure.md)
