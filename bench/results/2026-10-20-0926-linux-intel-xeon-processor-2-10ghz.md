# Benchmarks, 2026-10-20 09:26:31

Intel(R) Xeon(R) Processor @ 2.10GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v139.

- slight: 88499ec, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
- c: cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- go: go version go1.24.7 linux/amd64
- node: v22.22.0
- perl: perl v5.38.2
- python: Python 3.13.16
- rust: rustc 1.97.0 (2d8144b78 2026-07-07), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. A `-mc` implementation is the language's default, every core; without it, one core.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.001 | 0.001 | 0.001 | 2.4 | 1.00 |
| go | 0.002 | 0.001 | 0.002 | 1.7 | 1.17 |
| go-mc | 0.002 | 0.001 | 0.002 | 1.7 | 1.18 |
| rust | 0.002 | 0.002 | 0.002 | 2.7 | 1.23 |
| rust-mc | 0.002 | 0.002 | 0.002 | 2.9 | 1.57 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.421 | 0.414 | 0.418 | 3.4 | 1.00 |
| rust | 0.744 | 0.741 | 0.738 | 2.7 | 1.77 |
| go | 0.973 | 0.906 | 0.976 | 1.7 | 2.31 |
| go-mc | 0.981 | 0.938 | 1.251 | 1.7 | 2.33 |
| rust-mc | 77.9 | 77.6 | 75.3 | 3.0 | 184.92 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.767 | 0.710 | 0.740 | 2.8 | 1.00 |
| rust | 1.571 | 1.529 | 1.559 | 3.6 | 2.05 |
| go-mc | 2.587 | 2.373 | 3.388 | 3.3 | 3.37 |
| go | 2.802 | 2.651 | 2.774 | 3.2 | 3.65 |
| rust-mc | 5.493 | 5.490 | 6.623 | 3.8 | 7.16 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.530 | 0.528 | 0.526 | 2.0 | 0.60 |
| rust | 0.716 | 0.686 | 0.714 | 97.6 | 0.81 |
| slight | 0.886 | 0.874 | 0.878 | 640.1 | 1.00 |
| go-mc | 1.347 | 1.309 | 2.930 | 2.1 | 1.52 |
| rust-mc | 2.195 | 2.162 | 8.359 | 3.7 | 2.48 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.254 | 0.251 | 0.252 | 106.6 | 1.00 |
| rust | 0.503 | 0.483 | 0.478 | 266.6 | 1.98 |
| go-mc | 0.546 | 0.532 | 1.347 | 26.8 | 2.15 |
| go | 0.925 | 0.862 | 0.919 | 144.4 | 3.64 |
| rust-mc | 1.232 | 1.213 | 2.984 | 15.9 | 4.84 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.679 | 0.665 | 0.670 | 453.1 | 1.00 | 451 |
| rust | 1.588 | 1.581 | 1.574 | 1042.9 | 2.34 | 1040 |
| rust-mc | 2.375 | 2.313 | 5.634 | 1035.0 | 3.50 | 1032 |
| go-mc | 3.839 | 3.768 | 7.957 | 2816.8 | 5.66 | 2815 |
| go | 12.9 | 12.2 | 12.8 | 2804.5 | 19.08 | 2803 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust-mc | 0.055 | 0.051 | 0.188 | 3.1 | 0.11 |
| go-mc | 0.084 | 0.084 | 0.321 | 2.0 | 0.17 |
| rust | 0.192 | 0.177 | 0.188 | 2.7 | 0.38 |
| go | 0.313 | 0.308 | 0.306 | 1.9 | 0.62 |
| slight | 0.503 | 0.461 | 0.501 | 2.9 | 1.00 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| go/fan-in | 0.04 | 2230271 |
| go/idle-memory | 0.04 | 2230941 |
| go/parallel-fib | 0.05 | 2231446 |
| go/ping-pong | 0.04 | 2230719 |
| go/spawn | 0.04 | 2230823 |
| go/startup | 0.05 | 2209255 |
| go/thread-ring | 0.04 | 2230931 |
| rust/fan-in | 0.06 | 823800 |
| rust/idle-memory | 0.06 | 864808 |
| rust/parallel-fib | 0.07 | 827128 |
| rust/ping-pong | 0.08 | 826504 |
| rust/spawn | 0.07 | 823872 |
| rust/startup | 0.06 | 744016 |
| rust/thread-ring | 0.06 | 845496 |
| slight/fan-in | 0.33 | 682856 |
| slight/idle-memory | 0.35 | 687656 |
| slight/parallel-fib | 0.34 | 681752 |
| slight/ping-pong | 0.31 | 677496 |
| slight/spawn | 0.32 | 677824 |
| slight/startup | 0.32 | 673432 |
| slight/thread-ring | 0.34 | 687040 |
