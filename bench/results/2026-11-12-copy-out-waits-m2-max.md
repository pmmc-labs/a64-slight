# Every wait copied off its stack: on the M2 Max

12 Nov 2026. D292 built for the waits besides `recv`: `join`,
`join/race`, `read`, `read/any`, `open`, `sleep`, `fork/on` and
`timer/cancel` across islands. Against `9b78cd7`, the commit before,
where only `recv`'s were copied. macOS 15.7.9, Apple M2 Max, AArch64,
natively; each program built by each tree's own `slightc` and run in
turns, old then new, the best of seven rounds (`bench/copy-out.py OLD
--rounds 7`). Instructions are `/usr/bin/time -l`'s "instructions
retired", one run each: macOS has no callgrind.

`idle-join` and `idle-sleep` are `idle-memory` with its processes
waiting in `join`, each on the one started before it, and in `sleep`,
on the virtual clock, so that all are asleep at once
(`bench/idle-memory/`).

| | old | new | new ÷ old |
|---|---:|---:|---:|
| `idle-memory 30000`, seconds | 0.016 | 0.016 | 0.99 |
| `idle-memory 30000`, bytes a process | 1,303 | 1,313 | 1.01 |
| `idle-memory 1000000`, seconds | 0.367 | 0.350 | 0.95 |
| `idle-memory 1000000`, bytes a process | 713 | 741 | 1.04 |
| `idle-join 1000000`, seconds | 10.327 | 0.401 | 0.04 |
| `idle-join 1000000`, bytes a process | 8,242 | 699 | 0.08 |
| `idle-sleep 1000000`, seconds | 10.450 | 0.440 | 0.04 |
| `idle-sleep 1000000`, bytes a process | 9,099 | 773 | 0.08 |
| ring of 2, ns a hop | 45 | 45 | 1.00 |
| ring of 503, ns a hop | 50 | 50 | 1.00 |
| ring of 4,000, ns a hop | 65 | 66 | 1.01 |
| ring of 30,000, ns a hop | 72 | 71 | 0.99 |
| `ping-pong 2000000`, seconds | 0.293 | 0.298 | 1.02 |
| `payload 1000000`, seconds | 0.717 | 0.721 | 1.00 |
| `thread-ring 10000000`, seconds | 0.491 | 0.494 | 1.01 |
| `fan-in 10000000`, seconds | 0.366 | 0.365 | 1.00 |

| instructions | old | new | new ÷ old |
|---|---:|---:|---:|
| `ping-pong 2000000` | 4,619,545,620 | 4,620,890,955 | 1.0003 |
| `thread-ring 10000000` | 7,601,950,675 | 7,589,661,695 | 0.9984 |

| new, ring of 2, 5,000,000 hops | waits keep their stacks | every wait copied out and in | the copy, a hop |
|---|---:|---:|---:|
| ns a hop | 45.1 | 65.0 | 19.9 |
| instructions a hop | 760.0 | 1,061.9 | 301.9 |

Before D276 (`2026-11-01-1722`), `idle-memory 1000000`, whose
processes wait in `recv`, took 0.311 s and 615 bytes a process; `join`
and `sleep` have always kept their stacks, so there is no earlier
figure for the other two.

## `ping-pong`, again

`ping-pong`'s 2 per cent is where the code landed, not what it does: it
retires the same instructions, and 21 more rounds gave 0.2895 s old and
0.2972 s new, but with both runtimes built with their functions aligned
(`SLIGHT_CC="cc -falign-functions=64"`, and `16`), 15 rounds each:

| `ping-pong 2000000`, seconds | old | new | new ÷ old |
|---|---:|---:|---:|
| functions aligned to 64 bytes | 0.2911 | 0.2915 | 1.001 |
| functions aligned to 16 bytes | 0.2898 | 0.2908 | 1.003 |

The day before, the same benchmark came out 3 per cent faster for a
change that added 2 per cent to its instructions
(`2026-11-11-copy-out-m2-max.md`).

## What was copied, and what the frame walk refused

Every golden test with `SLIGHT_COPY_OUT=1`, in a build with
`-DCOPY_TALLY`, AArch64: 976,977 waits copied out, 976,105 woken, 15,334
of those onto another stack than they left, 105 bytes a copy on
average; 276 of the 407 tests copied at least once. By wait:

| wait | copied out |
|---|---:|
| `recv` | 832,659 |
| `read`, `read/any` | 102,231 |
| `join` | 32,632 |
| `sleep` | 9,316 |
| `fork/on`, and `connect` from another island | 98 |
| `join/race` | 36 |
| `timer/cancel` | 4 |
| `open` | 1 |

Refused: six, each a process sent 300 frames down on purpose, too deep
to copy, in tests 383 (two) and 385 to 388.
