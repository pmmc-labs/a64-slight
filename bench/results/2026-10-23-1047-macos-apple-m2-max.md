# Benchmarks, 2026-10-23 10:47:53

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 3487cba, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| c | 0.002 | 0.001 | 0.001 | 1.2 | 0.70 |
| rust | 0.002 | 0.002 | 0.001 | 1.6 | 0.84 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 0.94 |
| slight | 0.002 | 0.002 | 0.002 | 1.5 | 1.00 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.39 |
| python | 0.019 | 0.017 | 0.016 | 12.1 | 8.49 |
| chez | 0.035 | 0.034 | 0.034 | 51.8 | 15.09 |
| node | 0.058 | 0.051 | 0.054 | 41.2 | 25.40 |
| erlang | 0.091 | 0.090 | 0.086 | 48.4 | 39.81 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.110 | 0.110 | 1.2 | 0.55 |
| go | 0.124 | 0.123 | 0.124 | 3.5 | 0.62 |
| slight | 0.200 | 0.199 | 0.200 | 1.5 | 1.00 |
| chez | 0.225 | 0.224 | 0.224 | 51.8 | 1.12 |
| node | 0.315 | 0.314 | 0.315 | 43.5 | 1.57 |
| erlang | 0.678 | 0.664 | 0.394 | 48.7 | 3.39 |
| python | 4.652 | 4.648 | 4.650 | 11.8 | 23.27 |
| perl | 12.8 | 12.8 | 12.8 | 4.3 | 63.95 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.083 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.102 | 0.102 | 0.102 | 3.5 | 0.72 |
| slight | 0.142 | 0.142 | 0.142 | 1.5 | 1.00 |
| chez | 0.191 | 0.190 | 0.190 | 51.8 | 1.34 |
| node | 0.290 | 0.287 | 0.288 | 43.8 | 2.04 |
| erlang | 0.520 | 0.505 | 0.307 | 48.4 | 3.65 |
| python | 3.597 | 3.596 | 3.596 | 11.8 | 25.29 |
| perl | 13.9 | 13.9 | 13.9 | 4.4 | 97.89 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.342 | 0.341 | 0.340 | 65.8 | 0.33 |
| node | 0.553 | 0.548 | 0.673 | 365.3 | 0.54 |
| erlang | 0.853 | 0.849 | 0.533 | 172.5 | 0.83 |
| c | 0.991 | 0.971 | 0.990 | 20.0 | 0.96 |
| slight | 1.032 | 1.015 | 1.030 | 172.8 | 1.00 |
| go | 1.381 | 1.365 | 1.379 | 74.7 | 1.34 |
| python | 5.913 | 5.895 | 5.911 | 45.5 | 5.73 |
| perl | 17.8 | 17.8 | 17.8 | 142.1 | 17.25 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.308 | 0.308 | 1.2 | 0.44 |
| chez | 0.389 | 0.386 | 0.388 | 51.8 | 0.55 |
| go | 0.577 | 0.574 | 0.571 | 8.7 | 0.82 |
| erlang | 0.704 | 0.680 | 0.534 | 48.6 | 1.00 |
| slight | 0.705 | 0.703 | 0.704 | 3.9 | 1.00 |
| node | 2.289 | 2.277 | 2.301 | 62.8 | 3.25 |
| python | 6.949 | 6.935 | 6.947 | 11.9 | 9.86 |
| perl | 13.8 | 13.8 | 13.8 | 5.2 | 19.62 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.133 | 0.133 | 0.133 | 1.2 | 0.35 |
| chez | 0.186 | 0.184 | 0.185 | 51.8 | 0.49 |
| slight | 0.380 | 0.378 | 0.379 | 3.8 | 1.00 |
| erlang | 0.510 | 0.497 | 0.343 | 48.5 | 1.34 |
| node | 0.620 | 0.619 | 0.669 | 56.0 | 1.63 |
| go | 1.069 | 1.052 | 1.052 | 8.8 | 2.81 |
| python | 4.049 | 4.040 | 4.048 | 12.0 | 10.66 |
| perl | 8.107 | 8.070 | 8.107 | 4.6 | 21.34 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.037 | 1.2 | 0.03 |
| go | 0.048 | 0.048 | 0.048 | 3.5 | 0.04 |
| node | 0.119 | 0.117 | 0.121 | 47.3 | 0.09 |
| chez | 1.164 | 1.157 | 1.163 | 51.8 | 0.90 |
| slight | 1.291 | 1.290 | 1.290 | 5.3 | 1.00 |
| erlang | 1.321 | 1.314 | 1.228 | 48.6 | 1.02 |
| python | 5.769 | 5.746 | 5.767 | 12.3 | 4.47 |
| perl | 7.473 | 7.148 | 7.471 | 6.2 | 5.79 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.047 | 0.046 | 0.047 | 1.2 | 0.11 |
| go | 0.048 | 0.047 | 0.048 | 3.6 | 0.11 |
| node | 0.121 | 0.119 | 0.124 | 46.3 | 0.29 |
| slight | 0.421 | 0.420 | 0.420 | 4.4 | 1.00 |
| chez | 0.578 | 0.575 | 0.577 | 51.8 | 1.37 |
| erlang | 0.657 | 0.655 | 0.503 | 48.8 | 1.56 |
| python | 5.149 | 5.141 | 5.148 | 12.5 | 12.23 |
| perl | 7.588 | 7.503 | 7.587 | 6.9 | 18.02 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.291 | 0.290 | 0.290 | 1.6 | 0.83 |
| slight | 0.350 | 0.347 | 0.350 | 2.6 | 1.00 |
| go | 0.388 | 0.385 | 0.387 | 3.5 | 1.11 |
| erlang | 3.270 | 3.234 | 1.199 | 48.5 | 9.33 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.530 | 0.526 | 0.529 | 2.0 | 1.00 |
| rust | 0.693 | 0.692 | 0.693 | 2.5 | 1.31 |
| go | 1.029 | 1.014 | 1.028 | 5.1 | 1.94 |
| erlang | 7.992 | 7.936 | 2.876 | 49.3 | 15.08 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.271 | 0.270 | 0.271 | 93.0 | 0.70 |
| go | 0.288 | 0.287 | 0.287 | 3.8 | 0.74 |
| slight | 0.388 | 0.381 | 0.380 | 483.6 | 1.00 |
| erlang | 1.698 | 1.642 | 1.655 | 649.8 | 4.38 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.138 | 0.136 | 0.136 | 90.2 | 1.00 |
| go | 0.428 | 0.422 | 0.422 | 387.9 | 3.11 |
| rust | 1.092 | 0.535 | 1.092 | 181.7 | 7.94 |
| erlang | 1.233 | 1.225 | 0.697 | 113.9 | 8.96 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.338 | 0.330 | 0.336 | 572.3 | 1.00 | 571 |
| rust | 0.747 | 0.730 | 0.745 | 1106.2 | 2.21 | 1105 |
| go | 1.487 | 1.408 | 1.484 | 2900.9 | 4.40 | 2897 |
| erlang | 2.938 | 2.862 | 1.873 | 2789.8 | 8.69 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.150 | 0.150 | 0.150 | 1.6 | 0.53 |
| go | 0.168 | 0.167 | 0.168 | 3.7 | 0.60 |
| slight | 0.282 | 0.281 | 0.281 | 3.7 | 1.00 |
| erlang | 0.868 | 0.855 | 0.502 | 48.4 | 3.08 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.98 | 0.92 |
|  | erlang-mc | 0.088 | 0.246 | 59.9 | 0.97 | 38.44 |
|  | rust-mc | 0.002 | 0.002 | 2.2 | 1.10 | 0.92 |
| fib | go-mc | 0.123 | 0.123 | 4.0 | 0.99 | 0.62 |
|  | erlang-mc | 0.370 | 0.526 | 60.2 | 0.55 | 1.85 |
| tak | go-mc | 0.102 | 0.102 | 4.0 | 1.00 | 0.72 |
|  | erlang-mc | 0.286 | 0.444 | 60.2 | 0.55 | 2.01 |
| binary-trees | go-mc | 0.664 | 1.872 | 41.2 | 0.48 | 0.64 |
|  | erlang-mc | 0.503 | 0.664 | 171.8 | 0.59 | 0.49 |
| nqueens | go-mc | 0.550 | 0.597 | 11.0 | 0.95 | 0.78 |
|  | erlang-mc | 0.475 | 0.646 | 59.7 | 0.68 | 0.67 |
| deriv | go-mc | 0.805 | 0.920 | 11.1 | 0.75 | 2.12 |
|  | erlang-mc | 0.316 | 0.473 | 60.2 | 0.62 | 0.83 |
| n-body | go-mc | 0.048 | 0.048 | 3.9 | 1.00 | 0.04 |
|  | erlang-mc | 1.191 | 1.357 | 60.0 | 0.90 | 0.92 |
| spectral-norm | go-mc | 0.048 | 0.048 | 4.0 | 1.00 | 0.11 |
|  | erlang-mc | 0.489 | 0.640 | 60.9 | 0.74 | 1.16 |
| ping-pong | go-mc | 0.534 | 0.767 | 4.1 | 1.38 | 1.52 |
|  | erlang-mc | 1.054 | 1.207 | 59.9 | 0.32 | 3.01 |
|  | rust-mc | 11.7 | 14.7 | 2.4 | 40.37 | 33.52 |
| thread-ring | go-mc | 1.460 | 2.074 | 5.8 | 1.42 | 2.75 |
|  | erlang-mc | 2.382 | 2.538 | 60.2 | 0.30 | 4.49 |
|  | rust-mc | 1.180 | 1.913 | 3.2 | 1.70 | 2.23 |
| fan-in | go-mc | 3.476 | 11.6 | 5.2 | 12.07 | 8.97 |
|  | erlang-mc | 1.097 | 8.656 | 656.6 | 0.65 | 2.83 |
|  | rust-mc | 4.136 | 48.0 | 39.9 | 15.24 | 10.67 |
| spawn | go-mc | 0.384 | 1.852 | 28.2 | 0.90 | 2.79 |
|  | erlang-mc | 0.923 | 5.875 | 195.2 | 0.75 | 6.71 |
|  | rust-mc | 0.608 | 1.912 | 11.9 | 0.56 | 4.42 |
| idle-memory | go-mc | 1.081 | 6.662 | 2913.5 | 0.73 | 3.20 |
|  | erlang-mc | 1.894 | 6.477 | 2795.5 | 0.64 | 5.60 |
|  | rust-mc | 1.392 | 7.629 | 1097.2 | 1.86 | 4.12 |
| parallel-fib | go-mc | 0.021 | 0.200 | 5.0 | 0.13 | 0.07 |
|  | erlang-mc | 0.138 | 0.733 | 59.8 | 0.16 | 0.49 |
|  | rust-mc | 0.018 | 0.173 | 2.5 | 0.12 | 0.07 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| c/deriv | 0.06 | 50440 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.06 | 50008 |
| c/nqueens | 0.05 | 33720 |
| c/spectral-norm | 0.06 | 33584 |
| c/startup | 0.04 | 33432 |
| c/tak | 0.05 | 33472 |
| chez/binary-trees | 0.04 | 4089 |
| chez/deriv | 0.04 | 4235 |
| chez/fib | 0.04 | 2166 |
| chez/n-body | 0.04 | 11318 |
| chez/nqueens | 0.04 | 3418 |
| chez/spectral-norm | 0.04 | 5510 |
| chez/startup | 0.04 | 1412 |
| chez/tak | 0.04 | 2582 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/deriv | 0.15 | 1612 |
| erlang/fan-in | 0.15 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.15 | 1312 |
| erlang/n-body | 0.17 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/ping-pong | 0.15 | 1196 |
| erlang/spawn | 0.15 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.15 | 764 |
| erlang/tak | 0.15 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.09 | 2430210 |
| go/deriv | 0.09 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/n-body | 0.09 | 2430274 |
| go/nqueens | 0.10 | 2430130 |
| go/parallel-fib | 0.10 | 2430194 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.10 | 2429698 |
| go/tak | 0.09 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.33 | 817760 |
| rust/parallel-fib | 0.31 | 787920 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/startup | 1.60 | 708464 |
| rust/thread-ring | 0.34 | 810560 |
| slight/binary-trees | 0.21 | 314752 |
| slight/deriv | 0.21 | 314600 |
| slight/fan-in | 0.21 | 298168 |
| slight/fib | 0.20 | 297840 |
| slight/idle-memory | 0.21 | 298280 |
| slight/n-body | 0.24 | 348504 |
| slight/nqueens | 0.20 | 297960 |
| slight/parallel-fib | 0.21 | 297872 |
| slight/ping-pong | 0.20 | 298168 |
| slight/spawn | 0.21 | 298152 |
| slight/spectral-norm | 0.22 | 314848 |
| slight/startup | 1.33 | 297768 |
| slight/tak | 0.20 | 297840 |
| slight/thread-ring | 0.21 | 298200 |
