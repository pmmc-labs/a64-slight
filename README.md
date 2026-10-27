# a64-slight

slight is a small Lisp built on actors, in which nothing is ever
mutated. This is its compiler, which turns a whole program into native
code for AArch64 (macOS on Apple Silicon, and Linux) or x86-64, linked
with a small actor runtime in C and assembly.

A slight program is processes sending each other messages. `spawn`
starts an actor, `send` copies a value into its mailbox, and `recv`
takes the next message; `fork` starts a task, which computes a value
that `join` waits for. What would be mutable state elsewhere lives in an
actor, in the arguments of the function it loops through, one call per
message. Errors are values: a process ends with `(:ok value)` or
`(:error reason)`, and a fault such as an integer overflow ends only the
process it happens in.

<!-- code: docs/guide/code/readme-counter.slight -->
```lisp
; A counter is an actor: its state is the argument of a function that
; waits for messages.
(defun counter (n)
    (recv
        ((:inc)    (counter (+ n 1)))
        ((:get to) (reply to n) (counter n))))

; ask sends a request with an address to answer at, and waits there.
(let c (spawn (counter 0)))
(dotimes 0 3 (lambda (i) (send c (list :inc))))
(ask c (list :get))
```

This prints `3`: the value of the program's last form.

One rule keeps the runtime small: `recv` can go only where a tail call
could, and a function that can reach one can only be tail-called. The
compiler splits a function at each `recv`, so every wait is the whole
body of a function of its own, and a process waiting for a message has
nothing on its stack. It's just a function, its arguments and a mailbox;
it gives its stack back while it waits. A heap is collected then, or at
any function's entry, once it has grown enough.

The compiler is TypeScript, run directly by Node, and is written to be
ported to slight later. The runtime schedules processes on one core, with
preemption, per-process heaps, timers, and devices for the terminal,
files and TCP sockets.

## Building and testing

You need Node 22.18 or later and clang. On macOS on Apple Silicon,
Xcode's clang is enough, and x86-64 builds run under Rosetta 2. On
x86-64 Linux, x86-64 builds run natively; for AArch64 you also need lld,
the cross libc and qemu (`apt-get install -y gcc-aarch64-linux-gnu
qemu-user`), and binaries run under `qemu-aarch64`.

```
npm install       # once: typescript, for make check
make test         # unit tests, header check, golden tests
node bin/slightc.ts -o counter counter.slight
./counter         # qemu-aarch64 ./counter on x86-64
```

`slightc` compiles for AArch64 unless given `--target x86_64`.
`make test` runs the golden tests (`t/` and `examples/`) for every
target the machine can run. [`CLAUDE.md`](CLAUDE.md) has the details:
the toolchain, the layout of the source, and the commands.

## Status

Steps 0 to 11 of the plan are done. The whole language in
[`docs/LANGUAGE.md`](docs/LANGUAGE.md) compiles and runs: integers,
floats, strings (bytes, with builtins that count UTF-8 characters too),
symbols, lists, closures, a prelude in slight, and processes (`fork`,
`send`, `recv`, `join`, `monitor`, `kill`, `raise`, preemption, faults
that end just their process), with a copying collector per process.
Timers, the terminal, files and TCP sockets work, and so do includes and
the program's arguments. Every test passes on both targets, on Linux and
on macOS. A million processes passing a message down a chain take a few
seconds.

Steps 12 to 14 are done too, but for C libraries and TLS: reading a
device in chunks, by length, and as JSON, s-expressions or source; JSON
and s-expressions in C; `recv` anywhere a tail call could go; and HTTP,
a server and a client written in slight on top of TCP. Step 15, agents,
has begun with a local model over plain HTTP: a reply's body streamed
to its caller as messages, and an agent that holds a conversation with
ollama (`examples/chat.slight`). Next is `:exec`, for tools, then TLS
when a hosted model is wanted. Self-hosting waits until the language
settles.

## Documents

- [`docs/guide/`](docs/guide/README.md): the guide, for learning slight:
  a tour, a tutorial that builds a key-value store, and (to come) the
  ideas behind it, and recipes.
- [`docs/LANGUAGE.md`](docs/LANGUAGE.md): the language: every form and
  builtin, and how a program runs.
- [`docs/DESIGN.md`](docs/DESIGN.md): the design of the language, the
  runtime and the compiler, including value layouts and the calling
  convention.
- [`docs/PLAN.md`](docs/PLAN.md): the build order, step by step, done
  and to come.
- [`docs/DECISIONS.md`](docs/DECISIONS.md): what was decided, why, and
  what was turned down.
- [`docs/BACKGROUND.md`](docs/BACKGROUND.md): how slight got here, from
  earlier actor VMs and a TypeScript interpreter, and ideas parked for
  later (other targets, multiple cores, WebAssembly).
- [`CLAUDE.md`](CLAUDE.md): working notes for development: toolchain,
  layout, commands and conventions.

## License

Copyright (c) 2026 Stevan Little. Released under the MIT No Attribution
License (MIT-0), so neither copies of it nor the programs it compiles
need carry the notice; the text is in [`LICENSE`](LICENSE).

Two parts come from elsewhere, under their own licences:
`t/data/json-suite/` is JSONTestSuite's parsing tests (MIT, Nicolas
Seriot; its `LICENSE` is beside them), and `runtime/unicode.c` is made
from the Unicode Character Database, under the Unicode License v3
(<https://www.unicode.org/license.txt>).
