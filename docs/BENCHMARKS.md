# Benchmarks

How slight performs, and how it compares with other languages (D194).
`bench/` has the programs and the harness; `bench/results/` has every
run worth keeping, as JSON and as a Markdown summary.

    make bench                  # or: python3 bench/run.py
    make bench BENCH=--quick    # small sizes, to see that everything works
    python3 bench/run.py --only fib,n-body --langs slight,c --runs 10

A full run takes about an hour, mostly Perl and Python. Languages whose
tools aren't installed are left out, and said to be. On a Mac, `brew
install go erlang chezscheme`; Node, Perl and Python are there already
(Node for the compiler, the others with Xcode's tools). On Debian or
Ubuntu, `apt-get install chezscheme`. Rust needs cargo and rustc (from
rustup), and its first build fetches tokio from crates.io.

## What's measured

Three questions, which need different programs:

1. **Sequential code:** how good the compiled code is at calls, integer
   arithmetic, allocating and collecting, and floats. Against C, the
   floor; Go and Erlang; Node, the everyday reference; Perl and Python;
   and Chez Scheme, a native Lisp, the nearest thing to slight's
   compiled code (D201).
2. **The actor runtime:** what a message, a switch between processes, a
   process started, and a process waiting cost, and what copying a
   message's data costs (D237). Against Erlang, the same
   model (per-process heaps, copied messages, preemption by reductions),
   and Go, goroutines and channels; and Rust with tokio, whose tasks are
   stackless, the floor, as C is for the sequential programs (D224).
3. **slight against itself, over time:** later, with the examples
   (boids), `html->text`, JSON and HTTP: comparing those across
   languages would mostly compare libraries.

Phase 3 (D201) added Chez Scheme, three of Gabriel's programs (`tak`,
`nqueens`, `deriv`), one more of the Benchmarks Game's
(`spectral-norm`), and `fan-in`, many senders to one mailbox.

## The programs

Each prints only numbers, one to a line, since slight's last line is its
root's value; the harness checks every output against what it works out
itself (or, for `n-body`, against C's).

- **`startup`:** prints 0. What every other time includes: the Erlang
  VM's start is about 0.15 seconds of each Erlang time, and Node's 0.03.
- **`fib 38`:** doubly recursive Fibonacci: calls and integer
  arithmetic.
- **`tak 10`:** Gabriel's `tak`, of 30, 20 and 10: 101 million calls,
  each of three arguments, with little arithmetic between.
- **`binary-trees 18`:** the Benchmarks Game's, without its workers:
  trees made and checked, allocation and collection. Its usual size is
  21, which a slight process can hold now that its heap's cap is 1 GB
  (D202: 16.4 s and 676 MB on the cloud machine); 18 is kept so that
  every run compares with those before it, and a full run doesn't take
  ten minutes more.
- **`nqueens 13`:** Gabriel's, as in the R7RS benchmarks: the 73,712
  ways to place 13 queens, found by trying each row in turn with three
  lists (to try, passed over, placed): calls, and short lists made and
  dropped. slight, Erlang and Chez use lists; C, Go, Node, Perl and
  Python their arrays, copied where the list version makes a new list.
- **`deriv 2000000`:** Gabriel's: the symbolic derivative of
  3x² + ax² + bx + 5, two million times: allocating, and walking lists
  and symbols. Each language's own way to hold an s-expression: lists and
  symbols in slight, Erlang and Chez; arrays and strings in Node, Perl
  and Python, slices and a string type in Go; in C, cells from an arena
  emptied after each derivative, with each symbol one cell. Prints the
  derivatives' lengths, summed (so none can be skipped), and the atoms in
  the last.
- **`n-body 1000000`:** the Benchmarks Game's, five bodies' orbits: float
  arithmetic, which slight boxes. Each version does the arithmetic in
  the C version's order, so the energies agree, printed times 10^9.
- **`spectral-norm 1000`:** the Benchmarks Game's, on one core: a
  matrix's norm by the power method, 40 million products of floats.
  slight's and Erlang's vectors are lists, made again each step; the
  others' arrays, changed in place. Each sum in the C version's order
  (Python's in loops, since its `sum` of floats rounds differently from
  3.12 on); the norm printed times 10^9. And `slight-arrays`, slight
  with arrays of numbers (D253), `spectral-norm-arrays.slight`: the
  matrix and its transpose made once, a million floats each, and each
  product `array/matmul`, summing in C's order, so the norm agrees.
- **`ping-pong 2000000`:** round trips between two processes.
- **`payload 1000000`:** `ping-pong` with each message carrying the same
  list of 100 integers there and back (D237): what copying a message's
  data costs. slight and Erlang copy it into the receiver's heap; Go
  passes a slice and tokio moves a `Vec`, neither copying.
- **`thread-ring 10000000`:** the Benchmarks Game's: 503 processes in a
  ring pass a token on, 10 million times.
- **`fan-in 10000000`:** 100 processes each send their number 100,000
  times to one, which sums them: many senders, one mailbox. Go's channel
  holds 1,024 (unbuffered, every message is a switch, 4.7 times slower;
  from 100 up the size makes no difference); Erlang's receiver keeps its
  queue off its heap, as `spawn`'s does. Rust's mailbox is unbounded, as
  slight's and Erlang's are, so it doesn't slow its senders either.
- **`spawn 1000000`:** processes started one after another, each sending
  its number back and ending.
- **`idle-memory 1000000`:** a million processes all waiting for a
  message at once, then each asked for one. Its peak memory, less
  `startup`'s, over a million is what a waiting process costs (with its
  pid in a list, as every version keeps them).
- **`parallel-fib 30`:** 64 processes each work out `fib(30)`: work more
  than one core can share.
- **`latency 10`:** how long a message waits for its turn while other
  processes work (D238). A pinger's ping k is due k ms after it starts,
  and till then it yields and looks at the clock again; each ping's
  latency is from when it was due to when its answer came, so a stall
  counts against every ping it held up. Idle; then beside a process
  working out `fib(40)` without ever waiting; then beside one holding
  1,000 blocks of 10,000 integers (160 MB in slight) and replacing the
  oldest 10,000 times, yielding after each. Its summary is each phase's
  percentiles, not times. tokio on every core is left out (D238).
- **`bank 1000000`:** a simulation (D241), SimPy's bank example: 3
  tellers and a million customers, a process for each, the bank a
  process (in SimPy a `Resource`) each asks for a teller and gives it
  back to; slight's runs on the virtual clock. Its times are whole
  milliseconds, uniform, from Park and Miller's generator in every
  version, so each comes to the same waits, which the harness works out
  by Kiefer and Wolfowitz's recursion; it prints the customers, their
  waits in all in ms, and how many waited. C's is an event list, not a
  process for each customer: the floor under any simulation engine.

The sequential ones are in all eight languages, the actor ones in slight,
Go, Erlang and Rust (D224), `startup` in all nine, and `bank` in slight,
C and Python with SimPy. Rust's are on
tokio, each process a task, its mailbox an unbounded channel
(`tokio::sync::mpsc`); a task waiting in `idle-memory` holds a channel
of its own, as a process holds its mailbox. Each is written the way its language is usually written,
and does the same work: the same algorithm, the same output. slight's
bend to slight (D84): nothing was added to the language for them, and
the Benchmarks Game programs that need what it hasn't got (arrays that
change, hash tables, bignums, regular expressions) aren't here.

## How it's measured

- Each program is built once (slight's for the machine's own target,
  Chez's with `compile-program`, so its runs don't include compiling,
  Rust's by cargo, `--release --locked`, one package for them all, so
  the first one's build includes tokio's),
  run once to check it, then timed 5 times; the programs take turns, so
  the machine's drift falls on all of them alike. One that takes long
  gets fewer runs, never under 3. The summary gives the median, the
  fastest, the CPU time, and the peak memory.
- Time, CPU and memory come from `wait4`, in `bench/rusage.c`, a small
  process that starts each program: on Linux a process forked from
  Python starts with Python's memory counted in its peak.
- **Cores:** slight runs every process on one core. Go, Erlang and Rust
  run on one core too (`GOMAXPROCS=1`, `+S 1:1`, tokio's current-thread
  runtime), the comparison, and again with their defaults, every
  core (`go-mc`, `erlang-mc`, `rust-mc`, tokio's multi-thread runtime, as
  `#[tokio::main]` makes it), to see what that gains or costs. The
  summary's tables are one core; every core is a table of its own at
  the end, each one's time over its own on one core and over slight's
  (D237). `bench/rt.rs`
  picks Rust's runtime, by `BENCH_MC`, which the harness sets for
  `rust-mc`. Node's collector has threads of its own either way.
