# Benchmarks, 2026-11-11 11:06:36

Intel(R) Xeon(R) Processor @ 2.10GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v378.

- slight: 9fd007c, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
- slight-arrays: Ubuntu clang version 18.1.3 (1ubuntu1)
- slight-mc: Ubuntu clang version 18.1.3 (1ubuntu1)
- c: cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- go: go version go1.24.7 linux/amd64
- erlang: OTP 25, erts 13.2.2.5
- node: v22.23.4
- perl: perl v5.38.2
- python: Python 3.13.18, SimPy 4.1.2
- chez: Chez Scheme 9.5.8
- rust: rustc 1.97.0 (2d8144b78 2026-07-07), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.001 | 0.001 | 0.001 | 1.3 | 0.80 |
| slight | 0.002 | 0.001 | 0.001 | 1.4 | 1.00 |
| rust | 0.002 | 0.002 | 0.002 | 2.7 | 1.09 |
| go | 0.002 | 0.002 | 0.002 | 1.7 | 1.22 |
| perl | 0.003 | 0.002 | 0.002 | 5.1 | 1.63 |
| python | 0.017 | 0.014 | 0.017 | 11.1 | 10.73 |
| node | 0.039 | 0.032 | 0.039 | 45.6 | 24.24 |
| chez | 0.085 | 0.080 | 0.084 | 50.7 | 52.62 |
| erlang | 0.164 | 0.159 | 0.180 | 36.2 | 101.32 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.067 | 0.064 | 0.066 | 1.7 | 0.18 |
| go | 0.195 | 0.188 | 0.195 | 1.7 | 0.53 |
| chez | 0.332 | 0.324 | 0.328 | 50.7 | 0.90 |
| slight | 0.369 | 0.279 | 0.362 | 1.3 | 1.00 |
| node | 0.515 | 0.459 | 0.505 | 50.8 | 1.40 |
| erlang | 0.662 | 0.633 | 0.683 | 36.2 | 1.80 |
| python | 4.991 | 4.472 | 4.972 | 11.2 | 13.53 |
| perl | 16.4 | 15.2 | 16.3 | 5.9 | 44.38 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.129 | 0.114 | 0.128 | 1.7 | 0.47 |
| go | 0.170 | 0.150 | 0.170 | 1.7 | 0.62 |
| chez | 0.254 | 0.240 | 0.249 | 50.6 | 0.92 |
| slight | 0.276 | 0.263 | 0.269 | 1.4 | 1.00 |
| node | 0.381 | 0.371 | 0.379 | 50.8 | 1.38 |
| erlang | 0.510 | 0.505 | 0.524 | 36.3 | 1.84 |
| python | 3.656 | 3.502 | 3.632 | 11.2 | 13.23 |
| perl | 20.1 | 19.7 | 19.9 | 5.9 | 72.62 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.531 | 0.520 | 0.530 | 62.1 | 0.28 |
| erlang | 0.959 | 0.878 | 0.974 | 161.2 | 0.51 |
| c | 1.527 | 1.404 | 1.520 | 35.3 | 0.81 |
| node | 1.859 | 1.737 | 2.958 | 295.2 | 0.99 |
| slight | 1.877 | 1.796 | 1.851 | 102.1 | 1.00 |
| go | 3.826 | 3.578 | 3.799 | 50.8 | 2.04 |
| python | 7.121 | 6.990 | 7.050 | 44.8 | 3.79 |
| perl | 32.6 | 32.2 | 32.5 | 158.0 | 17.39 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.395 | 0.376 | 0.390 | 1.7 | 0.56 |
| chez | 0.688 | 0.661 | 0.676 | 50.6 | 0.98 |
| slight | 0.703 | 0.697 | 0.691 | 3.0 | 1.00 |
| erlang | 1.054 | 0.968 | 1.064 | 36.0 | 1.50 |
| go | 1.174 | 1.077 | 1.146 | 6.7 | 1.67 |
| node | 3.907 | 3.691 | 3.918 | 68.9 | 5.55 |
| python | 8.225 | 7.605 | 8.120 | 11.4 | 11.69 |
| perl | 19.2 | 17.9 | 19.0 | 5.9 | 27.30 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.216 | 0.191 | 0.213 | 1.7 | 0.38 |
| chez | 0.350 | 0.295 | 0.345 | 50.7 | 0.61 |
| erlang | 0.411 | 0.382 | 0.427 | 36.4 | 0.71 |
| slight | 0.575 | 0.477 | 0.563 | 3.6 | 1.00 |
| node | 1.116 | 1.010 | 1.162 | 62.1 | 1.94 |
| go | 2.299 | 2.124 | 2.290 | 6.7 | 4.00 |
| python | 4.356 | 4.143 | 4.315 | 11.4 | 7.58 |
| perl | 11.8 | 11.2 | 11.7 | 6.0 | 20.58 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.056 | 0.054 | 0.053 | 1.7 | 0.07 |
| go | 0.067 | 0.065 | 0.067 | 1.8 | 0.09 |
| node | 0.134 | 0.121 | 0.130 | 53.9 | 0.17 |
| slight | 0.780 | 0.720 | 0.768 | 3.8 | 1.00 |
| chez | 1.647 | 1.580 | 1.628 | 50.7 | 2.11 |
| erlang | 1.896 | 1.684 | 1.895 | 36.1 | 2.43 |
| python | 4.403 | 4.074 | 4.345 | 11.8 | 5.64 |
| perl | 9.982 | 9.466 | 9.870 | 7.7 | 12.80 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.063 | 0.057 | 0.061 | 2.1 | 0.11 |
| go | 0.092 | 0.053 | 0.093 | 1.7 | 0.15 |
| node | 0.144 | 0.125 | 0.151 | 53.8 | 0.24 |
| slight-arrays | 0.156 | 0.140 | 0.153 | 40.7 | 0.26 |
| slight | 0.594 | 0.538 | 0.572 | 3.9 | 1.00 |
| chez | 1.042 | 0.981 | 1.030 | 50.6 | 1.75 |
| erlang | 1.479 | 1.379 | 1.475 | 36.4 | 2.49 |
| python | 6.245 | 6.020 | 6.192 | 11.6 | 10.52 |
| perl | 10.8 | 9.283 | 10.7 | 7.7 | 18.18 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.458 | 0.418 | 0.451 | 2.4 | 1.00 |
| rust | 0.643 | 0.600 | 0.617 | 2.7 | 1.40 |
| go | 0.828 | 0.824 | 0.822 | 1.7 | 1.81 |
| erlang | 1.732 | 1.617 | 1.732 | 35.9 | 3.78 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.337 | 0.312 | 0.335 | 2.8 | 0.37 |
| go | 0.441 | 0.414 | 0.439 | 1.8 | 0.48 |
| slight | 0.923 | 0.808 | 0.892 | 1.9 | 1.00 |
| erlang | 1.926 | 1.907 | 1.932 | 35.8 | 2.09 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.732 | 0.717 | 0.724 | 3.7 | 1.00 |
| rust | 1.354 | 1.263 | 1.342 | 3.5 | 1.85 |
| go | 2.316 | 2.191 | 2.283 | 3.2 | 3.16 |
| erlang | 3.899 | 3.714 | 3.848 | 36.9 | 5.33 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.519 | 0.425 | 0.517 | 2.0 | 0.54 |
| rust | 0.578 | 0.550 | 0.572 | 97.7 | 0.60 |
| slight | 0.968 | 0.873 | 0.953 | 635.5 | 1.00 |
| erlang | 2.479 | 2.180 | 2.472 | 637.1 | 2.56 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.293 | 0.240 | 0.289 | 114.0 | 1.00 |
| rust | 0.445 | 0.407 | 0.443 | 266.5 | 1.52 |
| erlang | 1.258 | 1.157 | 1.276 | 112.4 | 4.29 |
| go | 1.337 | 1.246 | 1.303 | 119.0 | 4.56 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| rust | 1.908 | 1.819 | 1.895 | 1043.0 | 0.22 | 1040 |
| erlang | 4.987 | 4.631 | 4.991 | 2791.9 | 0.56 | 2756 |
| go | 5.707 | 5.348 | 5.680 | 2796.2 | 0.64 | 2794 |
| slight | 8.868 | 8.480 | 8.782 | 4554.1 | 1.00 | 4553 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.177 | 0.162 | 0.172 | 2.7 | 0.38 |
| go | 0.281 | 0.263 | 0.271 | 2.0 | 0.61 |
| slight | 0.464 | 0.432 | 0.458 | 1.7 | 1.00 |
| erlang | 0.821 | 0.729 | 0.816 | 36.3 | 1.77 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 49 µs | 6 µs | 171 µs | 709 µs | 454 µs | 128.9 ms | 146.9 ms | 486.3 |
| go | 1 µs | 167 µs | 10.0 ms | 20.4 ms | 23.6 ms | 12 µs | 203 µs | 420 µs | 165.2 |
| erlang | 2 µs | 47 µs | 19 µs | 112 µs | 1.3 ms | 2 µs | 523 µs | 5.4 ms | 1734.2 |
| rust | 1 µs | 8 µs | 141.6 ms | 279.5 ms | 281.5 ms | 14 µs | 29 µs | 50 µs | 82.7 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 6 µs | 136 µs | 5 µs | 68 µs | 954 µs | 4 µs | 156 µs | 319 µs | 167.8 |
| erlang-mc | 2 µs | 326 µs | 2 µs | 169 µs | 3.6 ms | 2 µs | 190 µs | 4.6 ms | 1723.4 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.035 | 0.034 | 0.035 | 2.8 | 0.03 |
| slight | 1.200 | 1.146 | 1.186 | 52.1 | 1.00 |
| python | 8.396 | 8.079 | 8.297 | 19.8 | 6.99 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 1.8 | 0.97 | 1.19 |
|  | erlang-mc | 0.157 | 0.229 | 38.2 | 0.96 | 97.07 |
|  | rust-mc | 0.003 | 0.003 | 2.9 | 1.42 | 1.55 |
| fib | go-mc | 0.209 | 0.210 | 1.7 | 1.07 | 0.57 |
|  | erlang-mc | 0.673 | 0.754 | 38.6 | 1.02 | 1.83 |
| tak | go-mc | 0.181 | 0.168 | 1.7 | 1.06 | 0.66 |
|  | erlang-mc | 0.512 | 0.586 | 38.6 | 1.00 | 1.85 |
| binary-trees | go-mc | 2.566 | 5.069 | 44.5 | 0.67 | 1.37 |
|  | erlang-mc | 1.037 | 1.138 | 152.7 | 1.08 | 0.55 |
| nqueens | go-mc | 1.258 | 1.372 | 7.9 | 1.07 | 1.79 |
|  | erlang-mc | 1.080 | 1.148 | 38.4 | 1.02 | 1.54 |
| deriv | go-mc | 2.397 | 2.759 | 8.1 | 1.04 | 4.17 |
|  | erlang-mc | 0.429 | 0.513 | 38.7 | 1.04 | 0.75 |
| n-body | go-mc | 0.066 | 0.068 | 1.9 | 0.99 | 0.09 |
|  | erlang-mc | 1.805 | 1.859 | 38.3 | 0.95 | 2.31 |
| spectral-norm | slight-mc | 0.049 | 0.158 | 39.6 | 0.31 | 0.08 |
|  | go-mc | 0.057 | 0.057 | 1.9 | 0.62 | 0.10 |
|  | erlang-mc | 1.448 | 1.467 | 39.2 | 0.98 | 2.44 |
| ping-pong | slight-mc | 0.209 | 0.637 | 5.6 | 0.45 | 0.45 |
|  | go-mc | 0.860 | 1.095 | 1.7 | 1.04 | 1.88 |
|  | erlang-mc | 1.669 | 1.717 | 38.1 | 0.96 | 3.64 |
|  | rust-mc | 79.3 | 79.2 | 3.0 | 123.38 | 172.99 |
| payload | go-mc | 0.430 | 0.543 | 1.8 | 0.97 | 0.47 |
|  | erlang-mc | 2.156 | 2.493 | 38.3 | 1.12 | 2.34 |
|  | rust-mc | 39.9 | 39.6 | 3.0 | 118.17 | 43.19 |
| thread-ring | go-mc | 2.444 | 3.157 | 3.2 | 1.06 | 3.34 |
|  | erlang-mc | 3.827 | 3.877 | 38.9 | 0.98 | 5.23 |
|  | rust-mc | 4.920 | 6.021 | 3.6 | 3.63 | 6.72 |
| fan-in | slight-mc | 3.590 | 10.2 | 254.6 | 3.71 | 3.71 |
|  | go-mc | 1.621 | 3.650 | 2.2 | 3.13 | 1.68 |
|  | erlang-mc | 0.992 | 3.121 | 313.6 | 0.40 | 1.02 |
|  | rust-mc | 3.634 | 13.8 | 3.4 | 6.29 | 3.76 |
| spawn | slight-mc | 0.125 | 0.402 | 111.8 | 0.43 | 0.43 |
|  | go-mc | 0.697 | 1.897 | 31.2 | 0.52 | 2.38 |
|  | erlang-mc | 3.167 | 7.151 | 132.7 | 2.52 | 10.80 |
|  | rust-mc | 1.416 | 3.461 | 15.5 | 3.18 | 4.83 |
| idle-memory | slight-mc | 4.143 | 12.9 | 4554.2 | 0.47 | 0.47 |
|  | go-mc | 3.745 | 8.845 | 2816.6 | 0.66 | 0.42 |
|  | erlang-mc | 6.748 | 12.7 | 2789.4 | 1.35 | 0.76 |
|  | rust-mc | 2.860 | 6.866 | 1034.9 | 1.50 | 0.32 |
| parallel-fib | slight-mc | 0.131 | 0.444 | 1.7 | 0.28 | 0.28 |
|  | go-mc | 0.077 | 0.290 | 2.0 | 0.28 | 0.17 |
|  | erlang-mc | 0.338 | 0.917 | 38.5 | 0.41 | 0.73 |
|  | rust-mc | 0.046 | 0.170 | 3.0 | 0.26 | 0.10 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.09 | 16176 |
| c/binary-trees | 0.15 | 20288 |
| c/deriv | 0.14 | 17184 |
| c/fib | 0.09 | 16040 |
| c/n-body | 0.12 | 16368 |
| c/nqueens | 0.11 | 16144 |
| c/spectral-norm | 0.09 | 16168 |
| c/startup | 0.05 | 15960 |
| c/tak | 0.06 | 16040 |
| chez/binary-trees | 0.07 | 4113 |
| chez/deriv | 0.09 | 4401 |
| chez/fib | 0.09 | 2109 |
| chez/n-body | 0.10 | 11007 |
| chez/nqueens | 0.09 | 3431 |
| chez/spectral-norm | 0.07 | 5384 |
| chez/startup | 0.31 | 1312 |
| chez/tak | 0.07 | 2542 |
| erlang/binary-trees | 0.24 | 1464 |
| erlang/deriv | 0.22 | 1584 |
| erlang/fan-in | 0.28 | 1464 |
| erlang/fib | 0.24 | 900 |
| erlang/idle-memory | 0.29 | 1288 |
| erlang/latency | 0.22 | 3728 |
| erlang/n-body | 0.26 | 3824 |
| erlang/nqueens | 0.23 | 1204 |
| erlang/parallel-fib | 0.29 | 1404 |
| erlang/payload | 0.23 | 1268 |
| erlang/ping-pong | 0.25 | 1192 |
| erlang/spawn | 0.29 | 1260 |
| erlang/spectral-norm | 0.27 | 2220 |
| erlang/startup | 0.24 | 740 |
| erlang/tak | 0.21 | 988 |
| erlang/thread-ring | 0.22 | 1560 |
| go/binary-trees | 0.19 | 2231034 |
| go/deriv | 0.37 | 2236851 |
| go/fan-in | 0.29 | 2230271 |
| go/fib | 0.07 | 2230446 |
| go/idle-memory | 0.05 | 2230941 |
| go/latency | 0.16 | 2292888 |
| go/n-body | 0.20 | 2231717 |
| go/nqueens | 0.20 | 2230990 |
| go/parallel-fib | 0.05 | 2231446 |
| go/payload | 0.38 | 2230879 |
| go/ping-pong | 0.11 | 2230719 |
| go/spawn | 0.11 | 2230823 |
| go/spectral-norm | 0.26 | 2244557 |
| go/startup | 1.47 | 2209255 |
| go/tak | 0.21 | 2229998 |
| go/thread-ring | 0.05 | 2230931 |
| rust/fan-in | 0.05 | 823800 |
| rust/idle-memory | 0.06 | 864808 |
| rust/latency | 0.08 | 939464 |
| rust/parallel-fib | 0.06 | 827128 |
| rust/payload | 0.06 | 831440 |
| rust/ping-pong | 0.06 | 826504 |
| rust/spawn | 0.08 | 823872 |
| rust/startup | 0.45 | 744016 |
| rust/thread-ring | 0.06 | 845496 |
| slight-arrays/spectral-norm | 0.47 | 2744040 |
| slight-mc/fan-in | 0.42 | 2734008 |
| slight-mc/idle-memory | 0.46 | 2747072 |
| slight-mc/parallel-fib | 0.45 | 2734312 |
| slight-mc/ping-pong | 0.44 | 2741664 |
| slight-mc/spawn | 0.49 | 2742448 |
| slight-mc/spectral-norm | 0.49 | 2761040 |
| slight/bank | 0.50 | 2751680 |
| slight/binary-trees | 0.43 | 2743880 |
| slight/deriv | 0.51 | 2741624 |
| slight/fan-in | 0.42 | 2733648 |
| slight/fib | 0.43 | 2726792 |
| slight/idle-memory | 0.46 | 2734344 |
| slight/latency | 0.48 | 2769336 |
| slight/n-body | 0.50 | 2836888 |
| slight/nqueens | 0.43 | 2734576 |
| slight/parallel-fib | 0.45 | 2728288 |
| slight/payload | 0.45 | 2732592 |
| slight/ping-pong | 0.41 | 2732312 |
| slight/spawn | 0.44 | 2733088 |
| slight/spectral-norm | 0.43 | 2753296 |
| slight/startup | 1.87 | 2724272 |
| slight/tak | 0.41 | 2727904 |
| slight/thread-ring | 0.50 | 2733560 |
