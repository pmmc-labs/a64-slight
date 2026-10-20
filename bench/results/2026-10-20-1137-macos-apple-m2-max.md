# Benchmarks, 2026-10-20 11:37:33

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 70fea1b, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. A `-mc` implementation is the language's default, every core; without it, one core.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.001 | 0.001 | 0.001 | 1.2 | 0.77 |
| slight | 0.002 | 0.002 | 0.001 | 1.4 | 1.00 |
| rust | 0.002 | 0.002 | 0.001 | 1.6 | 1.04 |
| go-mc | 0.002 | 0.002 | 0.002 | 4.0 | 1.14 |
| rust-mc | 0.002 | 0.002 | 0.003 | 2.2 | 1.17 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 1.19 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.59 |
| python | 0.017 | 0.017 | 0.016 | 12.0 | 9.52 |
| chez | 0.036 | 0.035 | 0.035 | 51.8 | 19.85 |
| node | 0.052 | 0.051 | 0.052 | 41.3 | 28.80 |
| erlang-mc | 0.089 | 0.087 | 0.242 | 59.8 | 49.22 |
| erlang | 0.091 | 0.090 | 0.085 | 48.5 | 50.29 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.295 | 0.291 | 0.294 | 1.6 | 0.83 |
| slight | 0.357 | 0.352 | 0.356 | 2.7 | 1.00 |
| go | 0.391 | 0.387 | 0.390 | 3.5 | 1.09 |
| go-mc | 0.526 | 0.518 | 0.774 | 4.1 | 1.47 |
| erlang-mc | 1.052 | 1.006 | 1.206 | 59.9 | 2.95 |
| erlang | 3.414 | 3.234 | 1.299 | 48.4 | 9.57 |
| rust-mc | 12.0 | 11.8 | 14.9 | 2.5 | 33.61 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.528 | 0.526 | 0.527 | 2.1 | 1.00 |
| rust | 0.695 | 0.692 | 0.695 | 2.5 | 1.32 |
| go | 1.061 | 1.023 | 1.060 | 5.1 | 2.01 |
| rust-mc | 1.180 | 1.176 | 1.910 | 3.3 | 2.24 |
| go-mc | 1.425 | 1.421 | 2.076 | 5.9 | 2.70 |
| erlang-mc | 2.316 | 2.292 | 2.480 | 60.9 | 4.39 |
| erlang | 8.049 | 7.892 | 2.892 | 49.3 | 15.25 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.275 | 0.272 | 0.275 | 93.1 | 0.69 |
| go | 0.289 | 0.287 | 0.289 | 3.9 | 0.73 |
| slight | 0.398 | 0.378 | 0.394 | 483.6 | 1.00 |
| erlang-mc | 1.083 | 1.056 | 8.481 | 671.6 | 2.72 |
| erlang | 1.803 | 1.760 | 1.759 | 650.5 | 4.54 |
| go-mc | 3.586 | 3.552 | 12.1 | 5.3 | 9.02 |
| rust-mc | 4.017 | 3.727 | 46.3 | 25.2 | 10.10 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.138 | 0.137 | 0.136 | 90.1 | 1.00 |
| go-mc | 0.383 | 0.376 | 1.863 | 27.3 | 2.78 |
| go | 0.417 | 0.413 | 0.412 | 342.1 | 3.03 |
| rust-mc | 0.614 | 0.607 | 1.949 | 11.9 | 4.47 |
| rust | 0.732 | 0.382 | 0.730 | 182.8 | 5.32 |
| erlang-mc | 0.905 | 0.901 | 5.746 | 191.9 | 6.58 |
| erlang | 1.276 | 1.242 | 0.737 | 114.7 | 9.28 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.351 | 0.343 | 0.349 | 576.1 | 1.00 | 575 |
| rust | 0.773 | 0.743 | 0.771 | 1106.2 | 2.20 | 1105 |
| go-mc | 1.084 | 1.042 | 6.456 | 2915.1 | 3.09 | 2911 |
| rust-mc | 1.396 | 1.388 | 7.486 | 1097.7 | 3.98 | 1095 |
| go | 1.535 | 1.501 | 1.532 | 2890.0 | 4.38 | 2886 |
| erlang-mc | 1.908 | 1.886 | 6.516 | 2795.4 | 5.44 | 2736 |
| erlang | 2.953 | 2.874 | 1.887 | 2789.7 | 8.42 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust-mc | 0.018 | 0.018 | 0.174 | 2.5 | 0.06 |
| go-mc | 0.021 | 0.021 | 0.200 | 5.0 | 0.08 |
| erlang-mc | 0.139 | 0.138 | 0.724 | 59.7 | 0.50 |
| rust | 0.151 | 0.150 | 0.150 | 1.6 | 0.54 |
| go | 0.167 | 0.167 | 0.167 | 3.7 | 0.59 |
| slight | 0.281 | 0.281 | 0.281 | 3.6 | 1.00 |
| erlang | 0.872 | 0.856 | 0.505 | 48.5 | 3.10 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/startup | 0.04 | 33432 |
| chez/startup | 0.04 | 1412 |
| erlang/fan-in | 0.15 | 1468 |
| erlang/idle-memory | 0.15 | 1312 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/ping-pong | 0.15 | 1196 |
| erlang/spawn | 0.15 | 1264 |
| erlang/startup | 0.16 | 764 |
| erlang/thread-ring | 0.16 | 1572 |
| go/fan-in | 0.09 | 2430258 |
| go/idle-memory | 0.09 | 2430178 |
| go/parallel-fib | 0.09 | 2430194 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/startup | 0.28 | 2429698 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.33 | 817760 |
| rust/parallel-fib | 0.30 | 787920 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/startup | 1.88 | 708464 |
| rust/thread-ring | 0.33 | 810560 |
| slight/fan-in | 0.21 | 296632 |
| slight/idle-memory | 0.21 | 296744 |
| slight/parallel-fib | 0.21 | 296352 |
| slight/ping-pong | 0.21 | 296632 |
| slight/spawn | 0.21 | 296616 |
| slight/startup | 1.29 | 296248 |
| slight/thread-ring | 0.21 | 296664 |