- **`bank`:** slight's runs with `SLIGHT_CLOCK=virtual`, which the
  harness sets for it alone. Python's needs SimPy (`pip install simpy`),
  whose version the summary gives; without it, Python is left out.
- **`latency`'s measurements:** a program's lines that start with `#`,
  `# phase p50 p99 max n` in microseconds, are taken out before its
  output is checked and kept with each run (D238).
- CPU time over wall time shows more than one core at work. Erlang's
  schedulers spin a while before they sleep, so its CPU time overstates
  its work.
- Erlang allows 262,144 processes unless told otherwise, and a higher
  limit costs memory from the start, so `spawn` and `idle-memory` raise
  it as far as they need (`+P`), and nothing else does.
- Chez runs at its default optimize level, which is safe (as slight is:
  every operation checks its operands), and its programs use Scheme's
  ordinary arithmetic, as Scheme is usually written, not `fl+` or
  `fx+`.
- Timings under qemu mean nothing, so AArch64 is measured on a Mac. The
  cloud machine's x86-64 numbers are noisy, but fine for ratios.

## Results

Each run's full tables are in `bench/results/`. What they say:

### The M2 Max, 30 Oct, 17:51: a chunk's header two words again

`2026-10-30-1751-macos-apple-m2-max` (`82dda3f`):

| | 24 Oct | 17:10 | 17:51 |
|---|---:|---:|---:|
| `spawn` | 0.122 s, 48 MB | 0.145 s, 64 MB | 0.142 s, 49 MB |
| `fan-in` | 0.355 s, 461 MB | 0.412 s, 613 MB | 0.373 s, 461 MB |
| `ping-pong` | 0.285 s | 0.282 s | 0.280 s |

- **Memory, as it was.** `fan-in` is 5 per cent over 24 Oct.
- **`spawn`, 16 per cent over, is islands' price for now.** It runs 4
  per cent more instructions than on 24 Oct (`118291f`), about 70 a
  process: `finish` looking for watchers on other islands and a parent
  there, `rt_send` and `rt_recv` checking a pid's island, and the
  scheduler's loop its inbox. On the cloud machine that's 5 per cent,
  the best of seven runs 0.219 s against 0.208. How many islands there
  are makes no difference, to the start (2 ms) or to a million
  processes. Still 3 times faster than Go's goroutines (0.42 s).
- **Profiled on the Mac, the cause is still open**
  (`results/2026-10-30-spawn-profile-m2-max.md`). It isn't work: 10
  more instructions a process (2284 against 2274), but 46 more cycles,
  IPC 5.22 to 4.72, 13 to 15 ns a process by hyperfine. Nor
  thread-locals (0.003 lookups a process, counted under lldb), nor
  malloc (61 ms a million in both). The time arrived with islands' step
  1 (`20d78b3`), building each commit in between, and shows as a wait
  at `retire`'s epilogue. But the island's state as plain globals,
  the scheduler's registers on cache lines of their own, and both
  together leave the time where it is: with both, the wait leaves
  `retire` and spreads over everything else. So it's a latency that
  surfaces wherever there's room, not one load. Finding it needs the
  core's counters (L1D misses, load and memory-ordering stalls, under
  `sudo`), or `20d78b3` taken apart change by change. Left there for
  now: 11 per cent on one benchmark, with ping-pong, thread-ring and
  payload as they were.

### The M2 Max, 30 Oct, 17:10: no thread-locals, and a chunk's header

`2026-10-30-1710-macos-apple-m2-max`, with no thread-local on the
busiest paths (`ebb2312`):

| | 24 Oct | 16:22 | 17:10 |
|---|---:|---:|---:|
| `ping-pong` | 0.285 s | 0.345 s | 0.282 s |
| `thread-ring` | 0.493 s | 0.637 s | 0.506 s |
| `spawn` | 0.122 s | 0.143 s | 0.145 s |
| `fan-in` | 0.355 s | 0.420 s | 0.412 s |
| `payload` | 0.566 s | 0.569 s | 0.556 s |

- **Messages, back where they were.** `ping-pong`, `thread-ring` and
  `payload` are within 3 per cent of 24 Oct, islands and all.
- **`spawn` and `fan-in` weren't the thread-locals.** `fan-in` took
  628 MB, where it took 472 on 24 Oct: 16 bytes more for each of its
  10 million messages, which pile up in one mailbox. Shared buffers
  (D253) had put a copy's handles in every chunk's header, making it
  four words, so a message with nothing to copy was a malloc of 64
  bytes, not 48. The header is two words again: a bit of its size says
  whether the chunk has handles, and only such a chunk has them, after
  its bytes. On the cloud machine `fan-in` then takes 639 MB where it
  took 798, and runs in about 1.0 s where it took 1.1 to 5.3 (it was
  paging); `spawn` 66 MB where it took 82. Under callgrind `spawn` is
  4 per cent over 24 Oct (about 70 instructions a process, `finish`
  looking for watchers on other islands, and the scheduler's loop for
  its inbox), so most of its 19 per cent on the Mac should have been
  the bigger message too.

### The M2 Max, 30 Oct, 16:22: one thread-local, and then none

`2026-10-30-1622-macos-apple-m2-max`, the five programs that had slowed,
with an island's state one thread-local struct (`e46e023`):

| | 24 Oct | 15:33 | 16:22 |
|---|---:|---:|---:|
| `ping-pong` | 0.285 s | 0.416 s | 0.345 s |
| `thread-ring` | 0.493 s | 0.748 s | 0.637 s |
| `spawn` | 0.122 s | 0.171 s | 0.143 s |
| `fan-in` | 0.355 s | 0.452 s | 0.420 s |
| `payload` | 0.566 s | 0.618 s | 0.569 s |

- **Half way back.** `payload`, whose time is copying, is where it was;
  the others are still 18 to 29 per cent over, where callgrind on Linux
  says 5. Counted as macOS pays them (the runtime built with
  `-fPIC -ftls-model=global-dynamic` as a shared library, so each
  thread-local is a call to `__tls_get_addr`, as it's one to
  `tlv_get_addr` on macOS), a message still made six: `rt_current` in
  `rt_send` and `rt_recv`, and `my` in `copy_walk`, `to_scheduler`,
  `events` and `rt_recv` again; and a process, `spawn`'s, six more.
  At 2 to 3 ns each, that's the difference.
- **So none, where it's busiest.** Compiled code keeps the process in a
  register (x28, r15), so it hands it to `rt_send`, `rt_recv`,
  `rt_fork` and `rt_spawn` (as it does `rt_preempt` and `rt_exit`); a
  process points at its island's state (`isle`), which the run queue,
  the switch to the scheduler, the copy of a message, the table and a
  process's end reach through it; and `events` is inlined into the
  scheduler's loop, which looks the island up once. Counted the same
  way, `ping-pong`, `thread-ring`, `fan-in`, `payload` and `spawn` make
  under a thousand lookups in a run, where they made one or two million.
  The M2 Max is to say whether that's all of it.

