# Benchmarks, 2026-11-13 11:02:03

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 3dedbf4, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| c | 0.002 | 0.001 | 0.001 | 1.2 | 0.48 |
| rust | 0.003 | 0.002 | 0.002 | 1.6 | 0.85 |
| slight | 0.004 | 0.002 | 0.002 | 1.6 | 1.00 |
| go | 0.006 | 0.002 | 0.003 | 3.5 | 1.66 |
| perl | 0.010 | 0.003 | 0.004 | 3.4 | 2.58 |
| python | 0.016 | 0.016 | 0.016 | 12.0 | 4.44 |
| gambit | 0.017 | 0.005 | 0.007 | 6.6 | 4.66 |
| chez | 0.037 | 0.034 | 0.034 | 51.9 | 9.98 |
| racket | 0.062 | 0.060 | 0.060 | 81.5 | 16.74 |
| swish | 0.063 | 0.061 | 0.062 | 73.2 | 17.01 |
| erlang | 0.089 | 0.088 | 0.083 | 48.5 | 24.05 |
| node | 0.147 | 0.051 | 0.071 | 40.8 | 39.49 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.111 | 0.110 | 0.110 | 1.2 | 0.55 |
| go | 0.128 | 0.123 | 0.124 | 3.5 | 0.63 |
| slight | 0.202 | 0.199 | 0.200 | 1.5 | 1.00 |
| chez | 0.224 | 0.221 | 0.223 | 51.8 | 1.11 |
| node | 0.320 | 0.316 | 0.318 | 43.3 | 1.58 |
| erlang | 0.665 | 0.658 | 0.384 | 48.5 | 3.30 |
| python | 4.645 | 4.626 | 4.643 | 11.8 | 23.04 |
| perl | 12.8 | 12.8 | 12.8 | 4.2 | 63.67 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.083 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.106 | 0.102 | 0.103 | 3.5 | 0.74 |
| slight | 0.143 | 0.142 | 0.142 | 1.6 | 1.00 |
| chez | 0.189 | 0.186 | 0.189 | 51.8 | 1.32 |
| node | 0.289 | 0.288 | 0.288 | 43.5 | 2.01 |
| erlang | 0.543 | 0.510 | 0.329 | 48.5 | 3.79 |
| python | 3.604 | 3.597 | 3.603 | 11.8 | 25.14 |
| perl | 13.9 | 13.9 | 13.9 | 4.3 | 97.28 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.342 | 0.341 | 0.341 | 65.8 | 0.42 |
| node | 0.710 | 0.539 | 0.943 | 301.8 | 0.86 |
| slight | 0.824 | 0.817 | 0.822 | 106.0 | 1.00 |
| erlang | 0.861 | 0.839 | 0.541 | 172.1 | 1.05 |
| c | 0.993 | 0.988 | 0.991 | 20.0 | 1.21 |
| go | 1.370 | 1.358 | 1.365 | 73.4 | 1.66 |
| python | 5.889 | 5.848 | 5.887 | 45.5 | 7.15 |
| perl | 17.8 | 17.7 | 17.8 | 142.1 | 21.66 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.308 | 0.309 | 1.2 | 0.44 |
| chez | 0.389 | 0.379 | 0.388 | 51.8 | 0.55 |
| go | 0.579 | 0.572 | 0.570 | 8.9 | 0.82 |
| erlang | 0.682 | 0.652 | 0.511 | 48.7 | 0.96 |
| slight | 0.709 | 0.705 | 0.707 | 3.8 | 1.00 |
| node | 2.285 | 2.274 | 2.294 | 62.4 | 3.22 |
| python | 6.942 | 6.936 | 6.940 | 11.8 | 9.80 |
| perl | 13.8 | 13.8 | 13.8 | 5.0 | 19.52 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.2 | 0.35 |
| chez | 0.184 | 0.184 | 0.183 | 51.8 | 0.48 |
| slight | 0.380 | 0.379 | 0.379 | 3.9 | 1.00 |
| erlang | 0.497 | 0.494 | 0.331 | 48.5 | 1.31 |
| node | 0.625 | 0.616 | 0.674 | 56.1 | 1.64 |
| go | 1.062 | 1.053 | 1.046 | 8.8 | 2.79 |
| python | 4.088 | 4.061 | 4.087 | 11.9 | 10.75 |
| perl | 8.114 | 8.094 | 8.110 | 4.5 | 21.34 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.037 | 1.2 | 0.05 |
| go | 0.052 | 0.048 | 0.049 | 3.5 | 0.06 |
| node | 0.119 | 0.118 | 0.121 | 47.1 | 0.14 |
| slight | 0.849 | 0.846 | 0.847 | 4.0 | 1.00 |
| chez | 1.158 | 1.157 | 1.157 | 51.8 | 1.36 |
| erlang | 1.305 | 1.297 | 1.211 | 48.2 | 1.54 |
| python | 5.757 | 5.717 | 5.756 | 12.3 | 6.78 |
| perl | 7.423 | 7.136 | 7.422 | 6.0 | 8.74 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.047 | 0.046 | 0.047 | 1.2 | 0.10 |
| go | 0.051 | 0.047 | 0.048 | 3.6 | 0.11 |
| slight-arrays | 0.085 | 0.084 | 0.084 | 43.4 | 0.18 |
| node | 0.119 | 0.117 | 0.121 | 46.1 | 0.25 |
| slight | 0.472 | 0.471 | 0.472 | 3.9 | 1.00 |
| chez | 0.574 | 0.572 | 0.572 | 51.8 | 1.22 |
| erlang | 0.681 | 0.653 | 0.525 | 48.6 | 1.44 |
| python | 5.138 | 5.132 | 5.137 | 12.3 | 10.88 |
| perl | 7.671 | 7.545 | 7.670 | 6.2 | 16.24 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| gambit | 0.288 | 0.276 | 0.276 | 9.7 | 0.95 |
| rust | 0.293 | 0.291 | 0.292 | 1.6 | 0.97 |
| slight | 0.302 | 0.298 | 0.300 | 2.9 | 1.00 |
| go | 0.390 | 0.385 | 0.387 | 3.5 | 1.29 |
| racket | 2.087 | 2.055 | 2.068 | 126.6 | 6.90 |
| erlang | 3.410 | 3.316 | 1.272 | 48.5 | 11.27 |
| swish | 49.0 | 48.8 | 5.369 | 73.2 | 162.05 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust (not copied) | 0.160 | 0.157 | 0.158 | 1.7 | 0.22 |
| gambit (not copied) | 0.161 | 0.147 | 0.150 | 9.7 | 0.22 |
| go (not copied) | 0.202 | 0.197 | 0.198 | 3.5 | 0.28 |
| slight | 0.722 | 0.719 | 0.720 | 2.5 | 1.00 |
| racket (not copied) | 1.114 | 1.093 | 1.104 | 126.8 | 1.54 |
| erlang | 2.657 | 2.590 | 1.600 | 48.4 | 3.68 |
| swish (not copied) | 24.6 | 24.4 | 2.640 | 73.2 | 34.01 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.495 | 0.493 | 0.493 | 10.5 | 1.00 |
| rust | 0.697 | 0.695 | 0.694 | 2.4 | 1.41 |
| go | 1.044 | 1.005 | 1.044 | 5.1 | 2.11 |
| gambit | 1.398 | 1.391 | 1.390 | 10.1 | 2.82 |
| racket | 5.295 | 5.282 | 5.269 | 131.1 | 10.69 |
| erlang | 8.291 | 7.825 | 3.090 | 49.2 | 16.74 |
| swish | 122.4 | 122.1 | 13.4 | 73.2 | 247.15 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.268 | 0.264 | 0.268 | 93.1 | 0.73 |
| go | 0.287 | 0.282 | 0.286 | 3.8 | 0.78 |
| slight | 0.367 | 0.357 | 0.358 | 479.8 | 1.00 |
| racket | 0.806 | 0.799 | 0.791 | 462.2 | 2.20 |
| swish | 0.975 | 0.971 | 0.964 | 588.7 | 2.66 |
| gambit | 1.217 | 1.210 | 1.207 | 10.0 | 3.31 |
| erlang | 1.705 | 1.637 | 1.661 | 650.0 | 4.64 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.144 | 0.144 | 0.143 | 126.6 | 1.00 |
| gambit | 0.429 | 0.414 | 0.421 | 479.0 | 2.97 |
| go | 0.431 | 0.426 | 0.424 | 394.3 | 2.99 |
| swish | 0.782 | 0.778 | 0.775 | 133.3 | 5.42 |
| erlang | 1.251 | 1.202 | 0.718 | 114.1 | 8.67 |
| rust | 2.121 | 0.704 | 2.120 | 181.2 | 14.70 |
| racket | 2.135 | 2.121 | 2.114 | 120.5 | 14.80 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.392 | 0.384 | 0.386 | 749.2 | 1.00 | 748 |
| rust | 0.745 | 0.735 | 0.744 | 1106.2 | 1.90 | 1105 |
| gambit | 1.109 | 1.093 | 1.099 | 2055.9 | 2.83 | 2049 |
| go | 1.470 | 1.396 | 1.464 | 2899.2 | 3.75 | 2896 |
| erlang | 2.933 | 2.885 | 1.862 | 2789.8 | 7.48 | 2741 |
| swish | 4.262 | 4.210 | 4.245 | 5023.4 | 10.87 | 4950 |
| racket | 14.0 | 13.9 | 13.9 | 3635.2 | 35.59 | 3554 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.151 | 0.150 | 0.150 | 1.6 | 0.54 |
| go | 0.171 | 0.167 | 0.167 | 3.7 | 0.61 |
| gambit | 0.247 | 0.232 | 0.235 | 6.7 | 0.88 |
| slight | 0.283 | 0.281 | 0.280 | 2.7 | 1.00 |
| swish | 0.323 | 0.321 | 0.321 | 73.2 | 1.14 |
| racket | 0.338 | 0.333 | 0.322 | 81.5 | 1.20 |
| erlang | 0.931 | 0.879 | 0.553 | 48.6 | 3.30 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 2 µs | 4 µs | 4 µs | 8 µs | 117 µs | 43.6 ms | 45.3 ms | 532.0 |
| go | 0 µs | 0 µs | 14.0 ms | 29.1 ms | 30.0 ms | 4 µs | 11 µs | 11 µs | 166.3 |
| erlang | 5 µs | 13 µs | 29 µs | 42 µs | 48 µs | 8 µs | 74 µs | 141 µs | 1747.9 |
| rust | 0 µs | 0 µs | 142.1 ms | 281.0 ms | 283.0 ms | 2 µs | 3 µs | 3 µs | 83.8 |
| gambit | 0 µs | 30 µs | 9.8 ms | 19.9 ms | 20.9 ms | 58.4 ms | 130.7 ms | 133.4 ms | 1059.7 |
| racket | 1 µs | 4.0 ms | 797 µs | 956 µs | 967 µs | 30.8 ms | 113.2 ms | 123.6 ms | 604.6 |
| swish | 25 µs | 31 µs | 131.7 ms | 255.5 ms | 258.5 ms | 889.1 ms | 1.72 s | 1.73 s | 852.8 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 6 µs | 1 µs | 11 µs | 16 µs | 1 µs | 19 µs | 19 µs | 168.2 |
| erlang-mc | 0 µs | 4 µs | 0 µs | 1 µs | 10 µs | 0 µs | 5 µs | 43 µs | 1709.5 |

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.025 | 0.025 | 0.024 | 2.3 | 0.04 |
| slight | 0.626 | 0.623 | 0.624 | 76.3 | 1.00 |
| python | 6.351 | 6.327 | 6.336 | 20.0 | 10.15 |

