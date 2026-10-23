# Benchmarks, 2026-10-23 17:01:28

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: b91d32a, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## payload 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.158 | 0.158 | 0.157 | 1.6 | 0.28 |
| go | 0.199 | 0.197 | 0.199 | 3.5 | 0.36 |
| slight | 0.556 | 0.555 | 0.555 | 3.0 | 1.00 |
| erlang | 2.610 | 2.583 | 1.557 | 48.5 | 4.70 |

## ping-pong 2000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.292 | 0.291 | 0.291 | 1.6 | 0.97 |
| slight | 0.301 | 0.301 | 0.301 | 2.7 | 1.00 |
| go | 0.387 | 0.384 | 0.386 | 3.5 | 1.28 |
| erlang | 3.357 | 3.239 | 1.256 | 48.5 | 11.14 |

## spawn 1000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.126 | 0.123 | 0.125 | 90.2 | 1.00 |
| go | 0.412 | 0.410 | 0.409 | 346.9 | 3.26 |
| erlang | 1.242 | 1.219 | 0.709 | 114.2 | 9.83 |
| rust | 1.627 | 0.247 | 1.627 | 181.2 | 12.88 |

## thread-ring 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| slight | 0.509 | 0.505 | 0.508 | 2.0 | 1.00 |
| rust | 0.693 | 0.692 | 0.692 | 2.5 | 1.36 |
| go | 1.054 | 1.027 | 1.054 | 5.1 | 2.07 |
| erlang | 7.905 | 7.886 | 2.754 | 49.0 | 15.53 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.268 | 0.266 | 0.268 | 93.1 | 0.76 |
| go | 0.288 | 0.286 | 0.288 | 3.9 | 0.82 |
| slight | 0.352 | 0.347 | 0.347 | 483.6 | 1.00 |
| erlang | 1.650 | 1.579 | 1.607 | 649.9 | 4.69 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| payload | go-mc | 0.265 | 0.389 | 4.1 | 1.33 | 0.48 |
|  | erlang-mc | 1.428 | 1.645 | 61.0 | 0.55 | 2.57 |
|  | rust-mc | 5.892 | 7.330 | 2.4 | 37.35 | 10.60 |
| ping-pong | go-mc | 0.520 | 0.765 | 4.1 | 1.35 | 1.73 |
|  | erlang-mc | 1.051 | 1.197 | 59.9 | 0.31 | 3.49 |
|  | rust-mc | 11.7 | 14.6 | 2.5 | 40.13 | 38.84 |
| spawn | go-mc | 0.383 | 1.851 | 27.9 | 0.93 | 3.03 |
|  | erlang-mc | 0.906 | 5.801 | 197.7 | 0.73 | 7.17 |
|  | rust-mc | 0.602 | 1.888 | 11.9 | 0.37 | 4.77 |
| thread-ring | go-mc | 1.424 | 2.071 | 5.8 | 1.35 | 2.80 |
|  | erlang-mc | 2.333 | 2.494 | 60.4 | 0.30 | 4.58 |
|  | rust-mc | 1.181 | 1.920 | 3.3 | 1.70 | 2.32 |
| fan-in | go-mc | 3.612 | 12.3 | 5.2 | 12.54 | 10.27 |
|  | erlang-mc | 1.084 | 8.496 | 654.9 | 0.66 | 3.08 |
|  | rust-mc | 4.162 | 48.5 | 46.4 | 15.53 | 11.84 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/fan-in | 0.16 | 1468 |
| erlang/payload | 0.16 | 1268 |
| erlang/ping-pong | 0.16 | 1196 |
| erlang/spawn | 0.16 | 1264 |
| erlang/thread-ring | 0.16 | 1572 |
| go/fan-in | 0.10 | 2430258 |
| go/payload | 0.32 | 2430226 |
| go/ping-pong | 0.10 | 2430210 |
| go/spawn | 0.10 | 2430210 |
| go/thread-ring | 0.10 | 2430242 |
| rust/fan-in | 0.31 | 788208 |
| rust/payload | 0.59 | 789632 |
| rust/ping-pong | 0.30 | 787280 |
| rust/spawn | 0.32 | 785104 |
| rust/thread-ring | 0.34 | 810560 |
| slight/fan-in | 0.21 | 314376 |
| slight/payload | 1.64 | 314360 |
| slight/ping-pong | 0.22 | 314376 |
| slight/spawn | 0.21 | 314360 |
| slight/thread-ring | 0.22 | 314392 |
