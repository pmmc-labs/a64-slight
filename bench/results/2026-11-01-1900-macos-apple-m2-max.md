# Benchmarks, 2026-11-01 19:00:38

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 3779e8e, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-arrays: Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-mc: Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3, SimPy 4.1.2
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## bank 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.024 | 0.024 | 0.024 | 2.3 | 0.04 |
| slight | 0.577 | 0.567 | 0.576 | 74.6 | 1.00 |
| python | 6.200 | 6.157 | 6.199 | 19.8 | 10.75 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/bank | 0.06 | 33592 |
| slight/bank | 0.35 | 715416 |
