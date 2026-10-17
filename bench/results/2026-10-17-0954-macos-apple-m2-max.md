# Benchmarks, 2026-10-17 09:54:39

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: c242a27, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| c | 0.002 | 0.001 | 0.001 | 1.2 | 0.72 |
| go-mc | 0.002 | 0.002 | 0.002 | 4.0 | 0.81 |
| slight | 0.003 | 0.002 | 0.002 | 1.5 | 1.00 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.27 |
| go | 0.006 | 0.002 | 0.003 | 3.5 | 2.52 |
| python | 0.020 | 0.017 | 0.017 | 12.1 | 7.87 |
| erlang-mc | 0.090 | 0.088 | 0.242 | 60.3 | 35.61 |
| erlang | 0.091 | 0.091 | 0.085 | 48.3 | 36.10 |
| node | 0.150 | 0.052 | 0.072 | 41.3 | 59.60 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.107 | 0.109 | 1.2 | 0.54 |
| go-mc | 0.124 | 0.124 | 0.124 | 4.0 | 0.61 |
| go | 0.129 | 0.123 | 0.126 | 3.5 | 0.64 |
| slight | 0.203 | 0.200 | 0.201 | 1.5 | 1.00 |
| node | 0.322 | 0.317 | 0.322 | 44.1 | 1.59 |
| erlang-mc | 0.373 | 0.362 | 0.520 | 60.0 | 1.84 |
| erlang | 0.687 | 0.663 | 0.404 | 48.6 | 3.39 |
| python | 4.671 | 4.645 | 4.666 | 11.8 | 23.05 |
| perl | 12.9 | 12.9 | 12.9 | 4.3 | 63.80 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| erlang-mc | 0.504 | 0.496 | 0.660 | 172.4 | 0.47 |
| go-mc | 0.669 | 0.664 | 1.869 | 40.8 | 0.62 |
| node | 0.712 | 0.554 | 0.972 | 296.9 | 0.66 |
| erlang | 0.879 | 0.843 | 0.557 | 172.4 | 0.82 |
| c | 1.000 | 0.991 | 0.998 | 20.0 | 0.93 |
| slight | 1.075 | 1.058 | 1.073 | 113.1 | 1.00 |
| go | 1.405 | 1.372 | 1.399 | 59.6 | 1.31 |
| python | 5.951 | 5.919 | 5.943 | 45.5 | 5.54 |
| perl | 18.2 | 18.0 | 18.2 | 142.1 | 16.93 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.039 | 0.038 | 0.038 | 1.2 | 0.03 |
| go-mc | 0.048 | 0.048 | 0.048 | 3.9 | 0.04 |
| go | 0.053 | 0.049 | 0.049 | 3.5 | 0.04 |
| node | 0.119 | 0.117 | 0.122 | 47.7 | 0.09 |
| erlang-mc | 1.206 | 1.195 | 1.360 | 60.0 | 0.90 |
| erlang | 1.321 | 1.302 | 1.227 | 48.8 | 0.98 |
| slight | 1.346 | 1.341 | 1.344 | 6.2 | 1.00 |
| python | 5.799 | 5.785 | 5.793 | 12.3 | 4.31 |
| perl | 7.583 | 7.311 | 7.579 | 6.3 | 5.63 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.330 | 0.328 | 0.330 | 2.7 | 1.00 |
| go | 0.394 | 0.387 | 0.391 | 3.5 | 1.19 |
| go-mc | 0.540 | 0.538 | 0.796 | 4.1 | 1.63 |
| erlang-mc | 1.067 | 1.016 | 1.224 | 59.7 | 3.23 |
| erlang | 3.358 | 3.274 | 1.262 | 48.5 | 10.16 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.534 | 0.522 | 0.533 | 2.1 | 1.00 |
| go | 1.062 | 1.047 | 1.058 | 5.1 | 1.99 |
| go-mc | 1.483 | 1.475 | 2.171 | 5.8 | 2.78 |
| erlang-mc | 2.369 | 2.325 | 2.534 | 60.3 | 4.44 |
| erlang | 8.183 | 8.099 | 2.988 | 49.1 | 15.32 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.132 | 0.129 | 0.131 | 90.1 | 1.00 |
| go-mc | 0.397 | 0.382 | 1.860 | 27.7 | 3.01 |
| go | 0.423 | 0.417 | 0.419 | 346.7 | 3.21 |
| erlang-mc | 0.904 | 0.901 | 5.754 | 198.2 | 6.86 |
| erlang | 1.228 | 1.204 | 0.695 | 114.1 | 9.32 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.361 | 0.358 | 0.360 | 556.0 | 1.00 | 555 |
| go-mc | 1.011 | 0.984 | 6.047 | 2913.1 | 2.80 | 2909 |
| go | 1.529 | 1.505 | 1.523 | 2890.0 | 4.24 | 2887 |
| erlang-mc | 1.903 | 1.901 | 6.437 | 2795.0 | 5.28 | 2735 |
| erlang | 3.001 | 2.950 | 1.934 | 2790.0 | 8.32 | 2742 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.021 | 0.021 | 0.200 | 5.0 | 0.07 |
| erlang-mc | 0.139 | 0.134 | 0.739 | 59.9 | 0.49 |
| go | 0.171 | 0.167 | 0.168 | 3.7 | 0.60 |
| slight | 0.285 | 0.283 | 0.283 | 3.6 | 1.00 |
| erlang | 0.899 | 0.860 | 0.530 | 48.5 | 3.16 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.06 | 50008 |
| c/startup | 0.04 | 33432 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/n-body | 0.17 | 4064 |
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
| go/spawn | 0.09 | 2430210 |
| go/startup | 0.31 | 2429698 |
| go/thread-ring | 0.10 | 2430242 |
| slight/binary-trees | 0.21 | 204608 |
| slight/fib | 0.21 | 204208 |
| slight/idle-memory | 0.21 | 204648 |
| slight/n-body | 0.24 | 254872 |
| slight/parallel-fib | 0.21 | 204240 |
| slight/ping-pong | 0.21 | 204536 |
| slight/spawn | 0.21 | 204536 |
| slight/startup | 1.08 | 204136 |
| slight/thread-ring | 0.21 | 204568 |
