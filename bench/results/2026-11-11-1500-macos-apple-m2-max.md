# Benchmarks, 2026-11-11 15:00:59

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 1493d35, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-arrays: Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-mc: Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.13.0
- perl: perl v5.42.2
- python: Python 3.14.3, SimPy 4.1.2
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.002 | 0.002 | 0.001 | 1.2 | 0.46 |
| rust | 0.004 | 0.004 | 0.002 | 1.6 | 0.93 |
| slight | 0.004 | 0.004 | 0.002 | 1.6 | 1.00 |
| go | 0.007 | 0.006 | 0.003 | 3.5 | 1.70 |
| perl | 0.009 | 0.009 | 0.004 | 3.5 | 2.40 |
| python | 0.017 | 0.016 | 0.016 | 12.0 | 4.38 |
| chez | 0.039 | 0.037 | 0.035 | 51.9 | 10.04 |
| erlang | 0.089 | 0.089 | 0.083 | 48.3 | 23.12 |
| node | 0.149 | 0.146 | 0.071 | 40.9 | 38.43 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.111 | 0.110 | 0.110 | 1.2 | 0.55 |
| go | 0.128 | 0.128 | 0.124 | 3.5 | 0.63 |
| slight | 0.202 | 0.202 | 0.200 | 1.6 | 1.00 |
| chez | 0.225 | 0.224 | 0.224 | 51.8 | 1.11 |
| node | 0.319 | 0.319 | 0.320 | 43.3 | 1.58 |
| erlang | 0.699 | 0.691 | 0.414 | 48.5 | 3.46 |
| python | 4.645 | 4.626 | 4.644 | 11.8 | 23.00 |
| perl | 12.8 | 12.8 | 12.8 | 4.5 | 63.35 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.083 | 0.082 | 0.082 | 1.2 | 0.57 |
| go | 0.107 | 0.107 | 0.103 | 3.5 | 0.74 |
| slight | 0.144 | 0.143 | 0.142 | 1.5 | 1.00 |
| chez | 0.190 | 0.189 | 0.189 | 51.8 | 1.32 |
| node | 0.289 | 0.286 | 0.288 | 43.1 | 2.01 |
| erlang | 0.507 | 0.505 | 0.296 | 48.4 | 3.52 |
| python | 3.606 | 3.596 | 3.604 | 11.8 | 25.05 |
| perl | 13.9 | 13.8 | 13.9 | 4.2 | 96.57 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.344 | 0.341 | 0.342 | 65.8 | 0.41 |
| node | 0.724 | 0.677 | 0.970 | 291.9 | 0.87 |
| slight | 0.835 | 0.823 | 0.833 | 105.9 | 1.00 |
| erlang | 0.873 | 0.850 | 0.552 | 172.2 | 1.05 |
| c | 0.996 | 0.991 | 0.994 | 20.0 | 1.19 |
| go | 1.374 | 1.372 | 1.368 | 70.4 | 1.64 |
| python | 5.898 | 5.860 | 5.897 | 45.5 | 7.06 |
| perl | 17.9 | 17.7 | 17.9 | 142.1 | 21.42 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.310 | 0.309 | 0.309 | 1.2 | 0.44 |
| chez | 0.391 | 0.388 | 0.388 | 51.8 | 0.55 |
| go | 0.583 | 0.580 | 0.573 | 8.8 | 0.82 |
| erlang | 0.664 | 0.647 | 0.494 | 48.5 | 0.93 |
| slight | 0.712 | 0.707 | 0.710 | 3.9 | 1.00 |
| node | 2.282 | 2.279 | 2.292 | 62.0 | 3.20 |
| python | 6.974 | 6.940 | 6.972 | 11.8 | 9.79 |
| perl | 13.9 | 13.7 | 13.9 | 5.3 | 19.46 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.134 | 0.133 | 1.3 | 0.35 |
| chez | 0.187 | 0.184 | 0.186 | 51.8 | 0.49 |
| slight | 0.382 | 0.381 | 0.381 | 4.5 | 1.00 |
| erlang | 0.510 | 0.500 | 0.342 | 48.6 | 1.33 |
| node | 0.620 | 0.616 | 0.669 | 56.2 | 1.62 |
| go | 1.070 | 1.044 | 1.051 | 8.7 | 2.80 |
| python | 4.103 | 4.059 | 4.101 | 11.9 | 10.73 |
| perl | 8.152 | 8.091 | 8.151 | 4.5 | 21.31 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.039 | 0.039 | 0.038 | 1.3 | 0.05 |
| go | 0.053 | 0.052 | 0.049 | 3.5 | 0.06 |
| node | 0.119 | 0.118 | 0.121 | 46.9 | 0.14 |
| slight | 0.851 | 0.850 | 0.848 | 4.0 | 1.00 |
| chez | 1.164 | 1.158 | 1.163 | 51.8 | 1.37 |
| erlang | 1.316 | 1.315 | 1.223 | 48.6 | 1.55 |
| python | 5.765 | 5.727 | 5.763 | 12.3 | 6.78 |
| perl | 7.515 | 7.358 | 7.511 | 6.3 | 8.84 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.047 | 0.047 | 0.047 | 1.2 | 0.10 |
| go | 0.051 | 0.051 | 0.048 | 3.6 | 0.11 |
| slight-arrays | 0.086 | 0.085 | 0.084 | 43.5 | 0.18 |
| node | 0.120 | 0.119 | 0.123 | 46.3 | 0.25 |
| slight | 0.475 | 0.473 | 0.473 | 3.9 | 1.00 |
| chez | 0.574 | 0.573 | 0.572 | 51.8 | 1.21 |
| erlang | 0.678 | 0.666 | 0.523 | 48.7 | 1.43 |
| python | 5.138 | 5.126 | 5.137 | 12.2 | 10.82 |
| perl | 7.591 | 7.527 | 7.590 | 6.4 | 15.99 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.294 | 0.293 | 0.292 | 1.7 | 0.95 |
| slight | 0.308 | 0.306 | 0.306 | 2.9 | 1.00 |
| go | 0.392 | 0.389 | 0.388 | 3.5 | 1.27 |
| erlang | 3.374 | 3.296 | 1.276 | 48.5 | 10.95 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.160 | 0.160 | 0.158 | 1.7 | 0.22 |
| go | 0.204 | 0.201 | 0.199 | 3.5 | 0.28 |
| slight | 0.725 | 0.719 | 0.722 | 2.5 | 1.00 |
| erlang | 2.671 | 2.596 | 1.612 | 48.3 | 3.69 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.493 | 0.492 | 0.490 | 10.5 | 1.00 |
| rust | 0.693 | 0.692 | 0.690 | 2.5 | 1.40 |
| go | 1.071 | 1.045 | 1.067 | 5.1 | 2.17 |
| erlang | 8.263 | 7.994 | 3.038 | 49.0 | 16.75 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.274 | 0.271 | 0.272 | 93.1 | 0.73 |
| go | 0.291 | 0.290 | 0.287 | 3.8 | 0.77 |
| slight | 0.376 | 0.369 | 0.364 | 479.8 | 1.00 |
| erlang | 1.730 | 1.634 | 1.685 | 650.1 | 4.61 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.158 | 0.153 | 0.156 | 126.6 | 1.00 |
| go | 0.438 | 0.414 | 0.433 | 355.3 | 2.77 |
| erlang | 1.263 | 1.241 | 0.725 | 114.1 | 7.99 |
| rust | 2.386 | 0.275 | 2.384 | 181.7 | 15.10 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| rust | 0.763 | 0.746 | 0.761 | 1106.2 | 0.07 | 1105 |
| go | 1.502 | 1.491 | 1.494 | 2890.1 | 0.14 | 2887 |
| erlang | 3.012 | 2.920 | 1.927 | 2789.9 | 0.29 | 2742 |
| slight | 10.5 | 9.858 | 9.481 | 8914.6 | 1.00 | 8913 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.152 | 0.152 | 0.151 | 1.6 | 0.54 |
| go | 0.172 | 0.171 | 0.168 | 3.7 | 0.61 |
| slight | 0.283 | 0.283 | 0.281 | 2.8 | 1.00 |
| erlang | 0.893 | 0.871 | 0.519 | 48.5 | 3.15 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 2 µs | 4 µs | 5 µs | 11 µs | 115 µs | 44.3 ms | 47.3 ms | 589.0 |
| go | 0 µs | 0 µs | 14.1 ms | 29.1 ms | 29.1 ms | 3 µs | 23 µs | 23 µs | 166.4 |
| erlang | 6 µs | 14 µs | 28 µs | 41 µs | 48 µs | 8 µs | 68 µs | 110 µs | 1748.1 |
| rust | 0 µs | 0 µs | 142.5 ms | 281.5 ms | 283.5 ms | 2 µs | 5 µs | 5 µs | 83.8 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 7 µs | 1 µs | 10 µs | 18 µs | 1 µs | 9 µs | 9 µs | 168.2 |
| erlang-mc | 0 µs | 1 µs | 0 µs | 2 µs | 8 µs | 0 µs | 4 µs | 48 µs | 1709.7 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.025 | 0.025 | 0.024 | 2.3 | 0.04 |
| slight | 0.636 | 0.634 | 0.634 | 76.3 | 1.00 |
| python | 6.367 | 6.353 | 6.348 | 20.2 | 10.01 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.31 | 0.53 |
|  | erlang-mc | 0.086 | 0.233 | 59.7 | 0.96 | 22.19 |
|  | rust-mc | 0.002 | 0.002 | 2.2 | 0.57 | 0.53 |
| fib | go-mc | 0.123 | 0.123 | 4.0 | 0.96 | 0.61 |
|  | erlang-mc | 0.366 | 0.518 | 59.7 | 0.52 | 1.81 |
| tak | go-mc | 0.102 | 0.102 | 3.9 | 0.96 | 0.71 |
|  | erlang-mc | 0.286 | 0.436 | 60.2 | 0.56 | 1.99 |
| binary-trees | go-mc | 0.665 | 1.869 | 40.6 | 0.48 | 0.80 |
|  | erlang-mc | 0.501 | 0.657 | 172.0 | 0.57 | 0.60 |
| nqueens | go-mc | 0.549 | 0.594 | 10.9 | 0.94 | 0.77 |
|  | erlang-mc | 0.475 | 0.615 | 59.9 | 0.72 | 0.67 |
| deriv | go-mc | 0.803 | 0.915 | 11.3 | 0.75 | 2.10 |
|  | erlang-mc | 0.317 | 0.462 | 59.9 | 0.62 | 0.83 |
| n-body | go-mc | 0.048 | 0.048 | 4.0 | 0.92 | 0.06 |
|  | erlang-mc | 1.199 | 1.345 | 59.9 | 0.91 | 1.41 |
| spectral-norm | slight-mc | 0.019 | 0.139 | 69.3 | 0.22 | 0.04 |
|  | go-mc | 0.047 | 0.047 | 3.9 | 0.91 | 0.10 |
|  | erlang-mc | 0.487 | 0.634 | 60.5 | 0.72 | 1.03 |
| ping-pong | slight-mc | 0.065 | 0.639 | 17.5 | 0.21 | 0.21 |
|  | go-mc | 0.538 | 0.797 | 4.1 | 1.37 | 1.75 |
|  | erlang-mc | 1.007 | 1.149 | 59.8 | 0.30 | 3.27 |
|  | rust-mc | 11.7 | 14.7 | 2.4 | 39.83 | 37.97 |
| payload | go-mc | 0.273 | 0.402 | 4.1 | 1.34 | 0.38 |
|  | erlang-mc | 1.418 | 1.626 | 60.8 | 0.53 | 1.96 |
|  | rust-mc | 6.005 | 7.537 | 2.4 | 37.45 | 8.29 |
| thread-ring | go-mc | 1.481 | 2.176 | 5.8 | 1.38 | 3.00 |
|  | erlang-mc | 2.431 | 2.585 | 60.5 | 0.29 | 4.93 |
|  | rust-mc | 1.181 | 1.941 | 3.3 | 1.71 | 2.40 |
| fan-in | slight-mc | 0.713 | 7.271 | 128.8 | 1.90 | 1.90 |
|  | go-mc | 3.554 | 12.1 | 5.3 | 12.22 | 9.46 |
|  | erlang-mc | 1.085 | 8.676 | 657.9 | 0.63 | 2.89 |
|  | rust-mc | 4.139 | 47.7 | 44.6 | 15.09 | 11.02 |
| spawn | slight-mc | 0.031 | 0.240 | 126.7 | 0.20 | 0.20 |
|  | go-mc | 0.383 | 1.832 | 27.6 | 0.88 | 2.42 |
|  | erlang-mc | 0.918 | 5.611 | 196.5 | 0.73 | 5.81 |
|  | rust-mc | 0.604 | 1.905 | 12.0 | 0.25 | 3.82 |
| idle-memory | slight-mc | 13.7 | 69.5 | 9616.4 | 1.30 | 1.30 |
|  | go-mc | 1.114 | 6.305 | 2914.5 | 0.74 | 0.11 |
|  | erlang-mc | 1.897 | 6.344 | 2795.4 | 0.63 | 0.18 |
|  | rust-mc | 1.401 | 7.626 | 1097.6 | 1.84 | 0.13 |
| parallel-fib | slight-mc | 0.039 | 0.344 | 4.7 | 0.14 | 0.14 |
|  | go-mc | 0.021 | 0.198 | 5.0 | 0.12 | 0.07 |
|  | erlang-mc | 0.137 | 0.729 | 60.2 | 0.15 | 0.48 |
|  | rust-mc | 0.018 | 0.172 | 2.5 | 0.12 | 0.07 |

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
| erlang/binary-trees | 0.16 | 1516 |
| erlang/deriv | 0.16 | 1612 |
| erlang/fan-in | 0.16 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/latency | 0.17 | 3776 |
| erlang/n-body | 0.19 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.15 | 764 |
| erlang/tak | 0.16 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
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
| go/startup | 0.10 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.34 | 817760 |
| rust/latency | 0.45 | 887648 |
| rust/parallel-fib | 0.31 | 787920 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 0.30 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/startup | 1.76 | 708464 |
| rust/thread-ring | 0.34 | 810560 |
| slight-arrays/spectral-norm | 0.28 | 733488 |
| slight-mc/fan-in | 0.28 | 717048 |
| slight-mc/idle-memory | 0.29 | 733848 |
| slight-mc/parallel-fib | 0.28 | 717008 |
| slight-mc/ping-pong | 0.28 | 717128 |
| slight-mc/spawn | 0.28 | 717208 |
| slight-mc/spectral-norm | 0.29 | 733888 |
| slight/bank | 0.28 | 733928 |
| slight/binary-trees | 0.28 | 733648 |
| slight/deriv | 0.28 | 716984 |
| slight/fan-in | 0.28 | 717048 |
| slight/fib | 0.28 | 716736 |
| slight/idle-memory | 0.27 | 717144 |
| slight/latency | 0.29 | 734424 |
| slight/n-body | 0.32 | 767400 |
| slight/nqueens | 0.28 | 716856 |
| slight/parallel-fib | 0.28 | 716816 |
| slight/payload | 0.28 | 717048 |
| slight/ping-pong | 0.28 | 717048 |
| slight/spawn | 0.28 | 717128 |
| slight/spectral-norm | 0.28 | 733744 |
| slight/startup | 3.09 | 716664 |
| slight/tak | 0.28 | 716736 |
| slight/thread-ring | 0.28 | 717064 |
