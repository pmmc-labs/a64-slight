# Benchmarks, 2026-10-24 18:56:21

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 347bbc7, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.34.1
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.316 | 0.298 | 0.313 | 598.9 | 1.00 |
| rust | 0.792 | 0.761 | 0.781 | 1105.2 | 2.51 |
| go | 1.559 | 1.523 | 1.550 | 2890.4 | 4.94 |
| erlang | 3.075 | 2.938 | 2.004 | 2789.9 | 9.73 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| idle-memory | go-mc | 1.075 | 6.271 | 2878.0 | 0.69 | 3.40 |
|  | erlang-mc | 1.935 | 6.535 | 2795.1 | 0.63 | 6.12 |
|  | rust-mc | 1.405 | 7.288 | 1097.0 | 1.77 | 4.45 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/idle-memory | 0.16 | 1312 |
| go/idle-memory | 0.17 | 2430178 |
| rust/idle-memory | 0.09 | 817760 |
| slight/idle-memory | 1.66 | 333320 |
