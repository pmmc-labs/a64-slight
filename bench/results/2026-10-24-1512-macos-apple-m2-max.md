# Benchmarks, 2026-10-24 15:12:24

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 813eee4, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.34.1
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.002 | 0.001 | 0.001 | 1.2 | 0.74 |
| rust | 0.002 | 0.002 | 0.001 | 1.6 | 0.91 |
| slight | 0.002 | 0.002 | 0.001 | 1.5 | 1.00 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 1.01 |
| perl | 0.004 | 0.003 | 0.003 | 3.2 | 1.73 |
| python | 0.019 | 0.017 | 0.016 | 11.8 | 9.29 |
| chez | 0.035 | 0.035 | 0.034 | 51.9 | 17.31 |
| node | 0.058 | 0.052 | 0.053 | 41.1 | 28.22 |
| erlang | 0.091 | 0.090 | 0.085 | 48.5 | 44.61 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.110 | 0.110 | 1.2 | 0.55 |
| go | 0.124 | 0.123 | 0.123 | 3.5 | 0.62 |
| slight | 0.200 | 0.200 | 0.199 | 1.5 | 1.00 |
| chez | 0.225 | 0.224 | 0.224 | 51.8 | 1.12 |
| node | 0.316 | 0.315 | 0.314 | 43.8 | 1.58 |
| erlang | 0.705 | 0.699 | 0.420 | 48.4 | 3.53 |
| python | 4.642 | 4.634 | 4.641 | 11.8 | 23.24 |
| perl | 13.0 | 13.0 | 13.0 | 4.1 | 65.32 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.082 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.102 | 0.102 | 0.102 | 3.5 | 0.72 |
| slight | 0.141 | 0.141 | 0.140 | 1.5 | 1.00 |
| chez | 0.190 | 0.189 | 0.190 | 51.8 | 1.35 |
| node | 0.289 | 0.288 | 0.288 | 43.9 | 2.05 |
| erlang | 0.519 | 0.510 | 0.308 | 48.4 | 3.68 |
| python | 3.601 | 3.571 | 3.599 | 11.8 | 25.54 |
| perl | 14.2 | 14.0 | 14.2 | 4.2 | 100.80 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.345 | 0.341 | 0.344 | 65.8 | 0.43 |
| node | 0.553 | 0.546 | 0.673 | 362.1 | 0.69 |
| slight | 0.798 | 0.786 | 0.797 | 105.7 | 1.00 |
| erlang | 0.872 | 0.859 | 0.552 | 172.4 | 1.09 |
| c | 0.986 | 0.980 | 0.985 | 20.0 | 1.24 |
| go | 1.369 | 1.364 | 1.367 | 61.0 | 1.72 |
| python | 5.916 | 5.875 | 5.914 | 45.5 | 7.42 |
| perl | 18.6 | 18.6 | 18.6 | 141.9 | 23.32 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.309 | 0.308 | 1.2 | 0.43 |
| chez | 0.391 | 0.389 | 0.390 | 51.8 | 0.55 |
| go | 0.577 | 0.575 | 0.571 | 8.7 | 0.81 |
| erlang | 0.701 | 0.660 | 0.530 | 48.3 | 0.98 |
| slight | 0.711 | 0.710 | 0.711 | 3.9 | 1.00 |
| node | 2.288 | 2.265 | 2.300 | 62.6 | 3.22 |
| python | 6.959 | 6.934 | 6.958 | 11.8 | 9.78 |
| perl | 14.3 | 14.2 | 14.3 | 5.2 | 20.05 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.2 | 0.35 |
| chez | 0.185 | 0.184 | 0.184 | 51.8 | 0.49 |
| slight | 0.379 | 0.379 | 0.378 | 4.4 | 1.00 |
| erlang | 0.519 | 0.510 | 0.350 | 48.5 | 1.37 |
| node | 0.621 | 0.617 | 0.670 | 56.0 | 1.64 |
| go | 1.066 | 1.062 | 1.051 | 8.7 | 2.81 |
| python | 4.054 | 4.037 | 4.052 | 11.9 | 10.69 |
| perl | 8.879 | 8.872 | 8.879 | 4.4 | 23.42 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.037 | 1.2 | 0.03 |
| go | 0.048 | 0.048 | 0.048 | 3.5 | 0.04 |
| node | 0.119 | 0.117 | 0.120 | 47.6 | 0.09 |
| chez | 1.163 | 1.157 | 1.162 | 51.8 | 0.90 |
| slight | 1.287 | 1.278 | 1.287 | 4.8 | 1.00 |
| erlang | 1.331 | 1.304 | 1.238 | 48.5 | 1.03 |
| python | 5.749 | 5.743 | 5.747 | 12.3 | 4.47 |
| perl | 8.388 | 8.008 | 8.387 | 5.7 | 6.51 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.046 | 0.046 | 0.046 | 1.2 | 0.11 |
| go | 0.048 | 0.047 | 0.048 | 3.6 | 0.12 |
| node | 0.120 | 0.116 | 0.123 | 46.3 | 0.29 |
| slight | 0.416 | 0.415 | 0.416 | 4.4 | 1.00 |
| chez | 0.574 | 0.573 | 0.573 | 51.8 | 1.38 |
| erlang | 0.706 | 0.669 | 0.551 | 49.0 | 1.70 |
| python | 5.137 | 5.128 | 5.136 | 12.5 | 12.34 |
| perl | 8.024 | 7.972 | 8.023 | 6.8 | 19.28 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.285 | 0.281 | 0.285 | 2.8 | 1.00 |
| rust | 0.291 | 0.290 | 0.291 | 1.6 | 1.02 |
| go | 0.385 | 0.385 | 0.384 | 3.5 | 1.35 |
| erlang | 3.324 | 3.295 | 1.228 | 48.3 | 11.65 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.158 | 0.158 | 0.158 | 1.6 | 0.28 |
| go | 0.197 | 0.196 | 0.197 | 3.5 | 0.35 |
| slight | 0.566 | 0.562 | 0.566 | 3.1 | 1.00 |
| erlang | 2.686 | 2.621 | 1.628 | 48.5 | 4.74 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.493 | 0.490 | 0.493 | 2.1 | 1.00 |
| rust | 0.696 | 0.695 | 0.696 | 2.5 | 1.41 |
| go | 1.035 | 1.019 | 1.035 | 5.1 | 2.10 |
| erlang | 8.053 | 8.009 | 2.900 | 49.5 | 16.32 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.272 | 0.268 | 0.271 | 93.0 | 0.77 |
| go | 0.287 | 0.286 | 0.286 | 3.8 | 0.81 |
| slight | 0.355 | 0.346 | 0.352 | 483.7 | 1.00 |
| erlang | 1.760 | 1.649 | 1.718 | 650.2 | 4.96 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.137 | 0.135 | 0.136 | 126.5 | 1.00 |
| go | 0.440 | 0.413 | 0.435 | 392.2 | 3.20 |
| erlang | 1.268 | 1.248 | 0.735 | 114.1 | 9.23 |
| rust | 1.391 | 0.357 | 1.391 | 181.2 | 10.12 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.287 | 0.283 | 0.286 | 631.4 | 1.00 | 630 |
| rust | 0.780 | 0.745 | 0.779 | 1094.7 | 2.72 | 1093 |
| go | 1.495 | 1.441 | 1.493 | 2900.7 | 5.21 | 2897 |
| erlang | 2.961 | 2.929 | 1.890 | 2789.4 | 10.31 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.150 | 0.150 | 0.150 | 1.6 | 0.53 |
| go | 0.167 | 0.167 | 0.166 | 3.7 | 0.59 |
| slight | 0.281 | 0.281 | 0.280 | 3.7 | 1.00 |
| erlang | 0.899 | 0.880 | 0.522 | 48.5 | 3.20 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 3 µs | 4 µs | 5 µs | 11 µs | 119 µs | 42.7 ms | 45.7 ms | 531.4 |
| go | 0 µs | 0 µs | 14.0 ms | 29.1 ms | 29.1 ms | 4 µs | 15 µs | 15 µs | 166.2 |
| erlang | 6 µs | 13 µs | 28 µs | 41 µs | 50 µs | 8 µs | 72 µs | 133 µs | 1748.1 |
| rust | 0 µs | 0 µs | 142.0 ms | 281.0 ms | 283.0 ms | 2 µs | 8 µs | 8 µs | 83.7 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 9 µs | 1 µs | 8 µs | 17 µs | 1 µs | 10 µs | 10 µs | 168.0 |
| erlang-mc | 0 µs | 4 µs | 0 µs | 2 µs | 10 µs | 0 µs | 4 µs | 39 µs | 1708.9 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.025 | 0.025 | 0.024 | 2.3 | 0.05 |
| slight | 0.498 | 0.495 | 0.497 | 3.1 | 1.00 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 3.9 | 1.01 | 1.01 |
|  | erlang-mc | 0.089 | 0.230 | 59.6 | 0.98 | 43.61 |
|  | rust-mc | 0.002 | 0.003 | 2.2 | 1.12 | 1.03 |
| fib | go-mc | 0.124 | 0.123 | 3.9 | 1.00 | 0.62 |
|  | erlang-mc | 0.368 | 0.514 | 60.0 | 0.52 | 1.84 |
| tak | go-mc | 0.102 | 0.102 | 4.0 | 1.00 | 0.72 |
|  | erlang-mc | 0.285 | 0.428 | 60.1 | 0.55 | 2.02 |
| binary-trees | go-mc | 0.664 | 1.872 | 41.3 | 0.49 | 0.83 |
|  | erlang-mc | 0.502 | 0.649 | 171.4 | 0.58 | 0.63 |
| nqueens | go-mc | 0.548 | 0.594 | 10.8 | 0.95 | 0.77 |
|  | erlang-mc | 0.477 | 0.621 | 59.9 | 0.68 | 0.67 |
| deriv | go-mc | 0.803 | 0.921 | 11.2 | 0.75 | 2.12 |
|  | erlang-mc | 0.318 | 0.465 | 59.7 | 0.61 | 0.84 |
| n-body | go-mc | 0.048 | 0.048 | 4.0 | 1.00 | 0.04 |
|  | erlang-mc | 1.192 | 1.343 | 60.0 | 0.90 | 0.93 |
| spectral-norm | go-mc | 0.048 | 0.048 | 4.0 | 1.00 | 0.11 |
|  | erlang-mc | 0.488 | 0.636 | 60.6 | 0.69 | 1.17 |
| ping-pong | go-mc | 0.535 | 0.770 | 4.0 | 1.39 | 1.87 |
|  | erlang-mc | 0.985 | 1.122 | 59.9 | 0.30 | 3.45 |
|  | rust-mc | 11.6 | 14.6 | 2.4 | 39.99 | 40.81 |
| payload | go-mc | 0.273 | 0.393 | 4.1 | 1.38 | 0.48 |
|  | erlang-mc | 1.403 | 1.602 | 60.9 | 0.52 | 2.48 |
|  | rust-mc | 5.938 | 7.362 | 2.4 | 37.48 | 10.48 |
| thread-ring | go-mc | 1.453 | 2.079 | 5.8 | 1.40 | 2.94 |
|  | erlang-mc | 2.380 | 2.556 | 61.3 | 0.30 | 4.82 |
|  | rust-mc | 1.187 | 1.932 | 3.3 | 1.70 | 2.40 |
| fan-in | go-mc | 3.551 | 11.9 | 5.2 | 12.38 | 10.01 |
|  | erlang-mc | 1.091 | 8.646 | 657.3 | 0.62 | 3.07 |
|  | rust-mc | 4.116 | 47.9 | 42.9 | 15.16 | 11.60 |
| spawn | go-mc | 0.393 | 1.852 | 27.5 | 0.89 | 2.86 |
|  | erlang-mc | 0.906 | 5.690 | 194.1 | 0.71 | 6.59 |
|  | rust-mc | 0.601 | 1.892 | 11.9 | 0.43 | 4.37 |
| idle-memory | go-mc | 1.045 | 6.009 | 2879.6 | 0.70 | 3.64 |
|  | erlang-mc | 1.891 | 6.345 | 2796.2 | 0.64 | 6.59 |
|  | rust-mc | 1.405 | 7.618 | 1096.8 | 1.80 | 4.89 |
| parallel-fib | go-mc | 0.021 | 0.197 | 5.0 | 0.13 | 0.08 |
|  | erlang-mc | 0.137 | 0.708 | 60.2 | 0.15 | 0.49 |
|  | rust-mc | 0.018 | 0.172 | 2.4 | 0.12 | 0.07 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.06 | 33592 |
| c/binary-trees | 0.06 | 33632 |
| c/deriv | 0.06 | 50440 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.06 | 50008 |
| c/nqueens | 0.06 | 33720 |
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
| erlang/binary-trees | 0.17 | 1516 |
| erlang/deriv | 0.16 | 1612 |
| erlang/fan-in | 0.16 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/latency | 0.17 | 3776 |
| erlang/n-body | 0.18 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.16 | 1412 |
| erlang/payload | 0.17 | 1268 |
| erlang/ping-pong | 0.17 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/spectral-norm | 0.17 | 2228 |
| erlang/startup | 0.17 | 764 |
| erlang/tak | 0.16 | 996 |
| erlang/thread-ring | 0.17 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.10 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/latency | 0.10 | 2482354 |
| go/n-body | 0.10 | 2430274 |
| go/nqueens | 0.10 | 2430130 |
| go/parallel-fib | 0.10 | 2430194 |
| go/payload | 0.10 | 2430226 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.33 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.34 | 817760 |
| rust/latency | 0.45 | 887648 |
| rust/parallel-fib | 0.31 | 787920 |
| rust/payload | 0.31 | 789632 |
| rust/ping-pong | 0.30 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/startup | 2.04 | 708464 |
| rust/thread-ring | 0.34 | 810560 |
| slight/bank | 0.23 | 350088 |
| slight/binary-trees | 0.22 | 333184 |
| slight/deriv | 0.22 | 333032 |
| slight/fan-in | 0.22 | 333208 |
| slight/fib | 0.23 | 332784 |
| slight/idle-memory | 0.22 | 333320 |
| slight/latency | 0.24 | 350280 |
| slight/n-body | 0.25 | 383448 |
| slight/nqueens | 0.22 | 332904 |
| slight/parallel-fib | 0.22 | 332832 |
| slight/payload | 0.22 | 333208 |
| slight/ping-pong | 0.22 | 333224 |
| slight/spawn | 0.22 | 333208 |
| slight/spectral-norm | 0.23 | 349808 |
| slight/startup | 1.56 | 332712 |
| slight/tak | 0.22 | 332784 |
| slight/thread-ring | 0.22 | 333240 |
