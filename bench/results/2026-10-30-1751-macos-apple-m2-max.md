# Benchmarks, 2026-10-30 17:51:30

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 82dda3f, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.142 | 0.141 | 0.141 | 51.2 | 1.00 |
| rust | 0.408 | 0.256 | 0.407 | 182.8 | 2.87 |
| go | 0.428 | 0.424 | 0.425 | 333.7 | 3.01 |
| erlang | 1.279 | 1.268 | 0.741 | 114.0 | 9.02 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.276 | 0.274 | 0.275 | 93.1 | 0.74 |
| go | 0.290 | 0.288 | 0.289 | 3.8 | 0.78 |
| slight | 0.373 | 0.364 | 0.370 | 483.7 | 1.00 |
| erlang | 1.868 | 1.710 | 1.826 | 649.9 | 5.01 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.280 | 0.278 | 0.279 | 2.9 | 1.00 |
| rust | 0.295 | 0.291 | 0.295 | 1.7 | 1.06 |
| go | 0.389 | 0.385 | 0.389 | 3.5 | 1.39 |
| erlang | 3.553 | 3.407 | 1.378 | 48.6 | 12.71 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| spawn | go-mc | 0.392 | 1.891 | 27.5 | 0.92 | 2.76 |
|  | erlang-mc | 0.927 | 5.708 | 197.4 | 0.72 | 6.54 |
|  | rust-mc | 0.615 | 1.951 | 12.0 | 1.51 | 4.34 |
| fan-in | go-mc | 3.619 | 12.2 | 5.2 | 12.49 | 9.70 |
|  | erlang-mc | 1.077 | 8.410 | 656.8 | 0.58 | 2.89 |
|  | rust-mc | 4.179 | 48.6 | 47.6 | 15.15 | 11.20 |
| ping-pong | go-mc | 0.522 | 0.770 | 4.0 | 1.34 | 1.87 |
|  | erlang-mc | 1.003 | 1.139 | 59.8 | 0.28 | 3.59 |
|  | rust-mc | 11.8 | 14.7 | 2.4 | 40.10 | 42.34 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/fan-in | 0.16 | 1468 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.17 | 1264 |
| go/fan-in | 0.10 | 2430258 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.29 | 2430210 |
| rust/fan-in | 0.30 | 788208 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 1.97 | 785104 |
| slight/fan-in | 0.26 | 696856 |
| slight/ping-pong | 0.27 | 696856 |
| slight/spawn | 2.92 | 696840 |
