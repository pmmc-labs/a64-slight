# a64-slight

slight, compiled ahead of time to native AArch64.

slight is a small, fully immutable Lisp built on actors: `fork`, `send`,
`recv`, `join`. Mutable state lives in "data structure" actors that keep it
in the arguments of a tail-recursive loop. This version keeps the user
surface of [ts-slight](https://github.com/pmmc-labs/ts-slight) and replaces
everything underneath with:

- **an AOT compiler** (TypeScript for now; self-hosting later) that turns a
  whole program into one AArch64 assembly file;
- **a small actor runtime** in C and assembly: per-process heaps, a run
  queue, preemption by reduction counting, and stacks that processes hold
  only while they are running.

One rule makes the runtime small: `recv` may only appear as the whole body
of a top-level function, and functions that can reach it may only be
tail-called. So the stack is empty whenever a process waits, a waiting
process is just (function, arguments, mailbox), and collection happens only
where the roots are those arguments.

```lisp
(defun counter (n)
    (recv
        ((:inc)       (counter (+ n 1)))
        ((:get from)  (send from (list :count n)) (counter n))
        ((:stop)      n)))

(let c (fork (counter 0)))
(send c (list :inc))
(send c (list :get $$))
```

Targets macOS on Apple Silicon first, and AArch64 Linux from the same code.

## Building

```
npm install          # once: typescript, for `make check`
make test            # unit tests, runtime header check, golden tests
node bin/slightc.ts -o hello t/000-int.slight && ./hello
```

On an x86 machine it cross-compiles and runs under qemu; see `CLAUDE.md`
for the packages.

## Status

The design is settled, and implementation is through step 9 of the plan:
integers, floats, strings, booleans, symbols, lists, `cond`/`let`/`do`,
functions with tail calls, closures, a prelude, and processes (`fork`,
`send`, `recv`, `join`, `monitor`, `kill`, `raise`, preemption, faults
that end just their process) compile to native binaries, which pass
their tests on macOS and under qemu, with a per-process copying
collector that runs only when an actor waits for a message. A million
processes passing a message down a chain run in a few seconds. Next:
timers, the terminal and files. See:

- [`docs/DESIGN.md`](docs/DESIGN.md): the design
- [`docs/PLAN.md`](docs/PLAN.md): the build order
- [`docs/DECISIONS.md`](docs/DECISIONS.md): decisions and their reasons
- [`docs/BACKGROUND.md`](docs/BACKGROUND.md): history and prior art
- [`spike/aarch64/`](spike/aarch64/): the native spike that proved the
  runtime model (232 ns message round trips on an M2 Max)
- [`reference/`](reference/README.md): files from ts-slight and ts-cpi
