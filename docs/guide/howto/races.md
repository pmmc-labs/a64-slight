# Find a race

A program can depend, without meaning to, on the order its processes
happen to run in. It works every time, until something changes the
order. Here two mirrors are raced for a file, and whichever answers
first is used:

<!-- code: ../code/howto-race.slight -->
```lisp
(defun fib (n)
    (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

; Two mirrors of the same file: whichever answers first is used.
(defun fetch (mirror)
    (list mirror (fib 15)))

(let a (fork (fetch "mirror-a")))
(let b (fork (fetch "mirror-b")))
(match (join/race (list a b))
    ((_ (:ok (mirror _))) mirror))
```

<!-- output: ../code/howto-race.expected -->
```
"mirror-a"
```

It prints `mirror-a` every time. The run queue is first in, first out,
and `a` was started first. A test that expects `mirror-a` would pass
for ever. But the language doesn't promise it: two processes doing the
same work can finish in either order.

## Other orders

`SLIGHT_SCHED=n` runs the processes in an order picked by a generator
seeded with `n`: which ready process goes next, and how long each runs.
The same `n` gives the same order every time. So try a few:

```
$ for n in $(seq 1 12); do printf "%s " $n; SLIGHT_SCHED=$n ./race; done
1 "mirror-a"
2 "mirror-b"
3 "mirror-b"
4 "mirror-a"
...
```

Seed 2 is a run where `b` wins. Whatever relied on `a` winning can now
be fixed, and checked against seed 2, and against the rest. A test
suite can run under a loop of seeds like this. Anything that still
passes depends only on what the language promises: messages from one
process to another in the order sent, one message at a time per actor,
and timers due together in the order they were set.

The random numbers don't change with the order. Each process's
generator is split from its parent's when it's forked, so with
`SLIGHT_SEED` set, a process draws the same numbers whatever seed
`SLIGHT_SCHED` has.

## The same run again

`SLIGHT_RECORD=file` keeps everything a run takes from outside: its
arguments, environment, the clock, keys, what files, sockets and
commands gave, and its `SLIGHT_SCHED` and random seeds.
`SLIGHT_REPLAY=file` runs it again from the record, exactly:

```
$ SLIGHT_SCHED=2 SLIGHT_RECORD=race.rec ./race
"mirror-b"
$ SLIGHT_REPLAY=race.rec ./race
"mirror-b"
```

A replay reads nothing from the world and writes nothing to it but
stdout and stderr, so a run that went wrong on a server, against real
sockets, can be replayed on another machine of the same kind, as often
as it takes. The program must be the same one: a replay of a changed
program stops where the run goes another way (`replay: the run goes
another way at entry N`). The record holds the environment, so it's
written for its owner alone; keep it as you would the environment.

[How-to](README.md)
