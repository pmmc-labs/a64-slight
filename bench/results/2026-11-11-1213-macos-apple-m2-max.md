# Benchmarks, 2026-11-11 12:13:51

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 9fd007c, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| c | 0.002 | 0.002 | 0.001 | 1.2 | 0.50 |
| rust | 0.003 | 0.002 | 0.002 | 1.6 | 0.75 |
| slight | 0.004 | 0.002 | 0.002 | 1.6 | 1.00 |
| go | 0.007 | 0.002 | 0.003 | 3.5 | 1.73 |
| perl | 0.009 | 0.003 | 0.004 | 3.5 | 2.45 |
| python | 0.017 | 0.016 | 0.016 | 12.0 | 4.42 |
| chez | 0.038 | 0.035 | 0.035 | 51.9 | 10.01 |
| erlang | 0.090 | 0.089 | 0.083 | 48.3 | 23.72 |
| node | 0.147 | 0.051 | 0.070 | 40.8 | 38.81 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.107 | 0.109 | 1.2 | 0.55 |
| go | 0.127 | 0.123 | 0.124 | 3.5 | 0.63 |
| slight | 0.201 | 0.199 | 0.199 | 1.6 | 1.00 |
| chez | 0.224 | 0.220 | 0.224 | 51.8 | 1.12 |
| node | 0.321 | 0.316 | 0.320 | 43.5 | 1.59 |
| erlang | 0.679 | 0.661 | 0.397 | 48.6 | 3.37 |
| python | 4.636 | 4.629 | 4.635 | 11.8 | 23.05 |
| perl | 12.8 | 12.8 | 12.8 | 4.4 | 63.73 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.083 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.107 | 0.102 | 0.103 | 3.5 | 0.74 |
| slight | 0.143 | 0.142 | 0.142 | 1.6 | 1.00 |
| chez | 0.189 | 0.187 | 0.188 | 51.8 | 1.32 |
| node | 0.290 | 0.288 | 0.289 | 43.3 | 2.02 |
| erlang | 0.507 | 0.503 | 0.297 | 48.5 | 3.54 |
| python | 3.603 | 3.603 | 3.602 | 11.8 | 25.13 |
| perl | 13.9 | 13.8 | 13.9 | 4.3 | 96.79 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.342 | 0.340 | 0.340 | 65.8 | 0.42 |
| node | 0.703 | 0.551 | 0.960 | 295.4 | 0.85 |
| slight | 0.823 | 0.817 | 0.820 | 105.9 | 1.00 |
| erlang | 0.878 | 0.850 | 0.557 | 172.4 | 1.07 |
| c | 0.991 | 0.988 | 0.990 | 20.0 | 1.20 |
| go | 1.368 | 1.361 | 1.366 | 73.8 | 1.66 |
| python | 5.876 | 5.870 | 5.875 | 45.6 | 7.14 |
| perl | 17.8 | 17.7 | 17.8 | 142.1 | 21.61 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.310 | 0.301 | 0.309 | 1.2 | 0.44 |
| chez | 0.388 | 0.379 | 0.387 | 51.8 | 0.55 |
| go | 0.580 | 0.576 | 0.571 | 8.8 | 0.82 |
| erlang | 0.653 | 0.644 | 0.483 | 48.5 | 0.92 |
| slight | 0.707 | 0.705 | 0.705 | 3.8 | 1.00 |
| node | 2.295 | 2.276 | 2.306 | 61.8 | 3.25 |
| python | 6.962 | 6.952 | 6.960 | 11.8 | 9.84 |
| perl | 13.8 | 13.7 | 13.8 | 5.4 | 19.47 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.3 | 0.35 |
| chez | 0.184 | 0.183 | 0.183 | 51.8 | 0.48 |
| slight | 0.382 | 0.380 | 0.380 | 3.9 | 1.00 |
| erlang | 0.497 | 0.495 | 0.326 | 48.6 | 1.30 |
| node | 0.620 | 0.615 | 0.668 | 56.0 | 1.62 |
| go | 1.065 | 1.049 | 1.045 | 8.8 | 2.79 |
| python | 4.075 | 4.047 | 4.073 | 11.9 | 10.67 |
| perl | 8.098 | 8.080 | 8.097 | 4.6 | 21.21 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.037 | 1.3 | 0.05 |
| go | 0.053 | 0.048 | 0.049 | 3.5 | 0.06 |
| node | 0.118 | 0.116 | 0.121 | 47.2 | 0.14 |
| slight | 0.849 | 0.845 | 0.847 | 4.7 | 1.00 |
| chez | 1.157 | 1.156 | 1.156 | 51.8 | 1.36 |
| erlang | 1.301 | 1.300 | 1.207 | 48.5 | 1.53 |
| python | 5.736 | 5.725 | 5.735 | 12.3 | 6.76 |
| perl | 7.362 | 7.213 | 7.358 | 6.0 | 8.67 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.046 | 0.046 | 0.046 | 1.2 | 0.10 |
| go | 0.051 | 0.047 | 0.047 | 3.6 | 0.11 |
| slight-arrays | 0.085 | 0.083 | 0.083 | 43.5 | 0.18 |
| node | 0.119 | 0.117 | 0.121 | 46.2 | 0.25 |
| slight | 0.473 | 0.472 | 0.471 | 3.9 | 1.00 |
| chez | 0.573 | 0.572 | 0.572 | 51.8 | 1.21 |
| erlang | 0.712 | 0.661 | 0.557 | 48.6 | 1.51 |
| python | 5.132 | 5.123 | 5.131 | 12.3 | 10.85 |
| perl | 7.574 | 7.555 | 7.573 | 6.2 | 16.01 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.293 | 0.292 | 0.291 | 1.7 | 0.97 |
| slight | 0.303 | 0.294 | 0.301 | 2.9 | 1.00 |
| go | 0.389 | 0.385 | 0.385 | 3.5 | 1.28 |
| erlang | 3.500 | 3.459 | 1.342 | 48.5 | 11.55 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.160 | 0.158 | 0.157 | 1.7 | 0.22 |
| go | 0.201 | 0.197 | 0.198 | 3.5 | 0.28 |
| slight | 0.724 | 0.717 | 0.723 | 2.5 | 1.00 |
| erlang | 2.817 | 2.566 | 1.754 | 48.4 | 3.89 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.483 | 0.482 | 0.480 | 10.5 | 1.00 |
| rust | 0.692 | 0.690 | 0.690 | 2.5 | 1.43 |
| go | 1.065 | 1.040 | 1.061 | 5.2 | 2.20 |
| erlang | 8.200 | 8.152 | 3.033 | 49.3 | 16.98 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.271 | 0.268 | 0.270 | 93.1 | 0.73 |
| go | 0.291 | 0.286 | 0.287 | 3.8 | 0.78 |
| slight | 0.371 | 0.365 | 0.362 | 479.8 | 1.00 |
| erlang | 1.791 | 1.676 | 1.746 | 650.1 | 4.83 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.150 | 0.148 | 0.148 | 126.6 | 1.00 |
| go | 0.432 | 0.426 | 0.424 | 402.6 | 2.88 |
| erlang | 1.229 | 1.208 | 0.690 | 114.1 | 8.18 |
| rust | 1.383 | 0.287 | 1.381 | 181.2 | 9.21 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| rust | 0.751 | 0.740 | 0.748 | 1106.2 | 0.03 | 1105 |
| go | 1.487 | 1.444 | 1.476 | 2899.9 | 0.05 | 2896 |
| erlang | 2.879 | 2.840 | 1.805 | 2789.2 | 0.10 | 2741 |
| slight | 27.6 | 27.3 | 26.6 | 9258.8 | 1.00 | 9257 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.151 | 0.150 | 0.150 | 1.6 | 0.54 |
| go | 0.171 | 0.167 | 0.167 | 3.7 | 0.60 |
| slight | 0.283 | 0.281 | 0.281 | 2.9 | 1.00 |
| erlang | 0.861 | 0.857 | 0.492 | 48.8 | 3.05 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 2 µs | 4 µs | 5 µs | 10 µs | 110 µs | 42.4 ms | 45.4 ms | 531.9 |
| go | 0 µs | 0 µs | 14.1 ms | 29.5 ms | 29.5 ms | 4 µs | 12 µs | 12 µs | 166.3 |
| erlang | 5 µs | 14 µs | 28 µs | 42 µs | 51 µs | 8 µs | 71 µs | 204 µs | 1748.2 |
| rust | 0 µs | 0 µs | 142.1 ms | 281.1 ms | 283.1 ms | 2 µs | 3 µs | 3 µs | 83.8 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 8 µs | 1 µs | 10 µs | 18 µs | 1 µs | 15 µs | 15 µs | 167.9 |
| erlang-mc | 0 µs | 3 µs | 0 µs | 4 µs | 10 µs | 0 µs | 4 µs | 38 µs | 1708.8 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.025 | 0.024 | 0.024 | 2.3 | 0.04 |
| slight | 0.632 | 0.629 | 0.629 | 77.1 | 1.00 |
| python | 6.349 | 6.317 | 6.331 | 20.1 | 10.04 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.32 | 0.55 |
|  | erlang-mc | 0.085 | 0.232 | 60.1 | 0.95 | 22.55 |
|  | rust-mc | 0.002 | 0.003 | 2.2 | 0.71 | 0.54 |
| fib | go-mc | 0.124 | 0.123 | 4.0 | 0.97 | 0.61 |
|  | erlang-mc | 0.363 | 0.517 | 60.5 | 0.54 | 1.81 |
| tak | go-mc | 0.102 | 0.102 | 4.0 | 0.96 | 0.71 |
|  | erlang-mc | 0.283 | 0.444 | 60.5 | 0.56 | 1.97 |
| binary-trees | go-mc | 0.658 | 1.843 | 41.1 | 0.48 | 0.80 |
|  | erlang-mc | 0.495 | 0.650 | 171.8 | 0.56 | 0.60 |
| nqueens | go-mc | 0.548 | 0.594 | 11.0 | 0.94 | 0.77 |
|  | erlang-mc | 0.473 | 0.614 | 60.3 | 0.72 | 0.67 |
| deriv | go-mc | 0.799 | 0.913 | 11.2 | 0.75 | 2.09 |
|  | erlang-mc | 0.313 | 0.463 | 60.9 | 0.63 | 0.82 |
| n-body | go-mc | 0.048 | 0.048 | 3.9 | 0.91 | 0.06 |
|  | erlang-mc | 1.204 | 1.358 | 60.2 | 0.92 | 1.42 |
| spectral-norm | slight-mc | 0.020 | 0.141 | 70.3 | 0.23 | 0.04 |
|  | go-mc | 0.047 | 0.047 | 4.0 | 0.92 | 0.10 |
|  | erlang-mc | 0.485 | 0.631 | 61.0 | 0.68 | 1.03 |
| ping-pong | slight-mc | 0.067 | 0.644 | 17.7 | 0.22 | 0.22 |
|  | go-mc | 0.527 | 0.760 | 4.1 | 1.36 | 1.74 |
|  | erlang-mc | 0.984 | 1.134 | 60.1 | 0.28 | 3.25 |
|  | rust-mc | 11.7 | 14.7 | 2.4 | 39.83 | 38.49 |
| payload | go-mc | 0.269 | 0.388 | 4.1 | 1.34 | 0.37 |
|  | erlang-mc | 1.367 | 1.564 | 61.0 | 0.49 | 1.89 |
|  | rust-mc | 5.898 | 7.449 | 2.4 | 36.93 | 8.14 |
| thread-ring | go-mc | 1.444 | 2.051 | 5.8 | 1.36 | 2.99 |
|  | erlang-mc | 2.392 | 2.553 | 61.2 | 0.29 | 4.95 |
|  | rust-mc | 1.177 | 1.936 | 3.3 | 1.70 | 2.44 |
| fan-in | slight-mc | 0.707 | 7.118 | 135.3 | 1.91 | 1.91 |
|  | go-mc | 3.500 | 11.7 | 5.2 | 12.03 | 9.44 |
|  | erlang-mc | 1.098 | 8.556 | 668.2 | 0.61 | 2.96 |
|  | rust-mc | 4.113 | 47.7 | 45.4 | 15.15 | 11.09 |
| spawn | slight-mc | 0.031 | 0.250 | 128.2 | 0.21 | 0.21 |
|  | go-mc | 0.387 | 1.830 | 27.4 | 0.90 | 2.58 |
|  | erlang-mc | 0.906 | 5.599 | 196.4 | 0.74 | 6.03 |
|  | rust-mc | 0.613 | 1.947 | 12.0 | 0.44 | 4.08 |
| idle-memory | slight-mc | 29.4 | 50.1 | 9102.9 | 1.07 | 1.07 |
|  | go-mc | 1.003 | 6.087 | 2914.9 | 0.67 | 0.04 |
|  | erlang-mc | 1.907 | 6.324 | 2795.5 | 0.66 | 0.07 |
|  | rust-mc | 1.384 | 7.444 | 1097.3 | 1.84 | 0.05 |
| parallel-fib | slight-mc | 0.039 | 0.347 | 4.8 | 0.14 | 0.14 |
|  | go-mc | 0.021 | 0.196 | 5.0 | 0.12 | 0.08 |
|  | erlang-mc | 0.135 | 0.721 | 60.1 | 0.16 | 0.48 |
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
| erlang/parallel-fib | 0.16 | 1412 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.15 | 764 |
| erlang/tak | 0.15 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.10 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/latency | 0.14 | 2482354 |
| go/n-body | 0.10 | 2430274 |
| go/nqueens | 0.09 | 2430130 |
| go/parallel-fib | 0.10 | 2430194 |
| go/payload | 0.10 | 2430226 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.10 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.33 | 817760 |
| rust/latency | 0.45 | 887648 |
| rust/parallel-fib | 0.31 | 787920 |
| rust/payload | 0.31 | 789632 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/startup | 1.60 | 708464 |
| rust/thread-ring | 0.34 | 810560 |
| slight-arrays/spectral-norm | 0.28 | 733536 |
| slight-mc/fan-in | 0.28 | 717112 |
| slight-mc/idle-memory | 0.28 | 733896 |
| slight-mc/parallel-fib | 0.27 | 717056 |
| slight-mc/ping-pong | 0.28 | 717176 |
| slight-mc/spawn | 0.28 | 717272 |
| slight-mc/spectral-norm | 0.29 | 733936 |
| slight/bank | 0.27 | 733976 |
| slight/binary-trees | 0.28 | 733696 |
| slight/deriv | 0.29 | 717032 |
| slight/fan-in | 0.28 | 717096 |
| slight/fib | 0.27 | 716784 |
| slight/idle-memory | 0.28 | 717192 |
| slight/latency | 0.29 | 734472 |
| slight/n-body | 0.31 | 767448 |
| slight/nqueens | 0.28 | 716904 |
| slight/parallel-fib | 0.27 | 716864 |
| slight/payload | 0.27 | 717096 |
| slight/ping-pong | 0.28 | 717096 |
| slight/spawn | 0.27 | 717192 |
| slight/spectral-norm | 0.28 | 733792 |
| slight/startup | 3.03 | 716712 |
| slight/tak | 0.28 | 716784 |
| slight/thread-ring | 0.28 | 717112 |
