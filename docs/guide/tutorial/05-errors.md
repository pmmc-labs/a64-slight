# 5. When things go wrong

slight separates two kinds of failure. An **error** is something the
code expected might happen, such as a key that isn't there or a value of
the wrong kind. It's a value, `(:error reason)`, which the caller
matches on. A **fault** is a bug, such as `car` of an empty list or
adding a string. It ends the process it happens in, and only that one.
This chapter breaks the store, contains the damage, fixes it, and then
keeps it running through bugs not yet found.

## A bug ends the store

`incr` from chapter 4 assumes the value is a number:

<!-- code: ../code/tut-5-fault.slight -->
```lisp
(@include "tut-store.slight")

(let s (spawn (store ())))
(call s (list :put "apple" 3))
(call s (list :put "color" "red"))
(pprint (join (fork (call s (list :incr "color")))))
(read s)
```

<!-- output: ../code/tut-5-fault.expected -->
```
fault in #<pid 2>: not a number: "red" (+ at docs/guide/code/tut-store.slight:23:40)
(error (ended #<pid 2> (error (not-a-number "red" "+ at docs/guide/code/tut-store.slight:23:40"))))
(error (not-a-number "red" "+ at docs/guide/code/tut-store.slight:23:40"))
```

Three things happened, one per line:

1. `+` was given `"red"`, and the store, pid 2, **faulted**. The runtime
   logged it to stderr at once, with the value at fault and where.
2. The task asking had no answer coming. `ask` noticed the store end,
   and **raised** `(:ended pid result)`: it ended the task with that
   error. `join` gave the task's result, `(:error ...)`.
3. The top level read how the store ended: `(:error (kind value
   site))`, the fault as a value.

The top level went on. A fault ends one process, and the others find
out by waiting for it, with `join`, `read` or `ask`. There's no
exception to catch, and so no stack of handlers to unwind. The way to
contain something that might fail is to run it in a process of its
own, which is why the `call` went in a `fork`. Asked from the top level
directly, the raise would have ended the top level, and with it the
program, with `error: ...` and exit status 1.

The store's table went with it: `"apple"` is gone. That's the cost of a
fault, and why the next two sections exist.

## Expected errors are values

A string where a number should be is the caller's mistake, not the
store's. So the store should say so, and go on. Here's `incr` with that
case as a reply:

<!-- code: ../code/tut-5-checked.slight -->
```lisp
; A value that isn't an integer is the caller's mistake, so it's an
; error in the reply, and the store goes on.
(defun incr (key table)
    (match (table/get key table)
        (:not-found (list (table/put key 1 table) (list :ok 1)))
        (n          (if (int? n)
                        (list (table/put key (+ n 1) table) (list :ok (+ n 1)))
                        (list table (list :error (list :not-an-int key)))))))
```

`int?` is one of the type predicates (`str?`, `cons?`, `nil?` and the
rest). `:not-found` in a pattern is a literal, and matches only itself.

A task that can't go on without a value can turn an error back into an
ending of its own with `raise`:

<!-- code: ../code/tut-5-checked.slight -->
```lisp
; A key's value; a task that needs it, and can't go on without it, ends
; with an error.
(defun must-get (s key)
    (match (call s (list :get key))
        ((:ok value)     value)
        ((:error reason) (raise (list reason key)))))

(let s (spawn (store ())))
(call s (list :put "color" "red"))
(pprint (call s (list :incr "color")))
(pprint (call s (list :incr "hits")))
(pprint (join (fork (must-get s "hits"))))
(join (fork (must-get s "plum")))
```

<!-- output: ../code/tut-5-checked.expected -->
```
(error (not-an-int "color"))
(ok 1)
(ok 1)
(error (not-found "plum"))
```

`(raise reason)` ends the current process with `(:error reason)`, and
whoever joins it gets that. Unlike a fault, it isn't logged: it's a
decision, not a bug. Between them, `(:error ...)` replies and `raise`
cover what you can foresee. The convention is to reply with errors
from actors, which must keep going, and to raise from tasks, which have
one job and can just stop.

## A supervisor

Faults are bugs, and some will get through. An actor can ask to be told
when another process ends, with `monitor`, and start it again. Here's a
**supervisor**: an actor that starts the store, passes it every
message, and starts a fresh one when it ends:

<!-- code: ../code/tut-5-supervisor.slight -->
```lisp
; Starts the store and watches it, and passes it every message. When it
; ends, it starts another, with an empty table.
(defun supervise ()
    (let s (spawn (store ())))
    (monitor s)
    (supervisor s))

(defun supervisor (s)
    (recv
        ((:exit _ result)
            (pprint (list :restarting result))
            (supervise))
        (msg
            (send s msg)
            (supervisor s))))
```

`(monitor s)` asks for `(:exit pid result)` to be sent when `s` ends.
Callers talk to the supervisor's pid, which stays the same however
often the store behind it is replaced. Forwarding works because the
address to answer at is in the message, so the store answers the caller
directly.

That message clause catches every message, so the `:exit` clause has to
come first. The patterns are tried in order.

This is the store from `tut-store.slight`, bug and all:

<!-- code: ../code/tut-5-supervisor.slight -->
```lisp
(let sup (spawn (supervise)))
(call sup (list :put "apple" 3))
(call sup (list :put "color" "red"))
(pprint (join (fork (ask-within 1000 sup (list :do (list :incr "color"))))))
(pprint (call sup (list :get "apple")))
(call sup (list :put "apple" 4))
```

<!-- output: ../code/tut-5-supervisor.expected -->
```
fault in #<pid 3>: not a number: "red" (+ at docs/guide/code/tut-store.slight:23:40)
(restarting (error (not-a-number "red" "+ at docs/guide/code/tut-store.slight:23:40")))
(error (timeout #<pid 2>))
(error not-found)
(ok 4)
```

The store, pid 3, faulted. The supervisor, pid 2, heard, said so, and
started another. The program went on, and the next commands worked,
but on an empty table.

The asker had a problem. Its command died with the old store, so no
answer was coming, and the supervisor it had asked didn't end, so `ask`
had nothing to notice. That's what `ask-within` is for. It's `ask` with
a time limit, in milliseconds, and raises `(:timeout pid)` if nothing
comes. Anything that asks through a middleman, or across a network,
wants one.

A restart gives the program back, but not what the store held. Keeping
the table through a restart means keeping it somewhere a fault can't
take it, which is chapter 7's file.

## Tasks and actors

The tutorial has now used both kinds of process:

- A **task** (`fork`) computes a value. It can wait for other
  processes, with `join`, `read` and `ask`, keeping its stack while it
  waits, so it can ask from deep inside ordinary functions. It can't
  take messages with `recv`. The top level is a task.
- An **actor** (`spawn`) keeps state, in the arguments of a loop that
  takes messages with `recv`. It can be sent to and monitored, and it
  can monitor others. A function that waits in it can't return a value
  to its caller.

Actors own state; tasks do work and hold conversations. When you're
unsure which you need, ask whether anything should be able to send to
it. If so, it's an actor.

---

Previous: [4. Asking and answering](04-asking.md)

The next chapters, on timers and testing, files, and sockets, are still
to come. Meanwhile, [`docs/LANGUAGE.md`](../../LANGUAGE.md) has them
all, under Processes and Devices.
