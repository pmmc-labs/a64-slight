# Benchmarks, 2026-10-24 13:32:49

Intel(R) Xeon(R) Processor @ 2.80GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v153.

- slight: 813eee4, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
- c: cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- go: go version go1.24.7 linux/amd64
- node: v22.22.0
- perl: perl v5.38.2
- python: Python 3.13.17
- rust: rustc 1.97.0 (2d8144b78 2026-07-07), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.002 | 0.002 | 0.002 | 1.3 | 0.92 |
| slight | 0.002 | 0.002 | 0.002 | 1.2 | 1.00 |
| go | 0.002 | 0.002 | 0.002 | 1.8 | 1.13 |
| rust | 0.002 | 0.002 | 0.002 | 2.7 | 1.21 |
| perl | 0.004 | 0.003 | 0.003 | 5.1 | 2.01 |
| python | 0.020 | 0.020 | 0.020 | 9.1 | 9.72 |
| node | 0.040 | 0.039 | 0.040 | 45.3 | 19.21 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.124 | 0.102 | 0.123 | 1.7 | 0.25 |
| go | 0.242 | 0.218 | 0.242 | 1.7 | 0.50 |
| slight | 0.486 | 0.441 | 0.480 | 1.2 | 1.00 |
| node | 0.794 | 0.771 | 0.779 | 50.7 | 1.64 |
| python | 7.941 | 7.531 | 7.890 | 9.3 | 16.36 |
| perl | 24.7 | 24.4 | 24.3 | 5.9 | 50.85 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.190 | 0.182 | 0.189 | 1.7 | 0.50 |
| c | 0.198 | 0.189 | 0.196 | 1.7 | 0.52 |
| slight | 0.377 | 0.364 | 0.371 | 1.2 | 1.00 |
| node | 0.687 | 0.598 | 0.675 | 50.7 | 1.82 |
| python | 6.213 | 5.922 | 6.165 | 9.3 | 16.50 |
| perl | 29.8 | 28.9 | 29.3 | 5.8 | 79.15 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 2.291 | 2.204 | 2.270 | 35.2 | 0.90 |
| node | 2.460 | 2.219 | 3.906 | 276.7 | 0.96 |
| slight | 2.552 | 2.525 | 2.523 | 101.9 | 1.00 |
| go | 5.436 | 5.295 | 5.407 | 48.0 | 2.13 |
| python | 13.2 | 11.5 | 13.0 | 42.9 | 5.17 |
| perl | 48.6 | 46.1 | 48.1 | 157.9 | 19.05 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.616 | 0.500 | 0.612 | 1.7 | 0.43 |
| slight | 1.435 | 1.185 | 1.422 | 2.8 | 1.00 |
| go | 1.695 | 1.586 | 1.685 | 6.8 | 1.18 |
| node | 6.849 | 6.627 | 6.821 | 68.5 | 4.77 |
| python | 12.3 | 11.1 | 12.2 | 9.4 | 8.57 |
| perl | 27.7 | 26.7 | 27.5 | 6.0 | 19.31 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.238 | 0.229 | 0.237 | 1.7 | 0.31 |
| slight | 0.774 | 0.749 | 0.763 | 3.5 | 1.00 |
| node | 1.565 | 1.479 | 1.656 | 61.5 | 2.02 |
| go | 3.697 | 3.417 | 3.668 | 6.7 | 4.78 |
| python | 6.979 | 6.700 | 6.890 | 9.4 | 9.02 |
| perl | 16.9 | 16.5 | 16.8 | 6.0 | 21.87 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.090 | 0.086 | 0.090 | 1.7 | 0.05 |
| go | 0.156 | 0.116 | 0.158 | 1.8 | 0.09 |
| node | 0.210 | 0.204 | 0.215 | 53.2 | 0.11 |
| slight | 1.829 | 1.774 | 1.803 | 3.5 | 1.00 |
| python | 7.336 | 7.023 | 7.261 | 10.0 | 4.01 |
| perl | 16.6 | 16.2 | 16.4 | 7.7 | 9.10 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.055 | 0.054 | 0.054 | 2.1 | 0.04 |
| go | 0.074 | 0.071 | 0.075 | 1.7 | 0.05 |
| node | 0.195 | 0.188 | 0.205 | 53.4 | 0.13 |
| slight | 1.451 | 1.263 | 1.406 | 3.7 | 1.00 |
| python | 11.2 | 10.7 | 11.1 | 9.7 | 7.74 |
| perl | 16.2 | 15.5 | 16.0 | 7.8 | 11.15 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.568 | 0.558 | 0.558 | 2.3 | 1.00 |
| rust | 0.748 | 0.723 | 0.736 | 2.8 | 1.32 |
| go | 1.035 | 0.953 | 1.012 | 1.8 | 1.82 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.372 | 0.363 | 0.371 | 2.8 | 0.29 |
| go | 0.586 | 0.484 | 0.582 | 1.8 | 0.45 |
| slight | 1.288 | 1.154 | 1.247 | 1.8 | 1.00 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.793 | 0.780 | 0.791 | 1.6 | 1.00 |
| rust | 1.842 | 1.715 | 1.792 | 3.6 | 2.32 |
| go | 2.860 | 2.798 | 2.853 | 3.2 | 3.61 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.661 | 0.590 | 0.635 | 2.1 | 0.18 |
| rust | 0.677 | 0.631 | 0.671 | 97.5 | 0.18 |
| slight | 3.714 | 3.132 | 3.659 | 638.9 | 1.00 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.396 | 0.364 | 0.390 | 113.9 | 1.00 |
| rust | 0.672 | 0.583 | 0.657 | 266.6 | 1.70 |
| go | 1.588 | 1.521 | 1.582 | 96.8 | 4.01 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 2.354 | 2.140 | 2.333 | 458.1 | 1.00 | 457 |
| rust | 2.788 | 2.670 | 2.758 | 1042.9 | 1.18 | 1040 |
| go | 25.6 | 25.3 | 25.4 | 2805.2 | 10.87 | 2803 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.247 | 0.195 | 0.232 | 2.7 | 0.37 |
| go | 0.319 | 0.299 | 0.320 | 2.1 | 0.47 |
| slight | 0.674 | 0.592 | 0.655 | 1.8 | 1.00 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 1 µs | 33 µs | 10 µs | 483 µs | 2.1 ms | 365 µs | 145.7 ms | 167.4 ms | 486.5 |
| go | 1 µs | 73 µs | 10.0 ms | 20.3 ms | 21.2 ms | 18 µs | 107 µs | 214 µs | 164.9 |
| rust | 1 µs | 8 µs | 162.3 ms | 321.2 ms | 324.2 ms | 13 µs | 43 µs | 384 µs | 82.8 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 10 µs | 540 µs | 8 µs | 222 µs | 1.5 ms | 7 µs | 267 µs | 669 µs | 166.3 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.058 | 0.051 | 0.054 | 2.7 | 0.05 |
| slight | 1.187 | 1.113 | 1.183 | 2.1 | 1.00 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.003 | 0.003 | 1.9 | 1.09 | 1.23 |
|  | rust-mc | 0.003 | 0.004 | 2.9 | 1.22 | 1.48 |
| fib | go-mc | 0.235 | 0.227 | 1.7 | 0.97 | 0.48 |
| tak | go-mc | 0.189 | 0.189 | 1.7 | 1.00 | 0.50 |
| binary-trees | go-mc | 3.565 | 6.887 | 39.4 | 0.66 | 1.40 |
| nqueens | go-mc | 2.108 | 2.301 | 8.1 | 1.24 | 1.47 |
| deriv | go-mc | 4.077 | 4.613 | 8.4 | 1.10 | 5.27 |
| n-body | go-mc | 0.123 | 0.122 | 1.9 | 0.79 | 0.07 |
| spectral-norm | go-mc | 0.073 | 0.074 | 1.9 | 0.98 | 0.05 |
| ping-pong | go-mc | 1.054 | 1.439 | 1.8 | 1.02 | 1.85 |
|  | rust-mc | 113.3 | 114.6 | 3.0 | 151.32 | 199.37 |
| payload | go-mc | 0.548 | 0.749 | 1.8 | 0.94 | 0.43 |
|  | rust-mc | 56.5 | 56.7 | 3.1 | 152.07 | 43.86 |
| thread-ring | go-mc | 3.375 | 4.617 | 3.2 | 1.18 | 4.26 |
|  | rust-mc | 6.766 | 8.122 | 3.7 | 3.67 | 8.53 |
| fan-in | go-mc | 2.221 | 4.976 | 2.2 | 3.36 | 0.60 |
|  | rust-mc | 4.461 | 16.9 | 4.4 | 6.59 | 1.20 |
| spawn | go-mc | 0.669 | 1.618 | 31.7 | 0.42 | 1.69 |
|  | rust-mc | 1.680 | 4.113 | 13.9 | 2.50 | 4.25 |
| idle-memory | go-mc | 10.3 | 18.9 | 2814.9 | 0.40 | 4.38 |
|  | rust-mc | 3.771 | 9.182 | 1034.9 | 1.35 | 1.60 |
| parallel-fib | go-mc | 0.097 | 0.341 | 2.1 | 0.30 | 0.14 |
|  | rust-mc | 0.068 | 0.247 | 3.2 | 0.27 | 0.10 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.11 | 16176 |
| c/binary-trees | 0.26 | 20288 |
| c/deriv | 0.16 | 17184 |
| c/fib | 0.16 | 16040 |
| c/n-body | 0.17 | 16368 |
| c/nqueens | 0.18 | 16144 |
| c/spectral-norm | 0.13 | 16168 |
| c/startup | 0.07 | 15960 |
| c/tak | 0.07 | 16040 |
| go/binary-trees | 0.29 | 2231034 |
| go/deriv | 0.31 | 2236851 |
| go/fan-in | 0.30 | 2230271 |
| go/fib | 0.28 | 2230446 |
| go/idle-memory | 0.28 | 2230941 |
| go/latency | 0.52 | 2292888 |
| go/n-body | 0.30 | 2231717 |
| go/nqueens | 0.29 | 2230990 |
| go/parallel-fib | 0.28 | 2231446 |
| go/payload | 0.27 | 2230879 |
| go/ping-pong | 0.31 | 2230719 |
| go/spawn | 0.27 | 2230823 |
| go/spectral-norm | 0.34 | 2244557 |
| go/startup | 8.96 | 2209255 |
| go/tak | 0.29 | 2229998 |
| go/thread-ring | 0.30 | 2230931 |
| rust/fan-in | 0.68 | 823800 |
| rust/idle-memory | 0.91 | 864808 |
| rust/latency | 1.31 | 939464 |
| rust/parallel-fib | 0.70 | 827128 |
| rust/payload | 0.78 | 831440 |
| rust/ping-pong | 0.76 | 826504 |
| rust/spawn | 0.72 | 823872 |
| rust/startup | 15.20 | 744016 |
| rust/thread-ring | 0.82 | 845496 |
| slight/bank | 0.47 | 2035288 |
| slight/binary-trees | 0.55 | 2030728 |
| slight/deriv | 0.56 | 2024608 |
| slight/fan-in | 0.49 | 2017688 |
| slight/fib | 0.46 | 2013872 |
| slight/idle-memory | 0.48 | 2018400 |
| slight/latency | 0.54 | 2053536 |
| slight/n-body | 0.53 | 2109040 |
| slight/nqueens | 0.48 | 2017472 |
| slight/parallel-fib | 0.51 | 2015664 |
| slight/payload | 0.54 | 2016688 |
| slight/ping-pong | 0.53 | 2016424 |
| slight/spawn | 0.53 | 2016752 |
| slight/spectral-norm | 0.51 | 2035872 |
| slight/startup | 0.47 | 2011432 |
| slight/tak | 0.51 | 2014984 |
| slight/thread-ring | 0.47 | 2017768 |
