# Benchmarks, 2026-10-30 22:31:07

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 4ca8e3e, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-arrays: Apple clang version 17.0.0 (clang-1700.6.3.2)
- slight-mc: Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.132 | 0.130 | 0.131 | 51.1 | 1.00 |
| go | 0.422 | 0.420 | 0.422 | 340.5 | 3.20 |
| rust | 0.661 | 0.364 | 0.660 | 182.2 | 5.01 |
| erlang | 1.272 | 1.261 | 0.736 | 114.0 | 9.65 |

## idle-memory 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.317 | 0.314 | 0.316 | 615.2 | 1.00 |
| rust | 0.748 | 0.740 | 0.747 | 1106.2 | 2.36 |
| go | 1.508 | 1.474 | 1.503 | 2889.8 | 4.75 |
| erlang | 2.958 | 2.940 | 1.895 | 2790.0 | 9.32 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.281 | 0.278 | 0.280 | 2.9 | 1.00 |
| rust | 0.291 | 0.283 | 0.290 | 1.7 | 1.04 |
| go | 0.387 | 0.374 | 0.387 | 3.6 | 1.38 |
| erlang | 3.423 | 3.399 | 1.315 | 48.5 | 12.18 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| spawn | slight-mc | 0.026 | 0.200 | 59.2 | 0.20 | 0.20 |
|  | go-mc | 0.388 | 1.864 | 28.1 | 0.92 | 2.94 |
|  | erlang-mc | 0.908 | 5.634 | 198.0 | 0.71 | 6.89 |
|  | rust-mc | 0.606 | 1.905 | 12.0 | 0.92 | 4.60 |
| idle-memory | slight-mc | 0.101 | 0.878 | 621.1 | 0.32 | 0.32 |
|  | go-mc | 1.051 | 6.122 | 2890.4 | 0.70 | 3.31 |
|  | erlang-mc | 1.914 | 6.437 | 2795.2 | 0.65 | 6.03 |
|  | rust-mc | 1.408 | 7.679 | 1097.2 | 1.88 | 4.44 |
| ping-pong | slight-mc | 0.064 | 0.650 | 17.9 | 0.23 | 0.23 |
|  | go-mc | 0.515 | 0.759 | 4.0 | 1.33 | 1.83 |
|  | erlang-mc | 0.971 | 1.115 | 59.9 | 0.28 | 3.46 |
|  | rust-mc | 11.9 | 14.8 | 2.5 | 40.78 | 42.22 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/idle-memory | 0.15 | 1312 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.15 | 1264 |
| go/idle-memory | 0.10 | 2430178 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| rust/idle-memory | 0.33 | 817760 |
| rust/ping-pong | 0.29 | 787280 |
| rust/spawn | 1.60 | 785104 |
| slight-mc/idle-memory | 0.27 | 715576 |
| slight-mc/ping-pong | 0.26 | 715144 |
| slight-mc/spawn | 0.26 | 715144 |
| slight/idle-memory | 0.26 | 698648 |
| slight/ping-pong | 0.26 | 698536 |
| slight/spawn | 2.91 | 698536 |
