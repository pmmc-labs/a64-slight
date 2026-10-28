# The recv rule

> A `recv` can go only where a tail call could, and a function that can
> reach one can only be called in tail position.

This is the rule the rest of slight is built around (D8). The compiler
enforces it, and its messages say when you've broken it. This page
explains what it costs and what it buys, and how to write actors with
it rather than against it.

## What it says

A function that contains a `recv`, or tail-calls a function that does,
is a **state function**. The compiler works out which functions are.
A state function:

- can be called only in tail position, or as the body of a `spawn` or
  `connect`;
- can't be used as a value, or called from a lambda;
- can't be called from a task, the top level included.

Inside a state function, a `recv` can go anywhere a tail call could:
as the last form of a body, as a `let`'s value there
(`(let m (recv ...))`), or inside a call there. Everything before it
runs first.

## Why: a waiting process holds nothing

Most actors spend most of their time waiting. The rule means that when
one waits, nothing of it is on a stack: no frame is waiting for a
`recv` to return, because a `recv` only ever comes where the function
had nothing left to do but continue.

The compiler makes this literal. It splits a function at each `recv`.
The `recv` and everything after it become a **receive function** of
their own, which the first part tail-calls with the locals the rest
needs. A function written as

```
(defun tally (s words)
    ...
    (send s (list :do (list :incr word) $$))
    (let r (recv ((:reply r) r)))
    (pprint (list word r))
    (tally s rest))
```

becomes, in effect, two: one that sends and then tail-calls the second,
and a second whose whole body is the `recv` and what follows it. So a
waiting process is exactly a function, its arguments and its mailbox.

What that buys:

- **A waiting process gives its stack back** (D9). Stacks are pooled,
  and held only by processes that are running, so memory goes with how
  many processes are *busy*, not how many exist. A million idle actors
  is cheap.
- **Collection is simple there.** When a process waits, its only roots
  are its arguments, and that's when its heap is collected.
- **No selective receive.** An actor takes its messages in order, so
  there's no mailbox to scan, and no message left behind forever by a
  pattern that never matches. (One that matches nothing is a dead
  letter, logged at once.)
- **No call-cycle deadlocks between actors.** An actor can't call
  another and wait inside a call for its reply, so two actors can't
  each be stuck waiting for the other's answer inside a handler.

## What it costs, and the idioms

**A function that waits can't return a value to its caller.** That's
the real cost, and the idioms are about where to put the waiting.

- **Each state is a function.** An actor with modes (logged in or not,
  open or closed, playing or paused) is a function per mode. Each waits
  for what makes sense in that mode, and moves to another by calling it:

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
  (let door (spawn (locked "1234" 0)))
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
- **Waiting goes inline.** Within an actor, `(let r (recv ...))` in the
  function's own body, not in a helper that returns `r`.
- **A conversation goes in a task.** A task can't `recv`, but it can
  `ask`, `join` and `read`, which keep its stack while they wait, so a
  task can wait as deep in ordinary function calls as it likes and get
  values back. When an actor needs a multi-step exchange with others, it
  `fork`s a task to have it, and the task sends the actor the result
  as a message.
- **An actor waiting for a reply takes the next message, whatever it
  is.** Without selective receive, a reply it waits for mid-function can
  be beaten by another request, which then doesn't match. Actors that
  serve requests shouldn't stop to wait in the middle of one. That's
  another reason conversations go in tasks.

## The two compile errors

```
prog.slight:11:32: ask-store waits for messages (it reaches a recv), so it can only be called in tail position
```

A state function was called where its result would be used. Move the
wait inline, or move the work into a task.

```
prog.slight:5:1: a task (a fork's body, or the top level) can't wait for messages, and this recv would: spawn an actor for it
```

A task tried to `recv`. Spawn an actor, or, if the task only wants an
answer, `ask` an actor for it.

---

Previous: [Nothing changes](immutability.md) · Next: [Tasks and actors](tasks-and-actors.md)
