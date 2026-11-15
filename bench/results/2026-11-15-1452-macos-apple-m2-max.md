# Benchmarks, 2026-11-15 14:52:45

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 42d5033, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
- gambit: Gambit v4.9.8
- racket: Racket v9.3 [cs]
- racket-mc: Racket v9.3 [cs]
- swish: Swish Version 3.0.0 (a05e807530394fd5950d9b84bc38281cb605f8ea)

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is, and so are Gambit's, Racket's and Swish's threads; their defaults, and Racket's places, every core, are in a table of their own, at the end. "Not copied" marks a payload that passes its list by reference.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.002 | 0.001 | 0.001 | 1.2 | 0.45 |
| rust | 0.003 | 0.002 | 0.002 | 1.6 | 0.76 |
| slight | 0.004 | 0.002 | 0.002 | 1.7 | 1.00 |
| go | 0.006 | 0.002 | 0.003 | 3.6 | 1.57 |
| perl | 0.010 | 0.003 | 0.004 | 3.5 | 2.37 |
| python | 0.017 | 0.017 | 0.016 | 12.0 | 4.23 |
| gambit | 0.018 | 0.005 | 0.007 | 6.6 | 4.37 |
| chez | 0.038 | 0.035 | 0.035 | 51.9 | 9.50 |
| racket | 0.063 | 0.061 | 0.060 | 81.4 | 15.57 |
| swish | 0.064 | 0.063 | 0.062 | 73.2 | 15.74 |
| erlang | 0.092 | 0.090 | 0.085 | 48.3 | 22.68 |
| node | 0.145 | 0.052 | 0.071 | 40.9 | 35.95 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.112 | 0.110 | 0.111 | 1.2 | 0.55 |
| go | 0.129 | 0.123 | 0.125 | 3.5 | 0.63 |
| slight | 0.203 | 0.200 | 0.201 | 1.7 | 1.00 |
| chez | 0.226 | 0.224 | 0.225 | 51.8 | 1.11 |
| node | 0.322 | 0.320 | 0.321 | 43.3 | 1.59 |
| erlang | 0.686 | 0.660 | 0.402 | 48.2 | 3.38 |
| python | 4.677 | 4.643 | 4.672 | 11.8 | 23.07 |
| perl | 12.8 | 12.8 | 12.8 | 4.4 | 63.35 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.083 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.108 | 0.102 | 0.104 | 3.5 | 0.75 |
| slight | 0.143 | 0.141 | 0.141 | 1.7 | 1.00 |
| chez | 0.190 | 0.187 | 0.189 | 51.8 | 1.33 |
| node | 0.293 | 0.289 | 0.292 | 43.3 | 2.05 |
| erlang | 0.508 | 0.502 | 0.296 | 48.4 | 3.56 |
| python | 3.642 | 3.630 | 3.639 | 11.8 | 25.49 |
| perl | 14.0 | 14.0 | 14.0 | 4.3 | 97.75 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.344 | 0.342 | 0.342 | 65.8 | 0.46 |
| node | 0.719 | 0.558 | 0.967 | 297.0 | 0.96 |
| slight | 0.746 | 0.741 | 0.744 | 103.9 | 1.00 |
| erlang | 0.872 | 0.849 | 0.552 | 172.4 | 1.17 |
| c | 0.997 | 0.991 | 0.996 | 20.0 | 1.34 |
| go | 1.392 | 1.385 | 1.385 | 59.6 | 1.86 |
| python | 5.917 | 5.905 | 5.915 | 45.5 | 7.93 |
| perl | 17.9 | 17.8 | 17.9 | 142.3 | 23.96 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.310 | 0.309 | 0.309 | 1.2 | 0.43 |
| chez | 0.391 | 0.389 | 0.390 | 51.8 | 0.55 |
| go | 0.582 | 0.576 | 0.572 | 8.8 | 0.82 |
| erlang | 0.690 | 0.647 | 0.520 | 48.3 | 0.97 |
| slight | 0.713 | 0.711 | 0.711 | 4.1 | 1.00 |
| node | 2.310 | 2.294 | 2.321 | 62.6 | 3.24 |
| python | 6.970 | 6.966 | 6.968 | 11.8 | 9.78 |
| perl | 13.9 | 13.8 | 13.9 | 5.3 | 19.46 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.3 | 0.35 |
| chez | 0.185 | 0.185 | 0.184 | 51.8 | 0.48 |
| slight | 0.385 | 0.382 | 0.383 | 4.0 | 1.00 |
| erlang | 0.507 | 0.497 | 0.334 | 48.5 | 1.32 |
| node | 0.625 | 0.621 | 0.675 | 55.9 | 1.62 |
| go | 1.083 | 1.067 | 1.063 | 8.7 | 2.81 |
| python | 4.091 | 4.060 | 4.090 | 11.9 | 10.62 |
| perl | 8.122 | 8.087 | 8.120 | 4.6 | 21.09 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.039 | 0.038 | 0.038 | 1.2 | 0.05 |
| go | 0.053 | 0.048 | 0.049 | 3.5 | 0.06 |
| node | 0.119 | 0.119 | 0.122 | 47.0 | 0.14 |
| slight | 0.849 | 0.847 | 0.847 | 4.1 | 1.00 |
| chez | 1.176 | 1.164 | 1.174 | 51.8 | 1.38 |
| erlang | 1.316 | 1.308 | 1.223 | 48.3 | 1.55 |
| python | 5.769 | 5.738 | 5.767 | 12.3 | 6.79 |
| perl | 7.323 | 7.225 | 7.321 | 6.2 | 8.62 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.047 | 0.047 | 0.046 | 1.2 | 0.10 |
| go | 0.052 | 0.048 | 0.048 | 3.6 | 0.11 |
| slight-arrays | 0.087 | 0.086 | 0.085 | 42.6 | 0.18 |
| node | 0.119 | 0.117 | 0.121 | 46.4 | 0.25 |
| slight | 0.480 | 0.472 | 0.478 | 4.3 | 1.00 |
| chez | 0.580 | 0.577 | 0.579 | 51.8 | 1.21 |
| erlang | 0.656 | 0.652 | 0.502 | 48.7 | 1.37 |
| python | 5.153 | 5.149 | 5.151 | 12.5 | 10.74 |
| perl | 7.612 | 7.578 | 7.610 | 7.0 | 15.86 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.247 | 0.245 | 0.245 | 1.9 | 1.00 |
| gambit | 0.287 | 0.275 | 0.277 | 9.7 | 1.17 |
| rust | 0.296 | 0.292 | 0.294 | 1.7 | 1.20 |
| go | 0.392 | 0.387 | 0.388 | 3.5 | 1.59 |
| racket | 2.096 | 2.076 | 2.082 | 126.6 | 8.50 |
| erlang | 3.300 | 3.280 | 1.215 | 48.3 | 13.38 |
| swish | 48.9 | 48.9 | 4.991 | 73.2 | 198.21 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust (not copied) | 0.160 | 0.158 | 0.158 | 1.7 | 0.24 |
| gambit (not copied) | 0.163 | 0.148 | 0.151 | 9.7 | 0.24 |
| go (not copied) | 0.201 | 0.197 | 0.197 | 3.5 | 0.30 |
| slight | 0.672 | 0.668 | 0.670 | 1.9 | 1.00 |
| racket (not copied) | 1.112 | 1.100 | 1.102 | 126.8 | 1.66 |
| erlang | 2.644 | 2.570 | 1.589 | 48.4 | 3.94 |
| swish (not copied) | 24.5 | 24.4 | 2.531 | 73.2 | 36.41 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.411 | 0.409 | 0.409 | 10.6 | 1.00 |
| rust | 0.700 | 0.695 | 0.697 | 2.5 | 1.70 |
| go | 1.038 | 1.026 | 1.035 | 5.1 | 2.53 |
| gambit | 1.406 | 1.391 | 1.394 | 10.1 | 3.42 |
| racket | 5.295 | 5.266 | 5.270 | 131.1 | 12.88 |
| erlang | 8.028 | 7.932 | 2.892 | 49.4 | 19.52 |
| swish | 122.3 | 122.2 | 12.5 | 73.2 | 297.28 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.211 | 0.209 | 0.205 | 479.4 | 1.00 |
| rust | 0.274 | 0.268 | 0.271 | 93.2 | 1.30 |
| go | 0.284 | 0.280 | 0.281 | 3.8 | 1.35 |
| racket | 0.822 | 0.811 | 0.806 | 462.2 | 3.90 |
| swish | 1.001 | 0.999 | 0.989 | 587.0 | 4.74 |
| gambit | 1.227 | 1.219 | 1.218 | 10.0 | 5.81 |
| erlang | 1.820 | 1.654 | 1.777 | 650.1 | 8.63 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.081 | 0.080 | 0.079 | 50.6 | 1.00 |
| go | 0.430 | 0.419 | 0.423 | 350.8 | 5.33 |
| gambit | 0.464 | 0.411 | 0.453 | 468.1 | 5.75 |
| swish | 0.796 | 0.782 | 0.789 | 142.0 | 9.87 |
| erlang | 1.211 | 1.206 | 0.683 | 114.1 | 15.01 |
| racket | 2.158 | 2.140 | 2.141 | 120.4 | 26.74 |
| rust | 2.591 | 0.509 | 2.590 | 181.7 | 32.10 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.327 | 0.318 | 0.322 | 759.8 | 1.00 | 758 |
| rust | 0.780 | 0.764 | 0.778 | 1100.0 | 2.39 | 1098 |
| gambit | 1.136 | 1.123 | 1.127 | 2070.2 | 3.48 | 2064 |
| go | 1.525 | 1.498 | 1.525 | 2890.1 | 4.67 | 2887 |
| erlang | 2.983 | 2.907 | 1.917 | 2789.9 | 9.13 | 2742 |
| swish | 4.348 | 4.249 | 4.336 | 5023.6 | 13.30 | 4950 |
| racket | 14.3 | 14.3 | 14.3 | 3637.4 | 43.80 | 3556 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.152 | 0.150 | 0.151 | 1.6 | 0.53 |
| go | 0.171 | 0.167 | 0.167 | 3.7 | 0.60 |
| gambit | 0.247 | 0.232 | 0.235 | 6.8 | 0.87 |
| slight | 0.284 | 0.282 | 0.282 | 2.9 | 1.00 |
| swish | 0.327 | 0.326 | 0.325 | 73.2 | 1.15 |
| racket | 0.341 | 0.333 | 0.327 | 81.5 | 1.20 |
| erlang | 0.874 | 0.864 | 0.499 | 48.6 | 3.07 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 3 µs | 4 µs | 5 µs | 11 µs | 103 µs | 210 µs | 286 µs | 494.4 |
| go | 0 µs | 2 µs | 12.0 ms | 24.9 ms | 25.0 ms | 3 µs | 20 µs | 20 µs | 166.8 |
| erlang | 5 µs | 13 µs | 28 µs | 41 µs | 68 µs | 8 µs | 71 µs | 109 µs | 1747.9 |
| rust | 0 µs | 4 µs | 143.2 ms | 283.2 ms | 285.2 ms | 2 µs | 7 µs | 7 µs | 83.8 |
| gambit | 0 µs | 28 µs | 9.5 ms | 19.5 ms | 20.4 ms | 60.2 ms | 133.8 ms | 136.2 ms | 1059.6 |
| racket | 1 µs | 4.0 ms | 799 µs | 996 µs | 1.1 ms | 28.8 ms | 119.6 ms | 138.0 ms | 601.2 |
| swish | 25 µs | 40 µs | 132.9 ms | 258.5 ms | 260.7 ms | 902.6 ms | 1.76 s | 1.77 s | 851.2 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 7 µs | 1 µs | 9 µs | 14 µs | 1 µs | 22 µs | 22 µs | 168.1 |
| erlang-mc | 0 µs | 7 µs | 0 µs | 5 µs | 13 µs | 0 µs | 6 µs | 56 µs | 1708.9 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.025 | 0.025 | 0.025 | 2.3 | 0.05 |
| slight | 0.494 | 0.492 | 0.492 | 3.4 | 1.00 |
| python | 6.393 | 6.374 | 6.375 | 20.1 | 12.95 |

