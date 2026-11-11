# Benchmarks, 2026-11-11 08:39:40

Intel(R) Xeon(R) Processor @ 2.10GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v378.

- slight: 3fa502f, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
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
| c | 0.001 | 0.001 | 0.001 | 1.3 | 0.74 |
| slight | 0.002 | 0.002 | 0.002 | 1.4 | 1.00 |
| rust | 0.002 | 0.002 | 0.002 | 2.8 | 1.14 |
| go | 0.002 | 0.002 | 0.002 | 1.8 | 1.24 |
| perl | 0.002 | 0.002 | 0.002 | 5.2 | 1.36 |
| python | 0.016 | 0.015 | 0.016 | 11.4 | 9.12 |
| node | 0.040 | 0.035 | 0.036 | 46.1 | 22.19 |
| chez | 0.085 | 0.079 | 0.081 | 50.7 | 47.36 |
| erlang | 0.178 | 0.159 | 0.194 | 36.5 | 99.46 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.075 | 0.070 | 0.069 | 1.8 | 0.22 |
| go | 0.214 | 0.204 | 0.209 | 1.7 | 0.64 |
| slight | 0.337 | 0.316 | 0.335 | 1.4 | 1.00 |
| chez | 0.338 | 0.322 | 0.334 | 50.7 | 1.00 |
| node | 0.535 | 0.474 | 0.528 | 51.4 | 1.59 |
| erlang | 0.744 | 0.599 | 0.756 | 36.3 | 2.21 |
| python | 4.907 | 4.179 | 4.803 | 11.5 | 14.56 |
| perl | 16.0 | 15.5 | 15.8 | 6.0 | 47.37 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.148 | 0.115 | 0.148 | 1.8 | 0.48 |
| go | 0.160 | 0.156 | 0.159 | 1.7 | 0.52 |
| chez | 0.259 | 0.235 | 0.252 | 50.7 | 0.84 |
| slight | 0.309 | 0.259 | 0.295 | 1.4 | 1.00 |
| node | 0.492 | 0.368 | 0.487 | 51.5 | 1.59 |
| erlang | 0.552 | 0.492 | 0.563 | 36.6 | 1.78 |
| python | 3.731 | 3.453 | 3.665 | 11.5 | 12.06 |
| perl | 21.1 | 18.6 | 20.9 | 6.0 | 68.05 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.550 | 0.521 | 0.535 | 62.2 | 0.29 |
| erlang | 1.092 | 0.978 | 1.104 | 161.4 | 0.58 |
| c | 1.395 | 1.212 | 1.390 | 35.3 | 0.74 |
| slight | 1.879 | 1.751 | 1.848 | 102.1 | 1.00 |
| node | 1.908 | 1.626 | 3.073 | 277.1 | 1.02 |
| go | 4.038 | 3.779 | 4.020 | 50.1 | 2.15 |
| python | 7.451 | 7.122 | 7.428 | 45.1 | 3.97 |
| perl | 32.8 | 32.6 | 32.5 | 158.1 | 17.47 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.424 | 0.386 | 0.418 | 1.8 | 0.56 |
| chez | 0.695 | 0.668 | 0.684 | 50.7 | 0.92 |
| slight | 0.758 | 0.686 | 0.742 | 3.0 | 1.00 |
| erlang | 1.023 | 0.994 | 1.034 | 36.3 | 1.35 |
| go | 1.140 | 1.085 | 1.134 | 6.8 | 1.50 |
| node | 4.290 | 3.596 | 4.256 | 69.5 | 5.66 |
| python | 8.069 | 7.447 | 7.980 | 11.7 | 10.64 |
| perl | 20.4 | 18.8 | 20.3 | 6.1 | 26.88 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.226 | 0.193 | 0.215 | 1.8 | 0.49 |
| chez | 0.366 | 0.304 | 0.360 | 50.7 | 0.80 |
| erlang | 0.434 | 0.389 | 0.451 | 36.5 | 0.95 |
| slight | 0.457 | 0.439 | 0.446 | 3.6 | 1.00 |
| node | 1.122 | 1.094 | 1.184 | 62.5 | 2.46 |
| go | 2.361 | 2.224 | 2.355 | 6.7 | 5.17 |
| python | 4.709 | 3.826 | 4.703 | 11.6 | 10.31 |
| perl | 12.8 | 12.0 | 12.7 | 6.1 | 28.02 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.071 | 0.053 | 0.070 | 1.8 | 0.08 |
| go | 0.102 | 0.064 | 0.103 | 1.7 | 0.11 |
| node | 0.148 | 0.136 | 0.153 | 54.4 | 0.16 |
| slight | 0.931 | 0.732 | 0.895 | 3.8 | 1.00 |
| chez | 1.641 | 1.470 | 1.625 | 50.7 | 1.76 |
| erlang | 1.889 | 1.832 | 1.878 | 36.2 | 2.03 |
| python | 4.429 | 4.247 | 4.413 | 12.1 | 4.76 |
| perl | 10.6 | 10.4 | 10.5 | 7.8 | 11.36 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.059 | 0.054 | 0.056 | 1.7 | 0.09 |
| c | 0.062 | 0.052 | 0.054 | 2.3 | 0.10 |
| slight-arrays | 0.141 | 0.125 | 0.138 | 40.7 | 0.22 |
| node | 0.185 | 0.128 | 0.194 | 54.4 | 0.30 |
| slight | 0.627 | 0.521 | 0.622 | 3.9 | 1.00 |
| chez | 0.982 | 0.966 | 0.957 | 50.7 | 1.57 |
| erlang | 1.428 | 1.390 | 1.437 | 36.7 | 2.28 |
| python | 6.395 | 5.980 | 6.344 | 12.0 | 10.20 |
| perl | 10.9 | 9.331 | 10.8 | 8.0 | 17.40 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.545 | 0.491 | 0.528 | 2.5 | 1.00 |
| rust | 0.650 | 0.573 | 0.621 | 2.8 | 1.19 |
| go | 0.877 | 0.784 | 0.866 | 1.7 | 1.61 |
| erlang | 1.644 | 1.630 | 1.662 | 36.0 | 3.02 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.348 | 0.300 | 0.344 | 2.8 | 0.35 |
| go | 0.459 | 0.417 | 0.454 | 1.7 | 0.46 |
| slight | 0.988 | 0.856 | 0.982 | 1.9 | 1.00 |
| erlang | 2.123 | 2.002 | 2.100 | 36.0 | 2.15 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 1.357 | 1.289 | 1.328 | 3.6 | 0.95 |
| slight | 1.430 | 1.305 | 1.406 | 3.8 | 1.00 |
| go | 2.300 | 2.272 | 2.272 | 3.2 | 1.61 |
| erlang | 4.151 | 3.928 | 4.135 | 37.0 | 2.90 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.465 | 0.421 | 0.453 | 2.0 | 0.41 |
| rust | 0.581 | 0.541 | 0.573 | 97.7 | 0.51 |
| slight | 1.138 | 0.878 | 1.119 | 635.9 | 1.00 |
| erlang | 2.395 | 2.329 | 2.376 | 637.3 | 2.10 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.378 | 0.294 | 0.377 | 114.0 | 1.00 |
| rust | 0.520 | 0.504 | 0.502 | 266.6 | 1.37 |
| erlang | 1.173 | 1.129 | 1.188 | 112.5 | 3.10 |
| go | 1.372 | 1.213 | 1.367 | 124.0 | 3.63 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| rust | 1.770 | 1.683 | 1.765 | 1042.9 |  | 1040 |
| erlang | 4.628 | 4.542 | 4.587 | 2792.4 |  | 2756 |
| go | 5.412 | 4.854 | 5.403 | 2817.5 |  | 2816 |
| slight | wrong output or failed | | | | | |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.179 | 0.156 | 0.178 | 2.8 | 0.41 |
| go | 0.263 | 0.260 | 0.259 | 2.0 | 0.60 |
| slight | 0.436 | 0.399 | 0.428 | 1.9 | 1.00 |
| erlang | 0.813 | 0.773 | 0.828 | 36.5 | 1.86 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 121 µs | 7 µs | 70 µs | 1.1 ms | 578 µs | 162.8 ms | 183.7 ms | 486.5 |
| go | 1 µs | 7 µs | 10.0 ms | 20.2 ms | 21.5 ms | 12 µs | 212 µs | 396 µs | 165.1 |
| erlang | 2 µs | 21 µs | 25 µs | 229 µs | 3.3 ms | 4 µs | 737 µs | 3.9 ms | 1721.5 |
| rust | 1 µs | 8 µs | 125.5 ms | 248.5 ms | 250.5 ms | 14 µs | 103 µs | 322 µs | 82.8 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 5 µs | 61 µs | 5 µs | 48 µs | 963 µs | 5 µs | 37 µs | 287 µs | 169.3 |
| erlang-mc | 2 µs | 26 µs | 2 µs | 20 µs | 1.6 ms | 2 µs | 165 µs | 4.6 ms | 1723.8 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.042 | 0.034 | 0.042 | 2.8 | 0.04 |
| slight | 1.094 | 0.956 | 1.069 | 52.2 | 1.00 |
| python | 8.327 | 7.978 | 8.236 | 20.2 | 7.61 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 1.9 | 0.83 | 1.03 |
|  | erlang-mc | 0.159 | 0.250 | 38.5 | 0.89 | 88.90 |
|  | rust-mc | 0.003 | 0.003 | 2.8 | 1.23 | 1.40 |
| fib | go-mc | 0.195 | 0.192 | 1.9 | 0.91 | 0.58 |
|  | erlang-mc | 0.662 | 0.742 | 38.5 | 0.89 | 1.97 |
| tak | go-mc | 0.189 | 0.191 | 1.8 | 1.18 | 0.61 |
|  | erlang-mc | 0.554 | 0.635 | 38.5 | 1.00 | 1.79 |
| binary-trees | go-mc | 2.515 | 4.977 | 38.8 | 0.62 | 1.34 |
|  | erlang-mc | 1.023 | 1.119 | 152.6 | 0.94 | 0.54 |
| nqueens | go-mc | 1.278 | 1.387 | 7.9 | 1.12 | 1.69 |
|  | erlang-mc | 1.104 | 1.168 | 38.7 | 1.08 | 1.46 |
| deriv | go-mc | 2.591 | 2.898 | 10.5 | 1.10 | 5.67 |
|  | erlang-mc | 0.416 | 0.509 | 38.7 | 0.96 | 0.91 |
| n-body | go-mc | 0.067 | 0.067 | 1.9 | 0.66 | 0.07 |
|  | erlang-mc | 1.875 | 1.958 | 38.7 | 0.99 | 2.01 |
| spectral-norm | slight-mc | 0.048 | 0.150 | 39.5 | 0.34 | 0.08 |
|  | go-mc | 0.077 | 0.078 | 1.8 | 1.31 | 0.12 |
|  | erlang-mc | 1.399 | 1.475 | 39.1 | 0.98 | 2.23 |
| ping-pong | slight-mc | 0.224 | 0.642 | 5.6 | 0.41 | 0.41 |
|  | go-mc | 0.867 | 1.113 | 1.7 | 0.99 | 1.59 |
|  | erlang-mc | 1.815 | 1.878 | 38.6 | 1.10 | 3.33 |
|  | rust-mc | 81.6 | 82.5 | 3.2 | 125.51 | 149.72 |
| payload | go-mc | 0.453 | 0.575 | 1.9 | 0.99 | 0.46 |
|  | erlang-mc | 2.203 | 2.483 | 38.7 | 1.04 | 2.23 |
|  | rust-mc | 40.7 | 41.1 | 3.2 | 116.88 | 41.19 |
| thread-ring | go-mc | 2.782 | 3.637 | 3.2 | 1.21 | 1.95 |
|  | erlang-mc | 3.964 | 4.040 | 38.9 | 0.95 | 2.77 |
|  | rust-mc | 5.045 | 6.157 | 3.9 | 3.72 | 3.53 |
| fan-in | slight-mc | 3.326 | 9.451 | 267.8 | 2.92 | 2.92 |
|  | go-mc | 1.701 | 3.859 | 2.1 | 3.66 | 1.49 |
|  | erlang-mc | 1.216 | 3.451 | 571.3 | 0.51 | 1.07 |
|  | rust-mc | 3.885 | 14.9 | 3.6 | 6.69 | 3.41 |
| spawn | slight-mc | 0.150 | 0.460 | 111.3 | 0.40 | 0.40 |
|  | go-mc | 0.727 | 1.984 | 36.5 | 0.53 | 1.92 |
|  | erlang-mc | 3.354 | 7.535 | 132.9 | 2.86 | 8.87 |
|  | rust-mc | 1.475 | 3.678 | 15.6 | 2.84 | 3.90 |
| idle-memory | slight-mc | wrong output or failed | | | | |
|  | go-mc | 3.525 | 8.428 | 2816.7 | 0.65 |  |
|  | erlang-mc | 6.638 | 12.8 | 2790.5 | 1.43 |  |
|  | rust-mc | 2.921 | 7.095 | 1034.9 | 1.65 |  |
| parallel-fib | slight-mc | 0.142 | 0.461 | 1.9 | 0.33 | 0.33 |
|  | go-mc | 0.073 | 0.270 | 2.0 | 0.28 | 0.17 |
|  | erlang-mc | 0.368 | 1.019 | 38.7 | 0.45 | 0.84 |
|  | rust-mc | 0.048 | 0.177 | 3.2 | 0.27 | 0.11 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.09 | 16176 |
| c/binary-trees | 0.19 | 20288 |
| c/deriv | 0.14 | 17184 |
| c/fib | 0.10 | 16040 |
| c/n-body | 0.11 | 16368 |
| c/nqueens | 0.09 | 16144 |
| c/spectral-norm | 0.09 | 16168 |
| c/startup | 0.04 | 15960 |
| c/tak | 0.06 | 16040 |
| chez/binary-trees | 0.07 | 4113 |
| chez/deriv | 0.09 | 4401 |
| chez/fib | 0.07 | 2109 |
| chez/n-body | 0.11 | 11007 |
| chez/nqueens | 0.07 | 3431 |
| chez/spectral-norm | 0.09 | 5384 |
| chez/startup | 0.07 | 1312 |
| chez/tak | 0.07 | 2542 |
| erlang/binary-trees | 0.22 | 1464 |
| erlang/deriv | 0.26 | 1584 |
| erlang/fan-in | 0.26 | 1464 |
| erlang/fib | 0.24 | 900 |
| erlang/idle-memory | 0.26 | 1288 |
| erlang/latency | 0.28 | 3728 |
| erlang/n-body | 0.25 | 3824 |
| erlang/nqueens | 0.27 | 1204 |
| erlang/parallel-fib | 0.28 | 1404 |
| erlang/payload | 0.21 | 1268 |
| erlang/ping-pong | 0.24 | 1192 |
| erlang/spawn | 0.23 | 1260 |
| erlang/spectral-norm | 0.24 | 2220 |
| erlang/startup | 0.27 | 740 |
| erlang/tak | 0.28 | 988 |
| erlang/thread-ring | 0.24 | 1560 |
| go/binary-trees | 0.05 | 2231034 |
| go/deriv | 0.05 | 2236851 |
| go/fan-in | 0.05 | 2230271 |
| go/fib | 0.05 | 2230446 |
| go/idle-memory | 0.05 | 2230941 |
| go/latency | 0.05 | 2292888 |
| go/n-body | 0.05 | 2231717 |
| go/nqueens | 0.05 | 2230990 |
| go/parallel-fib | 0.05 | 2231446 |
| go/payload | 0.05 | 2230879 |
| go/ping-pong | 0.05 | 2230719 |
| go/spawn | 0.05 | 2230823 |
| go/spectral-norm | 0.05 | 2244557 |
| go/startup | 0.06 | 2209255 |
| go/tak | 0.04 | 2229998 |
| go/thread-ring | 0.04 | 2230931 |
| rust/fan-in | 0.07 | 823800 |
| rust/idle-memory | 0.07 | 864808 |
| rust/latency | 0.07 | 939464 |
| rust/parallel-fib | 0.06 | 827128 |
| rust/payload | 0.06 | 831440 |
| rust/ping-pong | 0.07 | 826504 |
| rust/spawn | 0.08 | 823872 |
| rust/startup | 0.09 | 744016 |
| rust/thread-ring | 0.07 | 845496 |
| slight-arrays/spectral-norm | 0.44 | 2739392 |
| slight-mc/fan-in | 0.47 | 2725264 |
| slight-mc/idle-memory | 0.44 | 2742424 |
| slight-mc/parallel-fib | 0.50 | 2725568 |
| slight-mc/ping-pong | 0.45 | 2737016 |
| slight-mc/spawn | 0.43 | 2737800 |
| slight-mc/spectral-norm | 0.52 | 2756392 |
| slight/bank | 0.47 | 2742936 |
| slight/binary-trees | 0.43 | 2739232 |
| slight/deriv | 0.51 | 2736976 |
| slight/fan-in | 0.47 | 2724904 |
| slight/fib | 0.47 | 2722144 |
| slight/idle-memory | 0.47 | 2725600 |
| slight/latency | 0.44 | 2760592 |
| slight/n-body | 0.49 | 2828144 |
| slight/nqueens | 0.49 | 2725832 |
| slight/parallel-fib | 0.45 | 2723640 |
| slight/payload | 0.50 | 2723848 |
| slight/ping-pong | 0.45 | 2723568 |
| slight/spawn | 0.50 | 2724344 |
| slight/spectral-norm | 0.50 | 2744552 |
| slight/startup | 0.52 | 2719624 |
| slight/tak | 0.44 | 2723256 |
| slight/thread-ring | 0.45 | 2724816 |
