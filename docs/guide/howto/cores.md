# Use every core

A slight program runs on **islands**: a thread each, one per core
unless `SLIGHT_ISLANDS` says otherwise, each a whole runtime of its
own, with its own processes, heaps and clock. Nothing moves between
them on its own. A program puts an actor on island `k` with
`(spawn/on k expr)`, and spreads its work by putting each piece on
`(% i (islands))`, so it runs the same on one core or twelve.

Here a parallel map works out Collatz's steps for twelve numbers, each
on an island in turn, and reads the answers back in order:

<!-- code: ../code/howto-cores.slight -->
```lisp
; Collatz's steps from n to 1.
(defun steps (n acc)
    (cond
        ((== n 1)       acc)
        ((== (% n 2) 0) (steps (div n 2) (+ acc 1)))
        (#true          (steps (+ (* 3 n) 1) (+ acc 1)))))

; f of each element, the ith worked out on island i mod (islands) and
; sent to an address of its own; the answers read in order.
(defun pmap (f xs)
    (let tos (map (lambda (_) (address)) xs))
    (dotimes 0 (length xs)
        (lambda (i) (spawn/on (% i (islands)) (send (nth i tos) (f (nth i xs))))))
    (map read tos))

(pprint (pmap (lambda (n) (steps n 0)) (range 1 13)))
```

<!-- output: ../code/howto-cores.expected -->
```
(0 1 7 2 5 8 16 3 19 6 14 9)
()
```

Each answer goes to an address of its own, and the addresses are read
in order, so the result doesn't depend on which island finishes first.
That's the habit to keep: gather by where a result belongs, never by
when it came.

## What crosses, and what it costs

A pid says its island: `#<pid 5@2>` is the fifth process on island 2,
and island 0's print as they always have. `send`, `monitor`, `kill` and
`read` work on any pid. A message to another island is copied, as any
message is, and handed to that island through its inbox, so it costs a
little more than one sent at home. A string or array of 4 KB or more
crosses as a handle, without a copy, so a frame of a simulation goes to
the island that draws it for next to nothing.

So put on another island what has work to do: a computation, a band of
a grid, a share of a simulation. Many small messages to one actor from
every island is the worst case, as it is in Go or Rust.
`bench/`'s `slight-mc` programs show both (`docs/BENCHMARKS.md`).

`fork` stays on its parent's island, so a task is always joined at home,
and `join` of another island's pid faults (`:not-a-task`).

## Island 0

Island 0 owns the terminal, the signals and `:system`. A `connect
:keypress` from anywhere starts its actor there, so a program that
computes on islands 1 to 11 still answers keys at once on island 0.
`examples/reaction.slight` is one: a reaction-diffusion simulation cut
into bands of rows, one per island, drawn live on island 0, and the same
picture on one island or twelve. `examples/boids.slight` is another: a
flock as arrays of floats, a share of the birds on each island, each
share handed the whole flock every tick and answering for its own,
while island 0 draws the tick before.

`:system` tells of every island: `(:processes)` lists them all, and
each process says how many messages it sent to and received from other
islands, and their bytes (see [Find a deadlock](deadlocks.md) for
`:system` itself).

## Recording and replaying

`SLIGHT_RECORD=f` writes a record for each island, `f` and `f.1`, `f.2`
and so on, and `SLIGHT_REPLAY=f` replays them together, exactly, however
the threads went the first time ([Find a race](races.md)).
