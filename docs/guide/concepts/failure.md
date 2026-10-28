# Failure

slight has no exceptions. A process ends well or badly, and whoever
cares finds out by waiting for it. This page sorts out the ways things
go wrong, and what each looks like.

## Four ways to fail

<!-- code: ../code/con-failure.slight -->
```lisp
(defun worker (job)
    (match job
        ((:div a b) (div a b))
        ((:check n) (if (< n 0) (raise (list :negative n)) n))))

(pprint (join (fork (worker (list :div 6 3)))))
(pprint (join (fork (worker (list :div 1 0)))))
(pprint (join (fork (worker (list :check -5)))))

(let sleeper (fork (sleep 1000)))
(kill sleeper)
(pprint (join sleeper))

; An actor hears of another process's end by monitoring it.
(defun watcher (job)
    (let p (fork (worker job)))
    (monitor p)
    (recv
        ((:exit _ result) (list :watched result))))

(pprint (read (spawn (watcher (list :check -1)))))

(raise :gave-up)
```

<!-- output: ../code/con-failure.expected -->
```
(ok 2)
fault in #<pid 3>: division by zero (div at docs/guide/code/con-failure.slight:3:21)
(error (div-by-zero () "div at docs/guide/code/con-failure.slight:3:21"))
(error (negative -5))
(error killed)
(ok (watched (error (negative -1))))
error: gave-up
exit: 1
```

1. **An error value.** Something the code expected might happen, such as
   a missing key, bad input, or a file that isn't there, is returned as
   `(:error reason)`, beside `(:ok value)` for success. The caller
   matches on it. This is the normal way, and most failures should be
   this kind. Nothing ends.
2. **A fault.** A bug, such as `car` of `()`, `div` by zero, a `cond`
   with no true clause, adding a string, or an integer overflow. It ends
   the process it happens in, with `(:error (kind value site))`: a
   keyword for what went wrong, the value at fault (or `()`), and where.
   The runtime logs it to stderr at once (`fault in #<pid 3>: ...`),
   because a fault is always a bug, and someone should see it.
3. **A raise.** `(raise reason)` ends the current process with
   `(:error reason)`, on purpose. It's for a task that can't go on: it
   was asked for a value and there isn't one. It isn't logged, except at
   the top level.
4. **A kill.** `(kill pid)` ends another process, wherever it is, with
   `(:error :killed)`. That's for timeouts, cancellation, and processes
   no longer wanted.

## Finding out

There's no `catch`. A failure ends a process, and the processes that
care are told:

- **`join`** or **`read`** gives a task's result, `(:error ...)`
  included. So `(join (fork (f)))` is how to run `f` and turn its
  failure into a value: a task is slight's `try`.
- **`ask`** raises `(:ended pid result)` if the actor it asked ends
  before answering, and `ask-within` raises `(:timeout pid)`. The asking
  task ends too, unless it's in a `fork` of its own.
- **`monitor`**, from an actor, sends `(:exit pid result)` when the
  process ends. It's how a supervisor watches what it started (tutorial,
  chapter 5).
- **The top level.** If the root ends with an error, the program prints
  `error: reason` and exits 1. A program whose root is left waiting for
  something that can never come prints `deadlock: ...`, saying what it
  waited for, and exits 1 too.

A message that no clause of an actor's `recv` matches is neither: it's
dropped, logged as a **dead letter** with the place of the `recv`, and
the actor goes on. It's a bug in the sender, reported where it shows.

## Why no exceptions

- **The process is the unit of failure.** It has its own heap and
  shares nothing, so when it ends, nothing it was halfway through
  changing is left inconsistent. There's nothing to roll back.
- **A fault doesn't spread.** One actor's bug ends that actor. The
  others, and the program, go on, and anything watching can start it
  again.
- **Errors you expect are part of what functions return**, and the
  patterns that take them apart say which case is which. Nothing is
  thrown past a caller that didn't know it might be.

The cost is that containing a failure means a process. `(join (fork
...))` is cheap, but it's a process, and its expression's locals are
copied into it. So handle expected errors as values, and keep
processes for isolation that's worth having.

---

Previous: [Tasks and actors](tasks-and-actors.md) · Next: [Time](time.md)
