# A waiting process copied off its stack: the prototype, on the M2 Max

11 Nov 2026. The prototype of PLAN.md's "A waiting process copied off
its stack", for `recv` alone, against `36f76d4`, the commit before it.
macOS 15.7.9, Apple M2 Max, AArch64, natively; each program built by
each tree's own `slightc` and run in turns, old then new, the best of
seven rounds (`bench/copy-out.py OLD --rounds 7`). Instructions are
`/usr/bin/time -l`'s "instructions retired", one run each: macOS has no
callgrind.

## The gate at 1,024 waiting stacks, as committed

| | old | new | new ÷ old |
|---|---:|---:|---:|
| `idle-memory 30000`, seconds | 0.184 | 0.015 | 0.08 |
| `idle-memory 30000`, bytes a process | 17,032 | 1,303 | 0.08 |
| `idle-memory 1000000`, seconds | 10.348 | 0.392 | 0.04 |
| `idle-memory 1000000`, bytes a process | 8,143 | 731 | 0.09 |
| ring of 2, ns a hop | 47 | 45 | 0.96 |
| ring of 503, ns a hop | 50 | 50 | 1.00 |
| ring of 4,000, ns a hop | 95 | 66 | 0.69 |
| ring of 30,000, ns a hop | 149 | 71 | 0.48 |
| `ping-pong 2000000`, seconds | 0.303 | 0.295 | 0.97 |
| `payload 1000000`, seconds | 0.720 | 0.719 | 1.00 |
| `thread-ring 10000000`, seconds | 0.491 | 0.492 | 1.00 |
| `fan-in 10000000`, seconds | 0.364 | 0.366 | 1.00 |

| instructions | old | new | new ÷ old |
|---|---:|---:|---:|
| `ping-pong 2000000` | 4,531,139,386 | 4,618,926,449 | 1.0194 |
| `thread-ring 10000000` | 7,391,606,689 | 7,601,365,565 | 1.0284 |

The instructions more are the gate's bookkeeping, 22 at each wait that
keeps its stack: the count of waiting stacks up and down, and a flag on
the process so that one killed as it waits is counted out.

Before D276 (`2026-11-01-1722`), `idle-memory 1000000` took 0.311 s and
615 bytes a process.

And `idle-memory-mc 1000000`, on every island (12), the best of three:
13.799 s old, 0.173 s new (0.102 s before D276).

## The gate at 256, as the proposal had it

The same script, the same day, `WAITS_KEPT` 256:

| | old | new | new ÷ old |
|---|---:|---:|---:|
| `idle-memory 30000`, seconds | 0.187 | 0.013 | 0.07 |
| `idle-memory 30000`, bytes a process | 17,032 | 886 | 0.05 |
| `idle-memory 1000000`, seconds | 10.487 | 0.387 | 0.04 |
| `idle-memory 1000000`, bytes a process | 6,309 | 719 | 0.11 |
| ring of 2, ns a hop | 47 | 45 | 0.96 |
| ring of 503, ns a hop | 49 | 59 | 1.21 |
| ring of 4,000, ns a hop | 89 | 69 | 0.77 |
| ring of 30,000, ns a hop | 151 | 71 | 0.47 |
| `ping-pong 2000000`, seconds | 0.304 | 0.294 | 0.97 |
| `payload 1000000`, seconds | 0.724 | 0.721 | 1.00 |
| `thread-ring 10000000`, seconds | 0.492 | 0.590 | 1.20 |
| `fan-in 10000000`, seconds | 0.367 | 0.368 | 1.00 |

| instructions | old | new | new ÷ old |
|---|---:|---:|---:|
| `ping-pong 2000000` | 4,532,267,001 | 4,620,241,533 | 1.0194 |
| `thread-ring 10000000` | 7,376,915,510 | 9,031,605,294 | 1.2243 |

`thread-ring`'s 503 are past 256, so 247 of them copied out and in at
every hop: a fifth slower. Hence 1,024.

## Where keeping a stack stops being cheaper than copying

A ring of m processes, 5 million hops, ns a hop, the best of five (two
sweeps, one for each gate; the old column and the last are the first's):

| m | old, every stack kept | new, gate 256 | new, gate 1,024 | new, every wait copied (`SLIGHT_COPY_OUT`) |
|---:|---:|---:|---:|---:|
| 2 | 47 | 45 | 45 | 65 |
| 256 | 49 | 49 | 49 | 68 |
| 503 | 49 | 59 | 50 | 68 |
| 1,000 | 50 | 64 | 52 | 68 |
| 1,500 | 52 | 66 | 57 | 68 |
| 2,000 | 55 | 68 | 61 | 68 |
| 3,000 | 69 | 70 | 64 | 69 |
| 4,000 | 93 | 68 | 65 | 69 |
| 8,000 | 110 | 69 | 69 | 72 |
| 30,000 | 151 | 71 | 72 | 73 |

On this machine a stack kept is cheaper up to about 3,000 waiting, and
dearer past it; the cloud machine's table (BENCHMARKS.md, 11 Nov) has
that at about 1,000.

## What a copy costs

A ring of 2 with `SLIGHT_COPY_OUT`, every wait copied out and every
wake copied in, on one warm stack, against the same ring keeping its
stacks:

| new, ring of 2, 5,000,000 hops | waits keep their stacks | every wait copied out and in | the copy, a hop |
|---|---:|---:|---:|
| ns a hop | 45.2 | 64.9 | 19.7 |
| instructions a hop | 761.3 | 1,055.2 | 293.9 |

Timed apart, in a build with `-DCOPY_TALLY` (a clock read before and
after each, about 17 ns of every figure): copying out 24 ns, copying in
24 ns, so about 7 ns each, 48 bytes each way; the rest of the 20 ns is
the stub, starting the `recv` again, and what the caches make of it.
`idle-memory`'s first copy out is 41 ns, about 24, with the `malloc` of
its buffer, 32 bytes of frame.

## What the frame walk refused

Every golden test with `SLIGHT_COPY_OUT=1`, in the same build, AArch64:
830,914 waits copied out, 830,125 woken, 20,466 of those onto another
stack than they left, 103 bytes a copy on average; 175 of the 400
tests copied at least once. Refused: one, the process test 383 sends 300 frames down, too
deep to copy, on purpose.
