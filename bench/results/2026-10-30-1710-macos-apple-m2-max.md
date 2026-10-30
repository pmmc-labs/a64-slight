# Benchmarks, 2026-10-30 17:10:30

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: ebb2312, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-arrays: Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.34.1
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.282 | 0.280 | 0.282 | 3.0 | 1.00 |
| rust | 0.291 | 0.291 | 0.291 | 1.6 | 1.03 |
| go | 0.387 | 0.384 | 0.386 | 3.5 | 1.37 |
| erlang | 3.411 | 3.389 | 1.308 | 48.5 | 12.09 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.506 | 0.503 | 0.505 | 2.2 | 1.00 |
| rust | 0.699 | 0.696 | 0.698 | 2.4 | 1.38 |
| go | 1.035 | 1.029 | 1.034 | 5.1 | 2.05 |
| erlang | 8.205 | 8.168 | 3.086 | 49.3 | 16.23 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.145 | 0.144 | 0.144 | 66.4 | 1.00 |
| rust | 0.386 | 0.245 | 0.385 | 182.8 | 2.66 |
| go | 0.424 | 0.418 | 0.423 | 338.0 | 2.92 |
| erlang | 1.283 | 1.269 | 0.747 | 114.1 | 8.84 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.277 | 0.272 | 0.277 | 93.1 | 0.67 |
| go | 0.288 | 0.287 | 0.288 | 3.8 | 0.70 |
| slight | 0.412 | 0.401 | 0.404 | 642.8 | 1.00 |
| erlang | 1.731 | 1.575 | 1.687 | 649.9 | 4.21 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.158 | 0.158 | 0.158 | 1.6 | 0.29 |
| go | 0.198 | 0.198 | 0.198 | 3.5 | 0.36 |
| slight | 0.556 | 0.554 | 0.554 | 3.1 | 1.00 |
| erlang | 2.736 | 2.726 | 1.676 | 48.3 | 4.92 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| ping-pong | go-mc | 0.523 | 0.770 | 4.0 | 1.35 | 1.85 |
|  | erlang-mc | 0.996 | 1.146 | 59.9 | 0.29 | 3.53 |
|  | rust-mc | 11.7 | 14.7 | 2.5 | 40.28 | 41.57 |
| thread-ring | go-mc | 1.415 | 2.065 | 5.7 | 1.37 | 2.80 |
|  | erlang-mc | 2.390 | 2.537 | 60.3 | 0.29 | 4.73 |
|  | rust-mc | 1.192 | 1.934 | 3.3 | 1.71 | 2.36 |
| spawn | go-mc | 0.392 | 1.863 | 27.9 | 0.92 | 2.70 |
|  | erlang-mc | 0.920 | 5.720 | 190.3 | 0.72 | 6.34 |
|  | rust-mc | 0.613 | 1.953 | 12.0 | 1.59 | 4.23 |
| fan-in | go-mc | 3.574 | 12.1 | 5.2 | 12.40 | 8.69 |
|  | erlang-mc | 1.103 | 8.580 | 657.6 | 0.64 | 2.68 |
|  | rust-mc | 4.172 | 48.7 | 46.3 | 15.04 | 10.14 |
| payload | go-mc | 0.266 | 0.390 | 4.0 | 1.34 | 0.48 |
|  | erlang-mc | 1.432 | 1.647 | 61.6 | 0.52 | 2.58 |
|  | rust-mc | 5.962 | 7.433 | 2.4 | 37.65 | 10.73 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/fan-in | 0.16 | 1468 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/thread-ring | 0.16 | 1572 |
| go/fan-in | 0.10 | 2430258 |
| go/payload | 0.10 | 2430226 |
| go/ping-pong | 0.29 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 1.92 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/thread-ring | 0.34 | 810560 |
| slight/fan-in | 0.27 | 696856 |
| slight/payload | 0.27 | 696856 |
| slight/ping-pong | 2.98 | 696856 |
| slight/spawn | 0.26 | 696840 |
| slight/thread-ring | 0.26 | 696872 |
