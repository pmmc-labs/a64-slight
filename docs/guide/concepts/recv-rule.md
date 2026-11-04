# Waiting for a message

> A `recv` goes anywhere in an actor's code, and the process keeps its
> stack while it waits.

That's how slight has worked since D276. Till then a `recv` could go
only where a tail call could, and a function that could reach one could
only be tail-called: the recv rule, D8, which let a waiting process
give its stack back. This page says what a wait is now, what it costs,
and the idioms that still hold.

## What it says

A `recv` can go anywhere in a function that runs in an actor: as the
last form of a body, as a `let`'s value (`(let m (recv ...))`), inside
a call, in a helper the function calls, in a lambda it runs. Everything
before it runs first, and a function that waits gives its value back to
its caller:

```
(defun ask-store (s cmd)
    (send s (list :do cmd $$))
    (recv ((:reply r) r)))
```

What stays: a task, a `fork`'s body or the top level, can't wait for a
message (see [Tasks and actors](tasks-and-actors.md)); and there's no
selective receive, so whatever `recv` a process is in takes the next
message.

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

Collection is as it was: a process's heap is collected when it waits
in `recv`, if it has grown enough, and at a function's entry, with its
frames as roots.

## The idioms

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
- **A conversation can go inline.** An actor that needs an answer from
  another can send and `recv` for it in a helper, and use the value.
  But a `recv` in the middle of a handler takes the next message,
  whatever it is: another request can arrive before the reply, and then
  doesn't match. An actor that serves requests shouldn't stop to wait
  inside one; what asks other actors, and must get the right answer, is
  a task, which reads the reply at an address of its own (`ask`), or
  one the actor forks to.

## The one compile error

```
prog.slight:5:1: a task (a fork's body, or the top level) can't wait for messages, and this recv would: spawn an actor for it
```

A task tried to `recv`. Spawn an actor, or, if the task only wants an
answer, `ask` an actor for it. A task that calls a function that waits
is found when it runs, with `:not-an-actor`.

---

Previous: [Nothing changes](immutability.md) · Next: [Tasks and actors](tasks-and-actors.md)
