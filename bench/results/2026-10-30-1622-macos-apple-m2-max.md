# Benchmarks, 2026-10-30 16:22:32

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: e46e023, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| rust | 0.291 | 0.288 | 0.291 | 1.6 | 0.84 |
| slight | 0.345 | 0.341 | 0.344 | 3.0 | 1.00 |
| go | 0.389 | 0.387 | 0.389 | 3.5 | 1.13 |
| erlang | 3.425 | 3.394 | 1.320 | 48.3 | 9.93 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.637 | 0.627 | 0.636 | 2.2 | 1.00 |
| rust | 0.696 | 0.695 | 0.696 | 2.5 | 1.09 |
| go | 1.031 | 1.024 | 1.031 | 5.1 | 1.62 |
| erlang | 8.374 | 8.177 | 3.112 | 49.3 | 13.15 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.143 | 0.141 | 0.142 | 66.4 | 1.00 |
| rust | 0.454 | 0.344 | 0.452 | 181.7 | 3.18 |
| go | 0.464 | 0.421 | 0.461 | 350.9 | 3.25 |
| erlang | 1.270 | 1.238 | 0.734 | 114.3 | 8.88 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.274 | 0.273 | 0.274 | 93.1 | 0.65 |
| go | 0.290 | 0.287 | 0.289 | 3.8 | 0.69 |
| slight | 0.420 | 0.418 | 0.415 | 642.8 | 1.00 |
| erlang | 1.757 | 1.679 | 1.710 | 650.0 | 4.18 |

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.158 | 0.158 | 0.158 | 1.6 | 0.28 |
| go | 0.198 | 0.197 | 0.197 | 3.5 | 0.35 |
| slight | 0.569 | 0.566 | 0.569 | 3.1 | 1.00 |
| erlang | 2.757 | 2.690 | 1.694 | 48.4 | 4.84 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| ping-pong | go-mc | 0.516 | 0.758 | 4.0 | 1.33 | 1.50 |
|  | erlang-mc | 0.997 | 1.140 | 59.9 | 0.29 | 2.89 |
|  | rust-mc | 11.8 | 14.7 | 2.4 | 40.39 | 34.11 |
| thread-ring | go-mc | 1.411 | 2.056 | 5.8 | 1.37 | 2.22 |
|  | erlang-mc | 2.437 | 2.549 | 60.8 | 0.29 | 3.83 |
|  | rust-mc | 1.182 | 1.927 | 3.3 | 1.70 | 1.86 |
| spawn | go-mc | 0.393 | 1.841 | 28.0 | 0.85 | 2.75 |
|  | erlang-mc | 0.908 | 5.692 | 188.4 | 0.72 | 6.35 |
|  | rust-mc | 0.610 | 1.933 | 12.0 | 1.34 | 4.27 |
| fan-in | go-mc | 3.576 | 12.0 | 5.2 | 12.34 | 8.50 |
|  | erlang-mc | 1.091 | 8.495 | 669.4 | 0.62 | 2.59 |
|  | rust-mc | 4.209 | 48.8 | 44.4 | 15.34 | 10.01 |
| payload | go-mc | 0.264 | 0.387 | 4.1 | 1.34 | 0.46 |
|  | erlang-mc | 1.433 | 1.617 | 60.9 | 0.52 | 2.52 |
|  | rust-mc | 6.043 | 7.436 | 2.5 | 38.13 | 10.61 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/fan-in | 0.16 | 1468 |
| erlang/payload | 0.17 | 1268 |
| erlang/ping-pong | 0.17 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/thread-ring | 0.16 | 1572 |
| go/fan-in | 0.10 | 2430258 |
| go/payload | 0.10 | 2430226 |
| go/ping-pong | 0.34 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.30 | 788208 |
| rust/payload | 0.30 | 789632 |
| rust/ping-pong | 2.11 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/thread-ring | 0.34 | 810560 |
| slight/fan-in | 0.26 | 696680 |
| slight/payload | 0.26 | 696664 |
| slight/ping-pong | 0.26 | 696680 |
| slight/spawn | 0.26 | 696664 |
| slight/thread-ring | 0.26 | 696696 |
