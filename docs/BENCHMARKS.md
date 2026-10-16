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

### What slight should look at (phase 2)

- **Allocating and collecting** (`binary-trees`): four times Erlang,
  whose heaps are per-process and copied as slight's are.
- **Floats** (`n-body`): PLAN.md's "Faster floats", with this as a second
  benchmark beside boids.
- **A busy process's garbage:** a heap under 256 KB (`GC_MIN`) is never
  collected, so each of `thread-ring`'s 503 processes keeps up to that
  much of its messages' chunks: 266 MB, where Erlang needs 37.
- **One structure's size:** the 64 MB cap on a heap, with a copying
  collector, holds a structure of about 25 MB at most (the stretch tree
  of depth 19 doesn't fit), which is why `binary-trees` is 18 here.
- **Calls** (`fib`): 1.8 times Go, which checks its stack at every call
  too; slight also counts reductions, and checks for overflow.

### The M2 Max

To come: the reference, AArch64 natively.
