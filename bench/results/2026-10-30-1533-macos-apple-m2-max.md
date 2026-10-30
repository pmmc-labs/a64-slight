# Benchmarks, 2026-10-30 15:33:17

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 0d3e608, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-arrays: Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.34.1
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.297 | 0.283 | 0.296 | 1.6 | 0.71 |
| go | 0.391 | 0.387 | 0.391 | 3.5 | 0.94 |
| slight | 0.416 | 0.398 | 0.416 | 2.9 | 1.00 |
| erlang | 3.502 | 3.426 | 1.349 | 48.4 | 8.41 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.160 | 0.158 | 0.160 | 1.6 | 0.26 |
| go | 0.202 | 0.194 | 0.202 | 3.5 | 0.33 |
| slight | 0.618 | 0.596 | 0.617 | 3.1 | 1.00 |
| erlang | 2.743 | 2.713 | 1.683 | 48.6 | 4.44 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.275 | 0.274 | 0.275 | 93.1 | 0.61 |
| go | 0.295 | 0.287 | 0.295 | 3.8 | 0.65 |
| slight | 0.452 | 0.450 | 0.450 | 642.8 | 1.00 |
| erlang | 1.816 | 1.645 | 1.774 | 650.2 | 4.01 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.698 | 0.695 | 0.697 | 2.5 | 0.93 |
| slight | 0.748 | 0.740 | 0.748 | 2.1 | 1.00 |
| go | 1.068 | 1.025 | 1.068 | 5.1 | 1.43 |
| erlang | 8.375 | 8.219 | 3.124 | 49.2 | 11.19 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.171 | 0.166 | 0.171 | 66.4 | 1.00 |
| go | 0.425 | 0.421 | 0.422 | 335.6 | 2.48 |
| rust | 0.438 | 0.252 | 0.438 | 182.8 | 2.56 |
| erlang | 1.298 | 1.256 | 0.758 | 114.1 | 7.57 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 6 µs | 4 µs | 9 µs | 17 µs | 119 µs | 42.7 ms | 46.1 ms | 531.7 |
| go | 0 µs | 3 µs | 11.6 ms | 24.5 ms | 24.5 ms | 3 µs | 14 µs | 14 µs | 166.4 |
| erlang | 5 µs | 14 µs | 29 µs | 44 µs | 62 µs | 7 µs | 72 µs | 103 µs | 1748.1 |
| rust | 0 µs | 0 µs | 144.5 ms | 285.5 ms | 287.5 ms | 2 µs | 4 µs | 4 µs | 83.7 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 8 µs | 1 µs | 10 µs | 23 µs | 1 µs | 8 µs | 8 µs | 168.1 |
| erlang-mc | 0 µs | 6 µs | 0 µs | 5 µs | 13 µs | 0 µs | 6 µs | 36 µs | 1709.0 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.047 | 0.046 | 0.047 | 1.2 | 0.10 |
| go | 0.048 | 0.047 | 0.048 | 3.6 | 0.10 |
| slight-arrays | 0.086 | 0.084 | 0.085 | 43.3 | 0.18 |
| node | 0.120 | 0.116 | 0.124 | 46.3 | 0.25 |
| slight | 0.478 | 0.472 | 0.478 | 3.8 | 1.00 |
| chez | 0.590 | 0.574 | 0.589 | 51.8 | 1.23 |
| erlang | 0.705 | 0.672 | 0.549 | 48.7 | 1.47 |
| python | 5.247 | 5.138 | 5.246 | 12.5 | 10.97 |
| perl | 8.243 | 8.020 | 8.242 | 6.2 | 17.23 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.108 | 0.109 | 1.2 | 0.54 |
| go | 0.125 | 0.123 | 0.124 | 3.5 | 0.61 |
| slight | 0.205 | 0.200 | 0.204 | 1.5 | 1.00 |
| chez | 0.229 | 0.219 | 0.228 | 51.8 | 1.12 |
| node | 0.320 | 0.315 | 0.320 | 44.1 | 1.56 |
| erlang | 0.707 | 0.701 | 0.422 | 48.4 | 3.46 |
| python | 4.745 | 4.605 | 4.744 | 11.8 | 23.19 |
| perl | 13.2 | 13.1 | 13.2 | 4.1 | 64.64 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.349 | 0.342 | 0.348 | 65.8 | 0.41 |
| node | 0.561 | 0.549 | 0.681 | 366.8 | 0.66 |
| slight | 0.847 | 0.835 | 0.846 | 105.7 | 1.00 |
| erlang | 0.888 | 0.873 | 0.568 | 172.4 | 1.05 |
| c | 1.008 | 0.980 | 1.007 | 20.0 | 1.19 |
| go | 1.403 | 1.366 | 1.401 | 59.8 | 1.66 |
| python | 5.996 | 5.901 | 5.994 | 45.5 | 7.08 |
| perl | 19.2 | 18.7 | 19.1 | 141.9 | 22.61 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| ping-pong | go-mc | 0.518 | 0.762 | 4.0 | 1.32 | 1.25 |
|  | erlang-mc | 1.022 | 1.165 | 60.0 | 0.29 | 2.45 |
|  | rust-mc | 12.0 | 14.9 | 2.4 | 40.37 | 28.78 |
| payload | go-mc | 0.266 | 0.388 | 4.1 | 1.31 | 0.43 |
|  | erlang-mc | 1.438 | 1.640 | 60.8 | 0.52 | 2.33 |
|  | rust-mc | 6.005 | 7.465 | 2.5 | 37.53 | 9.72 |
| fan-in | go-mc | 3.567 | 12.1 | 5.3 | 12.09 | 7.88 |
|  | erlang-mc | 1.068 | 8.330 | 661.0 | 0.59 | 2.36 |
|  | rust-mc | 4.125 | 47.9 | 45.6 | 14.99 | 9.12 |
| thread-ring | go-mc | 1.419 | 2.063 | 5.8 | 1.33 | 1.90 |
|  | erlang-mc | 2.339 | 2.483 | 60.6 | 0.28 | 3.13 |
|  | rust-mc | 1.194 | 1.933 | 3.3 | 1.71 | 1.60 |
| spawn | go-mc | 0.388 | 1.885 | 27.5 | 0.91 | 2.26 |
|  | erlang-mc | 0.915 | 5.703 | 194.3 | 0.70 | 5.34 |
|  | rust-mc | 0.610 | 1.949 | 12.0 | 1.39 | 3.56 |
| spectral-norm | go-mc | 0.048 | 0.048 | 3.9 | 1.00 | 0.10 |
|  | erlang-mc | 0.490 | 0.642 | 60.6 | 0.70 | 1.02 |
| fib | go-mc | 0.125 | 0.125 | 3.9 | 1.00 | 0.61 |
|  | erlang-mc | 0.368 | 0.506 | 59.8 | 0.52 | 1.80 |
| binary-trees | go-mc | 0.674 | 1.874 | 40.3 | 0.48 | 0.80 |
|  | erlang-mc | 0.509 | 0.651 | 172.2 | 0.57 | 0.60 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| c/fib | 0.05 | 33472 |
| c/spectral-norm | 0.06 | 33584 |
| chez/binary-trees | 0.04 | 4089 |
| chez/fib | 0.04 | 2166 |
| chez/spectral-norm | 0.04 | 5509 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/fan-in | 0.16 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/latency | 0.17 | 3776 |
| erlang/payload | 0.15 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.15 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.11 | 2430210 |
| go/fan-in | 0.11 | 2430258 |
| go/fib | 0.11 | 2430130 |
| go/latency | 0.14 | 2482354 |
| go/payload | 0.11 | 2430226 |
| go/ping-pong | 0.34 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/thread-ring | 0.11 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/latency | 0.45 | 887648 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 2.04 | 787280 |
| rust/spawn | 0.31 | 785104 |
| rust/thread-ring | 0.34 | 810560 |
| slight-arrays/spectral-norm | 0.26 | 699344 |
| slight/binary-trees | 0.27 | 699504 |
| slight/fan-in | 0.26 | 699528 |
| slight/fib | 0.25 | 699104 |
| slight/latency | 0.28 | 716600 |
| slight/payload | 0.26 | 699528 |
| slight/ping-pong | 2.88 | 699544 |
| slight/spawn | 0.26 | 699528 |
| slight/spectral-norm | 0.27 | 716128 |
| slight/thread-ring | 0.26 | 699560 |
