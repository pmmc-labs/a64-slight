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
Ubuntu, `apt-get install chezscheme`.

## What's measured

Three questions, which need different programs:

1. **Sequential code:** how good the compiled code is at calls, integer
   arithmetic, allocating and collecting, and floats. Against C, the
   floor; Go and Erlang; Node, the everyday reference; Perl and Python;
   and Chez Scheme, a native Lisp, the nearest thing to slight's
   compiled code (D201).
2. **The actor runtime:** what a message, a switch between processes, a
   process started, and a process waiting cost. Against Erlang, the same
   model (per-process heaps, copied messages, preemption by reductions),
   and Go, goroutines and channels.
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
  21, but the stretch tree of depth 22 has 8 million nodes, and the
  largest that fits in one slight process (its heap is capped at 64 MB,
  and collecting copies) is the stretch tree of depth 19, so 18.
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
  3.12 on); the norm printed times 10^9.
- **`ping-pong 2000000`:** round trips between two processes.
- **`thread-ring 10000000`:** the Benchmarks Game's: 503 processes in a
  ring pass a token on, 10 million times.
- **`fan-in 10000000`:** 100 processes each send their number 100,000
  times to one, which sums them: many senders, one mailbox. Go's channel
  holds 1,024 (unbuffered, every message is a switch, 4.7 times slower;
  from 100 up the size makes no difference); Erlang's receiver keeps its
  queue off its heap, as `spawn`'s does.
- **`spawn 1000000`:** processes started one after another, each sending
  its number back and ending.
- **`idle-memory 1000000`:** a million processes all waiting for a
  message at once, then each asked for one. Its peak memory, less
  `startup`'s, over a million is what a waiting process costs (with its
  pid in a list, as every version keeps them).
- **`parallel-fib 30`:** 64 processes each work out `fib(30)`: work more
  than one core can share.

The sequential ones are in all eight languages, the actor ones in slight,
Go and Erlang. Each is written the way its language is usually written,
and does the same work: the same algorithm, the same output. slight's
bend to slight (D84): nothing was added to the language for them, and
the Benchmarks Game programs that need what it hasn't got (arrays that
change, hash tables, bignums, regular expressions) aren't here.

## How it's measured

- Each program is built once (slight's for the machine's own target,
  Chez's with `compile-program`, so its runs don't include compiling),
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
- Chez runs at its default optimize level, which is safe (as slight is:
  every operation checks its operands), and its programs use Scheme's
  ordinary arithmetic, as Scheme is usually written, not `fl+` or
  `fx+`.
- Timings under qemu mean nothing, so AArch64 is measured on a Mac. The
  cloud machine's x86-64 numbers are noisy, but fine for ratios.

## Results

Each run's full tables are in `bench/results/`. What they say:

### The M2 Max after D203

`bench/results/2026-10-17-1311-macos-apple-m2-max.md`, at `448aa6d`: a
steady run, every other implementation within a few per cent of the run
before, and Node's start back at 0.053 s. slight's `deriv` 0.575 s to
0.380 (1.5 times faster), `nqueens` 0.841 to 0.703 (1.2), `n-body` and
`spectral-norm` a few per cent; the rest the same. Now, each one's time
over slight's: `deriv` Chez 0.40, Erlang (every core) 0.62, Node 1.49,
Go 2.81; `nqueens` Chez 0.50, Erlang 0.55, Go 0.82, Node 3.19. A page of
charts of this run, by category and benchmark, is the "Where slight
lands" dashboard.

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
  the M2, whose heaps are per-process and copied as slight's are.
  Building tuples in place (D195) halved the allocation, and the
  generational collector (D200) copies what dies young no more; what's
  left is the trees bigger than the nursery, and major collections
  copying the long-lived tree, about half of `binary-trees 16`'s
  instructions. A bigger nursery for a process that promotes a lot, or
  an old generation that isn't copied whole, would be the next steps,
  if it's wanted.
- **A busy process's garbage:** a heap under 256 KB (`GC_MIN`) is never
  collected, so each of `thread-ring`'s 503 processes kept up to that
  much of its messages' chunks: 266 MB, where Erlang needs 37. Done for
  messages with nothing to copy (D199): 2.6 MB.
- **One structure's size:** the 64 MB cap on a heap, with a copying
  collector, holds a structure of about 25 MB at most (the stretch tree
  of depth 19 doesn't fit: 2 million two-element tuples of 32 bytes),
  which is why `binary-trees` is 18 here. A process with more live than
  half the cap now collects before the cap (D196).
- **Calls** (`fib`): 1.6 times Go on the M2 (2.5 at first), which checks
  its stack at every call too; slight also counts reductions, and checks
  for overflow. Erlang's JIT, which counts reductions as well, is now
  1.4 times slower than slight. D197 took 40 per cent of fib's
  instructions out; the rest is the frame (every local in a slot,
  zeroed when the function calls another) and the checks.
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

