# Benchmarks, 2026-10-16 05:54:42

Intel(R) Xeon(R) Processor @ 2.10GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v139.

- slight: 1fe4278, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
- c: cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- go: go version go1.24.7 linux/amd64
- erlang: OTP 25, erts 13.2.2.5
- node: v22.22.0
- perl: perl v5.38.2
- python: Python 3.13.16

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. A `-mc` implementation is the language's default, every core; without it, one core.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.001 | 0.001 | 0.001 | 1.4 | 0.95 |
| slight | 0.001 | 0.001 | 0.001 | 2.3 | 1.00 |
| go | 0.001 | 0.001 | 0.001 | 1.8 | 1.08 |
| go-mc | 0.001 | 0.001 | 0.002 | 1.9 | 1.11 |
| perl | 0.002 | 0.002 | 0.002 | 5.2 | 1.62 |
| python | 0.015 | 0.013 | 0.014 | 8.9 | 10.98 |
| node | 0.035 | 0.034 | 0.035 | 45.3 | 26.22 |
| erlang-mc | 0.131 | 0.121 | 0.207 | 38.5 | 97.26 |
| erlang | 0.142 | 0.133 | 0.164 | 36.6 | 105.76 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.067 | 0.065 | 0.067 | 1.7 | 0.18 |
| go-mc | 0.209 | 0.201 | 0.210 | 1.9 | 0.56 |
| go | 0.214 | 0.210 | 0.213 | 1.7 | 0.57 |
| slight | 0.376 | 0.368 | 0.373 | 2.3 | 1.00 |
| node | 0.460 | 0.424 | 0.452 | 50.7 | 1.22 |
| erlang-mc | 0.644 | 0.616 | 0.717 | 38.5 | 1.71 |
| erlang | 0.648 | 0.621 | 0.669 | 36.3 | 1.72 |
| python | 4.717 | 4.570 | 4.688 | 9.1 | 12.55 |
| perl | 14.3 | 14.2 | 14.2 | 6.0 | 38.05 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| erlang | 0.898 | 0.886 | 0.940 | 161.3 | 0.24 |
| erlang-mc | 0.904 | 0.852 | 1.012 | 152.6 | 0.24 |
| c | 1.135 | 1.121 | 1.130 | 35.3 | 0.30 |
| node | 1.237 | 1.172 | 2.041 | 295.0 | 0.33 |
| go-mc | 2.145 | 1.969 | 4.242 | 43.2 | 0.57 |
| go | 3.677 | 3.260 | 3.663 | 49.6 | 0.97 |
| slight | 3.787 | 3.448 | 3.739 | 97.7 | 1.00 |
| python | 7.248 | 7.128 | 7.161 | 42.7 | 1.91 |
| perl | 26.0 | 25.1 | 25.7 | 158.1 | 6.88 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.051 | 0.049 | 0.051 | 1.7 | 0.02 |
| go-mc | 0.068 | 0.062 | 0.068 | 1.7 | 0.03 |
| go | 0.069 | 0.066 | 0.070 | 1.7 | 0.03 |
| node | 0.131 | 0.129 | 0.137 | 53.4 | 0.05 |
| erlang | 2.014 | 1.815 | 1.995 | 36.4 | 0.75 |
| erlang-mc | 2.050 | 1.872 | 2.118 | 38.4 | 0.77 |
| slight | 2.675 | 2.471 | 2.658 | 2.8 | 1.00 |
| python | 4.871 | 4.758 | 4.823 | 9.7 | 1.82 |
| perl | 9.724 | 9.244 | 9.623 | 7.7 | 3.64 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.424 | 0.382 | 0.423 | 3.0 | 1.00 |
| go | 0.878 | 0.822 | 0.876 | 1.7 | 2.07 |
| go-mc | 0.895 | 0.819 | 1.146 | 1.7 | 2.11 |
| erlang-mc | 1.826 | 1.775 | 1.876 | 38.2 | 4.31 |
| erlang | 1.829 | 1.740 | 1.839 | 36.0 | 4.31 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 2.284 | 2.063 | 2.945 | 3.2 | 0.71 |
| go | 2.488 | 2.351 | 2.457 | 3.2 | 0.77 |
| slight | 3.226 | 3.051 | 3.206 | 266.3 | 1.00 |
| erlang | 4.082 | 3.931 | 4.066 | 37.0 | 1.27 |
| erlang-mc | 4.154 | 3.928 | 4.188 | 39.2 | 1.29 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.250 | 0.247 | 0.248 | 106.6 | 1.00 |
| go-mc | 0.533 | 0.509 | 1.275 | 35.0 | 2.14 |
| go | 0.767 | 0.687 | 0.749 | 171.3 | 3.07 |
| erlang | 1.288 | 1.176 | 1.288 | 112.5 | 5.16 |
| erlang-mc | 2.250 | 2.147 | 5.174 | 135.3 | 9.01 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.697 | 0.633 | 0.696 | 433.0 | 1.00 | 431 |
| go-mc | 3.897 | 2.592 | 7.910 | 2816.7 | 5.59 | 2815 |
| erlang | 4.136 | 3.949 | 4.134 | 2792.2 | 5.94 | 2756 |
| erlang-mc | 4.989 | 4.812 | 9.456 | 2792.8 | 7.16 | 2754 |
| go | 7.004 | 6.326 | 6.965 | 2796.1 | 10.05 | 2794 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.081 | 0.073 | 0.294 | 2.1 | 0.15 |
| go | 0.296 | 0.279 | 0.292 | 1.9 | 0.54 |
| erlang-mc | 0.303 | 0.296 | 0.904 | 38.7 | 0.55 |
| slight | 0.553 | 0.529 | 0.547 | 2.8 | 1.00 |
| erlang | 0.806 | 0.785 | 0.825 | 36.4 | 1.46 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.18 | 20288 |
| c/fib | 0.08 | 16040 |
| c/n-body | 0.09 | 16368 |
| c/startup | 0.05 | 15960 |
| erlang/binary-trees | 0.24 | 1464 |
| erlang/fib | 0.22 | 900 |
| erlang/idle-memory | 0.23 | 1288 |
| erlang/n-body | 0.26 | 3824 |
| erlang/parallel-fib | 0.24 | 1404 |
| erlang/ping-pong | 0.23 | 1192 |
| erlang/spawn | 0.23 | 1260 |
| erlang/startup | 0.22 | 740 |
| erlang/thread-ring | 0.24 | 1560 |
| go/binary-trees | 0.05 | 2231034 |
| go/fib | 0.06 | 2230446 |
| go/idle-memory | 0.05 | 2230941 |
| go/n-body | 0.06 | 2231717 |
| go/parallel-fib | 0.06 | 2231446 |
| go/ping-pong | 0.05 | 2230719 |
| go/spawn | 0.06 | 2230823 |
| go/startup | 0.05 | 2209255 |
| go/thread-ring | 0.05 | 2230931 |
| slight/binary-trees | 0.35 | 520184 |
| slight/fib | 0.32 | 504520 |
| slight/idle-memory | 0.31 | 516616 |
| slight/n-body | 0.35 | 573800 |
| slight/parallel-fib | 0.33 | 514696 |
| slight/ping-pong | 0.32 | 514880 |
| slight/spawn | 0.33 | 514968 |
| slight/startup | 0.29 | 498480 |
| slight/thread-ring | 0.35 | 515976 |
