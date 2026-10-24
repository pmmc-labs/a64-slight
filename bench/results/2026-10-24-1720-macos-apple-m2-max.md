# Benchmarks, 2026-10-24 17:20:30

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 118291f, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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
| slight | 0.304 | 0.296 | 0.302 | 615.1 | 1.00 |
| rust | 0.734 | 0.730 | 0.733 | 1106.2 | 2.42 |
| go | 1.555 | 1.455 | 1.544 | 2890.0 | 5.12 |
| erlang | 2.933 | 2.914 | 1.868 | 2790.0 | 9.66 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.122 | 0.121 | 0.122 | 50.3 | 1.00 |
| go | 0.416 | 0.410 | 0.412 | 359.3 | 3.39 |
| rust | 0.424 | 0.268 | 0.422 | 182.3 | 3.46 |
| erlang | 1.245 | 1.233 | 0.712 | 114.2 | 10.17 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| idle-memory | go-mc | 1.024 | 5.992 | 2888.2 | 0.66 | 3.37 |
|  | erlang-mc | 1.890 | 6.414 | 2796.3 | 0.64 | 6.23 |
|  | rust-mc | 1.396 | 7.572 | 1097.4 | 1.90 | 4.60 |
| spawn | go-mc | 0.387 | 1.840 | 27.7 | 0.93 | 3.16 |
|  | erlang-mc | 0.907 | 5.632 | 189.6 | 0.73 | 7.41 |
|  | rust-mc | 0.611 | 1.940 | 12.0 | 1.44 | 4.99 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/idle-memory | 0.15 | 1312 |
| erlang/spawn | 0.15 | 1264 |
| go/idle-memory | 0.05 | 2430178 |
| go/spawn | 0.04 | 2430210 |
| rust/idle-memory | 0.03 | 817760 |
| rust/spawn | 0.02 | 785104 |
| slight/idle-memory | 1.50 | 333320 |
| slight/spawn | 0.21 | 333208 |
