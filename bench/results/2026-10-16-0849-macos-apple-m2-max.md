# Benchmarks, 2026-10-16 08:49:17

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: c46c2b8, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. A `-mc` implementation is the language's default, every core; without it, one core.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.001 | 0.001 | 0.001 | 1.2 | 0.80 |
| slight | 0.002 | 0.002 | 0.001 | 1.4 | 1.00 |
| go-mc | 0.002 | 0.002 | 0.002 | 4.0 | 1.17 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 1.18 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.63 |
| python | 0.017 | 0.017 | 0.016 | 12.0 | 9.63 |
| node | 0.053 | 0.051 | 0.052 | 41.3 | 30.31 |
| erlang-mc | 0.089 | 0.085 | 0.243 | 59.6 | 51.13 |
| erlang | 0.090 | 0.089 | 0.084 | 48.4 | 51.55 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.108 | 0.107 | 0.107 | 1.2 | 0.35 |
| go | 0.123 | 0.123 | 0.123 | 3.5 | 0.40 |
| go-mc | 0.123 | 0.122 | 0.123 | 4.0 | 0.40 |
| slight | 0.305 | 0.305 | 0.305 | 1.4 | 1.00 |
| node | 0.314 | 0.313 | 0.314 | 43.7 | 1.03 |
| erlang-mc | 0.364 | 0.359 | 0.518 | 60.2 | 1.19 |
| erlang | 0.687 | 0.660 | 0.404 | 48.3 | 2.25 |
| python | 4.666 | 4.547 | 4.662 | 11.8 | 15.28 |
| perl | 12.9 | 12.8 | 12.9 | 4.2 | 42.09 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| erlang-mc | 0.497 | 0.494 | 0.660 | 171.8 | 0.20 |
| node | 0.551 | 0.546 | 0.670 | 364.7 | 0.22 |
| go-mc | 0.665 | 0.655 | 1.863 | 41.2 | 0.26 |
| erlang | 0.875 | 0.869 | 0.556 | 172.4 | 0.34 |
| c | 0.998 | 0.992 | 0.997 | 20.0 | 0.39 |
| go | 1.372 | 1.371 | 1.370 | 61.0 | 0.54 |
| slight | 2.542 | 2.475 | 2.537 | 355.8 | 1.00 |
| python | 5.913 | 5.887 | 5.912 | 45.5 | 2.33 |
| perl | 18.1 | 18.0 | 18.0 | 142.1 | 7.10 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.038 | 1.2 | 0.02 |
| go | 0.048 | 0.048 | 0.048 | 3.5 | 0.02 |
| go-mc | 0.048 | 0.048 | 0.048 | 3.9 | 0.02 |
| node | 0.118 | 0.115 | 0.122 | 47.5 | 0.06 |
| erlang-mc | 1.214 | 1.188 | 1.375 | 59.9 | 0.59 |
| erlang | 1.319 | 1.298 | 1.227 | 48.5 | 0.64 |
| slight | 2.073 | 2.064 | 2.072 | 4.0 | 1.00 |
| python | 5.784 | 5.750 | 5.783 | 12.3 | 2.79 |
| perl | 7.344 | 7.298 | 7.343 | 5.9 | 3.54 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.318 | 0.314 | 0.318 | 2.7 | 1.00 |
| go | 0.390 | 0.387 | 0.390 | 3.5 | 1.23 |
| go-mc | 0.538 | 0.535 | 0.789 | 4.1 | 1.69 |
| erlang-mc | 1.045 | 0.988 | 1.199 | 60.3 | 3.28 |
| erlang | 3.375 | 3.288 | 1.246 | 48.4 | 10.60 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.853 | 0.835 | 0.851 | 209.8 | 1.00 |
| go | 1.067 | 1.015 | 1.066 | 5.0 | 1.25 |
| go-mc | 1.474 | 1.461 | 2.152 | 5.8 | 1.73 |
| erlang-mc | 2.395 | 2.350 | 2.556 | 61.0 | 2.81 |
| erlang | 8.076 | 8.005 | 2.920 | 49.2 | 9.47 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.137 | 0.133 | 0.136 | 90.1 | 1.00 |
| go-mc | 0.393 | 0.383 | 1.840 | 27.7 | 2.88 |
| go | 0.428 | 0.413 | 0.424 | 338.5 | 3.13 |
| erlang-mc | 0.917 | 0.896 | 5.906 | 196.4 | 6.71 |
| erlang | 1.257 | 1.218 | 0.724 | 114.1 | 9.19 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.378 | 0.338 | 0.375 | 555.5 | 1.00 | 554 |
| go-mc | 1.052 | 0.983 | 6.072 | 2875.9 | 2.78 | 2872 |
| go | 1.501 | 1.433 | 1.496 | 2889.8 | 3.97 | 2886 |
| erlang-mc | 1.908 | 1.860 | 6.386 | 2795.7 | 5.05 | 2736 |
| erlang | 2.966 | 2.926 | 1.896 | 2789.8 | 7.85 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.021 | 0.021 | 0.198 | 5.0 | 0.05 |
| erlang-mc | 0.138 | 0.134 | 0.732 | 59.4 | 0.32 |
| go | 0.168 | 0.167 | 0.167 | 3.7 | 0.39 |
| slight | 0.430 | 0.425 | 0.429 | 3.6 | 1.00 |
| erlang | 0.886 | 0.855 | 0.517 | 48.5 | 2.06 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.06 | 33632 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.07 | 50008 |
| c/startup | 0.04 | 33432 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/n-body | 0.18 | 4064 |
| erlang/parallel-fib | 0.17 | 1412 |
| erlang/ping-pong | 0.17 | 1196 |
| erlang/spawn | 0.17 | 1264 |
| erlang/startup | 0.30 | 764 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.11 | 2430210 |
| go/fib | 0.11 | 2430130 |
| go/idle-memory | 0.11 | 2430178 |
| go/n-body | 0.12 | 2430274 |
| go/parallel-fib | 0.12 | 2430194 |
| go/ping-pong | 0.11 | 2430210 |
| go/spawn | 0.11 | 2430210 |
| go/startup | 2.47 | 2429698 |
| go/thread-ring | 0.11 | 2430242 |
| slight/binary-trees | 0.26 | 204144 |
| slight/fib | 0.21 | 203760 |
| slight/idle-memory | 0.21 | 204184 |
| slight/n-body | 0.25 | 237896 |
| slight/parallel-fib | 0.22 | 203792 |
| slight/ping-pong | 0.22 | 204072 |
| slight/spawn | 0.22 | 204072 |
| slight/startup | 1.06 | 203672 |
| slight/thread-ring | 0.22 | 204104 |
