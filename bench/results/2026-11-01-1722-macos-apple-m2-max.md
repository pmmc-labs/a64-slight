# Benchmarks, 2026-11-01 17:22:12

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: e3df489, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-arrays: Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-mc: Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.001 | 0.001 | 0.001 | 1.2 | 0.72 |
| rust | 0.002 | 0.002 | 0.001 | 1.6 | 0.91 |
| slight | 0.002 | 0.002 | 0.002 | 1.5 | 1.00 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 1.02 |
| perl | 0.003 | 0.003 | 0.003 | 3.3 | 1.45 |
| python | 0.019 | 0.017 | 0.016 | 11.9 | 9.15 |
| chez | 0.035 | 0.035 | 0.034 | 51.9 | 16.77 |
| node | 0.059 | 0.051 | 0.054 | 41.1 | 28.49 |
| erlang | 0.090 | 0.088 | 0.083 | 48.4 | 43.41 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.107 | 0.109 | 1.2 | 0.55 |
| go | 0.123 | 0.120 | 0.123 | 3.5 | 0.62 |
| slight | 0.199 | 0.199 | 0.199 | 1.5 | 1.00 |
| chez | 0.224 | 0.219 | 0.223 | 51.8 | 1.12 |
| node | 0.313 | 0.312 | 0.313 | 43.7 | 1.57 |
| erlang | 0.665 | 0.661 | 0.385 | 48.4 | 3.34 |
| python | 4.661 | 4.534 | 4.659 | 11.8 | 23.39 |
| perl | 12.8 | 12.8 | 12.8 | 4.2 | 64.12 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.082 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.102 | 0.102 | 0.102 | 3.5 | 0.72 |
| slight | 0.141 | 0.141 | 0.141 | 1.5 | 1.00 |
| chez | 0.188 | 0.184 | 0.188 | 51.8 | 1.33 |
| node | 0.287 | 0.285 | 0.287 | 43.9 | 2.03 |
| erlang | 0.519 | 0.502 | 0.308 | 48.6 | 3.67 |
| python | 3.591 | 3.510 | 3.589 | 11.8 | 25.40 |
| perl | 13.9 | 13.9 | 13.9 | 4.3 | 98.39 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.341 | 0.341 | 0.340 | 65.8 | 0.41 |
| node | 0.553 | 0.550 | 0.674 | 366.2 | 0.67 |
| slight | 0.825 | 0.813 | 0.824 | 106.0 | 1.00 |
| erlang | 0.860 | 0.845 | 0.541 | 172.3 | 1.04 |
| c | 0.993 | 0.990 | 0.992 | 20.0 | 1.20 |
| go | 1.373 | 1.364 | 1.371 | 75.1 | 1.66 |
| python | 5.871 | 5.785 | 5.870 | 45.5 | 7.11 |
| perl | 17.9 | 17.8 | 17.9 | 142.1 | 21.64 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.303 | 0.308 | 1.2 | 0.44 |
| chez | 0.388 | 0.382 | 0.387 | 51.8 | 0.55 |
| go | 0.575 | 0.572 | 0.569 | 8.7 | 0.82 |
| slight | 0.704 | 0.704 | 0.704 | 3.8 | 1.00 |
| erlang | 0.708 | 0.650 | 0.537 | 48.4 | 1.00 |
| node | 2.270 | 2.260 | 2.280 | 62.1 | 3.22 |
| python | 6.931 | 6.785 | 6.929 | 11.9 | 9.84 |
| perl | 13.9 | 13.7 | 13.8 | 5.3 | 19.69 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.2 | 0.35 |
| chez | 0.184 | 0.184 | 0.183 | 51.8 | 0.49 |
| slight | 0.380 | 0.379 | 0.379 | 3.8 | 1.00 |
| erlang | 0.517 | 0.493 | 0.348 | 48.5 | 1.36 |
| node | 0.618 | 0.617 | 0.666 | 56.7 | 1.63 |
| go | 1.061 | 1.059 | 1.045 | 8.9 | 2.79 |
| python | 4.072 | 3.999 | 4.071 | 11.9 | 10.72 |
| perl | 8.086 | 8.065 | 8.085 | 4.5 | 21.29 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.037 | 1.2 | 0.04 |
| go | 0.048 | 0.048 | 0.048 | 3.5 | 0.06 |
| node | 0.117 | 0.116 | 0.120 | 47.4 | 0.14 |
| slight | 0.850 | 0.849 | 0.850 | 3.9 | 1.00 |
| chez | 1.160 | 1.158 | 1.159 | 51.8 | 1.36 |
| erlang | 1.299 | 1.291 | 1.205 | 48.6 | 1.53 |
| python | 5.740 | 5.592 | 5.739 | 12.3 | 6.75 |
| perl | 7.407 | 7.288 | 7.406 | 5.9 | 8.71 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.046 | 0.046 | 0.045 | 1.2 | 0.10 |
| go | 0.047 | 0.047 | 0.047 | 3.6 | 0.10 |
| slight-arrays | 0.085 | 0.084 | 0.084 | 43.3 | 0.18 |
| node | 0.117 | 0.116 | 0.121 | 46.6 | 0.25 |
| slight | 0.472 | 0.469 | 0.471 | 3.9 | 1.00 |
| chez | 0.573 | 0.571 | 0.571 | 51.8 | 1.21 |
| erlang | 0.682 | 0.651 | 0.527 | 48.7 | 1.45 |
| python | 5.128 | 5.118 | 5.127 | 12.3 | 10.86 |
| perl | 7.571 | 7.550 | 7.571 | 6.4 | 16.04 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.288 | 0.287 | 0.287 | 2.8 | 1.00 |
| rust | 0.293 | 0.284 | 0.292 | 1.6 | 1.02 |
| go | 0.387 | 0.384 | 0.386 | 3.5 | 1.34 |
| erlang | 3.263 | 3.214 | 1.195 | 48.3 | 11.34 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.158 | 0.153 | 0.157 | 1.6 | 0.29 |
| go | 0.200 | 0.198 | 0.199 | 3.5 | 0.36 |
| slight | 0.552 | 0.551 | 0.552 | 2.3 | 1.00 |
| erlang | 2.773 | 2.525 | 1.711 | 48.6 | 5.02 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.517 | 0.510 | 0.516 | 2.1 | 1.00 |
| rust | 0.695 | 0.676 | 0.694 | 2.5 | 1.34 |
| go | 1.025 | 1.022 | 1.025 | 5.1 | 1.98 |
| erlang | 7.970 | 7.907 | 2.850 | 49.2 | 15.42 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.269 | 0.268 | 0.269 | 93.0 | 0.72 |
| go | 0.287 | 0.285 | 0.286 | 3.9 | 0.77 |
| slight | 0.373 | 0.364 | 0.369 | 483.9 | 1.00 |
| erlang | 1.747 | 1.672 | 1.702 | 650.0 | 4.69 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.132 | 0.131 | 0.131 | 50.4 | 1.00 |
| go | 0.468 | 0.426 | 0.463 | 426.1 | 3.56 |
| erlang | 1.232 | 1.206 | 0.700 | 113.9 | 9.36 |
| rust | 1.309 | 0.347 | 1.308 | 181.7 | 9.95 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.311 | 0.309 | 0.309 | 615.2 | 1.00 | 614 |
| rust | 0.743 | 0.733 | 0.742 | 1106.2 | 2.39 | 1105 |
| go | 1.446 | 1.432 | 1.443 | 2901.0 | 4.65 | 2898 |
| erlang | 2.922 | 2.902 | 1.851 | 2789.8 | 9.40 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.150 | 0.146 | 0.149 | 1.6 | 0.53 |
| go | 0.167 | 0.167 | 0.166 | 3.7 | 0.59 |
| slight | 0.282 | 0.281 | 0.281 | 3.9 | 1.00 |
| erlang | 0.919 | 0.853 | 0.547 | 48.3 | 3.26 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 2 µs | 4 µs | 5 µs | 8 µs | 117 µs | 41.4 ms | 44.3 ms | 531.9 |
| go | 0 µs | 0 µs | 14.5 ms | 29.5 ms | 29.6 ms | 4 µs | 11 µs | 11 µs | 166.2 |
| erlang | 5 µs | 13 µs | 29 µs | 41 µs | 47 µs | 8 µs | 70 µs | 136 µs | 1748.2 |
| rust | 0 µs | 0 µs | 141.7 ms | 280.7 ms | 282.7 ms | 2 µs | 3 µs | 3 µs | 83.8 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 7 µs | 1 µs | 8 µs | 13 µs | 1 µs | 15 µs | 15 µs | 168.1 |
| erlang-mc | 0 µs | 3 µs | 0 µs | 1 µs | 9 µs | 0 µs | 5 µs | 36 µs | 1709.0 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.025 | 0.025 | 0.024 | 2.3 | 0.04 |
| slight | 0.584 | 0.579 | 0.583 | 74.6 | 1.00 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.96 | 0.98 |
|  | erlang-mc | 0.088 | 0.229 | 60.3 | 0.98 | 42.57 |
|  | rust-mc | 0.002 | 0.003 | 2.2 | 1.04 | 0.94 |
| fib | go-mc | 0.123 | 0.123 | 4.0 | 1.00 | 0.62 |
|  | erlang-mc | 0.363 | 0.532 | 59.9 | 0.55 | 1.82 |
| tak | go-mc | 0.102 | 0.102 | 4.0 | 1.00 | 0.72 |
|  | erlang-mc | 0.282 | 0.427 | 59.7 | 0.54 | 2.00 |
| binary-trees | go-mc | 0.658 | 1.839 | 41.2 | 0.48 | 0.80 |
|  | erlang-mc | 0.496 | 0.639 | 171.4 | 0.58 | 0.60 |
| nqueens | go-mc | 0.546 | 0.591 | 10.9 | 0.95 | 0.78 |
|  | erlang-mc | 0.468 | 0.617 | 60.0 | 0.66 | 0.66 |
| deriv | go-mc | 0.801 | 0.912 | 11.1 | 0.75 | 2.11 |
|  | erlang-mc | 0.314 | 0.451 | 59.6 | 0.61 | 0.83 |
| n-body | go-mc | 0.048 | 0.048 | 4.0 | 1.00 | 0.06 |
|  | erlang-mc | 1.189 | 1.337 | 60.0 | 0.92 | 1.40 |
| spectral-norm | slight-mc | 0.019 | 0.141 | 66.8 | 0.23 | 0.04 |
|  | go-mc | 0.047 | 0.047 | 4.0 | 1.00 | 0.10 |
|  | erlang-mc | 0.484 | 0.628 | 60.4 | 0.71 | 1.02 |
| ping-pong | slight-mc | 0.065 | 0.624 | 17.6 | 0.23 | 0.23 |
|  | go-mc | 0.526 | 0.758 | 4.1 | 1.36 | 1.83 |
|  | erlang-mc | 0.982 | 1.146 | 59.5 | 0.30 | 3.41 |
|  | rust-mc | 11.8 | 14.9 | 2.4 | 40.36 | 41.01 |
| payload | go-mc | 0.267 | 0.385 | 4.1 | 1.34 | 0.48 |
|  | erlang-mc | 1.420 | 1.644 | 61.3 | 0.51 | 2.57 |
|  | rust-mc | 5.974 | 7.475 | 2.4 | 37.89 | 10.82 |
| thread-ring | go-mc | 1.441 | 2.058 | 5.8 | 1.41 | 2.79 |
|  | erlang-mc | 2.325 | 2.472 | 61.2 | 0.29 | 4.50 |
|  | rust-mc | 1.192 | 1.953 | 3.3 | 1.72 | 2.31 |
| fan-in | slight-mc | 0.681 | 6.755 | 180.7 | 1.83 | 1.83 |
|  | go-mc | 3.476 | 11.6 | 5.3 | 12.13 | 9.33 |
|  | erlang-mc | 1.105 | 8.613 | 659.0 | 0.63 | 2.97 |
|  | rust-mc | 4.101 | 47.3 | 47.5 | 15.25 | 11.01 |
| spawn | slight-mc | 0.024 | 0.192 | 57.9 | 0.19 | 0.19 |
|  | go-mc | 0.387 | 1.857 | 27.9 | 0.83 | 2.94 |
|  | erlang-mc | 0.897 | 5.640 | 201.2 | 0.73 | 6.81 |
|  | rust-mc | 0.603 | 1.913 | 11.9 | 0.46 | 4.59 |
| idle-memory | slight-mc | 0.102 | 0.875 | 619.3 | 0.33 | 0.33 |
|  | go-mc | 1.040 | 6.112 | 2889.4 | 0.72 | 3.35 |
|  | erlang-mc | 1.885 | 6.322 | 2795.1 | 0.64 | 6.06 |
|  | rust-mc | 1.389 | 7.425 | 1096.9 | 1.87 | 4.47 |
| parallel-fib | slight-mc | 0.039 | 0.343 | 5.8 | 0.14 | 0.14 |
|  | go-mc | 0.021 | 0.194 | 4.9 | 0.13 | 0.08 |
|  | erlang-mc | 0.134 | 0.724 | 60.1 | 0.15 | 0.48 |
|  | rust-mc | 0.019 | 0.171 | 2.5 | 0.13 | 0.07 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.06 | 33592 |
| c/binary-trees | 0.05 | 33632 |
| c/deriv | 0.06 | 50440 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.06 | 50008 |
| c/nqueens | 0.05 | 33720 |
| c/spectral-norm | 0.06 | 33584 |
| c/startup | 0.04 | 33432 |
| c/tak | 0.05 | 33472 |
| chez/binary-trees | 0.04 | 4089 |
| chez/deriv | 0.04 | 4237 |
| chez/fib | 0.04 | 2166 |
| chez/n-body | 0.04 | 11318 |
| chez/nqueens | 0.04 | 3418 |
| chez/spectral-norm | 0.04 | 5509 |
| chez/startup | 0.04 | 1412 |
| chez/tak | 0.04 | 2582 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/deriv | 0.16 | 1612 |
| erlang/fan-in | 0.16 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/latency | 0.17 | 3776 |
| erlang/n-body | 0.17 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.17 | 1412 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.16 | 764 |
| erlang/tak | 0.15 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.11 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/latency | 0.10 | 2482354 |
| go/n-body | 0.11 | 2430274 |
| go/nqueens | 0.11 | 2430130 |
| go/parallel-fib | 0.09 | 2430194 |
| go/payload | 0.10 | 2430226 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.33 | 2429698 |
| go/tak | 0.11 | 2430130 |
| go/thread-ring | 0.09 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.33 | 817760 |
| rust/latency | 0.45 | 887648 |
| rust/parallel-fib | 0.30 | 787920 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.31 | 785104 |
| rust/startup | 2.52 | 708464 |
| rust/thread-ring | 0.33 | 810560 |
| slight-arrays/spectral-norm | 0.26 | 714864 |
| slight-mc/fan-in | 0.26 | 715064 |
| slight-mc/idle-memory | 0.33 | 715576 |
| slight-mc/parallel-fib | 0.26 | 714848 |
| slight-mc/ping-pong | 0.26 | 715144 |
| slight-mc/spawn | 0.26 | 715144 |
| slight-mc/spectral-norm | 0.27 | 715216 |
| slight/bank | 0.26 | 715416 |
| slight/binary-trees | 0.26 | 715040 |
| slight/deriv | 0.61 | 714872 |
| slight/fan-in | 0.26 | 698536 |
| slight/fib | 0.26 | 698128 |
| slight/idle-memory | 0.26 | 715160 |
| slight/latency | 0.28 | 732120 |
| slight/n-body | 0.29 | 748776 |
| slight/nqueens | 0.26 | 698232 |
| slight/parallel-fib | 0.26 | 698160 |
| slight/payload | 0.26 | 698536 |
| slight/ping-pong | 0.26 | 698536 |
| slight/spawn | 0.27 | 698536 |
| slight/spectral-norm | 0.27 | 715120 |
| slight/startup | 2.92 | 698040 |
| slight/tak | 0.26 | 698128 |
| slight/thread-ring | 0.26 | 715080 |
