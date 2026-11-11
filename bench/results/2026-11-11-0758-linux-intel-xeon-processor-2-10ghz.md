# Benchmarks, 2026-11-11 07:58:50

Intel(R) Xeon(R) Processor @ 2.10GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v378.

- slight: c6fe222, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
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
| slight | 0.002 | 0.002 | 0.002 | 1.4 | 1.00 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.354 | 0.292 | 0.354 | 1.4 | 1.00 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.280 | 0.250 | 0.268 | 1.4 | 1.00 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 1.933 | 1.670 | 1.914 | 102.1 | 1.00 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.762 | 0.677 | 0.729 | 3.0 | 1.00 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.463 | 0.424 | 0.449 | 3.6 | 1.00 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 1.040 | 0.694 | 1.033 | 3.8 | 1.00 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight-arrays | 0.155 | 0.132 | 0.141 | 40.7 | 0.25 |
| slight | 0.610 | 0.557 | 0.594 | 3.9 | 1.00 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.536 | 0.416 | 0.532 | 2.5 | 1.00 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 1.123 | 0.947 | 1.112 | 1.9 | 1.00 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.742 | 0.679 | 0.734 | 1.8 | 1.00 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.959 | 0.900 | 0.945 | 639.1 | 1.00 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.250 | 0.234 | 0.240 | 65.8 | 1.00 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.776 | 0.710 | 0.766 | 442.3 | 1.00 | 441 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.457 | 0.416 | 0.453 | 1.9 | 1.00 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 1 µs | 530 µs | 8 µs | 98 µs | 492 µs | 455 µs | 137.7 ms | 157.4 ms | 486.4 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 1.191 | 1.017 | 1.170 | 50.8 | 1.00 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| spectral-norm | slight-mc | 0.052 | 0.165 | 40.7 | 0.33 | 0.08 |
| ping-pong | slight-mc | 0.165 | 0.540 | 5.6 | 0.31 | 0.31 |
| fan-in | slight-mc | 3.655 | 10.2 | 245.5 | 3.81 | 3.81 |
| spawn | slight-mc | 0.111 | 0.341 | 66.9 | 0.45 | 0.45 |
| idle-memory | slight-mc | 0.322 | 1.022 | 449.5 | 0.42 | 0.42 |
| parallel-fib | slight-mc | 0.130 | 0.438 | 1.9 | 0.28 | 0.28 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| slight-arrays/spectral-norm | 0.43 | 2741880 |
| slight-mc/fan-in | 0.47 | 2733184 |
| slight-mc/idle-memory | 0.52 | 2755240 |
| slight-mc/parallel-fib | 0.46 | 2732456 |
| slight-mc/ping-pong | 0.46 | 2740872 |
| slight-mc/spawn | 0.47 | 2741272 |
| slight-mc/spectral-norm | 0.46 | 2763224 |
| slight/bank | 0.51 | 2754664 |
| slight/binary-trees | 0.47 | 2741728 |
| slight/deriv | 1.02 | 2735376 |
| slight/fan-in | 0.49 | 2732808 |
| slight/fib | 0.46 | 2724632 |
| slight/idle-memory | 0.54 | 2733520 |
| slight/latency | 0.49 | 2773120 |
| slight/n-body | 0.45 | 2846968 |
| slight/nqueens | 0.43 | 2732424 |
| slight/parallel-fib | 0.47 | 2730520 |
| slight/payload | 0.48 | 2731744 |
| slight/ping-pong | 0.46 | 2731480 |
| slight/spawn | 0.48 | 2731872 |
| slight/spectral-norm | 0.47 | 2755192 |
| slight/startup | 0.43 | 2718024 |
| slight/tak | 0.45 | 2725760 |
| slight/thread-ring | 0.48 | 2732736 |