## Every core

slight on every island, one per core (D256), Go, Erlang and Rust with their defaults, every core, and Racket on places, one per core, each an instance of Racket of its own. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| startup | go-mc | 0.002 | 0.002 | 4.0 | 0.34 | 0.56 |
|  | erlang-mc | 0.086 | 0.235 | 60.3 | 0.97 | 23.26 |
|  | rust-mc | 0.002 | 0.002 | 2.2 | 0.65 | 0.55 |
| fib | go-mc | 0.123 | 0.123 | 3.9 | 0.96 | 0.61 |
|  | erlang-mc | 0.365 | 0.518 | 59.7 | 0.55 | 1.81 |
| tak | go-mc | 0.102 | 0.102 | 3.9 | 0.96 | 0.71 |
|  | erlang-mc | 0.282 | 0.419 | 60.0 | 0.52 | 1.97 |
| binary-trees | go-mc | 0.659 | 1.839 | 39.9 | 0.48 | 0.80 |
|  | erlang-mc | 0.497 | 0.651 | 171.8 | 0.58 | 0.60 |
| nqueens | go-mc | 0.547 | 0.593 | 10.7 | 0.95 | 0.77 |
|  | erlang-mc | 0.469 | 0.616 | 60.1 | 0.69 | 0.66 |
| deriv | go-mc | 0.801 | 0.916 | 11.2 | 0.75 | 2.11 |
|  | erlang-mc | 0.319 | 0.471 | 60.4 | 0.64 | 0.84 |
| n-body | go-mc | 0.048 | 0.048 | 3.9 | 0.92 | 0.06 |
|  | erlang-mc | 1.194 | 1.340 | 60.0 | 0.91 | 1.41 |
| spectral-norm | slight-mc | 0.020 | 0.139 | 66.6 | 0.23 | 0.04 |
|  | go-mc | 0.047 | 0.047 | 4.0 | 0.92 | 0.10 |
|  | erlang-mc | 0.487 | 0.631 | 60.7 | 0.71 | 1.03 |
| ping-pong | slight-mc | 0.064 | 0.640 | 17.6 | 0.21 | 0.21 |
|  | go-mc | 0.531 | 0.768 | 4.1 | 1.36 | 1.76 |
|  | erlang-mc | 0.991 | 1.146 | 60.1 | 0.29 | 3.28 |
|  | rust-mc | 11.6 | 14.6 | 2.4 | 39.73 | 38.47 |
|  | racket-mc | 6.289 | 52.6 | 712.8 | 3.01 | 20.79 |
| payload | go-mc (not copied) | 0.270 | 0.388 | 4.1 | 1.33 | 0.37 |
|  | erlang-mc | 1.431 | 1.655 | 61.4 | 0.54 | 1.98 |
|  | rust-mc (not copied) | 5.963 | 7.470 | 2.4 | 37.23 | 8.26 |
| thread-ring | go-mc | 1.446 | 2.069 | 5.8 | 1.39 | 2.92 |
|  | erlang-mc | 2.301 | 2.459 | 61.1 | 0.28 | 4.65 |
|  | rust-mc | 1.189 | 1.947 | 3.2 | 1.71 | 2.40 |
| fan-in | slight-mc | 0.711 | 7.228 | 141.4 | 1.94 | 1.94 |
|  | go-mc | 3.511 | 11.8 | 5.2 | 12.25 | 9.57 |
|  | erlang-mc | 1.080 | 8.419 | 659.6 | 0.63 | 2.94 |
|  | rust-mc | 4.131 | 47.5 | 34.7 | 15.41 | 11.25 |
|  | racket-mc | 10.4 | 43.9 | 900.9 | 12.84 | 28.20 |
| spawn | slight-mc | 0.029 | 0.225 | 126.3 | 0.20 | 0.20 |
|  | go-mc | 0.388 | 1.835 | 27.8 | 0.90 | 2.69 |
|  | erlang-mc | 0.922 | 5.774 | 197.4 | 0.74 | 6.39 |
|  | rust-mc | 0.606 | 1.915 | 12.0 | 0.29 | 4.20 |
|  | racket-mc | 5.016 | 35.0 | 773.5 | 2.35 | 34.77 |
| idle-memory | slight-mc | 0.191 | 1.398 | 935.0 | 0.49 | 0.49 |
|  | go-mc | 1.058 | 6.319 | 2913.1 | 0.72 | 2.70 |
|  | erlang-mc | 1.902 | 6.335 | 2799.1 | 0.65 | 4.85 |
|  | rust-mc | 1.377 | 7.584 | 1097.3 | 1.85 | 3.51 |
|  | racket-mc | 27.2 | 237.3 | 6986.7 | 1.95 | 69.41 |
| parallel-fib | slight-mc | 0.037 | 0.346 | 4.7 | 0.13 | 0.13 |
|  | go-mc | 0.021 | 0.197 | 5.1 | 0.13 | 0.08 |
|  | erlang-mc | 0.136 | 0.724 | 60.1 | 0.15 | 0.48 |
|  | rust-mc | 0.018 | 0.171 | 2.5 | 0.12 | 0.07 |
|  | racket-mc | 0.938 | 1.387 | 633.6 | 2.78 | 3.32 |

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
| erlang/binary-trees | 0.16 | 1516 |
| erlang/deriv | 0.16 | 1612 |
| erlang/fan-in | 0.16 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/latency | 0.17 | 3776 |
| erlang/n-body | 0.17 | 4064 |
| erlang/nqueens | 0.15 | 1200 |
| erlang/parallel-fib | 0.16 | 1412 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.16 | 764 |
| erlang/tak | 0.16 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| gambit/fan-in | 0.57 | 6679080 |
| gambit/idle-memory | 0.55 | 6679144 |
| gambit/latency | 0.81 | 6714120 |
| gambit/parallel-fib | 0.64 | 6679136 |
| gambit/payload | 0.54 | 6679160 |
| gambit/ping-pong | 0.53 | 6679080 |
| gambit/spawn | 0.53 | 6678824 |
| gambit/startup | 0.58 | 6678712 |
| gambit/thread-ring | 0.53 | 6679192 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.10 | 2430482 |
| go/fan-in | 0.09 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/latency | 0.10 | 2482354 |
| go/n-body | 0.10 | 2430274 |
| go/nqueens | 0.10 | 2430130 |
| go/parallel-fib | 0.09 | 2430194 |
| go/payload | 0.09 | 2430226 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.09 | 2430338 |
| go/startup | 0.20 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| racket-mc/fan-in | 9.51 | 10404627 |
| racket-mc/idle-memory | 9.53 | 10421144 |
| racket-mc/parallel-fib | 9.50 | 10404633 |
| racket-mc/ping-pong | 9.51 | 10421142 |
| racket-mc/spawn | 9.53 | 10404626 |
| racket/fan-in | 0.35 | 2115603 |
| racket/idle-memory | 9.44 | 10256024 |
| racket/latency | 9.37 | 10256020 |
| racket/parallel-fib | 0.35 | 2115609 |
| racket/payload | 9.46 | 10256020 |
| racket/ping-pong | 9.51 | 10256022 |
| racket/spawn | 0.35 | 2115602 |
| racket/startup | 0.35 | 2115604 |
| racket/thread-ring | 9.36 | 10256024 |
| rust/fan-in | 0.30 | 788208 |
| rust/idle-memory | 0.33 | 817760 |
| rust/latency | 0.45 | 887648 |
| rust/parallel-fib | 0.31 | 787920 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/startup | 1.70 | 708464 |
| rust/thread-ring | 0.34 | 810560 |
| slight-arrays/spectral-norm | 0.28 | 736304 |
| slight-mc/fan-in | 0.27 | 736392 |
| slight-mc/idle-memory | 0.28 | 736664 |
| slight-mc/parallel-fib | 0.28 | 736336 |
| slight-mc/ping-pong | 0.28 | 736472 |
| slight-mc/spawn | 0.27 | 736552 |
| slight-mc/spectral-norm | 0.28 | 736704 |
| slight/bank | 0.28 | 736744 |
| slight/binary-trees | 0.28 | 736480 |
| slight/deriv | 0.28 | 736312 |
| slight/fan-in | 0.28 | 719880 |
| slight/fib | 0.27 | 719568 |
| slight/idle-memory | 0.27 | 736488 |
| slight/latency | 0.28 | 753768 |
| slight/n-body | 0.31 | 770232 |
| slight/nqueens | 0.27 | 719672 |
| slight/parallel-fib | 0.28 | 719648 |
| slight/payload | 0.27 | 719880 |
| slight/ping-pong | 0.28 | 719880 |
| slight/spawn | 0.27 | 719960 |
| slight/spectral-norm | 0.28 | 736560 |
| slight/startup | 3.03 | 719496 |
| slight/tak | 0.27 | 719568 |
| slight/thread-ring | 0.28 | 736408 |
| swish/fan-in | 0.09 | 3999 |
| swish/idle-memory | 0.09 | 4103 |
| swish/latency | 0.10 | 9513 |
| swish/parallel-fib | 0.09 | 4010 |
| swish/payload | 0.09 | 4518 |
| swish/ping-pong | 0.09 | 4040 |
| swish/spawn | 0.09 | 3752 |
| swish/startup | 0.08 | 721 |
| swish/thread-ring | 0.09 | 4491 |
