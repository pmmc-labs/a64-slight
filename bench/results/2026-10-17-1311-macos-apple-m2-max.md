# Benchmarks, 2026-10-17 13:11:57

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 448aa6d, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| c | 0.001 | 0.001 | 0.001 | 1.2 | 0.78 |
| slight | 0.002 | 0.002 | 0.001 | 1.4 | 1.00 |
| go-mc | 0.002 | 0.002 | 0.002 | 4.0 | 1.15 |
| go | 0.002 | 0.002 | 0.002 | 3.6 | 1.17 |
| perl | 0.003 | 0.003 | 0.002 | 3.3 | 1.58 |
| python | 0.017 | 0.017 | 0.016 | 12.0 | 9.53 |
| chez | 0.034 | 0.034 | 0.034 | 51.8 | 19.34 |
| node | 0.053 | 0.050 | 0.052 | 41.3 | 29.75 |
| erlang-mc | 0.087 | 0.084 | 0.243 | 59.7 | 48.77 |
| erlang | 0.091 | 0.088 | 0.085 | 48.3 | 51.22 |

## fib 38

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.110 | 0.107 | 0.110 | 1.2 | 0.55 |
| go | 0.123 | 0.123 | 0.123 | 3.5 | 0.62 |
| go-mc | 0.123 | 0.123 | 0.123 | 4.1 | 0.62 |
| slight | 0.200 | 0.199 | 0.199 | 1.4 | 1.00 |
| chez | 0.225 | 0.220 | 0.224 | 51.8 | 1.13 |
| node | 0.315 | 0.312 | 0.315 | 43.7 | 1.58 |
| erlang-mc | 0.361 | 0.357 | 0.523 | 59.9 | 1.81 |
| erlang | 0.686 | 0.667 | 0.403 | 48.4 | 3.44 |
| python | 4.645 | 4.550 | 4.643 | 11.8 | 23.28 |
| perl | 12.8 | 12.7 | 12.8 | 4.2 | 64.10 |

## tak 10

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.082 | 0.082 | 0.082 | 1.2 | 0.58 |
| go | 0.102 | 0.102 | 0.102 | 3.5 | 0.72 |
| go-mc | 0.102 | 0.102 | 0.102 | 4.0 | 0.72 |
| slight | 0.141 | 0.141 | 0.141 | 1.4 | 1.00 |
| chez | 0.189 | 0.187 | 0.188 | 51.8 | 1.33 |
| node | 0.286 | 0.286 | 0.287 | 43.9 | 2.02 |
| erlang-mc | 0.288 | 0.279 | 0.448 | 60.0 | 2.04 |
| erlang | 0.512 | 0.504 | 0.301 | 48.5 | 3.62 |
| python | 3.605 | 3.506 | 3.604 | 11.8 | 25.51 |
| perl | 14.0 | 13.9 | 14.0 | 4.3 | 98.82 |

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.345 | 0.340 | 0.344 | 65.8 | 0.34 |
| erlang-mc | 0.497 | 0.493 | 0.657 | 171.8 | 0.48 |
| node | 0.551 | 0.546 | 0.669 | 371.0 | 0.54 |
| go-mc | 0.665 | 0.657 | 1.869 | 41.6 | 0.65 |
| erlang | 0.884 | 0.848 | 0.565 | 172.1 | 0.86 |
| c | 0.999 | 0.988 | 0.998 | 20.0 | 0.97 |
| slight | 1.028 | 1.023 | 1.027 | 172.7 | 1.00 |
| go | 1.376 | 1.366 | 1.373 | 62.0 | 1.34 |
| python | 5.919 | 5.865 | 5.918 | 45.5 | 5.76 |
| perl | 17.9 | 17.8 | 17.9 | 142.1 | 17.40 |

## nqueens 13

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.309 | 0.301 | 0.308 | 1.2 | 0.44 |
| chez | 0.388 | 0.381 | 0.387 | 51.8 | 0.55 |
| erlang-mc | 0.476 | 0.468 | 0.637 | 59.9 | 0.68 |
| go-mc | 0.548 | 0.538 | 0.592 | 10.7 | 0.78 |
| go | 0.578 | 0.577 | 0.572 | 8.9 | 0.82 |
| erlang | 0.667 | 0.645 | 0.498 | 48.6 | 0.95 |
| slight | 0.705 | 0.703 | 0.705 | 3.6 | 1.00 |
| node | 2.296 | 2.273 | 2.307 | 62.5 | 3.26 |
| python | 6.972 | 6.812 | 6.969 | 11.9 | 9.89 |
| perl | 13.9 | 13.8 | 13.9 | 5.2 | 19.70 |

