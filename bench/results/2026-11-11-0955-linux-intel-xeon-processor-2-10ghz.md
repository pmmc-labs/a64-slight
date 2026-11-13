# Benchmarks, 2026-11-11 09:55:57

Intel(R) Xeon(R) Processor @ 2.10GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v378.

- slight: 1a06d52, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
- slight-arrays: Ubuntu clang version 18.1.3 (1ubuntu1)
- slight-mc: Ubuntu clang version 18.1.3 (1ubuntu1)
- c: cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- go: go version go1.24.7 linux/amd64
- erlang: OTP 25, erts 13.2.2.5
- node: v22.23.4
- perl: perl v5.38.2
- python: Python 3.13.18
- chez: Chez Scheme 9.5.8
- rust: rustc 1.97.0 (2d8144b78 2026-07-07), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.003 | 0.002 | 0.002 | 1.4 | 1.00 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.351 | 0.299 | 0.349 | 1.3 | 1.00 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.297 | 0.266 | 0.295 | 1.4 | 1.00 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 2.084 | 1.857 | 2.040 | 102.1 | 1.00 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.759 | 0.678 | 0.747 | 3.0 | 1.00 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.578 | 0.446 | 0.577 | 3.6 | 1.00 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.975 | 0.874 | 0.972 | 3.8 | 1.00 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight-arrays | 0.165 | 0.159 | 0.163 | 40.7 | 0.28 |
| slight | 0.585 | 0.550 | 0.564 | 3.9 | 1.00 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.521 | 0.433 | 0.499 | 2.4 | 1.00 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 1.152 | 0.981 | 1.123 | 1.9 | 1.00 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.749 | 0.717 | 0.734 | 3.7 | 1.00 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 1.381 | 1.211 | 1.330 | 635.5 | 1.00 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.388 | 0.344 | 0.388 | 114.0 | 1.00 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 14.3 | 12.9 | 14.2 | 4554.1 | 1.00 | 4553 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.480 | 0.419 | 0.474 | 1.7 | 1.00 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 548 µs | 7 µs | 400 µs | 4.0 ms | 1.1 ms | 246.9 ms | 279.4 ms | 486.5 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 1.286 | 1.073 | 1.272 | 52.1 | 1.00 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| spectral-norm | slight-mc | 0.069 | 0.201 | 38.6 | 0.42 | 0.12 |
| ping-pong | slight-mc | 0.214 | 0.644 | 5.6 | 0.41 | 0.41 |
| fan-in | slight-mc | 3.764 | 10.8 | 271.8 | 2.73 | 2.73 |
| spawn | slight-mc | 0.156 | 0.463 | 111.2 | 0.40 | 0.40 |
| idle-memory | slight-mc | 5.608 | 17.3 | 4554.2 | 0.39 | 0.39 |
| parallel-fib | slight-mc | 0.143 | 0.497 | 1.7 | 0.30 | 0.30 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| slight-arrays/spectral-norm | 0.52 | 2744040 |
| slight-mc/fan-in | 0.45 | 2734008 |
| slight-mc/idle-memory | 0.46 | 2747072 |
| slight-mc/parallel-fib | 0.43 | 2734312 |
| slight-mc/ping-pong | 0.45 | 2741664 |
| slight-mc/spawn | 0.46 | 2742448 |
| slight-mc/spectral-norm | 0.52 | 2761040 |
| slight/bank | 0.44 | 2751680 |
| slight/binary-trees | 0.44 | 2743880 |
| slight/deriv | 0.57 | 2741624 |
| slight/fan-in | 0.44 | 2733648 |
| slight/fib | 0.50 | 2726792 |
| slight/idle-memory | 0.43 | 2734344 |
| slight/latency | 0.44 | 2769336 |
| slight/n-body | 0.62 | 2836888 |
| slight/nqueens | 0.48 | 2734576 |
| slight/parallel-fib | 0.46 | 2728288 |
| slight/payload | 0.47 | 2732592 |
| slight/ping-pong | 0.44 | 2732312 |
| slight/spawn | 0.49 | 2733088 |
| slight/spectral-norm | 0.56 | 2753296 |
| slight/startup | 0.43 | 2724272 |
| slight/tak | 0.51 | 2727904 |
| slight/thread-ring | 0.43 | 2733560 |