### The M2 Max, 30 Oct, 15:33: arrays, and what islands' thread-locals cost

`2026-10-30-1533-macos-apple-m2-max`, at islands' step 2 (`0d3e608`).

- **`spectral-norm` with arrays** (D253): 0.086 s, against 0.478 s for
  the lists, and 0.12 s for Node, 0.59 for Chez, 0.047 for C: 5.6 times
  faster than the lists, and 1.8 times C, though it makes its matrix
  and the transpose through `array/generate` first. 43 MB at its peak.
- **What sends and processes cost, slower since 24 Oct:** `ping-pong`
  0.285 s to 0.416, `thread-ring` 0.493 to 0.748, `spawn` 0.122 to
  0.171, `fan-in` 0.355 to 0.452; `fib` and `binary-trees` much as they
  were. On the cloud machine, under callgrind, the same programs run 6
  to 10 per cent more instructions: shared buffers' (D253) half of it,
  `adopt` no longer inlined among it, and islands' (D256) the other
  half. The rest of macOS's 30 to 50 per cent is the thread-locals of
  islands' step 1: macOS reaches each through a call, a variable at a
  time, about twenty for each message. So an island's state became one
  thread-local struct, `my`, which a function reaches with one call;
  `adopt` is inlined again, a chunk's handles taken apart from it; and
  `rt_send` looks a pid of its own island up in the table at once.
  Under callgrind, `ping-pong` is then 5 per cent over 24 Oct, from 10,
  `thread-ring` 6 from 9. The M2 Max is to say what that comes to there.

### The cloud machine, 30 Oct: `spectral-norm` with arrays (D253)

x86-64, n = 1000, the median of five, not saved (the cloud machine's
numbers are for ratios):

| | time | memory |
|---|---:|---:|
| C | 0.064 s | 2.1 MB |
| Go, one core | 0.094 s | 1.8 MB |
| Node | 0.188 s | 54 MB |
| slight, arrays | 0.200 s | 41 MB |
| slight, lists | 0.721 s | 3.8 MB |

- **Arrays against lists:** 3.6 times faster. The forty products, in
  `array/matmul`, take about 0.03 s, less than C's whole run, since C
  works out each of A's elements as it goes and this program makes A
  and its transpose once; the rest is making them, through
  `array/generate`, two million calls of a slight function.
- **Making them:** through one list as long as the array at first,
  0.27 s and 87 MB at its peak; a list of 512 at a time since, which
  dies young, the chunks joined in a tree, 0.16 s and 41 MB. A builtin
  that filled a fresh array would take the lists out (D253 turned it
  down until a benchmark asked); what's left is calling the function,
  a float box for each result.
- **`array/matmul` on a vector:** the kind's test hoisted out of its
  loops, and a vector (one column) summed in a register: the products
  went from about 0.12 s to 0.03.
