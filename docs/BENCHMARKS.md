# Benchmarks

How slight performs, and how it compares with other languages (D194).
`bench/` has the programs and the harness; `bench/results/` has every
run worth keeping, as JSON and as a Markdown summary.

    make bench                  # or: python3 bench/run.py
    make bench BENCH=--quick    # small sizes, to see that everything works
    python3 bench/run.py --only fib,n-body --langs slight,c --runs 10

A full run takes about half an hour, mostly Perl and Python. Languages
whose tools aren't installed are left out, and said to be. On a Mac,
`brew install go erlang`; Node, Perl and Python are there already
(Node for the compiler, the others with Xcode's tools).

## What's measured

Three questions, which need different programs:

1. **Sequential code:** how good the compiled code is at calls, integer
   arithmetic, allocating and collecting, and floats. Against C, the
   floor; Go and Erlang; Node, the everyday reference; Perl and Python.
2. **The actor runtime:** what a message, a switch between processes, a
   process started, and a process waiting cost. Against Erlang, the same
   model (per-process heaps, copied messages, preemption by reductions),
   and Go, goroutines and channels.
3. **slight against itself, over time:** later, with the examples
   (boids), `html->text`, JSON and HTTP: comparing those across
   languages would mostly compare libraries.

Chez Scheme, a native Lisp, comes in phase 2, with more of the Gabriel and
Benchmarks Game programs (`tak`, `nqueens`, `deriv`, `spectral-norm`) and
Savina's fan-in.

## The programs

Each prints only numbers, one to a line, since slight's last line is its
root's value; the harness checks every output against what it works out
itself (or, for `n-body`, against C's).

- **`startup`:** prints 0. What every other time includes: the Erlang
  VM's start is about 0.15 seconds of each Erlang time, and Node's 0.03.
- **`fib 38`:** doubly recursive Fibonacci: calls and integer
  arithmetic.
- **`binary-trees 18`:** the Benchmarks Game's, without its workers:
  trees made and checked, allocation and collection. Its usual size is
  21, but the stretch tree of depth 22 has 8 million nodes, and the
  largest that fits in one slight process (its heap is capped at 64 MB,
  and collecting copies) is the stretch tree of depth 19, so 18.
- **`n-body 1000000`:** the Benchmarks Game's, five bodies' orbits: float
  arithmetic, which slight boxes. Each version does the arithmetic in
  the C version's order, so the energies agree, printed times 10^9.
- **`ping-pong 2000000`:** round trips between two processes.
- **`thread-ring 10000000`:** the Benchmarks Game's: 503 processes in a
  ring pass a token on, 10 million times.
- **`spawn 1000000`:** processes started one after another, each sending
  its number back and ending.
- **`idle-memory 1000000`:** a million processes all waiting for a
  message at once, then each asked for one. Its peak memory, less
  `startup`'s, over a million is what a waiting process costs (with its
  pid in a list, as every version keeps them).
- **`parallel-fib 30`:** 64 processes each work out `fib(30)`: work more
  than one core can share.

The sequential ones are in all seven languages, the actor ones in slight,
Go and Erlang. Each is written the way its language is usually written,
and does the same work: the same algorithm, the same output. slight's
bend to slight (D84): nothing was added to the language for them, and
the Benchmarks Game programs that need what it hasn't got (arrays that
change, hash tables, bignums, regular expressions) aren't here.

## How it's measured

- Each program is built once (slight's for the machine's own target),
  run once to check it, then timed 5 times; the programs take turns, so
  the machine's drift falls on all of them alike. One that takes long
  gets fewer runs, never under 3. The summary gives the median, the
  fastest, the CPU time, and the peak memory.
- Time, CPU and memory come from `wait4`, in `bench/rusage.c`, a small
  process that starts each program: on Linux a process forked from
  Python starts with Python's memory counted in its peak.
- **Cores:** slight runs every process on one core. Go and Erlang run on
  one core too (`GOMAXPROCS=1`, `+S 1:1`), the fair comparison, and again
  with their defaults, every core (`go-mc`, `erlang-mc`), to see what
  that gains. Node's collector has threads of its own either way.
- CPU time over wall time shows more than one core at work. Erlang's
  schedulers spin a while before they sleep, so its CPU time overstates
  its work.
- Erlang allows 262,144 processes unless told otherwise, and a higher
  limit costs memory from the start, so `spawn` and `idle-memory` raise
  it as far as they need (`+P`), and nothing else does.
- Timings under qemu mean nothing, so AArch64 is measured on a Mac. The
  cloud machine's x86-64 numbers are noisy, but fine for ratios.

## Results

Each run's full tables are in `bench/results/`. What they say:

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
  than slight. Still 303 MB, against 356.
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

Together: `binary-trees 14` takes half the instructions it did (1,590
million to 789), fib 30 40 per cent fewer (131 to 79), `n-body 20000` 41
per cent fewer (642 to 378). The cloud machine's times are too noisy to
say more than that `binary-trees 18` went from 3.8 s to 2 or so; the
M2's run after them is to come.

### What slight should look at (phase 2)

- **Allocating and collecting** (`binary-trees`): four times Erlang,
  whose heaps are per-process and copied as slight's are. Building
  tuples in place (D195) halved the allocation; the collector, which
  copies everything live at each collection, is the rest: a
  generational one is next on the list.
- **Floats** (`n-body`): PLAN.md's "Faster floats", with this as a second
  benchmark beside boids. Their arithmetic is inline now (D198); each
  result is still a 16-byte box, and comparisons and `sqrt` are still C.
- **A busy process's garbage:** a heap under 256 KB (`GC_MIN`) is never
  collected, so each of `thread-ring`'s 503 processes keeps up to that
  much of its messages' chunks: 266 MB, where Erlang needs 37.
- **One structure's size:** the 64 MB cap on a heap, with a copying
  collector, holds a structure of about 25 MB at most (the stretch tree
  of depth 19 doesn't fit: 2 million two-element tuples of 32 bytes),
  which is why `binary-trees` is 18 here. A process with more live than
  half the cap now collects before the cap (D196).
- **Calls** (`fib`): 2.5 times Go on the M2 (1.8 on Linux), which checks
  its stack at every call too; slight also counts reductions, and checks
  for overflow. Erlang's JIT, which counts reductions as well, is a
  little faster than slight on the M2 (1.1 times). D197 took a third of
  fib's instructions out; the rest is the frame (every local in a slot,
  zeroed when the function calls another) and the checks.
- **Memory on macOS** (above): whether the collector should keep and
  reuse its chunks rather than free them, which might also make
  `binary-trees` faster.

