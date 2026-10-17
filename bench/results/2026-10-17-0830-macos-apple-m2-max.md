# Benchmarks, 2026-10-17 08:30:30

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 9ab66e5, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| go | 0.002 | 0.002 | 0.002 | 3.5 | 1.15 |
| go-mc | 0.002 | 0.002 | 0.002 | 4.0 | 1.16 |
| perl | 0.003 | 0.003 | 0.003 | 3.3 | 1.64 |
| python | 0.017 | 0.017 | 0.016 | 12.0 | 9.56 |
| node | 0.053 | 0.053 | 0.053 | 41.3 | 29.39 |
| erlang-mc | 0.089 | 0.088 | 0.243 | 59.7 | 49.15 |
| erlang | 0.092 | 0.091 | 0.086 | 48.7 | 51.18 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.112 | 0.110 | 0.112 | 1.2 | 0.55 |
| go | 0.126 | 0.125 | 0.125 | 3.5 | 0.62 |
| go-mc | 0.126 | 0.125 | 0.125 | 4.0 | 0.62 |
| slight | 0.203 | 0.202 | 0.202 | 1.4 | 1.00 |
| node | 0.321 | 0.319 | 0.321 | 44.2 | 1.58 |
| erlang-mc | 0.377 | 0.372 | 0.536 | 60.4 | 1.86 |
| erlang | 0.715 | 0.698 | 0.427 | 48.6 | 3.52 |
| python | 4.762 | 4.732 | 4.758 | 11.8 | 23.48 |
| perl | 13.1 | 13.0 | 13.0 | 4.2 | 64.36 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| erlang-mc | 0.516 | 0.510 | 0.675 | 172.9 | 0.38 |
| node | 0.566 | 0.561 | 0.698 | 369.8 | 0.42 |
| go-mc | 0.679 | 0.676 | 1.862 | 41.0 | 0.50 |
| erlang | 0.884 | 0.875 | 0.561 | 172.4 | 0.65 |
| c | 1.016 | 1.010 | 1.015 | 20.0 | 0.75 |
| slight | 1.350 | 1.345 | 1.349 | 143.0 | 1.00 |
| go | 1.448 | 1.428 | 1.445 | 60.5 | 1.07 |
| python | 6.002 | 5.991 | 5.999 | 45.5 | 4.44 |
| perl | 18.3 | 18.2 | 18.3 | 142.1 | 13.53 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.039 | 0.038 | 0.038 | 1.2 | 0.03 |
| go-mc | 0.049 | 0.049 | 0.049 | 4.0 | 0.03 |
| go | 0.049 | 0.049 | 0.049 | 3.5 | 0.03 |
| node | 0.121 | 0.120 | 0.124 | 47.5 | 0.08 |
| erlang-mc | 1.233 | 1.220 | 1.385 | 59.5 | 0.85 |
| erlang | 1.364 | 1.358 | 1.271 | 48.5 | 0.94 |
| slight | 1.448 | 1.445 | 1.447 | 5.5 | 1.00 |
| python | 5.925 | 5.892 | 5.918 | 12.3 | 4.09 |
| perl | 7.509 | 7.291 | 7.500 | 5.9 | 5.18 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.328 | 0.323 | 0.327 | 3.5 | 1.00 |
| go | 0.395 | 0.395 | 0.395 | 3.5 | 1.21 |
| go-mc | 0.553 | 0.552 | 0.821 | 4.1 | 1.69 |
| erlang-mc | 1.027 | 1.019 | 1.190 | 59.8 | 3.13 |
| erlang | 3.455 | 3.423 | 1.327 | 48.6 | 10.55 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.538 | 0.532 | 0.537 | 2.0 | 1.00 |
| go | 1.066 | 1.035 | 1.064 | 5.1 | 1.98 |
| go-mc | 1.509 | 1.482 | 2.224 | 5.7 | 2.81 |
| erlang-mc | 2.379 | 2.362 | 2.542 | 60.9 | 4.42 |
| erlang | 8.278 | 8.197 | 3.058 | 49.4 | 15.39 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.131 | 0.130 | 0.130 | 90.1 | 1.00 |
| go-mc | 0.390 | 0.385 | 1.861 | 27.7 | 2.99 |
| go | 0.451 | 0.450 | 0.449 | 313.8 | 3.46 |
| erlang-mc | 0.934 | 0.926 | 5.777 | 195.6 | 7.15 |
| erlang | 1.235 | 1.230 | 0.703 | 114.0 | 9.46 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.355 | 0.351 | 0.353 | 555.5 | 1.00 | 554 |
| go-mc | 1.048 | 0.902 | 5.980 | 2909.4 | 2.95 | 2905 |
| go | 1.554 | 1.515 | 1.553 | 2889.6 | 4.37 | 2886 |
| erlang-mc | 1.929 | 1.923 | 6.448 | 2794.7 | 5.43 | 2735 |
| erlang | 3.018 | 2.998 | 1.956 | 2789.8 | 8.50 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.021 | 0.021 | 0.198 | 5.0 | 0.07 |
| erlang-mc | 0.140 | 0.139 | 0.722 | 59.9 | 0.48 |
| go | 0.171 | 0.170 | 0.170 | 3.7 | 0.59 |
| slight | 0.288 | 0.287 | 0.287 | 3.5 | 1.00 |
| erlang | 0.908 | 0.900 | 0.533 | 48.6 | 3.15 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.07 | 50008 |
| c/startup | 0.04 | 33432 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/n-body | 0.18 | 4064 |
| erlang/parallel-fib | 0.16 | 1412 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/startup | 0.16 | 764 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.10 | 2430178 |
| go/n-body | 0.10 | 2430274 |
| go/parallel-fib | 0.10 | 2430194 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/startup | 0.10 | 2429698 |
| go/thread-ring | 0.10 | 2430242 |
| slight/binary-trees | 0.23 | 204144 |
| slight/fib | 0.21 | 203744 |
| slight/idle-memory | 0.21 | 204168 |
| slight/n-body | 0.25 | 237896 |
| slight/parallel-fib | 0.21 | 203776 |
| slight/ping-pong | 0.21 | 204072 |
| slight/spawn | 0.21 | 204056 |
| slight/startup | 1.06 | 203672 |
| slight/thread-ring | 0.21 | 204088 |