## deriv 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.134 | 0.133 | 0.133 | 1.2 | 0.35 |
| chez | 0.185 | 0.184 | 0.184 | 51.8 | 0.48 |
| erlang-mc | 0.320 | 0.315 | 0.473 | 60.2 | 0.84 |
| slight | 0.382 | 0.380 | 0.381 | 3.7 | 1.00 |
| erlang | 0.506 | 0.502 | 0.334 | 48.4 | 1.33 |
| node | 0.620 | 0.619 | 0.669 | 56.6 | 1.63 |
| go-mc | 0.802 | 0.797 | 0.915 | 11.4 | 2.10 |
| go | 1.070 | 1.061 | 1.055 | 8.7 | 2.81 |
| python | 4.059 | 4.038 | 4.058 | 11.9 | 10.64 |
| perl | 8.144 | 8.115 | 8.143 | 4.5 | 21.35 |

## n-body 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.038 | 0.038 | 0.038 | 1.2 | 0.03 |
| go | 0.048 | 0.048 | 0.048 | 3.5 | 0.04 |
| go-mc | 0.048 | 0.048 | 0.048 | 3.9 | 0.04 |
| node | 0.117 | 0.117 | 0.121 | 47.2 | 0.09 |
| chez | 1.167 | 1.161 | 1.165 | 51.8 | 0.90 |
| erlang-mc | 1.203 | 1.195 | 1.356 | 60.2 | 0.93 |
| slight | 1.297 | 1.287 | 1.296 | 4.6 | 1.00 |
| erlang | 1.318 | 1.280 | 1.225 | 48.5 | 1.02 |
| python | 5.772 | 5.636 | 5.770 | 12.3 | 4.45 |
| perl | 7.526 | 7.404 | 7.516 | 5.9 | 5.80 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.047 | 0.046 | 0.046 | 1.2 | 0.11 |
| go | 0.047 | 0.047 | 0.047 | 3.6 | 0.11 |
| go-mc | 0.047 | 0.047 | 0.047 | 4.0 | 0.11 |
| node | 0.119 | 0.118 | 0.123 | 45.9 | 0.28 |
| slight | 0.432 | 0.431 | 0.431 | 3.7 | 1.00 |
| erlang-mc | 0.489 | 0.485 | 0.655 | 60.4 | 1.13 |
| chez | 0.576 | 0.573 | 0.574 | 51.8 | 1.33 |
| erlang | 0.681 | 0.655 | 0.526 | 48.7 | 1.58 |
| python | 5.149 | 5.142 | 5.147 | 12.5 | 11.93 |
| perl | 7.615 | 7.580 | 7.614 | 7.0 | 17.64 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.328 | 0.326 | 0.327 | 2.6 | 1.00 |
| go | 0.387 | 0.385 | 0.387 | 3.5 | 1.18 |
| go-mc | 0.539 | 0.522 | 0.791 | 4.1 | 1.65 |
| erlang-mc | 1.012 | 0.987 | 1.170 | 59.7 | 3.09 |
| erlang | 3.394 | 3.292 | 1.285 | 48.5 | 10.36 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.526 | 0.523 | 0.526 | 2.0 | 1.00 |
| go | 1.050 | 1.030 | 1.049 | 5.1 | 2.00 |
| go-mc | 1.471 | 1.435 | 2.149 | 5.8 | 2.80 |
| erlang-mc | 2.410 | 2.305 | 2.569 | 60.8 | 4.58 |
| erlang | 7.962 | 7.844 | 2.802 | 49.2 | 15.14 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go | 0.289 | 0.287 | 0.289 | 3.8 | 0.77 |
| slight | 0.375 | 0.365 | 0.370 | 483.5 | 1.00 |
| erlang-mc | 1.103 | 1.088 | 8.675 | 675.6 | 2.94 |
| erlang | 1.692 | 1.605 | 1.648 | 650.0 | 4.51 |
| go-mc | 3.437 | 3.383 | 11.5 | 5.2 | 9.17 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.131 | 0.130 | 0.130 | 90.1 | 1.00 |
| go-mc | 0.390 | 0.383 | 1.857 | 27.7 | 2.97 |
| go | 0.441 | 0.421 | 0.438 | 360.8 | 3.36 |
| erlang-mc | 0.911 | 0.899 | 5.909 | 195.4 | 6.94 |
| erlang | 1.244 | 1.234 | 0.711 | 114.0 | 9.48 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight | bytes a process |
|---|---:|---:|---:|---:|---:|---:|
| slight | 0.341 | 0.330 | 0.339 | 556.0 | 1.00 | 555 |
| go-mc | 1.099 | 0.902 | 6.433 | 2911.6 | 3.22 | 2908 |
| go | 1.517 | 1.397 | 1.515 | 2889.8 | 4.45 | 2886 |
| erlang-mc | 1.896 | 1.889 | 6.491 | 2795.1 | 5.56 | 2735 |
| erlang | 2.968 | 2.918 | 1.899 | 2789.7 | 8.71 | 2741 |

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| go-mc | 0.021 | 0.021 | 0.199 | 5.0 | 0.07 |
| erlang-mc | 0.139 | 0.135 | 0.719 | 60.0 | 0.49 |
| go | 0.169 | 0.167 | 0.168 | 3.7 | 0.60 |
| slight | 0.283 | 0.281 | 0.282 | 3.6 | 1.00 |
| erlang | 0.885 | 0.855 | 0.516 | 48.5 | 3.13 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| c/deriv | 0.06 | 50440 |
| c/fib | 0.05 | 33472 |
| c/n-body | 0.06 | 50008 |
| c/nqueens | 0.06 | 33720 |
| c/spectral-norm | 0.06 | 33584 |
| c/startup | 0.05 | 33432 |
| c/tak | 0.05 | 33472 |
| chez/binary-trees | 0.04 | 4089 |
| chez/deriv | 0.04 | 4235 |
| chez/fib | 0.04 | 2166 |
| chez/n-body | 0.04 | 11316 |
| chez/nqueens | 0.04 | 3418 |
| chez/spectral-norm | 0.04 | 5509 |
| chez/startup | 0.04 | 1412 |
| chez/tak | 0.04 | 2582 |
| erlang/binary-trees | 0.17 | 1516 |
| erlang/deriv | 0.17 | 1612 |
| erlang/fan-in | 0.17 | 1468 |
| erlang/fib | 0.16 | 928 |
| erlang/idle-memory | 0.16 | 1312 |
| erlang/n-body | 0.18 | 4064 |
| erlang/nqueens | 0.16 | 1200 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/ping-pong | 0.17 | 1196 |
| erlang/spawn | 0.17 | 1264 |
| erlang/spectral-norm | 0.16 | 2228 |
| erlang/startup | 0.16 | 764 |
| erlang/tak | 0.16 | 996 |
| erlang/thread-ring | 0.16 | 1572 |
| go/binary-trees | 0.10 | 2430210 |
| go/deriv | 0.10 | 2430482 |
| go/fan-in | 0.10 | 2430258 |
| go/fib | 0.10 | 2430130 |
| go/idle-memory | 0.09 | 2430178 |
| go/n-body | 0.10 | 2430274 |
| go/nqueens | 0.13 | 2430130 |
| go/parallel-fib | 0.10 | 2430194 |
| go/ping-pong | 0.09 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/spectral-norm | 0.10 | 2430338 |
| go/startup | 0.28 | 2429698 |
| go/tak | 0.10 | 2430130 |
| go/thread-ring | 0.10 | 2430242 |
| slight/binary-trees | 0.21 | 205472 |
| slight/deriv | 0.21 | 205320 |
| slight/fan-in | 0.21 | 205400 |
| slight/fib | 0.20 | 205072 |
| slight/idle-memory | 0.21 | 205512 |
| slight/n-body | 0.24 | 255736 |
| slight/nqueens | 0.23 | 205192 |
| slight/parallel-fib | 0.21 | 205104 |
| slight/ping-pong | 0.21 | 205400 |
| slight/spawn | 0.20 | 205384 |
| slight/spectral-norm | 0.22 | 222080 |
| slight/startup | 1.14 | 205000 |
| slight/tak | 0.21 | 205072 |
| slight/thread-ring | 0.21 | 205432 |
