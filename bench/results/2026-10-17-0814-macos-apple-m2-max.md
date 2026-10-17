# Benchmarks, 2026-10-17 08:14:46

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 6f53b39, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| c | 0.001 | 0.001 | 0.001 | 1.2 | 0.79 |
| slight | 0.002 | 0.002 | 0.001 | 1.4 | 1.00 |
| go-mc | 0.002 | 0.002 | 0.002 | 4.0 | 1.14 |
| go | 0.002 | 0.002 | 0.002 | 3.5 | 1.16 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.56 |
| python | 0.017 | 0.017 | 0.016 | 12.0 | 9.55 |
| node | 0.053 | 0.050 | 0.053 | 41.2 | 29.77 |
| erlang-mc | 0.090 | 0.090 | 0.249 | 60.0 | 50.42 |
| erlang | 0.092 | 0.090 | 0.086 | 48.7 | 51.28 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.107 | 0.109 | 1.2 | 0.54 |
| go | 0.126 | 0.123 | 0.125 | 3.5 | 0.62 |
| go-mc | 0.126 | 0.122 | 0.126 | 4.0 | 0.62 |
| slight | 0.203 | 0.199 | 0.202 | 1.5 | 1.00 |
| node | 0.321 | 0.313 | 0.321 | 43.9 | 1.58 |
| erlang-mc | 0.370 | 0.363 | 0.529 | 59.6 | 1.83 |
| erlang | 0.731 | 0.669 | 0.442 | 48.4 | 3.61 |
| python | 4.751 | 4.532 | 4.747 | 11.8 | 23.45 |
| perl | 13.0 | 12.6 | 13.0 | 4.2 | 63.98 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| erlang-mc | 0.506 | 0.490 | 0.673 | 172.3 | 0.37 |
| node | 0.561 | 0.539 | 0.679 | 368.2 | 0.41 |
| go-mc | 0.670 | 0.656 | 1.864 | 41.3 | 0.49 |
| erlang | 0.883 | 0.841 | 0.561 | 172.5 | 0.65 |
| c | 1.007 | 0.963 | 1.006 | 20.0 | 0.74 |
| slight | 1.354 | 1.239 | 1.350 | 302.9 | 1.00 |
| go | 1.459 | 1.368 | 1.456 | 59.9 | 1.08 |
| python | 6.018 | 5.795 | 6.015 | 45.5 | 4.44 |
| perl | 18.3 | 17.7 | 18.3 | 142.1 | 13.50 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.039 | 0.037 | 0.038 | 1.2 | 0.03 |
| go-mc | 0.049 | 0.048 | 0.049 | 3.9 | 0.03 |
| go | 0.049 | 0.048 | 0.049 | 3.5 | 0.03 |
| node | 0.120 | 0.119 | 0.123 | 47.4 | 0.08 |
| erlang-mc | 1.224 | 1.173 | 1.376 | 59.5 | 0.84 |
| erlang | 1.377 | 1.312 | 1.279 | 48.5 | 0.95 |
| slight | 1.449 | 1.395 | 1.447 | 5.6 | 1.00 |
| python | 5.884 | 5.569 | 5.880 | 12.3 | 4.06 |
| perl | 7.559 | 7.150 | 7.545 | 5.9 | 5.22 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.328 | 0.308 | 0.327 | 3.5 | 1.00 |
| go | 0.397 | 0.373 | 0.397 | 3.6 | 1.21 |
| go-mc | 0.547 | 0.534 | 0.807 | 4.1 | 1.67 |
| erlang-mc | 1.042 | 0.980 | 1.188 | 59.9 | 3.18 |
| erlang | 3.463 | 3.343 | 1.338 | 48.4 | 10.56 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.883 | 0.847 | 0.880 | 209.8 | 1.00 |
| go | 1.047 | 1.026 | 1.047 | 5.1 | 1.19 |
| go-mc | 1.499 | 1.474 | 2.188 | 5.8 | 1.70 |
| erlang-mc | 2.400 | 2.334 | 2.564 | 60.5 | 2.72 |
| erlang | 8.430 | 7.961 | 3.208 | 49.1 | 9.54 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.133 | 0.128 | 0.132 | 90.1 | 1.00 |
| go-mc | 0.389 | 0.382 | 1.867 | 27.3 | 2.93 |
| go | 0.455 | 0.410 | 0.450 | 333.3 | 3.42 |
| erlang-mc | 0.927 | 0.896 | 5.842 | 194.3 | 6.98 |
| erlang | 1.285 | 1.209 | 0.746 | 114.2 | 9.67 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.361 | 0.343 | 0.359 | 555.5 | 1.00 | 554 |
| go-mc | 1.068 | 0.994 | 6.190 | 2913.3 | 2.96 | 2909 |
| go | 1.577 | 1.407 | 1.571 | 2890.0 | 4.36 | 2886 |
| erlang-mc | 1.934 | 1.882 | 6.502 | 2795.1 | 5.35 | 2735 |
| erlang | 3.008 | 2.890 | 1.935 | 2790.0 | 8.32 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.021 | 0.021 | 0.198 | 5.0 | 0.07 |
| erlang-mc | 0.139 | 0.136 | 0.721 | 59.7 | 0.48 |
| go | 0.171 | 0.167 | 0.170 | 3.7 | 0.59 |
| slight | 0.288 | 0.282 | 0.287 | 3.6 | 1.00 |
| erlang | 0.909 | 0.849 | 0.537 | 48.4 | 3.16 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.06 | 50008 |
| c/startup | 0.04 | 33432 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/fib | 0.15 | 928 |
| erlang/idle-memory | 0.17 | 1312 |
| erlang/n-body | 0.17 | 4064 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/ping-pong | 0.15 | 1196 |
| erlang/spawn | 0.18 | 1264 |
| erlang/startup | 0.15 | 764 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/fib | 0.09 | 2430130 |
| go/idle-memory | 0.09 | 2430178 |
| go/n-body | 0.10 | 2430274 |
| go/parallel-fib | 0.10 | 2430194 |
| go/ping-pong | 0.09 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/startup | 0.32 | 2429698 |
| go/thread-ring | 0.10 | 2430242 |
| slight/binary-trees | 0.21 | 204144 |
| slight/fib | 0.21 | 203744 |
| slight/idle-memory | 0.20 | 204168 |
| slight/n-body | 0.23 | 237896 |
| slight/parallel-fib | 0.21 | 203776 |
| slight/ping-pong | 0.20 | 204072 |
| slight/spawn | 0.22 | 204056 |
| slight/startup | 1.01 | 203672 |
| slight/thread-ring | 0.22 | 204088 |