- **`n-body`:** not written with arrays. Its five bodies are scalar
  arithmetic on each pair, with nothing to do element by element across
  an array; arrays would be `array/get` in place of a tuple's pattern,
  each element a float box all the same. What it needs is floats
  unboxed in compiled code (TYPES.md's step 5), not arrays.

### The cloud machine, 23 Oct: `bank`, a simulation (D241, D242)

x86-64, a million customers, the median of five, not saved (the cloud
machine's numbers are for ratios):

| | before D242 | after | memory |
|---|---:|---:|---:|
| C, an event list | 0.037 s | 0.036 s | 2.7 MB |
| slight, a process for each customer | 1.50 s | 0.96 s | 42 MB |
| SimPy 4.1.2, CPython 3.13 | 10.9 s | 10.9 s | 17 MB |

- **Against SimPy:** 7.3 times faster, and 11.3 since D242, which took
  out a `select()` the virtual clock made at every event with nothing to
  look at, a quarter of the time (system time 0.42 s to 0.03).
- **Against the floor:** C's event list is 27 times faster: a customer
  there is two events in a heap, where slight's is a process, three
  messages and two timers, each a malloc. Under callgrind, about 6,800
  instructions a customer before D242, 28 per cent in malloc and free.
- **Memory:** 40 MB for each million customers served, the exit records
  of the processes that have ended (D88): 13.6 MB at 250,000, 83.7 at 2
  million. SimPy's stays at 17.

### The M2 Max, 23 Oct, 17:01: the message copy, faster (D240)

`bench/results/2026-10-23-1701-macos-apple-m2-max.md`, at `b91d32a`,
the five programs that send, against the run at 12:08 (the other
languages within 6 per cent of it, most within 2):

- **`payload 1000000`:** slight 0.84 s to 0.56, 1.51 times faster (the
  cloud machine's 1.4). What the 100 integers add to a round trip,
  `payload`'s less `ping-pong`'s (which makes twice as many), 0.67 µs to
  0.41: about 2.0 ns a cell copied, where it was 3.3. Go is now 2.8
  times faster than slight, where it was 4.3, and tokio 3.5, where it
  was 5.4; Erlang 4.7 times slower on one scheduler and 2.6 on every
  core.
- **`ping-pong 2000000`:** 0.348 s to 0.301, 1.16 times faster, where
  x86-64's instructions were 0.6 per cent more: level with tokio (0.292)
  and ahead of Go (0.387). AArch64's 31 registers probably hold more of
  the walk's state than x86-64's 16, so what a copy costs whatever its
  size fell there too; a guess, not measured.
- **`spawn`, `fan-in` and `thread-ring`:** 0.137 s to 0.126, 0.383 to
  0.352, and 0.528 to 0.509: 8, 9 and 4 per cent faster, beside the
  cloud machine's 9, 7 and 8 per cent fewer instructions, most of it
  values with no pointer in them no longer walked. In `fan-in`, Go and
  tokio take 0.82 and 0.76 of slight's time, where they took 0.75 and
  0.71.

The guide reads this run for all five, since it came in.

### The M2 Max, 23 Oct, 15:16: the collector's walk, faster (D239)

`bench/results/2026-10-23-1516-macos-apple-m2-max.md`, at `b56a719`,
`binary-trees` and `latency`, against the runs at 12:08 and 13:41:

- **`binary-trees 18`:** slight 1.03 s to 0.79, 1.30 times faster, where
  the cloud machine's was 1.08 times. Each one's time less its start,
  over slight's: Chez 0.39, Erlang on every core 0.53, Node 0.63, Go on
  every core 0.84, Erlang on one scheduler 0.98, C 1.26, Go on one core
  1.74, Python 7.5, Perl 23. slight was behind C and Erlang on one
  scheduler, and is now ahead of C and level with Erlang there. Its peak,
  121 to 191 MB, the median 142, is macOS's usual spread.
- **`latency`'s heap phase:** at the median, the 99th percentile and the
  worst, 0.23, 57 and 61 ms to 0.12, 44 and 47: the worst pause beside
  160 MB live about 0.3 ms a MB, where it was 0.4. The phase 1.42 s to
  1.05. Busy, 4, 6 and 18 µs to 4, 5 and 12; the others as before.

The guide reads this run for both, since it came in.

### The M2 Max, 23 Oct, 13:41: `latency` (D238)

`bench/results/2026-10-23-1341-macos-apple-m2-max.md`, at `c8cbba4`,
`latency` alone, five runs each, every one's figures within a few
microseconds of its others' but the worst cases. Each phase's median,
99th percentile and worst wait, the median over the runs:

| | idle | busy | heap | peak |
|---|---:|---:|---:|---:|
| slight | 0, 4, 13 µs | 4, 6, 18 µs | 0.2, 57, 61 ms | 600 MB |
| Go, one core | 0, 1, 4 µs | 11.5, 24, 25 ms | 4, 20, 20 µs | 167 MB |
| Erlang, one scheduler | 5, 12, 15 µs | 28, 41, 50 µs | 8, 69, 84 µs | 1,748 MB |
| tokio, one thread | 0, 0, 7 µs | 139, 274, 276 ms | 2, 9, 9 µs | 84 MB |
| Go, every core | 0, 9, 17 µs | 1, 10, 14 µs | 1, 9, 9 µs | 168 MB |
| Erlang, every core | 0, 4, 9 µs | 0, 4, 11 µs | 0, 4, 37 µs | 1,709 MB |

- **Busy:** slight is the most responsive on one core, its busy process
  preempted every 1,000 reductions; Erlang, preempting the same way,
  waits 28 to 50 µs; Go on one core 11.5 to 25 ms, preempting about
  every 10 ms; tokio all of `fib`, not preempting. On every core Go and
  Erlang wait microseconds.
- **Heap:** slight is the least responsive: a major collection of 160
  MB holds every process up, 61 ms at most, about 0.4 ms a MB (a sixth
  of the cloud machine's). Go, collecting concurrently, waits 20 µs at
  most, tokio, with no collector, 9. Erlang, which collects each
  process on its own as slight does, shows no pause either, 84 µs at
  most, but peaks at 1.7 GB; why hasn't been looked into (a large heap
  collected on a dirty scheduler, a thread of its own, or a heap let
  grow rather than copied whole). Go's and tokio's heap phases are 66
  and 20 ms, their work quicker, so their pings are fewer.
- **Erlang on one scheduler** is on a core 63 per cent of the time it
  takes here, the Mac's waiting (D194), so its CPU time can't say
  whether another thread did its collecting.

The guide (below) reads this run for `latency`, as a feature of its
own, responsiveness under load.

### The M2 Max, 23 Oct, 12:08: `payload` (D237)

`bench/results/2026-10-23-1208-macos-apple-m2-max.md`, at `b45e37d`:
the first run with `payload`, and with every core in a table of its own.
slight's times are within 1.5 per cent of the 10:47 run's (below), and
everyone else's within 5 but a few that are uneven from run to run:
Erlang's one-scheduler `spectral-norm` (0.66 s to 0.72), its every-core
`ping-pong` (1.05 to 0.99), Go's every-core `idle-memory` (1.08 to
1.02) and tokio's one-thread `spawn` (1.09 to 1.58, its fastest 0.37).
`binary-trees`' peak, 106 to 191 MB over its five runs, is macOS's
usual spread.

- **`payload`:** a round trip with 100 integers each way takes slight
  840 ns, against 173 in `ping-pong`, so the copy costs about 330 ns a
  message, a little over 3 ns a cell. tokio on one thread takes 156 ns
  and Go on one core 196, what their `ping-pong` round trips take, since
  they don't copy: 5.4 and 4.3 times faster than slight. Erlang, which
  copies too, takes 1,310 ns on every core, 1.6 times slight's (3.0 on
  one scheduler).
- **Every core**, the summary's last table: Erlang's `fib` and `tak`
  take 0.55 of their one-core times on every core, which a sequential
  program can't gain, so its one scheduler's waiting on the Mac (D194)
  is still there; tokio's multi-thread runtime takes 38 times its
  one-thread time at `payload`, 40 at `ping-pong`.

### The M2 Max, 23 Oct: every program, Rust included

`bench/results/2026-10-23-1047-macos-apple-m2-max.md`, at `3487cba`:
the whole suite, the first since Rust came in (D224). A steady run:
slight's sequential times are within 3 per cent of the run at `448aa6d`
(17 Oct), though strings, numbers, random numbers, maps, `rpc` and Unix
sockets have come in since; `ping-pong` is 7 per cent slower and `spawn`
5, as they were on 20 Oct (not looked into). Every other implementation
is within 5 per cent of its last run, but tokio's one-thread `spawn`,
uneven (its fastest run 0.54 s, its median 1.09). Start-up is 2.3 ms,
against 1.8 before, and C's 1.6 against 1.4: a few hundred microseconds
of noise.

Each one's time less its start, over slight's (under 1 is faster than
slight); Go and Rust on one core, Erlang on every core (under "Two
things the Mac shows"):

| | slight | C | Go | Rust | Chez | Node | Erlang | Python | Perl |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `startup` | 2.3 ms | 0.70 | 0.94 | 0.84 | 15 | 25 | 38 | 8.5 | 1.39 |
| `fib` | 0.198 s | 0.55 | 0.62 | | 0.96 | 1.30 | 1.43 | 23 | 65 |
| `tak` | 0.140 s | 0.58 | 0.72 | | 1.11 | 1.65 | 1.41 | 26 | 99 |
| `binary-trees` | 1.030 s | 0.96 | 1.34 | | 0.30 | 0.48 | 0.40 | 5.7 | 17 |
| `nqueens` | 0.702 s | 0.44 | 0.82 | | 0.51 | 3.18 | 0.55 | 9.9 | 20 |
| `deriv` | 0.378 s | 0.35 | 2.82 | | 0.40 | 1.49 | 0.60 | 11 | 21 |
| `n-body` | 1.288 s | 0.03 | 0.04 | | 0.88 | 0.05 | 0.86 | 4.5 | 5.8 |
| `spectral-norm` | 0.419 s | 0.11 | 0.11 | | 1.30 | 0.15 | 0.96 | 12 | 18 |
| `ping-pong` | 0.348 s | | 1.11 | 0.83 | | | 2.78 | | |
| `thread-ring` | 0.528 s | | 1.94 | 1.31 | | | 4.35 | | |
| `fan-in` | 0.385 s | | 0.74 | 0.70 | | | 2.62 | | |
| `spawn` | 0.135 s | | 3.14 | 8.06 | | | 6.17 | | |
| `idle-memory` | 0.336 s | | 4.42 | 2.22 | | | 5.38 | | |
| `parallel-fib` | 0.280 s | | 0.59 | 0.53 | | | 0.18 | | |

- **Processes:** the fastest at starting one (135 ns) and at keeping one
  waiting (571 bytes; a tokio task with its channel 1,105, a goroutine
  2,900, an Erlang process 2,740).
- **Messages:** the fastest round the ring (53 ns a hop); tokio on one
  thread 1.2 times faster at `ping-pong` (87 ns a message in slight)
  and 1.4 at `fan-in`, Go on one core 1.35 at `fan-in`.
- **Sequential:** calls 1.4 to 1.8 times C's and Go's time, level with
  Chez, ahead of Node and Erlang; lists 2.0 to 2.9 times C's and Chez's,
  ahead of Node, and of Go at `deriv`; `binary-trees` 3.4 times Chez's;
  floats 9 to 35 times C's and Go's, level with Chez and Erlang.
- **Every core:** Rust and Go 17 and 15 times faster than slight at
  `parallel-fib`; and slower at messages, tokio's multi-thread runtime
  34 times slight's `ping-pong` and 11 its `fan-in`, Go 9 at `fan-in`.

A page reading the runs from slight's side, for someone deciding
whether to use it (each benchmark's place and why, the benchmarks by
feature, and slight against each language), is the "slight Performance
Guide" (https://claude.ai/artifact/RHne7keXWUwADYqKHuHdmi, private to
Stevan till he shares it); it reads the 12:08 run, above, since that
came in. Unlike the tables here, it takes each
language once, at the faster of its one-core and every-core runs, unless
told to hold them to one core.

### Rust: a floor for a message, not for a process (D224)

The M2 Max, `bench/results/2026-10-20-1137-macos-apple-m2-max.md`,
against slight on one core:

- **A message, tokio is faster:** `ping-pong` 0.83 of slight's time
  (148 ns a round trip, slight's 179), and `fan-in` 0.69, its queue
  holding ten million messages in 93 MB, slight's in 484.
- **A process, slight is:** `thread-ring` 1.3 times faster, `spawn` 2.8
  (fastest against fastest: Rust's runs went from 0.38 s to 0.73, its
  median), and `idle-memory` 2.2. A waiting process is 575 bytes; a task
  with its channel 1,105 (a goroutine 2,886, an Erlang process 2,741):
  the task is small, but tokio's channel, its fields padded to cache
  lines and its first block of 32 slots made with it, isn't.
- **`parallel-fib`:** 0.54, LLVM's `fib` against slight's; `rust-mc`
  0.06, on 12 cores.
- **`rust-mc` is the slowest at messages:** `main`'s future runs on a
  thread of its own, outside the workers (`#[tokio::main]`'s way), so
  each of `ping-pong`'s round trips wakes a sleeping thread (12 s, 34
  times slight); `fan-in` takes 4.0 s and 46 s of CPU, 12 workers
  contending for one channel.

The cloud machine, `bench/results/2026-10-20-0926-linux-intel-xeon-processor-2-10ghz.md`
(no Erlang there), puts slight ahead at the messages too: `ping-pong`
1.8 times faster (210 ns a round trip, tokio 370), `thread-ring` 2.0,
`spawn` 2.0, `idle-memory` 2.3 (451 bytes to 1,040); tokio still wins
`fan-in` (0.81) and `parallel-fib` (0.38). `rust-mc`'s `ping-pong`
there takes 78 s.

### The airline at a small airline's size (D210)

Not a benchmark of the others' kind, since it's only in slight:
`examples/airline.slight --time` on a day from
`examples/airline-gen.slight` (seed 1: 300 aircraft, 1,499 legs, 2,287
connections, ten what-ifs). Each minute the desk ticks every aircraft and
hears from each, so a run of the day is 324,000 aircraft-minutes and
648,000 messages. On the cloud machine, x86-64, three runs:

- reading the day: 54 ms;
- the base: 311 to 346 ms, about 300 µs a minute, a microsecond an
  aircraft-minute, 1.9 million messages a second;
- the ten branches: 2.2 to 2.5 s, of which 1.15 to 1.35 s on the
  minutes before their changes, which forking at the change would save;
- the whole run: 2.6 to 2.9 s, at most 84 MB.

A program of the same shape with nothing else, 300 processes ticked
1,080 times, takes 80 to 270 ms and 56 MB, so the model is about half
of a minute's time. The M2 Max's numbers are to come.

Since D212 each branch starts from a checkpoint of its parent's, at
most half an hour before its change: on the cloud machine the whole
run is 2.1 to 2.2 s where it was 3.3 to 3.6 (`--every 0`, measured the
same day, the machine slower than for the runs above), the branches 1.4
to 1.6 s where they were 2.7 to 3.2. Three more branches from a kept
history (`--from`) take 0.6 s in all.

```
node bin/slightc.ts -o build/airline-gen examples/airline-gen.slight
node bin/slightc.ts -o build/airline examples/airline.slight
build/airline-gen > build/day.sexp
build/airline --record build/day.sexp >> build/day.sexp
build/airline --time build/day.sexp
```

### The airline's what-ifs at random (D214)

`bench/airline.py` on the same day, the cloud machine, x86-64, a run
each, `--random` what-ifs made from seed 1:

- 10: 2.8 s in all, the branches 1.9 s, 171 ms each at the median, 7
  of them rejoining a run before their day ends;
- 100: 15.5 s, 131 ms each at the median, 312 at the 95th percentile,
  82 rejoining, 185 MB at most;
- 1,000: 162 s, 141 ms each at the median, 326 at the 95th percentile
  and 948 at most, 833 rejoining, 1.4 GB at most. A minute simulated
  takes 368 µs, as at 100 (359) and in the base (about 330), so a
  thousand branches cost about a thousand times one. The slowest ran
  964 minutes, the most of any, at 983 µs a minute, and a handful at
  twice the usual pace; why, the run doesn't say (a major collection
  of everything kept is a guess).
- In rounds of 30, each from the history the last kept: 4.7, 4.3 and
  5.4 s, the history 2.9, 5.0 and 7.2 MB, read in 46, 148 and 209 ms.
- Recovering from a 60-minute delay at 11:35: 7,629 remedies made in
  0.55 s, each run in 115 to 140 ms; 10 of them in 2.5 s in all, 50 in
  7 to 8 s, 250 in 36 s, 1,000 in 135 s (1.3 GB at most), 2,000 in
  281 s (2.3 GB); all of them run out of the 1 GB heap (D214).
  Checking a dozen over HTTP takes 0.15 to 1.3 s.

Since D215 the remedies are made near what the disruption touched, and
let go once measured; a run makes checkpoints only when a later branch
will start from them:

- a 45-minute delay at 11:35: 63 remedies made in 13 ms, all run in
  7.0 s, 111 ms each, 118 MB at most; 64 days, 16 checked, 15 pass;
- a hub closed for an hour and a half: 905 made in 64 ms, run in
  136 s, 151 ms each, 251 MB at most (745 before the checkpoints were
  made only when wanted); 885 days, none passing;
- 1,000 what-ifs: 149 s, 828 MB at most, where they took 162 s and
  1.4 GB.

Since D218 the what-ifs can run in shares, a copy of the program a core
(`bench/airline.py --random 1000 --shards 1,2,4,8`), each share whole
trees of branches, every share's report checked against one copy's. On
the cloud machine's 4 cores:

- 1 copy: 149.7 s, 838 MB;
- 2: 74.7 s, 2.00 times as fast, 371 MB a copy, 734 in all;
- 4: 38.0 s, 3.94 times as fast, 248 MB a copy, 923 in all;
- 8, more copies than cores: 40.2 s.

Modelled from each what-if's measured time, the slowest share setting
the pace: 7.1 times as fast on 8 cores, 13 on 16, 22 on 32, 39 on 64.

On the M2 Max (12 cores: 8 performance, 4 efficiency), Stevan's run:

| Copies | Wall | Speed-up | CPU in all | Peak a copy | In all |
|---|---|---|---|---|---|
| 1 | 63.2 s | 1.00 | 63.0 s | 1,696 MB | 1,696 MB |
| 2 | 34.0 s | 1.86 | 67.4 s | 1,428 MB | 2,754 MB |
| 4 | 19.3 s | 3.27 | 75.1 s | 1,172 MB | 4,420 MB |
| 8 | 11.0 s | 5.77 | 80.0 s | 895 MB | 6,512 MB |
| 12 | 9.9 s | 6.37 | 99.7 s | 788 MB | 8,066 MB |

One copy is 2.4 times as fast as the cloud machine's. The copies slow
each other as the cores fill (the CPU in all grows), and the model's
speed-up divided by that growth is what was measured (D218). A copy's
memory there is about 820 MB more than on Linux, whatever its share.

```
bench/airline.py --random 10,100,1000
bench/airline.py --random 30 --rounds 3
bench/airline.py --recover --remedies 10,50 --seed 1,2
```

### The M2 Max after D203

`bench/results/2026-10-17-1311-macos-apple-m2-max.md`, at `448aa6d`: a
steady run, every other implementation within a few per cent of the run
before, and Node's start back at 0.053 s. slight's `deriv` 0.575 s to
0.380 (1.5 times faster), `nqueens` 0.841 to 0.703 (1.2), `n-body` and
`spectral-norm` a few per cent; the rest the same. Now, each one's time
over slight's: `deriv` Chez 0.40, Erlang (every core) 0.62, Node 1.49,
Go 2.81; `nqueens` Chez 0.50, Erlang 0.55, Go 0.82, Node 3.19. A page of
charts of this run, by category and benchmark, is the "Where slight
lands" dashboard (https://claude.ai/artifact/BvHTJx4PfxJSZBcTARniCh,
private to Stevan till he shares it).