## Every core

slight on every island, one per core (D256), Go, Erlang and Rust with their defaults, every core, and Racket on places, one per core, each an instance of Racket of its own. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.33 | 0.51 |
|  | erlang-mc | 0.090 | 0.230 | 60.1 | 0.98 | 22.32 |
|  | rust-mc | 0.002 | 0.003 | 2.2 | 0.68 | 0.52 |
| fib | go-mc | 0.123 | 0.123 | 4.0 | 0.96 | 0.61 |
|  | erlang-mc | 0.369 | 0.510 | 59.6 | 0.54 | 1.82 |
| tak | go-mc | 0.102 | 0.102 | 4.0 | 0.95 | 0.72 |
|  | erlang-mc | 0.287 | 0.433 | 60.0 | 0.57 | 2.01 |
| binary-trees | go-mc | 0.669 | 1.872 | 41.9 | 0.48 | 0.90 |
|  | erlang-mc | 0.504 | 0.652 | 173.0 | 0.58 | 0.68 |
| nqueens | go-mc | 0.547 | 0.593 | 10.9 | 0.94 | 0.77 |
|  | erlang-mc | 0.477 | 0.616 | 59.7 | 0.69 | 0.67 |
| deriv | go-mc | 0.808 | 0.928 | 11.1 | 0.75 | 2.10 |
|  | erlang-mc | 0.317 | 0.455 | 60.0 | 0.62 | 0.82 |
| n-body | go-mc | 0.048 | 0.048 | 4.0 | 0.91 | 0.06 |
|  | erlang-mc | 1.202 | 1.343 | 59.8 | 0.91 | 1.42 |
| spectral-norm | slight-mc | 0.020 | 0.138 | 74.3 | 0.23 | 0.04 |
|  | go-mc | 0.047 | 0.047 | 4.0 | 0.91 | 0.10 |
|  | erlang-mc | 0.489 | 0.633 | 60.4 | 0.75 | 1.02 |
| ping-pong | slight-mc | 0.035 | 0.299 | 6.6 | 0.14 | 0.14 |
|  | go-mc | 0.537 | 0.793 | 4.1 | 1.37 | 2.18 |
|  | erlang-mc | 1.060 | 1.200 | 60.1 | 0.32 | 4.30 |
|  | rust-mc | 11.6 | 14.7 | 2.5 | 39.31 | 47.22 |
|  | racket-mc | 6.407 | 53.1 | 713.9 | 3.06 | 25.98 |
| payload | go-mc (not copied) | 0.274 | 0.402 | 4.1 | 1.36 | 0.41 |
|  | erlang-mc | 1.439 | 1.613 | 61.0 | 0.54 | 2.14 |
|  | rust-mc (not copied) | 5.963 | 7.426 | 2.4 | 37.37 | 8.88 |
| thread-ring | go-mc | 1.478 | 2.158 | 5.8 | 1.42 | 3.59 |
|  | erlang-mc | 2.316 | 2.463 | 60.8 | 0.29 | 5.63 |
|  | rust-mc | 1.190 | 1.951 | 3.3 | 1.70 | 2.89 |
| fan-in | slight-mc | 0.651 | 6.623 | 129.1 | 3.08 | 3.08 |
|  | go-mc | 3.539 | 12.0 | 5.2 | 12.44 | 16.77 |
|  | erlang-mc | 1.071 | 8.302 | 664.8 | 0.59 | 5.08 |
|  | rust-mc | 4.055 | 46.9 | 34.8 | 14.82 | 19.22 |
|  | racket-mc | 11.0 | 45.8 | 884.3 | 13.41 | 52.23 |
| spawn | slight-mc | 0.017 | 0.104 | 60.7 | 0.21 | 0.21 |
|  | go-mc | 0.386 | 1.849 | 27.7 | 0.90 | 4.78 |
|  | erlang-mc | 0.914 | 5.655 | 190.5 | 0.75 | 11.33 |
|  | rust-mc | 0.612 | 1.926 | 12.0 | 0.24 | 7.59 |
|  | racket-mc | 5.107 | 35.4 | 772.9 | 2.37 | 63.29 |
| idle-memory | slight-mc | 0.139 | 0.855 | 990.3 | 0.42 | 0.42 |
|  | go-mc | 1.015 | 6.324 | 2913.2 | 0.67 | 3.10 |
|  | erlang-mc | 1.920 | 6.400 | 2795.0 | 0.64 | 5.88 |
|  | rust-mc | 1.402 | 7.553 | 1097.0 | 1.80 | 4.29 |
|  | racket-mc | 27.1 | 233.0 | 7038.3 | 1.89 | 82.83 |
| parallel-fib | slight-mc | 0.037 | 0.346 | 5.1 | 0.13 | 0.13 |
|  | go-mc | 0.021 | 0.200 | 5.0 | 0.12 | 0.07 |
|  | erlang-mc | 0.139 | 0.713 | 59.7 | 0.16 | 0.49 |
|  | rust-mc | 0.018 | 0.174 | 2.5 | 0.12 | 0.06 |
|  | racket-mc | 0.968 | 1.420 | 631.8 | 2.84 | 3.40 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.06 | 33592 |
| c/binary-trees | 0.05 | 33632 |
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
| erlang/n-body | 0.17 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.16 | 764 |
| erlang/tak | 0.16 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| gambit/fan-in | 0.58 | 6679080 |
| gambit/idle-memory | 0.54 | 6679144 |
| gambit/latency | 0.80 | 6714120 |
| gambit/parallel-fib | 0.55 | 6679136 |
| gambit/payload | 0.54 | 6679160 |
| gambit/ping-pong | 0.53 | 6679080 |
| gambit/spawn | 0.55 | 6678824 |
| gambit/startup | 0.61 | 6678712 |
| gambit/thread-ring | 0.53 | 6679192 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.10 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.09 | 2430178 |
| go/latency | 0.09 | 2482354 |
| go/n-body | 0.09 | 2430274 |
| go/nqueens | 0.10 | 2430130 |
| go/parallel-fib | 0.09 | 2430194 |
| go/payload | 0.10 | 2430226 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.29 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| racket-mc/fan-in | 10.31 | 10404627 |
| racket-mc/idle-memory | 9.51 | 10421144 |
| racket-mc/parallel-fib | 9.50 | 10404633 |
| racket-mc/ping-pong | 9.62 | 10421142 |
| racket-mc/spawn | 9.60 | 10404626 |
| racket/fan-in | 0.35 | 2115603 |
| racket/idle-memory | 9.23 | 10256024 |
| racket/latency | 9.41 | 10256020 |
| racket/parallel-fib | 0.34 | 2115609 |
| racket/payload | 9.45 | 10256020 |
| racket/ping-pong | 9.60 | 10256022 |
| racket/spawn | 0.34 | 2115602 |
| racket/startup | 0.36 | 2115604 |
| racket/thread-ring | 9.81 | 10256024 |
| rust/fan-in | 0.31 | 788208 |
| rust/idle-memory | 0.33 | 817760 |
| rust/latency | 0.44 | 887648 |
| rust/parallel-fib | 0.30 | 787920 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.31 | 785104 |
| rust/startup | 1.94 | 708464 |
| rust/thread-ring | 0.34 | 810560 |
| slight-arrays/spectral-norm | 0.28 | 739648 |
| slight-mc/fan-in | 0.28 | 739736 |
| slight-mc/idle-memory | 0.27 | 756520 |
| slight-mc/parallel-fib | 0.27 | 739680 |
| slight-mc/ping-pong | 0.28 | 739800 |
| slight-mc/spawn | 0.27 | 739896 |
| slight-mc/spectral-norm | 0.28 | 756576 |
| slight/bank | 0.28 | 756600 |
| slight/binary-trees | 0.28 | 739808 |
| slight/deriv | 0.28 | 739656 |
| slight/fan-in | 0.28 | 739736 |
| slight/fib | 0.27 | 739408 |
| slight/idle-memory | 0.27 | 739832 |
| slight/latency | 0.28 | 757112 |
| slight/n-body | 0.31 | 790072 |
| slight/nqueens | 0.28 | 739528 |
| slight/parallel-fib | 0.28 | 739488 |
| slight/payload | 0.27 | 739720 |
| slight/ping-pong | 0.27 | 739736 |
| slight/spawn | 0.27 | 739816 |
| slight/spectral-norm | 0.29 | 756432 |
| slight/startup | 3.14 | 739336 |
| slight/tak | 0.28 | 739408 |
| slight/thread-ring | 0.28 | 739736 |
| swish/fan-in | 0.10 | 3999 |
| swish/idle-memory | 0.09 | 4103 |
| swish/latency | 0.10 | 9513 |
| swish/parallel-fib | 0.09 | 4010 |
| swish/payload | 0.09 | 4518 |
| swish/ping-pong | 0.09 | 4040 |
| swish/spawn | 0.09 | 3752 |
| swish/startup | 0.09 | 721 |
| swish/thread-ring | 0.10 | 4491 |
