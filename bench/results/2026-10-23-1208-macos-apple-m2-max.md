# Benchmarks, 2026-10-23 12:08:20

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: b45e37d, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| rust | 0.002 | 0.002 | 0.001 | 1.6 | 0.80 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 0.95 |
| slight | 0.002 | 0.002 | 0.002 | 1.5 | 1.00 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.29 |
| python | 0.019 | 0.017 | 0.016 | 12.0 | 8.26 |
| chez | 0.035 | 0.034 | 0.034 | 51.9 | 15.16 |
| node | 0.057 | 0.051 | 0.052 | 41.3 | 24.66 |
| erlang | 0.091 | 0.089 | 0.085 | 48.3 | 39.63 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.108 | 0.109 | 1.2 | 0.55 |
| go | 0.123 | 0.123 | 0.123 | 3.5 | 0.62 |
| slight | 0.199 | 0.199 | 0.199 | 1.5 | 1.00 |
| chez | 0.225 | 0.221 | 0.224 | 51.8 | 1.13 |
| node | 0.314 | 0.312 | 0.313 | 43.7 | 1.58 |
| erlang | 0.662 | 0.658 | 0.381 | 48.4 | 3.32 |
| python | 4.639 | 4.637 | 4.638 | 11.8 | 23.26 |
| perl | 12.8 | 12.7 | 12.8 | 4.2 | 64.18 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.082 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.102 | 0.102 | 0.102 | 3.5 | 0.72 |
| slight | 0.142 | 0.141 | 0.141 | 1.5 | 1.00 |
| chez | 0.189 | 0.186 | 0.188 | 51.8 | 1.33 |
| node | 0.288 | 0.287 | 0.289 | 43.9 | 2.03 |
| erlang | 0.530 | 0.505 | 0.318 | 48.3 | 3.74 |
| python | 3.589 | 3.568 | 3.588 | 11.8 | 25.32 |
| perl | 13.9 | 13.9 | 13.9 | 4.3 | 97.82 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.342 | 0.341 | 0.340 | 65.8 | 0.33 |
| node | 0.551 | 0.546 | 0.678 | 367.4 | 0.54 |
| erlang | 0.862 | 0.844 | 0.542 | 172.2 | 0.84 |
| c | 0.993 | 0.991 | 0.992 | 20.0 | 0.97 |
| slight | 1.026 | 1.019 | 1.025 | 121.8 | 1.00 |
| go | 1.359 | 1.359 | 1.357 | 74.1 | 1.32 |
| python | 5.883 | 5.863 | 5.881 | 45.5 | 5.73 |
| perl | 17.8 | 17.7 | 17.8 | 142.1 | 17.31 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.301 | 0.308 | 1.2 | 0.44 |
| chez | 0.387 | 0.383 | 0.386 | 51.8 | 0.55 |
| go | 0.575 | 0.574 | 0.569 | 8.8 | 0.82 |
| erlang | 0.678 | 0.651 | 0.508 | 48.3 | 0.96 |
| slight | 0.703 | 0.703 | 0.703 | 3.8 | 1.00 |
| node | 2.280 | 2.262 | 2.290 | 62.5 | 3.24 |
| python | 6.942 | 6.934 | 6.940 | 11.9 | 9.87 |
| perl | 13.8 | 13.8 | 13.8 | 5.2 | 19.63 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.3 | 0.35 |
| chez | 0.185 | 0.184 | 0.184 | 51.8 | 0.49 |
| slight | 0.380 | 0.379 | 0.379 | 3.8 | 1.00 |
| erlang | 0.507 | 0.496 | 0.334 | 48.5 | 1.34 |
| node | 0.619 | 0.615 | 0.667 | 56.7 | 1.63 |
| go | 1.063 | 1.046 | 1.049 | 8.7 | 2.80 |
| python | 4.058 | 4.046 | 4.056 | 12.0 | 10.69 |
| perl | 8.092 | 8.053 | 8.091 | 4.5 | 21.32 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.037 | 1.2 | 0.03 |
| go | 0.048 | 0.048 | 0.048 | 3.5 | 0.04 |
| node | 0.117 | 0.117 | 0.120 | 47.4 | 0.09 |
| chez | 1.159 | 1.157 | 1.158 | 51.8 | 0.90 |
| slight | 1.289 | 1.288 | 1.288 | 4.5 | 1.00 |
| erlang | 1.324 | 1.316 | 1.231 | 48.6 | 1.03 |
| python | 5.740 | 5.698 | 5.739 | 12.3 | 4.45 |
| perl | 7.354 | 7.279 | 7.350 | 5.9 | 5.71 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.047 | 0.046 | 0.046 | 1.2 | 0.11 |
| go | 0.047 | 0.047 | 0.047 | 3.6 | 0.11 |
| node | 0.117 | 0.115 | 0.120 | 46.3 | 0.28 |
| slight | 0.420 | 0.419 | 0.420 | 3.8 | 1.00 |
| chez | 0.574 | 0.573 | 0.573 | 51.8 | 1.37 |
| erlang | 0.722 | 0.655 | 0.567 | 48.6 | 1.72 |
| python | 5.142 | 5.126 | 5.140 | 12.4 | 12.23 |
| perl | 7.550 | 7.527 | 7.549 | 7.0 | 17.96 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.294 | 0.287 | 0.292 | 1.7 | 0.84 |
| slight | 0.348 | 0.347 | 0.347 | 2.6 | 1.00 |
| go | 0.387 | 0.385 | 0.386 | 3.5 | 1.11 |
| erlang | 3.374 | 3.314 | 1.280 | 48.2 | 9.69 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.158 | 0.158 | 0.157 | 1.6 | 0.19 |
| go | 0.198 | 0.197 | 0.197 | 3.6 | 0.24 |
| slight | 0.842 | 0.829 | 0.841 | 3.0 | 1.00 |
| erlang | 2.611 | 2.540 | 1.555 | 48.4 | 3.10 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.528 | 0.520 | 0.528 | 2.1 | 1.00 |
| rust | 0.692 | 0.691 | 0.691 | 2.4 | 1.31 |
| go | 1.047 | 1.020 | 1.047 | 5.1 | 1.98 |
| erlang | 8.114 | 7.851 | 2.962 | 49.1 | 15.36 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.271 | 0.268 | 0.270 | 93.0 | 0.71 |
| go | 0.286 | 0.286 | 0.286 | 3.8 | 0.75 |
| slight | 0.383 | 0.381 | 0.381 | 483.6 | 1.00 |
| erlang | 1.731 | 1.724 | 1.687 | 650.1 | 4.52 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.137 | 0.135 | 0.135 | 90.2 | 1.00 |
| go | 0.439 | 0.414 | 0.433 | 409.7 | 3.21 |
| erlang | 1.226 | 1.210 | 0.694 | 114.1 | 8.96 |
| rust | 1.575 | 0.369 | 1.575 | 181.2 | 11.51 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.338 | 0.330 | 0.336 | 572.3 | 1.00 | 571 |
| rust | 0.780 | 0.751 | 0.779 | 1105.2 | 2.31 | 1104 |
| go | 1.460 | 1.418 | 1.454 | 2900.4 | 4.32 | 2897 |
| erlang | 2.964 | 2.905 | 1.892 | 2789.7 | 8.78 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.150 | 0.150 | 0.150 | 1.6 | 0.53 |
| go | 0.167 | 0.166 | 0.166 | 3.7 | 0.59 |
| slight | 0.281 | 0.281 | 0.280 | 3.6 | 1.00 |
| erlang | 0.890 | 0.875 | 0.521 | 48.5 | 3.17 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.98 | 0.94 |
|  | erlang-mc | 0.087 | 0.247 | 59.9 | 0.96 | 37.99 |
|  | rust-mc | 0.002 | 0.003 | 2.2 | 1.10 | 0.88 |
| fib | go-mc | 0.123 | 0.123 | 3.9 | 1.00 | 0.62 |
|  | erlang-mc | 0.365 | 0.531 | 60.5 | 0.55 | 1.83 |
| tak | go-mc | 0.102 | 0.102 | 3.9 | 1.00 | 0.72 |
|  | erlang-mc | 0.285 | 0.446 | 60.0 | 0.54 | 2.01 |
| binary-trees | go-mc | 0.657 | 1.861 | 40.8 | 0.48 | 0.64 |
|  | erlang-mc | 0.494 | 0.658 | 171.5 | 0.57 | 0.48 |
| nqueens | go-mc | 0.546 | 0.592 | 10.9 | 0.95 | 0.78 |
|  | erlang-mc | 0.466 | 0.628 | 60.0 | 0.69 | 0.66 |
| deriv | go-mc | 0.805 | 0.921 | 11.1 | 0.76 | 2.12 |
|  | erlang-mc | 0.312 | 0.479 | 59.7 | 0.62 | 0.82 |
| n-body | go-mc | 0.048 | 0.048 | 3.9 | 1.00 | 0.04 |
|  | erlang-mc | 1.188 | 1.366 | 60.0 | 0.90 | 0.92 |
| spectral-norm | go-mc | 0.047 | 0.047 | 4.0 | 1.00 | 0.11 |
|  | erlang-mc | 0.485 | 0.649 | 60.7 | 0.67 | 1.15 |
| ping-pong | go-mc | 0.524 | 0.752 | 4.0 | 1.36 | 1.51 |
|  | erlang-mc | 0.994 | 1.152 | 60.1 | 0.29 | 2.85 |
|  | rust-mc | 11.9 | 14.8 | 2.5 | 40.50 | 34.19 |
| payload | go-mc | 0.270 | 0.387 | 4.1 | 1.37 | 0.32 |
|  | erlang-mc | 1.397 | 1.629 | 61.3 | 0.54 | 1.66 |
|  | rust-mc | 5.965 | 7.419 | 2.4 | 37.84 | 7.09 |
| thread-ring | go-mc | 1.443 | 2.046 | 5.8 | 1.38 | 2.73 |
|  | erlang-mc | 2.366 | 2.548 | 61.0 | 0.29 | 4.48 |
|  | rust-mc | 1.178 | 1.920 | 3.2 | 1.70 | 2.23 |
| fan-in | go-mc | 3.505 | 11.8 | 5.2 | 12.24 | 9.14 |
|  | erlang-mc | 1.104 | 8.849 | 657.3 | 0.64 | 2.88 |
|  | rust-mc | 4.177 | 48.5 | 44.8 | 15.44 | 10.90 |
| spawn | go-mc | 0.393 | 1.838 | 27.4 | 0.89 | 2.87 |
|  | erlang-mc | 0.894 | 5.655 | 195.8 | 0.73 | 6.54 |
|  | rust-mc | 0.601 | 1.890 | 11.9 | 0.38 | 4.39 |
| idle-memory | go-mc | 1.018 | 5.993 | 2892.5 | 0.70 | 3.02 |
|  | erlang-mc | 1.886 | 6.444 | 2795.0 | 0.64 | 5.59 |
|  | rust-mc | 1.390 | 7.520 | 1097.9 | 1.78 | 4.12 |
| parallel-fib | go-mc | 0.021 | 0.197 | 4.9 | 0.13 | 0.08 |
|  | erlang-mc | 0.136 | 0.736 | 59.8 | 0.15 | 0.48 |
|  | rust-mc | 0.019 | 0.172 | 2.5 | 0.12 | 0.07 |

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
| chez/deriv | 0.04 | 4237 |
| chez/fib | 0.04 | 2166 |
| chez/n-body | 0.04 | 11321 |
| chez/nqueens | 0.04 | 3418 |
| chez/spectral-norm | 0.04 | 5509 |
| chez/startup | 0.04 | 1412 |
| chez/tak | 0.04 | 2582 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/deriv | 0.15 | 1612 |
| erlang/fan-in | 0.15 | 1468 |
| erlang/fib | 0.15 | 928 |
| erlang/idle-memory | 0.15 | 1312 |
| erlang/n-body | 0.17 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/payload | 0.15 | 1268 |
| erlang/ping-pong | 0.15 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.15 | 764 |
| erlang/tak | 0.15 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.09 | 2430210 |
| go/deriv | 0.10 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/n-body | 0.10 | 2430274 |
| go/nqueens | 0.10 | 2430130 |
| go/parallel-fib | 0.09 | 2430194 |
| go/payload | 0.12 | 2430226 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.30 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.36 | 817760 |
| rust/parallel-fib | 0.30 | 787920 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/startup | 1.90 | 708464 |
| rust/thread-ring | 0.33 | 810560 |
| slight/binary-trees | 0.21 | 314752 |
| slight/deriv | 0.21 | 314600 |
| slight/fan-in | 0.21 | 298168 |
| slight/fib | 0.21 | 297840 |
| slight/idle-memory | 0.21 | 298280 |
| slight/n-body | 0.24 | 348504 |
| slight/nqueens | 0.21 | 297960 |
| slight/parallel-fib | 0.21 | 297872 |
| slight/payload | 0.21 | 298152 |
| slight/ping-pong | 0.21 | 298168 |
| slight/spawn | 0.20 | 298152 |
| slight/spectral-norm | 0.21 | 314848 |
| slight/startup | 1.36 | 297768 |
| slight/tak | 0.21 | 297840 |
| slight/thread-ring | 0.21 | 298200 |
