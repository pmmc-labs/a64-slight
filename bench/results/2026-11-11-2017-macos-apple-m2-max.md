# Benchmarks, 2026-11-11 20:17:32

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: d076751, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-arrays: Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-mc: Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.13.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.001 | 0.001 | 0.001 | 1.2 | 0.68 |
| rust | 0.002 | 0.002 | 0.001 | 1.6 | 0.90 |
| slight | 0.002 | 0.002 | 0.002 | 1.5 | 1.00 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 1.02 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.39 |
| python | 0.017 | 0.017 | 0.016 | 11.9 | 8.49 |
| chez | 0.035 | 0.034 | 0.034 | 51.8 | 17.12 |
| node | 0.054 | 0.052 | 0.053 | 40.8 | 26.25 |
| erlang | 0.091 | 0.090 | 0.085 | 48.6 | 44.73 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.108 | 0.110 | 1.2 | 0.55 |
| go | 0.123 | 0.123 | 0.123 | 3.5 | 0.61 |
| slight | 0.201 | 0.200 | 0.200 | 1.5 | 1.00 |
| chez | 0.225 | 0.220 | 0.224 | 51.8 | 1.12 |
| node | 0.317 | 0.316 | 0.317 | 43.6 | 1.57 |
| erlang | 0.695 | 0.685 | 0.411 | 48.5 | 3.45 |
| python | 4.645 | 4.630 | 4.644 | 11.8 | 23.08 |
| perl | 12.8 | 12.8 | 12.8 | 4.2 | 63.62 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.082 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.102 | 0.102 | 0.102 | 3.5 | 0.72 |
| slight | 0.142 | 0.141 | 0.141 | 1.5 | 1.00 |
| chez | 0.186 | 0.186 | 0.186 | 51.8 | 1.31 |
| node | 0.290 | 0.286 | 0.289 | 43.3 | 2.04 |
| erlang | 0.521 | 0.504 | 0.311 | 48.6 | 3.68 |
| python | 3.607 | 3.563 | 3.606 | 11.8 | 25.45 |
| perl | 14.1 | 13.9 | 14.1 | 4.3 | 99.34 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.343 | 0.341 | 0.341 | 65.8 | 0.42 |
| node | 0.553 | 0.550 | 0.673 | 364.9 | 0.67 |
| slight | 0.821 | 0.815 | 0.820 | 105.9 | 1.00 |
| erlang | 0.866 | 0.858 | 0.547 | 172.3 | 1.05 |
| c | 0.987 | 0.976 | 0.986 | 20.0 | 1.20 |
| go | 1.375 | 1.367 | 1.370 | 61.6 | 1.67 |
| python | 5.909 | 5.894 | 5.908 | 45.5 | 7.19 |
| perl | 17.9 | 17.7 | 17.9 | 142.1 | 21.84 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.300 | 0.309 | 1.2 | 0.44 |
| chez | 0.388 | 0.378 | 0.386 | 51.8 | 0.55 |
| go | 0.578 | 0.574 | 0.571 | 8.8 | 0.82 |
| erlang | 0.668 | 0.655 | 0.498 | 48.5 | 0.94 |
| slight | 0.709 | 0.706 | 0.708 | 3.7 | 1.00 |
| node | 2.286 | 2.285 | 2.298 | 62.1 | 3.23 |
| python | 6.961 | 6.941 | 6.959 | 11.9 | 9.82 |
| perl | 13.9 | 13.8 | 13.9 | 5.2 | 19.56 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.133 | 0.133 | 0.133 | 1.2 | 0.35 |
| chez | 0.185 | 0.185 | 0.185 | 51.8 | 0.49 |
| slight | 0.381 | 0.379 | 0.380 | 3.8 | 1.00 |
| erlang | 0.504 | 0.500 | 0.333 | 48.4 | 1.32 |
| node | 0.620 | 0.618 | 0.670 | 56.0 | 1.63 |
| go | 1.063 | 1.060 | 1.048 | 9.0 | 2.79 |
| python | 4.070 | 4.055 | 4.068 | 11.9 | 10.69 |
| perl | 8.146 | 8.112 | 8.139 | 4.4 | 21.40 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.037 | 1.2 | 0.04 |
| go | 0.048 | 0.048 | 0.048 | 3.5 | 0.06 |
| node | 0.118 | 0.117 | 0.121 | 47.2 | 0.14 |
| slight | 0.849 | 0.845 | 0.848 | 3.9 | 1.00 |
| chez | 1.168 | 1.160 | 1.166 | 51.8 | 1.38 |
| erlang | 1.315 | 1.304 | 1.223 | 48.4 | 1.55 |
| python | 5.761 | 5.686 | 5.756 | 12.3 | 6.79 |
| perl | 7.320 | 7.294 | 7.319 | 5.9 | 8.62 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.046 | 0.046 | 0.046 | 1.2 | 0.10 |
| go | 0.047 | 0.047 | 0.047 | 3.6 | 0.10 |
| slight-arrays | 0.084 | 0.084 | 0.083 | 43.4 | 0.18 |
| node | 0.119 | 0.117 | 0.122 | 45.8 | 0.25 |
| slight | 0.472 | 0.466 | 0.471 | 3.8 | 1.00 |
| chez | 0.576 | 0.575 | 0.575 | 51.8 | 1.22 |
| erlang | 0.664 | 0.649 | 0.508 | 48.6 | 1.41 |
| python | 5.161 | 5.151 | 5.158 | 12.3 | 10.94 |
| perl | 7.573 | 7.513 | 7.572 | 6.4 | 16.06 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.291 | 0.290 | 0.291 | 1.6 | 0.99 |
| slight | 0.293 | 0.291 | 0.292 | 2.8 | 1.00 |
| go | 0.387 | 0.384 | 0.387 | 3.6 | 1.32 |
| erlang | 3.321 | 3.207 | 1.223 | 48.6 | 11.33 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.158 | 0.157 | 0.157 | 1.6 | 0.22 |
| go | 0.198 | 0.197 | 0.197 | 3.5 | 0.27 |
| slight | 0.725 | 0.716 | 0.725 | 3.1 | 1.00 |
| erlang | 2.711 | 2.566 | 1.649 | 48.7 | 3.74 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.495 | 0.492 | 0.494 | 10.4 | 1.00 |
| rust | 0.691 | 0.690 | 0.691 | 2.4 | 1.39 |
| go | 1.052 | 1.030 | 1.052 | 5.1 | 2.12 |
| erlang | 8.145 | 7.829 | 2.960 | 49.6 | 16.44 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.274 | 0.269 | 0.273 | 93.0 | 0.74 |
| go | 0.288 | 0.286 | 0.286 | 3.9 | 0.77 |
| slight | 0.372 | 0.366 | 0.369 | 479.7 | 1.00 |
| erlang | 1.665 | 1.630 | 1.623 | 650.1 | 4.47 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.144 | 0.142 | 0.144 | 126.5 | 1.00 |
| go | 0.440 | 0.419 | 0.435 | 351.1 | 3.05 |
| erlang | 1.226 | 1.201 | 0.696 | 114.0 | 8.50 |
| rust | 2.105 | 0.447 | 2.105 | 181.2 | 14.59 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.401 | 0.395 | 0.400 | 732.9 | 1.00 | 731 |
| rust | 0.758 | 0.755 | 0.757 | 1106.2 | 1.89 | 1105 |
| go | 1.532 | 1.512 | 1.523 | 2890.1 | 3.82 | 2887 |
| erlang | 2.955 | 2.919 | 1.893 | 2789.9 | 7.38 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.150 | 0.150 | 0.150 | 1.6 | 0.53 |
| go | 0.168 | 0.167 | 0.168 | 3.7 | 0.59 |
| slight | 0.283 | 0.282 | 0.282 | 2.7 | 1.00 |
| erlang | 0.891 | 0.852 | 0.520 | 48.6 | 3.15 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 6 µs | 4 µs | 5 µs | 10 µs | 125 µs | 44.5 ms | 48.6 ms | 595.5 |
| go | 0 µs | 4 µs | 11.8 ms | 24.5 ms | 24.5 ms | 4 µs | 31 µs | 31 µs | 166.1 |
| erlang | 5 µs | 13 µs | 28 µs | 41 µs | 52 µs | 9 µs | 73 µs | 189 µs | 1748.4 |
| rust | 0 µs | 0 µs | 142.9 ms | 281.9 ms | 283.9 ms | 1 µs | 3 µs | 3 µs | 83.7 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 8 µs | 1 µs | 11 µs | 14 µs | 1 µs | 13 µs | 13 µs | 168.0 |
| erlang-mc | 0 µs | 4 µs | 0 µs | 1 µs | 10 µs | 0 µs | 4 µs | 42 µs | 1709.7 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.025 | 0.024 | 0.024 | 2.3 | 0.04 |
| slight | 0.624 | 0.622 | 0.623 | 76.2 | 1.00 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.96 | 0.98 |
|  | erlang-mc | 0.088 | 0.236 | 60.1 | 0.97 | 43.24 |
|  | rust-mc | 0.002 | 0.003 | 2.2 | 1.16 | 1.04 |
| fib | go-mc | 0.123 | 0.123 | 4.0 | 1.00 | 0.61 |
|  | erlang-mc | 0.362 | 0.504 | 59.9 | 0.52 | 1.80 |
| tak | go-mc | 0.103 | 0.103 | 4.0 | 1.01 | 0.73 |
|  | erlang-mc | 0.283 | 0.425 | 59.9 | 0.54 | 2.00 |
| binary-trees | go-mc | 0.663 | 1.864 | 39.3 | 0.48 | 0.81 |
|  | erlang-mc | 0.499 | 0.649 | 172.1 | 0.58 | 0.61 |
| nqueens | go-mc | 0.542 | 0.589 | 10.9 | 0.94 | 0.76 |
|  | erlang-mc | 0.464 | 0.602 | 59.7 | 0.69 | 0.66 |
| deriv | go-mc | 0.800 | 0.913 | 10.9 | 0.75 | 2.10 |
|  | erlang-mc | 0.313 | 0.459 | 59.9 | 0.62 | 0.82 |
| n-body | go-mc | 0.048 | 0.048 | 3.9 | 1.00 | 0.06 |
|  | erlang-mc | 1.197 | 1.347 | 59.8 | 0.91 | 1.41 |
| spectral-norm | slight-mc | 0.017 | 0.136 | 70.2 | 0.21 | 0.04 |
|  | go-mc | 0.047 | 0.047 | 4.0 | 1.00 | 0.10 |
|  | erlang-mc | 0.487 | 0.623 | 60.0 | 0.73 | 1.03 |
| ping-pong | slight-mc | 0.065 | 0.648 | 17.4 | 0.22 | 0.22 |
|  | go-mc | 0.538 | 0.794 | 4.1 | 1.39 | 1.83 |
|  | erlang-mc | 0.997 | 1.142 | 59.5 | 0.30 | 3.40 |
|  | rust-mc | 11.9 | 15.0 | 2.4 | 40.98 | 40.72 |
| payload | go-mc | 0.275 | 0.403 | 4.1 | 1.39 | 0.38 |
|  | erlang-mc | 1.381 | 1.586 | 61.0 | 0.51 | 1.90 |
|  | rust-mc | 6.074 | 7.552 | 2.4 | 38.50 | 8.37 |
| thread-ring | go-mc | 1.481 | 2.180 | 5.8 | 1.41 | 2.99 |
|  | erlang-mc | 3.030 | 2.555 | 61.4 | 0.37 | 6.11 |
|  | rust-mc | 1.175 | 1.931 | 3.2 | 1.70 | 2.37 |
| fan-in | slight-mc | 0.718 | 7.242 | 138.2 | 1.93 | 1.93 |
|  | go-mc | 3.525 | 11.9 | 5.3 | 12.26 | 9.47 |
|  | erlang-mc | 1.097 | 8.555 | 672.6 | 0.66 | 2.95 |
|  | rust-mc | 4.178 | 48.7 | 37.6 | 15.27 | 11.22 |
| spawn | slight-mc | 0.030 | 0.234 | 129.3 | 0.21 | 0.21 |
|  | go-mc | 0.387 | 1.859 | 27.4 | 0.88 | 2.68 |
|  | erlang-mc | 0.908 | 5.642 | 192.0 | 0.74 | 6.29 |
|  | rust-mc | 0.602 | 1.909 | 12.0 | 0.29 | 4.17 |
| idle-memory | slight-mc | 0.194 | 1.370 | 919.7 | 0.48 | 0.48 |
|  | go-mc | 1.021 | 6.132 | 2919.6 | 0.67 | 2.55 |
|  | erlang-mc | 1.902 | 6.366 | 2795.0 | 0.64 | 4.75 |
|  | rust-mc | 1.403 | 7.580 | 1096.6 | 1.85 | 3.50 |
| parallel-fib | slight-mc | 0.036 | 0.345 | 4.6 | 0.13 | 0.13 |
|  | go-mc | 0.021 | 0.201 | 5.0 | 0.13 | 0.07 |
|  | erlang-mc | 0.139 | 0.719 | 60.0 | 0.16 | 0.49 |
|  | rust-mc | 0.018 | 0.174 | 2.5 | 0.12 | 0.06 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.07 | 33592 |
| c/binary-trees | 0.06 | 33632 |
| c/deriv | 0.06 | 50440 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.06 | 50008 |
| c/nqueens | 0.07 | 33720 |
| c/spectral-norm | 0.06 | 33584 |
| c/startup | 0.05 | 33432 |
| c/tak | 0.05 | 33472 |
| chez/binary-trees | 0.04 | 4088 |
| chez/deriv | 0.04 | 4237 |
| chez/fib | 0.04 | 2166 |
| chez/n-body | 0.04 | 11318 |
| chez/nqueens | 0.05 | 3418 |
| chez/spectral-norm | 0.04 | 5509 |
| chez/startup | 0.04 | 1412 |
| chez/tak | 0.04 | 2582 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/deriv | 0.16 | 1612 |
| erlang/fan-in | 0.15 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/latency | 0.18 | 3776 |
| erlang/n-body | 0.18 | 4064 |
| erlang/nqueens | 0.17 | 1200 |
| erlang/parallel-fib | 0.16 | 1412 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.15 | 1196 |
| erlang/spawn | 0.15 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.16 | 764 |
| erlang/tak | 0.16 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.10 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/latency | 0.11 | 2482354 |
| go/n-body | 0.10 | 2430274 |
| go/nqueens | 0.10 | 2430130 |
| go/parallel-fib | 0.10 | 2430194 |
| go/payload | 0.09 | 2430226 |
| go/ping-pong | 0.09 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.30 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.33 | 817760 |
| rust/latency | 0.47 | 887648 |
| rust/parallel-fib | 0.30 | 787920 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.31 | 785104 |
| rust/startup | 1.93 | 708464 |
| rust/thread-ring | 0.33 | 810560 |
| slight-arrays/spectral-norm | 0.27 | 734048 |
| slight-mc/fan-in | 0.27 | 717624 |
| slight-mc/idle-memory | 0.28 | 734408 |
| slight-mc/parallel-fib | 0.27 | 717584 |
| slight-mc/ping-pong | 0.27 | 734216 |
| slight-mc/spawn | 0.27 | 734296 |
| slight-mc/spectral-norm | 0.28 | 734464 |
| slight/bank | 0.28 | 734504 |
| slight/binary-trees | 0.29 | 734224 |
| slight/deriv | 0.28 | 734072 |
| slight/fan-in | 0.27 | 717624 |
| slight/fib | 0.27 | 717312 |
| slight/idle-memory | 0.27 | 717720 |
| slight/latency | 0.29 | 735032 |
| slight/n-body | 0.32 | 767992 |
| slight/nqueens | 0.27 | 717432 |
| slight/parallel-fib | 0.28 | 717392 |
| slight/payload | 0.27 | 717624 |
| slight/ping-pong | 0.27 | 717624 |
| slight/spawn | 0.27 | 717704 |
| slight/spectral-norm | 0.28 | 734320 |
| slight/startup | 3.20 | 717240 |
| slight/tak | 0.29 | 717312 |
| slight/thread-ring | 0.27 | 717624 |
