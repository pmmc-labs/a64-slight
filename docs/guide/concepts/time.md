# Time

A slight program runs on one of two clocks, and what it takes from the
world, the clock included, can be recorded and played back. This page
explains the clocks, what a run depends on, and what that makes
possible.

## Two clocks

The **real clock** is the machine's. `(sleep 500)` takes half a second,
and `(clock/us)` counts microseconds since the program started.

The **virtual clock**, with `SLIGHT_CLOCK=virtual` in the environment,
starts at 0, and moves only when nothing can run. Then it goes straight
to the next timer. So:

<!-- code: ../code/con-time.slight -->
```lisp
(defun fib (n)
    (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

; On the virtual clock, work takes no time.
(let t0 (clock/us))
(fib 20)
(pprint (- (clock/us) t0))

; Timers fire in the order they're due, and those due together in the
; order they were set.
(defun collect (got n)
    (if (== n 0)
        (reverse got)
        (recv (m (collect (cons m got) (- n 1))))))

(defun timed ()
    (after 20 $$ :b)
    (after 10 $$ :a)
    (after 20 $$ :c)
    (send $$ :now)
    (collect () 4))

(pprint (read (spawn (timed))))
(clock/us)
```

<!-- output: ../code/con-time.expected -->
```
0
(ok (now a b c))
20000
```

On the virtual clock, `fib` took no time, and the whole program took
exactly 20 ms, the latest timer. On the real clock the first line would
be how long `fib` took, and the last a little over 20000.

A program means the same on either clock. The virtual one only says
that the computer is infinitely fast. That makes it right for tests:

- Anything with timeouts, retries, expiry or schedules runs in an
  instant, however long the times it uses.
- The times are exact, so a test can say a thing happened at 1500 ms,
  not "about".
- A run goes the same way every time.

The guide's tests, and slight's own, all run on it. Real time is never
used in a test.

A command run with `:exec` is a special case. While a program waits for
a command whose input is closed, so that it can only be working on its
own, the virtual clock stands still and waits for it, for real.

## What a run depends on

A slight program decides almost everything about how it runs by
itself. Its scheduler counts **reductions** (function entries), not
time, so which process runs when, and for how long, is the same every
run. What can make two runs differ is what comes from outside: the
arguments, the environment, the clock, keys typed, what files, sockets
and commands give, and the random numbers' seed.

So:

- **`SLIGHT_RECORD=file`** writes everything a run takes from the
  world to `file`, as it takes it, and **`SLIGHT_REPLAY=file`** runs the
  program again from the record, exactly. A replay touches nothing but
  stdout and stderr: no file is written, no socket opened, no command
  run. A bug that came from the world (an odd reply, a key at the wrong
  moment) can be seen again, as often as it takes.
- **`SLIGHT_SCHED=n`** runs the processes in another order, picked by a
  generator seeded with `n`, the same for the same `n`. A program that
  relies on more than the language promises shows it under some seed.
- **`SLIGHT_SEED=n`** seeds the random numbers. Each process has its
  own generator, split from its parent's when it's forked, so the
  numbers a process draws don't depend on the order processes run in.

## What the language promises

Whatever the order:

- Messages from one process to another arrive in the order they were
  sent.
- An actor takes one message at a time.
- Timers due at the same time fire in the order they were set.

Nothing else about order is promised. Two processes started one after
the other may finish in either order, and messages from two senders may
arrive interleaved either way. A program that's right under every
`SLIGHT_SCHED` seed doesn't depend on more. The
[race how-to](../howto/races.md) shows how to look.

---

Previous: [Failure](failure.md) · Next: [Memory](memory.md)
