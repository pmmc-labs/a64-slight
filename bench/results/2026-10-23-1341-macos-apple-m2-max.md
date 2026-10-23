# Benchmarks, 2026-10-23 13:41:11

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: c8cbba4, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
- c: Apple clang version 17.0.0 (clang-1700.6.3.2)
- go: go version go1.27.2 darwin/arm64
- erlang: OTP 29, erts 17.1.1
- node: v26.9.0
- perl: perl v5.42.2
- python: Python 3.14.3
- chez: Chez Scheme 10.4.1
- rust: rustc 1.98.1 (48a229cea 2026-09-01) (Homebrew), tokio 1.54.0

Times are in seconds, the median of each one's runs (and the fastest), and include starting (see startup: Erlang's VM takes about a seventh of a second). CPU is user and system time together, which goes past the time taken when more than one core works; memory is the peak resident set, in MB. "÷ slight" is the median over slight's, so under 1 is faster than slight. Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table of their own, at the end.

## latency 10

How long a ping waited for its answer, from when it was due: idle, then beside a process that never waits (busy), then beside one holding a big heap (heap); each the median over the runs (D238).

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| slight | 0 µs | 4 µs | 4 µs | 6 µs | 18 µs | 225 µs | 56.5 ms | 60.9 ms | 599.8 |
| go | 0 µs | 1 µs | 11.5 ms | 24.4 ms | 24.5 ms | 4 µs | 20 µs | 20 µs | 166.5 |
| erlang | 5 µs | 12 µs | 28 µs | 41 µs | 50 µs | 8 µs | 69 µs | 84 µs | 1748.1 |
| rust | 0 µs | 0 µs | 138.6 ms | 273.6 ms | 275.6 ms | 2 µs | 9 µs | 9 µs | 83.7 |

Every core:

| | idle p50 | idle p99 | busy p50 | busy p99 | busy max | heap p50 | heap p99 | heap max | memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| go-mc | 0 µs | 9 µs | 1 µs | 10 µs | 14 µs | 1 µs | 9 µs | 9 µs | 168.1 |
| erlang-mc | 0 µs | 4 µs | 0 µs | 4 µs | 11 µs | 0 µs | 4 µs | 37 µs | 1709.4 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| erlang/latency | 0.18 | 3776 |
| go/latency | 0.33 | 2482354 |
| rust/latency | 0.73 | 887648 |
| slight/latency | 0.30 | 315336 |
