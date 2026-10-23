# Benchmarks, 2026-10-23 15:16:40

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: b56a719, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## binary-trees 18

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| chez | 0.342 | 0.341 | 0.341 | 65.8 | 0.43 |
| node | 0.555 | 0.552 | 0.674 | 367.1 | 0.70 |
| slight | 0.789 | 0.786 | 0.788 | 141.8 | 1.00 |
| erlang | 0.865 | 0.848 | 0.545 | 172.4 | 1.10 |
| c | 0.994 | 0.980 | 0.993 | 20.0 | 1.26 |
| go | 1.375 | 1.369 | 1.372 | 60.4 | 1.74 |
| python | 5.911 | 5.892 | 5.909 | 45.8 | 7.49 |
| perl | 17.8 | 17.7 | 17.8 | 142.1 | 22.52 |

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 5 µs | 4 µs | 5 µs | 12 µs | 121 µs | 43.9 ms | 46.9 ms | 626.9 |
| go | 0 µs | 1 µs | 12.3 ms | 24.5 ms | 24.5 ms | 3 µs | 13 µs | 13 µs | 166.5 |
| erlang | 5 µs | 13 µs | 28 µs | 40 µs | 51 µs | 8 µs | 72 µs | 134 µs | 1748.1 |
| rust | 0 µs | 0 µs | 142.1 ms | 281.0 ms | 283.0 ms | 2 µs | 6 µs | 6 µs | 83.7 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 9 µs | 2 µs | 9 µs | 13 µs | 1 µs | 11 µs | 11 µs | 168.0 |
| erlang-mc | 0 µs | 4 µs | 0 µs | 4 µs | 13 µs | 0 µs | 6 µs | 55 µs | 1709.2 |

## Every core

Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| binary-trees | go-mc | 0.663 | 1.873 | 40.8 | 0.48 | 0.84 |
|  | erlang-mc | 0.503 | 0.667 | 171.3 | 0.58 | 0.64 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/binary-trees | 0.05 | 33632 |
| chez/binary-trees | 0.04 | 4089 |
| erlang/binary-trees | 0.16 | 1516 |
| erlang/latency | 0.17 | 3776 |
| go/binary-trees | 0.05 | 2430210 |
| go/latency | 0.04 | 2482354 |
| rust/latency | 0.02 | 887648 |
| slight/binary-trees | 0.21 | 314672 |
| slight/latency | 0.22 | 331752 |