### Phase 3 on the M2 Max: Chez Scheme and five more programs (D201)

`bench/results/2026-10-17-1043-macos-apple-m2-max.md`, at `51893a0`
(slight's code as at `c242a27`; phase 3 changed only `bench/`), with
Chez Scheme 10.4.1. A steady run: every benchmark that was there before
is within 4 per cent of the run before. Node's start has been 0.15 s
(half of it waiting) since the 09:54 run, against 0.053 before, so its
start is taken as 0.053 here. Each one's time less its start, over
slight's (under 1 is faster than slight); Go on one core, Erlang on
every core (under "Two things the Mac shows"):

| | slight | C | Go | Chez | Node | Erlang | Python | Perl |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `fib` | 0.199 s | 0.55 | 0.62 | 0.94 | 1.33 | 1.39 | 23 | 64 |
| `tak` | 0.140 s | 0.58 | 0.72 | 1.08 | 1.69 | 1.39 | 26 | 100 |
| `binary-trees` | 1.051 s | 0.94 | 1.30 | 0.29 | 0.62 | 0.39 | 5.6 | 17 |
| `nqueens` | 0.841 s | 0.37 | 0.68 | 0.42 | 2.64 | 0.46 | 8.3 | 16 |
| `deriv` | 0.575 s | 0.23 | 1.85 | 0.26 | 1.00 | 0.41 | 7.1 | 14 |
| `n-body` | 1.338 s | 0.03 | 0.03 | 0.84 | 0.05 | 0.83 | 4.3 | 5.5 |
| `spectral-norm` | 0.451 s | 0.10 | 0.10 | 1.19 | 0.15 | 0.89 | 11 | 17 |

- **Against Chez**, the nearest thing to slight: calls level (fib 0.94,
  `tak` 1.08), floats either way (`n-body` 0.84, `spectral-norm` 1.19:
  Chez boxes them too, at its default arithmetic); but where lists are
  made and dropped, 2.4 to 3.9 times faster (`nqueens`, `deriv`,
  `binary-trees`).
- **Where slight's time goes there** (callgrind, cloud machine): in
  `deriv`, about 80 per cent of the instructions are the prelude's
  `map` (22), `fold/l` (18), the closures they call (27), `length` (8)
  and `not` (5); `deriv` itself is 18. In `nqueens`, the prelude's `not`
  alone is 18 per cent, `append` and `reverse` 12. `map` and `append`
  are `fold/l` and `reverse`, with a closure called for each element;
  `not` is a function call. Collecting hardly shows in either.
- **`fan-in`:** slight 0.374 s, with 484 MB at its peak; Go on one core
  0.75 (4 MB: its channel blocks the senders), Erlang 2.7 times slower
  on every core (658 MB), and Go on every core 9.3 (the senders fight
  over the channel's lock). In slight and Erlang nearly every message
  waits in the receiver's mailbox at once.
- **The cloud machine** (`bench/results/2026-10-17-0842-linux-intel-xeon-processor-2-10ghz.md`,
  Chez 9.5.8) agrees: Chez 0.25 of slight's time on `binary-trees`,
  0.31 on `deriv`, 0.55 on `nqueens` and `tak`, 0.93 on fib; 1.26
  times slower on `n-body`, 1.86 on `spectral-norm`.

### Phase 2 on the M2 Max: what it did (D195–D200)

`bench/results/2026-10-17-0954-macos-apple-m2-max.md`, at `c242a27`,
against the first run (`2026-10-16-0849`), slight's times, each less
its start:

| | first | now | faster |
|---|---:|---:|---:|
| `fib` | 0.304 s | 0.201 s | 1.5 times |
| `binary-trees` | 2.540 s | 1.073 s | 2.4 times |
| `n-body` | 2.071 s | 1.344 s | 1.5 times |
| `thread-ring` | 0.851 s, 210 MB | 0.532 s, 2.1 MB | 1.6 times |
| `parallel-fib` | 0.428 s | 0.283 s | 1.5 times |
| `ping-pong`, `spawn`, `idle-memory` | | | the same |

On the cloud machine, in instructions: `binary-trees 14` 1,590 million
to 450 (3.5 times fewer), fib 30 131 to 79, `n-body 20000` 642 to 328.
Memory is the same everywhere but `thread-ring` (D199) and `n-body`
(4 MB to 6, its nursery); a waiting process still costs about 555
bytes, a fifth of Go's or Erlang's. `binary-trees`' peak memory on macOS still
ranges from about 90 MB to 370 from run to run (89 to 90 MB on Linux).

D200 alone, against the run before it: `binary-trees` 1.35 s to 1.07
(1.26 times), `n-body` 1.45 to 1.35 (1.08), the rest the same.

Where slight stands now, each language's start taken off, Erlang and Go
on every core where that's said, one core otherwise:

- **`fib`:** C 1.85 times faster (2.9 at first), Go 1.6 (2.5). slight
  is now 1.3 times faster than Node and 1.4 than Erlang, where at first
  both were faster than slight; Python 23 times slower, Perl 64.
- **`binary-trees`:** Erlang 2.6 times faster (6.2 at first), Node 2.1
  (5.1), Go on every core 1.6 (3.8), C 1.08 (2.6), which mallocs and
  frees each node; Go on one core is 1.3 times slower than slight.
- **`n-body`:** the widest gap left. C 36 times faster (57), Go 27
  (45), Node 20 (32), Erlang 1.2 (1.8). Every float is a 16-byte box.
- **Actors:** slight is the fastest at all four, as before: 82 ns a
  message (Go on one core 1.2 times slower, Erlang 3.0), 53 a hop round
  the ring (Go 2.0, Erlang 4.3), 130 a process started (Go 3.2, Erlang
  6.3), and a million waiting in 0.36 s and 556 MB (Go 4.3 times slower
  and 2.9 GB, Erlang 5.1 and 2.8 GB).
- **`parallel-fib`:** one core against twelve: Go on every core 15
  times faster, Erlang 5.6; Go on one core 1.7.

This run's start was disturbed: `startup`'s medians for Go, Node and C
are two to three times their usual (Node 0.15 s of wall time on 0.07 of
CPU), and three of Node's five `binary-trees` runs are a quarter slower
than the other two. The figures above take their starts, and Node's
`binary-trees`, from the run before (`2026-10-17-0830`); every other
implementation's times are within a few per cent of that run's.

### The M2 Max after D199

`bench/results/2026-10-17-0830-macos-apple-m2-max.md`, at `9ab66e5`:
freeing the chunk of a message with nothing to copy as it's taken.
`thread-ring` 1.6 times faster, 0.88 s to 0.54, and 2.0 MB where it was
210: 54 ns a hop, Go 106 (one core), Erlang 229 (every core). The rest
within a per cent or two of the run before, as they should be.

`binary-trees`' peak memory went from 303 MB to 143 in the medians, but
that's noise: its five runs in each of the three Mac files range from
about 90 MB to 380, so its memory on macOS isn't worth comparing till
that's understood (below, "Two things the Mac shows").

### The M2 Max after phase 2's first fixes (D195–D198)

`bench/results/2026-10-17-0814-macos-apple-m2-max.md`, at `6f53b39`.
Against the first run, slight's times: fib 1.5 times faster (0.31 s to
0.20), `binary-trees` 1.9 (2.5 to 1.35), `n-body` 1.4 (2.1 to 1.45),
`parallel-fib` 1.5; the actor benchmarks the same, as they should be.
Each one's start taken off, and Erlang on every core as before:

- **`fib`:** C 1.9 times faster (2.9 before), Go 1.6 (2.5); slight is
  now faster than Node (1.3 times) and Erlang (1.4), which it trailed.
- **`binary-trees`:** Erlang 3.3 times faster (6.2), Node 2.7 (5.1), Go
  on every core 2.0 (3.8), C 1.3 (2.6); Go on one core is now slower
  than slight. 303 MB, against 356, but that varies from run to run
  (see the run after D199).
- **`n-body`:** C 39 times faster (57), Go 31 (45), Node 22 (32), Erlang
  1.3 (1.8).
- **Actors:** 82 ns a message, 88 a hop, 131 a process started, as
  before; `thread-ring` still at 210 MB, D199 coming after this run.

### The M2 Max, AArch64 (Oct 2026): the reference

`bench/results/2026-10-16-0849-macos-apple-m2-max.md`: 12 cores (8 for
performance, 4 for efficiency), macOS 15.7, OTP 29, Go 1.27, Node 26. A
quiet machine: each one's runs are within a few per cent of each other.
Each one's start is taken off its times below; and Erlang's are on every
core, for the reason under "Two things the Mac shows".

Sequential, slight's time against each:

- **`fib`:** 0.30 s. C is 2.9 times faster (Apple's clang doesn't turn
  the recursion into a loop, as GCC did on Linux), Go 2.5, Node 1.2 and
  Erlang 1.1; Python is 15 times slower, Perl 42.
- **`binary-trees`:** 2.5 s, the weak spot. Erlang is 6.2 times faster,
  Node 5.1, Go on every core 3.8, C 2.6, Go on one core 1.9; Python is
  2.3 times slower, Perl 7.1.
- **`n-body`:** 2.1 s. C is 57 times faster, Go 45, Node 32, Erlang 1.8;
  Python is 2.8 times slower, Perl 3.6.

Actors, slight on one core:

- **`ping-pong`:** 79 ns a message; Go 97 (one core), Erlang 239.
- **`thread-ring`:** 85 ns a hop; Go 106, Erlang 231. The fastest here
  too, as it wasn't on Linux, but still at 210 MB (Go 5, Erlang 61).
- **`spawn`:** 135 ns a process; Go 426, Erlang 828.
- **`idle-memory`:** 554 bytes a waiting process; Go 2,886, Erlang
  2,741: a fifth.
- **`parallel-fib`:** 0.43 s; Go on one core 0.17.

What 12 cores gain: `parallel-fib` is 8.7 times faster in Go, and about
8.9 in Erlang (against its time on a core on one scheduler), so Go is 22
times faster than slight there, and Erlang 8.8. Passing a message between
cores costs Go more than on one (`ping-pong` 1.4 times slower).

Two things the Mac shows that Linux didn't:

- **Erlang on one scheduler waits.** With `+S 1:1` it's on a core only
  37 to 60 per cent of the time it takes, in every run (`ping-pong`: 3.4
  s, 1.2 of them on a core), where on Linux it's on one all the time; so
  its one-core numbers here aren't its speed on one core. Not explained
  yet; until it is, Erlang's Mac numbers are its `-mc` ones, which flatter
  it where more cores help (`spawn`, `idle-memory`).
- **slight's peak memory is higher than on Linux:** `binary-trees` 356 MB
  (98 on Linux), a waiting process 554 bytes (431). Probably macOS's
  allocator keeping the collector's freed 1 MB chunks resident, and its
  16 KB pages; not checked yet.

### Where the time goes (phase 2's profiles)

Instructions counted by callgrind, on x86-64 (counts don't depend on the
machine's noise; the generated code is alike on both targets):

- **`binary-trees 14`:** collecting is about 60 per cent (`forward`,
  `collect`, and the copying). The collector copies everything live at
  each collection, and collects when the heap reaches twice what was
  live after the last one, so the long-lived tree is copied over and
  over: about a byte copied for each byte allocated. Building tuples is
  13 per cent: `(tuple a b)` makes a list of `a` and `b` and calls
  `rt_tuple`, which counts it and copies it, so each node allocates 64
  bytes, half of them garbage at once. A two-element tuple is 32 bytes
  (allocations are rounded to 16), a cons 16. The program's own code is
  about a quarter.
- **`fib 25`:** all in the program's own code, 48 instructions a call
  (about 61 on AArch64's longer path): every value goes through the
  stack, every arithmetic operation tag-checks both operands, a
  constant's included, `(< n 2)` makes `#true` or `#false` and then tests
  it twice, and the reduction count is a load, a subtraction and a store.
- **`n-body 20000`:** float arithmetic in C (`rt_add`, `rt_mul`...), 27
  per cent; boxing each result (`rt_new_float`), 14; collecting, 11;
  building tuples (as above), 8; the program's own code, the rest.

Two more things the experiments showed: a process with more than 32 MB
live never collects before the 64 MB cap (its next collection would be
at twice what's live), so it fails there even if most of its heap is
garbage; and letting the heap grow three times what's live, not twice,
makes `binary-trees` about 30 per cent faster.

### The cloud machine, x86-64 (Oct 2026)

`bench/results/2026-10-16-0554-linux-intel-xeon-processor-2-10ghz.md`:
a shared 4-core Xeon VM, so noisy, but the ratios hold from run to run.
Times include starting, which matters only for Erlang (0.14 s) and Node
(0.035 s); per-message figures below take Erlang's off.

Sequential, slight's time against each:

- **`fib`:** 0.38 s. C is 5.6 times faster (clang turns some of the
  recursion into a loop), Go 1.8; slight is 1.2 times faster than Node,
  1.7 than Erlang, 12 than Python and 38 than Perl.
- **`binary-trees`:** 3.8 s, the weak spot. Erlang is 4.2 times faster,
  C (a malloc and a free a node) 3.3, Node 3.1, Go on every core 1.8; Go
  on one core is the same; Python is 1.9 times slower, Perl 6.9.
- **`n-body`:** 2.7 s: floats, boxed. C is 52 times faster, Go 39, Node
  20; Erlang, which boxes them too, 1.3. Python is 1.8 times slower, Perl
  3.6.

Actors, on one core each:

- **`ping-pong`:** slight is the fastest: 106 ns a message, Go 220
  (unbuffered channels), Erlang 420.
- **`thread-ring`:** 323 ns a hop, between Go (249) and Erlang (394);
  but at 266 MB, against Go's 3 and Erlang's 37 (below).
- **`spawn`:** slight is the fastest: 250 ns a process (started, its
  message sent, ended), Go 770, Erlang 1,150.
- **`idle-memory`:** 431 bytes a waiting process, Go 2,794 and Erlang
  2,756: a sixth. And the fastest to start, ask and end a million: 0.7 s,
  Go 7.0, Erlang 4.1.
- **`parallel-fib`:** 0.55 s, between Go (0.30) and Erlang (0.81).

What every core gains: on 4 cores `parallel-fib` is 3.7 times faster in
Go and 2.7 in Erlang (3.9 without its start), so 6.8 and 1.8 times faster
than slight. Go's collector uses the other cores too: `binary-trees` 1.7
times faster, `idle-memory` 1.8. Passing a message along gains nothing,
and Erlang's `spawn` is 1.7 times slower on every core.

Two programs were changed when their first numbers looked wrong, to
measure what they were meant to (D194): Erlang's `spawn` keeps its
mailbox off its heap (36 s to 1.3), and Go's `spawn` and `idle-memory`
send on channels that hold every answer, as a mailbox would (15 s to 0.8,
and 9.4 to 7.0).

### What phase 2 has changed

The fixes the profiles led to, so far, each its own decision; numbers
are instructions on the cloud machine, which don't depend on its noise:

- **Tuples built in place** (D195): `(tuple ...)` allocates once, inline,
  with no list made and copied in C. `binary-trees 14` 46 per cent fewer
  instructions; `n-body` 14.
- **Collecting before the cap** (D196): a process with more than 32 MB
  live collects before its heap reaches 64 MB, rather than failing there.
  `binary-trees 18` 0.8 per cent more.
- **Better code for calls, arithmetic and tests** (D197): operands read
  straight into registers, integer literals as immediates, comparisons
  as branches. fib 40 per cent fewer (its path on AArch64 39
  instructions, not 61), and 20 per cent less time; `binary-trees` 8.5
  per cent fewer.
- **Float arithmetic inline** (D198): `+ - * /` on floats in the compiled
  code, the result's box allocated inline, bit for bit what C made.
  `n-body` 29 per cent fewer, and half the time (2.4 s to 1.25); boids
  4.4 per cent fewer.
- **A message with nothing to copy leaves nothing behind** (D199): its
  chunk is freed as it's taken. `thread-ring` 3.3 s and 266 MB to 0.74 s
  and 2.6 MB.
- **A generational collector** (D200): a nursery per process, minor
  collections that copy only what survives it, and major ones, as
  collections were, when the old generation has doubled; no write
  barrier, since nothing old can point at anything young.
  `binary-trees 14` 43 per cent fewer (collecting 3.2 times less), 16
  28 per cent fewer; `n-body 20000` 13 per cent fewer, its collecting
  nearly gone. Times: `binary-trees 18` 2.2 s to 1.7, `n-body` 1.41 to
  1.16. The bigger trees are bigger than the nursery (1 MB at most), so
  they're promoted as they're made, and their garbage left to major
  collections, which copy the long-lived tree each time: collecting is
  still about half of `binary-trees 16`.

Together: `binary-trees 14` takes 28 per cent of the instructions it
did (1,590 million to 450), fib 30 40 per cent fewer (131 to 79),
`n-body 20000` 49 per cent fewer (642 to 328). The cloud machine's times
are too noisy to say more than that `binary-trees 18` went from 3.8 s
to under 2. The M2's runs after each are above.

### What slight should look at next

- **Lists in the prelude** (`deriv`, `nqueens`): `map`, `append`,
  `filter`, `length` and the rest went through `fold/l` and `reverse`,
  with a closure called for each element, and `not` was a call; about
  80 per cent of `deriv`'s instructions. Done (D203): each its own loop,
  and `not` inline, `deriv` and `nqueens` 35 per cent fewer
  instructions. What's left there is mostly the call, through a
  closure, to what `map` is given; folds the compiler specialises to
  their lambdas (PLAN.md, D203's B) are for later.
- **Floats** (`n-body`): now the widest gap by far, 20 to 36 times the
  compiled languages. PLAN.md's "Faster floats", with this as a second
  benchmark beside boids. Their arithmetic is inline (D198); each
  result is still a 16-byte box, allocated and later collected, and
  comparisons and `sqrt` are still C. Erlang, which boxes them too, is
  only 1.2 times faster.
- **Allocating and collecting** (`binary-trees`): 2.6 times Erlang on
  the M2, whose heaps are per-process and copied as slight's are (1.9
  times Erlang on every core, and level with it on one scheduler, since
  D239).
  Building tuples in place (D195) halved the allocation, and the
  generational collector (D200) copies what dies young no more; what's
  left is the trees bigger than the nursery, and major collections
  copying the long-lived tree, about half of `binary-trees 16`'s
  instructions. A bigger nursery for a process that promotes a lot, or
  an old generation that isn't copied whole, would be the next steps,
  if it's wanted. And the objects are bigger than they need be: a
  two-element tuple is 32 bytes (a header and two words, padded to a
  multiple of 16), where a pair is 16 and Erlang's tuple 24.
  `binary-trees 14` with pairs in place of tuples takes 30 per cent
  fewer instructions (318 million to 453); objects in multiples of 8
  bytes would make a two-element tuple 24.
- **A busy process's garbage:** a heap under 256 KB (`GC_MIN`) is never
  collected, so each of `thread-ring`'s 503 processes kept up to that
  much of its messages' chunks: 266 MB, where Erlang needs 37. Done for
  messages with nothing to copy (D199): 2.6 MB. Not for messages with
  something in them, nor for what minor collections promote and is
  dropped after: the airline's 300 aircraft, ticked with `(:tick t)`
  each minute, hold 84 MB, 61 with bare integers (D210).
- **One structure's size:** done (D202). A heap's cap is 1 GB, or what
  `SLIGHT_HEAP_MAX` says, where it was 64 MB, which held a structure of
  about 56 MB at most. The cost: a major collection takes 0.5 to 1 ms
  for each MB live, everything else waiting, so a process with a few
  hundred MB live holds the rest up for a quarter of a second.
- **Calls** (`fib`): 1.6 times Go on the M2 (2.5 at first), which checks
  its stack at every call too; slight also counts reductions, and checks
  for overflow. Erlang's JIT, which counts reductions as well, is now
  1.4 times slower than slight. D197 took 40 per cent of fib's
  instructions out; the rest is the frame (every local in a slot,
  zeroed when the function calls another) and the checks.
- **Copying a message** (`payload`, D237): a list of 100 integers there
  and back takes slight 1.1 s a million times on the cloud machine,
  against 0.17 for `ping-pong`, Go's and tokio's 0.3 to 0.4 hardly more
  than theirs, since they don't copy; on the M2 Max 0.84 s, 4.3 times
  Go's and 5.4 tokio's, and 1.6 times faster than Erlang's copy. 84 per cent of the instructions
  are the copy's walk, about 73 a cell, most of them bookkeeping: each
  cell checked against both ranges of static data, scratch's bounds,
  each pointer noted for the move into the chunk, and the sizes of
  scratch and the notes, globals, loaded and stored back at each cell.
  D240 made the walk the collector's: 41 instructions a cell where it
  took 68 (measured again), `payload` 33 per cent fewer instructions and
  0.48 s to 0.34 at 300,000 on the cloud machine, and on the M2 Max
  0.84 s to 0.56, 2.8 times Go's where it was 4.3; and values with no
  pointer in them aren't walked, `spawn`, `thread-ring` and `fan-in` 7 to
  9 per cent fewer instructions, 4 to 9 per cent faster on the M2. Two
  walks, the first adding up the bytes and the second copying straight
  into the chunk, might do better still, but would undo D177.
- **Collection pauses** (`latency`, D238): a major collection holds up
  every process, the program's one core copying the heap: about 2.5 ms
  a MB live on the cloud machine, 400 ms for 160 MB, and a ping's wait
  at the 99th percentile 375 ms beside such a heap; on the M2 Max 0.4
  ms a MB, 61 ms at most, where Go, Erlang and tokio stay under 0.1
  ms. Busy processes cost
  others little (5 to 7 µs at the median), where Go on one core costs
  10 ms and tokio all of the busy task's time. D239 made the copy's
  walk take 21 instructions a cell where it took 52: on the cloud
  machine the worst pause beside 160 MB went from 197 ms to 133, at the
  median of five runs, and on the M2 Max from 61 ms to 47. A second thread for big collections (D239's C)
  waits till it's needed; D200's next steps, a bigger nursery or an old
  generation that isn't copied whole, would shorten the pauses too; so
  does keeping a process's heap small.
- **Simulation** (`bank`, D241): slight is 11 times faster than SimPy
  on the cloud machine, and 27 times slower than an event list in C. A
  customer is a process, three messages and two timers, each a malloc;
  and every process that ends keeps its exit record, 40 MB a million,
  for good (D88), which a long simulation can't afford. And the virtual
  clock is a switch for the whole program, so a simulation that also
  talks to the world (the airline's services) keeps its own time.
- **A mailbox under many senders** (`fan-in`, D201): nothing slows a
  sender whose receiver is behind, so on one core nearly every message
  waits at once, 640 MB at ten million, as in Erlang; Go's channel
  blocks its senders. And a mailbox isn't counted in the heap's cap.
- **One core** (`parallel-fib`): Go on every core is 15 times faster,
  Erlang 5.6. Running on more than one is a design question of its own
  (BACKGROUND.md, multiple cores), not a fix.
- **Memory on macOS** (above): whether the collector should keep and
  reuse its chunks rather than free them, which might also make
  `binary-trees` faster.

