# Benchmarks, 2026-10-30 21:51:17

Apple M2 Max, arm64, 12 cores, 32 GB, macOS 15.7.9.

- slight: 02f57de, its runtime built by Apple clang version 17.0.0 (clang-1700.6.3.2)
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

## parallel-fib 30

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.147 | 0.147 | 0.147 | 1.6 | 0.52 |
| go | 0.167 | 0.166 | 0.166 | 3.7 | 0.59 |
| slight | 0.283 | 0.282 | 0.282 | 3.7 | 1.00 |
| erlang | 0.915 | 0.907 | 0.546 | 48.5 | 3.23 |

## fan-in 10000000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| rust | 0.266 | 0.265 | 0.265 | 93.1 | 0.73 |
| go | 0.287 | 0.286 | 0.286 | 3.8 | 0.79 |
| slight | 0.362 | 0.360 | 0.360 | 483.7 | 1.00 |
| erlang | 1.838 | 1.670 | 1.795 | 651.2 | 5.07 |

## spectral-norm 1000

| | time | fastest | CPU | memory | ÷ slight |
|---|---:|---:|---:|---:|---:|
| c | 0.046 | 0.046 | 0.046 | 1.2 | 0.10 |
| go | 0.047 | 0.047 | 0.047 | 3.6 | 0.10 |
| slight-arrays | 0.084 | 0.084 | 0.083 | 43.3 | 0.18 |
| node | 0.119 | 0.117 | 0.123 | 46.2 | 0.26 |
| slight | 0.467 | 0.465 | 0.466 | 3.8 | 1.00 |
| chez | 0.573 | 0.573 | 0.572 | 51.8 | 1.23 |
| erlang | 0.701 | 0.679 | 0.547 | 48.6 | 1.50 |
| python | 5.131 | 5.125 | 5.130 | 12.3 | 10.99 |
| perl | 7.600 | 7.567 | 7.600 | 6.2 | 16.28 |

## Every core

slight on every island, one per core (D256), and Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same language's on one core (slight-arrays' for slight-mc's spectral-norm), so under 1 is what every core gains, over 1 what it costs; "÷ slight" is the median over slight's, on its one core.

| | | time | CPU | memory | ÷ one core | ÷ slight |
|---|---|---:|---:|---:|---:|---:|
| parallel-fib | slight-mc | 0.036 | 0.344 | 5.9 | 0.13 | 0.13 |
|  | go-mc | 0.021 | 0.200 | 5.0 | 0.13 | 0.07 |
|  | erlang-mc | 0.138 | 0.712 | 60.0 | 0.15 | 0.49 |
|  | rust-mc | 0.018 | 0.174 | 2.5 | 0.12 | 0.06 |
| fan-in | slight-mc | 1.296 | 13.0 | 76.7 | 3.58 | 3.58 |
|  | go-mc | 3.528 | 11.9 | 5.3 | 12.30 | 9.74 |
|  | erlang-mc | 1.099 | 8.620 | 673.4 | 0.60 | 3.03 |
|  | rust-mc | 4.155 | 48.5 | 42.6 | 15.62 | 11.47 |
| spectral-norm | slight-mc | 0.018 | 0.135 | 69.6 | 0.22 | 0.04 |
|  | go-mc | 0.047 | 0.047 | 4.0 | 1.00 | 0.10 |
|  | erlang-mc | 0.486 | 0.627 | 60.2 | 0.69 | 1.04 |

## Builds

| | seconds | bytes |
|---|---:|---:|
| c/spectral-norm | 0.06 | 33584 |
| chez/spectral-norm | 0.04 | 5509 |
| erlang/fan-in | 0.15 | 1468 |
| erlang/parallel-fib | 0.15 | 1412 |
| erlang/spectral-norm | 0.16 | 2228 |
| go/fan-in | 0.04 | 2430258 |
| go/parallel-fib | 0.04 | 2430194 |
| go/spectral-norm | 0.04 | 2430338 |
| rust/fan-in | 0.02 | 788208 |
| rust/parallel-fib | 0.02 | 787920 |
| slight-arrays/spectral-norm | 0.26 | 714864 |
| slight-mc/fan-in | 0.26 | 698552 |
| slight-mc/parallel-fib | 0.26 | 698336 |
| slight-mc/spectral-norm | 0.27 | 715216 |
| slight/fan-in | 0.26 | 698536 |
| slight/parallel-fib | 0.26 | 698160 |
| slight/spectral-norm | 0.27 | 715120 |
