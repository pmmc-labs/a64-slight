# Benchmarks, 2026-10-17 10:43:46

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 51893a0, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. A `-mc` implementation is the language's default, every core; without it, one core.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.002 | 0.002 | 0.001 | 1.2 | 0.72 |
| go-mc | 0.002 | 0.002 | 0.002 | 4.0 | 0.82 |
| slight | 0.003 | 0.002 | 0.002 | 1.5 | 1.00 |
| go | 0.006 | 0.006 | 0.003 | 3.5 | 2.50 |
| perl | 0.010 | 0.009 | 0.004 | 3.5 | 3.75 |
| python | 0.023 | 0.022 | 0.017 | 12.0 | 8.89 |
| chez | 0.038 | 0.038 | 0.035 | 51.9 | 15.10 |
| erlang-mc | 0.089 | 0.086 | 0.248 | 60.0 | 35.12 |
| erlang | 0.092 | 0.089 | 0.085 | 48.5 | 36.13 |
| node | 0.150 | 0.147 | 0.072 | 41.4 | 59.25 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.110 | 0.110 | 1.2 | 0.55 |
| go-mc | 0.123 | 0.123 | 0.123 | 4.0 | 0.61 |
| go | 0.129 | 0.127 | 0.124 | 3.5 | 0.64 |
| slight | 0.201 | 0.200 | 0.200 | 1.5 | 1.00 |
| chez | 0.225 | 0.224 | 0.225 | 51.8 | 1.12 |
| node | 0.317 | 0.316 | 0.317 | 43.9 | 1.58 |
| erlang-mc | 0.365 | 0.359 | 0.522 | 60.2 | 1.81 |
| erlang | 0.682 | 0.660 | 0.400 | 48.3 | 3.39 |
| python | 4.652 | 4.638 | 4.650 | 11.8 | 23.10 |
| perl | 12.8 | 12.8 | 12.8 | 4.3 | 63.69 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.083 | 0.083 | 0.082 | 1.2 | 0.58 |
| go-mc | 0.102 | 0.102 | 0.102 | 4.0 | 0.72 |
| go | 0.107 | 0.107 | 0.103 | 3.5 | 0.75 |
| slight | 0.142 | 0.142 | 0.141 | 1.5 | 1.00 |
| chez | 0.190 | 0.189 | 0.189 | 51.8 | 1.33 |
| erlang-mc | 0.284 | 0.279 | 0.447 | 59.9 | 2.00 |
| node | 0.289 | 0.286 | 0.289 | 43.8 | 2.03 |
| erlang | 0.521 | 0.503 | 0.308 | 48.4 | 3.66 |
| python | 3.604 | 3.593 | 3.603 | 11.8 | 25.33 |
| perl | 14.0 | 13.9 | 14.0 | 4.4 | 98.12 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.342 | 0.340 | 0.340 | 65.8 | 0.32 |
| erlang-mc | 0.498 | 0.491 | 0.667 | 173.0 | 0.47 |
| go-mc | 0.657 | 0.655 | 1.857 | 40.9 | 0.62 |
| node | 0.708 | 0.693 | 0.957 | 298.9 | 0.67 |
| erlang | 0.858 | 0.837 | 0.538 | 172.1 | 0.81 |
| c | 0.994 | 0.991 | 0.992 | 20.0 | 0.94 |
| slight | 1.054 | 1.052 | 1.052 | 92.9 | 1.00 |
| go | 1.369 | 1.367 | 1.363 | 73.1 | 1.30 |
| python | 5.876 | 5.874 | 5.875 | 45.5 | 5.58 |
| perl | 17.9 | 17.9 | 17.9 | 142.1 | 17.03 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.309 | 0.308 | 1.2 | 0.37 |
| chez | 0.392 | 0.388 | 0.391 | 51.8 | 0.46 |
| erlang-mc | 0.474 | 0.465 | 0.646 | 60.5 | 0.56 |
| go-mc | 0.549 | 0.547 | 0.595 | 10.8 | 0.65 |
| go | 0.581 | 0.578 | 0.573 | 8.9 | 0.69 |
| erlang | 0.677 | 0.645 | 0.507 | 48.5 | 0.80 |
| slight | 0.843 | 0.842 | 0.842 | 3.7 | 1.00 |
| node | 2.276 | 2.262 | 2.289 | 62.2 | 2.70 |
| python | 6.965 | 6.931 | 6.963 | 11.9 | 8.26 |
| perl | 13.9 | 13.8 | 13.8 | 5.4 | 16.43 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.3 | 0.23 |
| chez | 0.186 | 0.185 | 0.184 | 51.8 | 0.32 |
| erlang-mc | 0.323 | 0.311 | 0.489 | 59.8 | 0.56 |
| erlang | 0.517 | 0.499 | 0.349 | 48.5 | 0.89 |
| slight | 0.578 | 0.577 | 0.576 | 3.7 | 1.00 |
| node | 0.627 | 0.620 | 0.675 | 56.3 | 1.08 |
| go-mc | 0.804 | 0.797 | 0.922 | 11.2 | 1.39 |
| go | 1.070 | 1.067 | 1.051 | 8.6 | 1.85 |
| python | 4.084 | 4.065 | 4.083 | 11.9 | 7.07 |
| perl | 8.144 | 8.118 | 8.143 | 4.5 | 14.09 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.038 | 1.3 | 0.03 |
| go-mc | 0.048 | 0.048 | 0.048 | 4.0 | 0.04 |
| go | 0.052 | 0.052 | 0.049 | 3.5 | 0.04 |
| node | 0.120 | 0.118 | 0.121 | 47.3 | 0.09 |
| chez | 1.162 | 1.158 | 1.160 | 51.8 | 0.87 |
| erlang-mc | 1.203 | 1.188 | 1.368 | 60.1 | 0.90 |
| erlang | 1.326 | 1.305 | 1.231 | 48.4 | 0.99 |
| slight | 1.340 | 1.333 | 1.339 | 4.5 | 1.00 |
| python | 5.747 | 5.733 | 5.746 | 12.3 | 4.29 |
| perl | 7.366 | 7.160 | 7.363 | 6.3 | 5.50 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.046 | 0.046 | 0.046 | 1.2 | 0.10 |
| go-mc | 0.047 | 0.047 | 0.047 | 4.0 | 0.10 |
| go | 0.051 | 0.051 | 0.048 | 3.6 | 0.11 |
| node | 0.120 | 0.120 | 0.121 | 46.2 | 0.26 |
| slight | 0.453 | 0.449 | 0.452 | 3.8 | 1.00 |
| erlang-mc | 0.488 | 0.486 | 0.651 | 60.8 | 1.08 |
| chez | 0.576 | 0.574 | 0.574 | 51.8 | 1.27 |
| erlang | 0.658 | 0.652 | 0.503 | 48.6 | 1.45 |
| python | 5.147 | 5.133 | 5.146 | 12.5 | 11.36 |
| perl | 7.643 | 7.523 | 7.641 | 7.3 | 16.87 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.332 | 0.328 | 0.331 | 2.7 | 1.00 |
| go | 0.392 | 0.390 | 0.388 | 3.5 | 1.18 |
| go-mc | 0.528 | 0.523 | 0.757 | 4.1 | 1.59 |
| erlang-mc | 0.999 | 0.987 | 1.165 | 59.9 | 3.01 |
| erlang | 3.378 | 3.333 | 1.284 | 48.5 | 10.18 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.529 | 0.524 | 0.528 | 2.1 | 1.00 |
| go | 1.034 | 1.023 | 1.030 | 5.1 | 1.95 |
| go-mc | 1.443 | 1.440 | 2.054 | 5.8 | 2.73 |
| erlang-mc | 2.324 | 2.303 | 2.497 | 60.9 | 4.39 |
| erlang | 8.096 | 7.935 | 2.940 | 49.1 | 15.30 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.289 | 0.287 | 0.288 | 3.8 | 0.77 |
| slight | 0.377 | 0.369 | 0.372 | 483.6 | 1.00 |
| erlang-mc | 1.090 | 1.069 | 8.704 | 658.1 | 2.89 |
| erlang | 1.735 | 1.645 | 1.691 | 650.1 | 4.60 |
| go-mc | 3.495 | 3.458 | 11.7 | 5.3 | 9.27 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.130 | 0.129 | 0.129 | 90.1 | 1.00 |
| go-mc | 0.385 | 0.383 | 1.854 | 27.8 | 2.96 |
| go | 0.428 | 0.423 | 0.423 | 391.6 | 3.29 |
| erlang-mc | 0.896 | 0.893 | 5.749 | 196.0 | 6.90 |
| erlang | 1.276 | 1.211 | 0.737 | 114.0 | 9.82 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.347 | 0.330 | 0.345 | 556.0 | 1.00 | 555 |
| go-mc | 1.021 | 0.984 | 6.146 | 2912.8 | 2.94 | 2909 |
| go | 1.475 | 1.400 | 1.469 | 2901.2 | 4.25 | 2898 |
| erlang-mc | 1.919 | 1.890 | 6.484 | 2796.3 | 5.54 | 2736 |
| erlang | 2.998 | 2.943 | 1.932 | 2789.9 | 8.65 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.021 | 0.021 | 0.200 | 4.9 | 0.08 |
| erlang-mc | 0.138 | 0.134 | 0.734 | 59.7 | 0.49 |
| go | 0.171 | 0.171 | 0.168 | 3.7 | 0.61 |
| slight | 0.282 | 0.282 | 0.281 | 3.6 | 1.00 |
| erlang | 0.916 | 0.866 | 0.545 | 48.6 | 3.24 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| c/deriv | 0.06 | 50440 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.07 | 50008 |
| c/nqueens | 0.06 | 33720 |
| c/spectral-norm | 0.06 | 33584 |
| c/startup | 0.04 | 33432 |
| c/tak | 0.05 | 33472 |
| chez/binary-trees | 0.04 | 4089 |
| chez/deriv | 0.04 | 4237 |
| chez/fib | 0.04 | 2166 |
| chez/n-body | 0.06 | 11318 |
| chez/nqueens | 0.04 | 3418 |
| chez/spectral-norm | 0.04 | 5509 |
| chez/startup | 0.04 | 1412 |
| chez/tak | 0.04 | 2582 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/deriv | 0.16 | 1612 |
| erlang/fan-in | 0.16 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/n-body | 0.19 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.16 | 1412 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.17 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.17 | 764 |
| erlang/tak | 0.16 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.11 | 2430482 |
| go/fan-in | 0.11 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/n-body | 0.10 | 2430274 |
| go/nqueens | 0.11 | 2430130 |
| go/parallel-fib | 0.10 | 2430194 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.11 | 2430338 |
| go/startup | 0.29 | 2429698 |
| go/tak | 0.12 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| slight/binary-trees | 0.21 | 204608 |
| slight/deriv | 0.22 | 204456 |
| slight/fan-in | 0.21 | 204536 |
| slight/fib | 0.21 | 204208 |
| slight/idle-memory | 0.21 | 204648 |
| slight/n-body | 0.24 | 254872 |
| slight/nqueens | 0.21 | 204328 |
| slight/parallel-fib | 0.21 | 204240 |
| slight/ping-pong | 0.21 | 204536 |
| slight/spawn | 0.21 | 204536 |
| slight/spectral-norm | 0.21 | 221216 |
| slight/startup | 1.09 | 204136 |
| slight/tak | 0.21 | 204208 |
| slight/thread-ring | 0.21 | 204568 |
