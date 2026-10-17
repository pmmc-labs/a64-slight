# Benchmarks, 2026-10-17 08:42:55

Intel(R) Xeon(R) Processor @ 2.10GHz, x86_64, 4 cores, 16 GB, Linux 6.18.44-fc-v139.

- slight: c05ba52, its runtime built by Ubuntu clang version 18.1.3 (1ubuntu1)
- c: cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- go: go version go1.24.7 linux/amd64
- erlang: OTP 25, erts 13.2.2.5
- node: v22.22.0
- perl: perl v5.38.2
- python: Python 3.13.16
- chez: Chez Scheme 9.5.8

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. A `-mc` implementation is the language's default, every core; without it, one core.

## startup 0

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.001 | 0.001 | 0.001 | 1.4 | 0.70 |
| go-mc | 0.002 | 0.001 | 0.002 | 1.8 | 0.94 |
| slight | 0.002 | 0.001 | 0.002 | 2.2 | 1.00 |
| go | 0.002 | 0.001 | 0.002 | 1.8 | 1.09 |
| perl | 0.002 | 0.002 | 0.002 | 5.2 | 1.39 |
| python | 0.015 | 0.014 | 0.014 | 8.9 | 8.76 |
| node | 0.039 | 0.037 | 0.039 | 45.3 | 23.49 |
| chez | 0.071 | 0.064 | 0.070 | 50.7 | 42.81 |
| erlang-mc | 0.154 | 0.134 | 0.240 | 38.5 | 92.93 |
| erlang | 0.162 | 0.153 | 0.183 | 36.3 | 97.73 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.092 | 0.076 | 0.088 | 1.7 | 0.27 |
| go | 0.232 | 0.225 | 0.233 | 1.8 | 0.69 |
| go-mc | 0.234 | 0.225 | 0.232 | 1.9 | 0.70 |
| slight | 0.337 | 0.329 | 0.330 | 2.3 | 1.00 |
| chez | 0.384 | 0.343 | 0.379 | 50.7 | 1.14 |
| node | 0.511 | 0.491 | 0.511 | 50.8 | 1.52 |
| erlang-mc | 0.695 | 0.691 | 0.779 | 38.5 | 2.06 |
| erlang | 0.756 | 0.709 | 0.776 | 36.4 | 2.25 |
| python | 5.109 | 5.044 | 5.080 | 9.1 | 15.17 |
| perl | 15.4 | 15.0 | 15.3 | 5.9 | 45.78 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.165 | 0.162 | 0.165 | 1.7 | 0.49 |
| go-mc | 0.220 | 0.214 | 0.221 | 1.7 | 0.66 |
| go | 0.223 | 0.215 | 0.224 | 1.7 | 0.67 |
| chez | 0.255 | 0.239 | 0.249 | 50.7 | 0.76 |
| slight | 0.335 | 0.318 | 0.332 | 2.3 | 1.00 |
| node | 0.424 | 0.404 | 0.424 | 50.9 | 1.26 |
| erlang | 0.570 | 0.542 | 0.592 | 36.4 | 1.70 |
| erlang-mc | 0.616 | 0.524 | 0.690 | 38.3 | 1.84 |
| python | 4.129 | 4.021 | 4.085 | 9.1 | 12.33 |
| perl | 20.4 | 20.1 | 20.1 | 5.8 | 61.05 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.534 | 0.523 | 0.531 | 62.1 | 0.29 |
| erlang-mc | 1.041 | 0.919 | 1.146 | 154.2 | 0.57 |
| erlang | 1.054 | 0.940 | 1.048 | 161.4 | 0.58 |
| c | 1.292 | 1.220 | 1.282 | 35.3 | 0.70 |
| node | 1.409 | 1.347 | 2.338 | 276.2 | 0.77 |
| slight | 1.833 | 1.693 | 1.816 | 90.3 | 1.00 |
| go-mc | 2.483 | 2.429 | 4.912 | 44.7 | 1.35 |
| go | 4.047 | 3.853 | 4.045 | 57.6 | 2.21 |
| python | 8.150 | 7.854 | 8.075 | 42.8 | 4.45 |
| perl | 28.3 | 28.0 | 28.0 | 158.1 | 15.44 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.487 | 0.476 | 0.477 | 1.7 | 0.39 |
| chez | 0.750 | 0.717 | 0.744 | 50.7 | 0.61 |
| erlang | 1.131 | 1.098 | 1.149 | 36.1 | 0.91 |
| erlang-mc | 1.150 | 1.025 | 1.217 | 38.3 | 0.93 |
| go | 1.186 | 1.118 | 1.188 | 6.8 | 0.96 |
| slight | 1.238 | 1.159 | 1.227 | 3.6 | 1.00 |
| go-mc | 1.307 | 1.243 | 1.424 | 7.9 | 1.06 |
| node | 4.089 | 4.003 | 4.085 | 68.8 | 3.30 |
| python | 9.435 | 9.131 | 9.340 | 9.3 | 7.62 |
| perl | 20.3 | 19.5 | 20.1 | 6.1 | 16.39 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.223 | 0.211 | 0.217 | 1.7 | 0.27 |
| chez | 0.330 | 0.302 | 0.321 | 50.7 | 0.39 |
| erlang-mc | 0.448 | 0.430 | 0.512 | 38.2 | 0.54 |
| erlang | 0.476 | 0.446 | 0.495 | 36.4 | 0.57 |
| slight | 0.835 | 0.820 | 0.817 | 4.5 | 1.00 |
| node | 1.146 | 1.108 | 1.211 | 62.0 | 1.37 |
| go | 2.550 | 2.381 | 2.525 | 8.3 | 3.05 |
| go-mc | 2.564 | 2.519 | 2.887 | 7.9 | 3.07 |
| python | 5.345 | 4.985 | 5.294 | 9.3 | 6.40 |
| perl | 13.4 | 13.3 | 13.3 | 6.0 | 16.05 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.061 | 0.060 | 0.059 | 1.7 | 0.05 |
| go | 0.080 | 0.071 | 0.080 | 1.8 | 0.06 |
| go-mc | 0.081 | 0.071 | 0.082 | 1.8 | 0.06 |
| node | 0.140 | 0.137 | 0.145 | 53.4 | 0.11 |
| slight | 1.247 | 1.107 | 1.233 | 4.6 | 1.00 |
| chez | 1.638 | 1.575 | 1.632 | 50.7 | 1.31 |
| erlang | 2.130 | 2.083 | 2.132 | 36.3 | 1.71 |
| erlang-mc | 2.134 | 2.107 | 2.205 | 38.5 | 1.71 |
| python | 5.386 | 5.190 | 5.356 | 9.9 | 4.32 |
| perl | 10.6 | 10.3 | 10.6 | 7.7 | 8.53 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.060 | 0.059 | 0.059 | 2.2 | 0.11 |
| go-mc | 0.061 | 0.060 | 0.062 | 1.9 | 0.11 |
| go | 0.066 | 0.061 | 0.063 | 1.8 | 0.12 |
| node | 0.173 | 0.134 | 0.178 | 53.6 | 0.31 |
| slight | 0.556 | 0.528 | 0.553 | 4.5 | 1.00 |
| chez | 1.102 | 1.018 | 1.082 | 50.7 | 1.98 |
| erlang-mc | 1.668 | 1.573 | 1.727 | 39.5 | 3.00 |
| erlang | 1.670 | 1.622 | 1.682 | 36.5 | 3.01 |
| python | 7.531 | 6.975 | 7.472 | 9.7 | 13.55 |
| perl | 11.6 | 10.8 | 11.5 | 8.0 | 20.85 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.447 | 0.403 | 0.444 | 3.4 | 1.00 |
| go-mc | 0.936 | 0.915 | 1.197 | 1.7 | 2.09 |
| go | 0.962 | 0.920 | 0.955 | 1.7 | 2.15 |
| erlang-mc | 2.006 | 1.905 | 2.041 | 38.3 | 4.48 |
| erlang | 2.028 | 1.879 | 2.025 | 36.1 | 4.53 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.765 | 0.747 | 0.763 | 2.7 | 1.00 |
| go-mc | 2.445 | 2.410 | 3.125 | 3.2 | 3.20 |
| go | 2.585 | 2.562 | 2.561 | 3.2 | 3.38 |
| erlang-mc | 4.609 | 4.450 | 4.686 | 38.9 | 6.03 |
| erlang | 4.662 | 4.484 | 4.656 | 36.9 | 6.10 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.526 | 0.517 | 0.527 | 2.1 | 0.54 |
| slight | 0.973 | 0.920 | 0.962 | 640.0 | 1.00 |
| erlang-mc | 1.081 | 0.850 | 3.436 | 357.5 | 1.11 |
| go-mc | 1.374 | 1.299 | 2.956 | 2.2 | 1.41 |
| erlang | 2.478 | 2.411 | 2.494 | 637.1 | 2.55 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.281 | 0.257 | 0.275 | 106.6 | 1.00 |
| go-mc | 0.549 | 0.534 | 1.312 | 30.7 | 1.96 |
| go | 0.835 | 0.767 | 0.830 | 163.3 | 2.97 |
| erlang | 1.370 | 1.337 | 1.384 | 112.5 | 4.88 |
| erlang-mc | 2.381 | 2.301 | 5.507 | 134.1 | 8.48 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.736 | 0.684 | 0.728 | 437.2 | 1.00 | 435 |
| go-mc | 4.295 | 3.388 | 8.928 | 2816.9 | 5.84 | 2815 |
| erlang | 4.798 | 4.500 | 4.791 | 2792.5 | 6.52 | 2756 |
| erlang-mc | 5.294 | 5.146 | 10.2 | 2790.8 | 7.19 | 2752 |
| go | 11.8 | 9.418 | 11.8 | 2790.6 | 16.08 | 2789 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.089 | 0.080 | 0.332 | 2.0 | 0.19 |
| go | 0.323 | 0.311 | 0.320 | 1.9 | 0.68 |
| erlang-mc | 0.336 | 0.321 | 0.972 | 38.4 | 0.70 |
| slight | 0.477 | 0.475 | 0.472 | 2.8 | 1.00 |
| erlang | 0.869 | 0.844 | 0.879 | 36.4 | 1.82 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.17 | 20288 |
| c/deriv | 0.18 | 17184 |
| c/fib | 0.12 | 16040 |
| c/n-body | 0.09 | 16368 |
| c/nqueens | 0.08 | 16144 |
| c/spectral-norm | 0.09 | 16168 |
| c/startup | 0.05 | 15960 |
| c/tak | 0.06 | 16040 |
| chez/binary-trees | 0.07 | 4113 |
| chez/deriv | 0.09 | 4401 |
| chez/fib | 0.08 | 2109 |
| chez/n-body | 0.09 | 11007 |
| chez/nqueens | 0.08 | 3431 |
| chez/spectral-norm | 0.07 | 5384 |
| chez/startup | 0.07 | 1312 |
| chez/tak | 0.07 | 2542 |
| erlang/binary-trees | 0.23 | 1464 |
| erlang/deriv | 0.26 | 1584 |
| erlang/fan-in | 0.27 | 1464 |
| erlang/fib | 0.27 | 900 |
| erlang/idle-memory | 0.24 | 1288 |
| erlang/n-body | 0.25 | 3824 |
| erlang/nqueens | 0.25 | 1204 |
| erlang/parallel-fib | 0.24 | 1404 |
| erlang/ping-pong | 0.25 | 1192 |
| erlang/spawn | 0.26 | 1260 |
| erlang/spectral-norm | 0.27 | 2220 |
| erlang/startup | 0.31 | 740 |
| erlang/tak | 0.34 | 988 |
| erlang/thread-ring | 0.23 | 1560 |
| go/binary-trees | 0.07 | 2231034 |
| go/deriv | 0.05 | 2236851 |
| go/fan-in | 0.05 | 2230271 |
| go/fib | 0.05 | 2230446 |
| go/idle-memory | 0.04 | 2230941 |
| go/n-body | 0.05 | 2231717 |
| go/nqueens | 0.04 | 2230990 |
| go/parallel-fib | 0.04 | 2231446 |
| go/ping-pong | 0.05 | 2230719 |
| go/spawn | 0.07 | 2230823 |
| go/spectral-norm | 0.05 | 2244557 |
| go/startup | 0.05 | 2209255 |
| go/tak | 0.05 | 2229998 |
| go/thread-ring | 0.04 | 2230931 |
| slight/binary-trees | 0.35 | 527536 |
| slight/deriv | 0.36 | 525552 |
| slight/fan-in | 0.35 | 517656 |
| slight/fib | 0.33 | 514768 |
| slight/idle-memory | 0.33 | 522464 |
| slight/n-body | 0.37 | 605856 |
| slight/nqueens | 0.36 | 518600 |
| slight/parallel-fib | 0.36 | 516560 |
| slight/ping-pong | 0.30 | 516400 |
| slight/spawn | 0.35 | 516720 |
| slight/spectral-norm | 0.40 | 540864 |
| slight/startup | 0.34 | 504144 |
| slight/tak | 0.34 | 515888 |
| slight/thread-ring | 0.37 | 521856 |
