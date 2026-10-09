# a64-slight

slight is a small Lisp built on actors, in which nothing is ever
mutated. This is its compiler, which turns a whole program into native
code for AArch64 (macOS on Apple Silicon, and Linux) or x86-64, linked
with a small actor runtime in C and assembly.

A slight program is processes sending each other messages. `fork`
starts a process, `send` copies a value into its mailbox, and `recv`
takes the next message. What would be mutable state elsewhere lives in a
process, in the arguments of the function it loops through, one call per
message. Errors are values: a process ends with `(:ok value)` or
`(:error reason)`, and a fault such as an integer overflow ends only the
process it happens in.

```lisp
; A counter is a process: its state is the argument of a function that
; waits for messages.
(defun counter (n)
    (recv
        ((:inc)      (counter (+ n 1)))
        ((:get from) (send from (list :count n)) (counter n))))

; Asking means sending, then waiting for the answer in a function of its
; own, since only a whole function body can wait.
(defun ask (c)
    (send c (list :get $$))
    (answer))

(defun answer ()
    (recv
        ((:count n) n)))

(let c (fork (counter 0)))
(dotimes 0 3 (lambda (i) (send c (list :inc))))
(ask c)
```

This prints `3`: the value of the program's last form.

One rule keeps the runtime small: `recv` can only be the whole body of a
top-level function, and a function that can reach one can only be
tail-called. So a process waiting for a message has nothing on its
stack. It's just a function, its arguments and a mailbox; it gives its
stack back while it waits, and that's when its heap is collected, with
the arguments as the only roots.

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

Next is the groundwork HTTP needs (step 12): reading a device in chunks,
JSON and s-expressions parsed in C, inline documentation, collecting
outside `recv`, C libraries and TLS, and looking up host names. Then
where `recv` can go (13), and HTTP, written in slight on top of TCP
(14). Self-hosting waits until the language settles.

## Documents

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
