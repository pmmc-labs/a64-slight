# a64-slight

slight (an immutable, actor-based Lisp) compiled ahead of time to native
AArch64, on a small actor runtime in C and assembly. The compiler is
TypeScript for now and should self-host later.

## Status

**Steps 0–9 and 10a–10d of [`docs/PLAN.md`](docs/PLAN.md) are done**: the
reader, and a compiler for integers, floats, `#true`/`#false`, `()`,
symbols, lists (a heap per process of chunks, capped at 64 MB, and
collected when a receive function waits for a message), strings, closures
(`lambda`, functions and builtins as values, `apply`), arithmetic
(integers inline; floats and mixed in C), structural `eq?`, type
predicates, the string and math builtins (C, in `runtime/`), `cond`,
`let`, `do`, `pprint`, `tty/write`, and top-level `defun`s with calls and
tail calls (arguments in `x0`–`x7`; stack and reduction checks at every
function entry). The prelude (`lib/prelude.slight`) is compiled with every
program; `lib/test.slight` is a TAP library. Processes: `fork`, `send`,
`recv` (receive functions, with the `recv` rule checked by `classify.ts`),
`$$`, `^$$`, `yield`, preemption, a FIFO run queue, pooled stacks that a
process waiting in `recv` gives back, and the lifecycle: `join`,
`monitor`, `kill`, `raise`, exit records, and faults that end just their
process with `(:error (kind value site))` (`runtime/process.c`). Timers:
`after`, `sleep`, and a virtual clock for tests (`SLIGHT_CLOCK=virtual`).
The terminal: `connect :keypress`, raw mode, decoded keys, the screen's
size (`runtime/tty.c`). Files: `connect :fs/read` (`:fs/write`,
`:fs/append`) opens a file on a device, a pid the runtime serves, and
`disconnect` closes it; `slurp` and `spew` are slight, in an opt-in
`lib/fs.slight`. Sockets: `connect :tcp "host:port"` and `connect
:tcp/listen port` open sockets as devices too, waited for in `select()`,
and `(connect conn expr)` hands an accepted connection to a process.
Fifteen of ts-slight's examples are ported (`examples/`; all but
`ping-pong-tournament` are golden tests). `make test` passes under qemu on
x86 Linux, and natively on macOS (Stevan runs it on his M2 Max after every
step, and reports only failures). **Next: step 10e** (HTTP, written in
slight on `:tcp`; its open points are in `docs/PLAN.md`, to settle with
Stevan first). Update this section as steps land.

## Read first, in this order

1. [`docs/DESIGN.md`](docs/DESIGN.md): the language, runtime and compiler
   as agreed. **(open)** marks proposals that haven't been confirmed.
2. [`docs/PLAN.md`](docs/PLAN.md): the build order and the example-port
   table.
3. [`docs/DECISIONS.md`](docs/DECISIONS.md): why, and what was turned down.
   Check it before "improving" something; it may have been decided against.
4. [`docs/BACKGROUND.md`](docs/BACKGROUND.md): AVM, VM3, the spike, prior
   art, and notes on other targets, multiple cores and embedded boards.
   Optional.

Reference material (read-only, never built): [`reference/`](reference/README.md)
holds ts-slight's examples and libraries (the target user surface) and the
ts-cpi reader to port. [`spike/`](spike/) holds the AArch64 spike, which
works and has the runtime pieces to borrow.

## Working rules

- **Aggressively simple** is the main design rule. When a feature needs
  machinery, say so and offer to cut or change it.
- **Propose before building** any change to language semantics, a new
  special form or builtin, a change to the `recv` rule, or anything marked
  (open). Give options and a recommendation. Record the outcome in
  `docs/DECISIONS.md` (who decided, why, what was rejected) and update
  `docs/DESIGN.md`.
- Keep a working compiler with passing tests at the end of every step
  (Ghuloum). Each step adds golden tests.
- **Examples bend to the language, not the other way round.** When a
  ts-slight example needs something the compiler or runtime doesn't do,
  change the example, or leave it unported; don't add to the language to
  make it pass (D84).
- Verify a reported misbehavior against an independent implementation
  before calling it a bug.
- When editing with scripts, assert that each replacement matched.
- Test timing uses a virtual clock; real time is never used in tests.

## Code style

- **4-space indent everywhere**: TypeScript, `.slight`, C, assembly
  operands, JSON, and code blocks in Markdown.
- slight code indents 4 spaces per open parenthesis:

  ```lisp
  (defun sum-to (n acc)
      (cond
          ((== n 0) acc)
          (#true    (sum-to (- n 1) (+ acc n)))))
  ```

- The compiler is **slight-shaped**: pure functions over immutable
  s-expressions, association lists for environments, no classes (except
  `CompileError`, which plays the part of `raise`). It will be ported to
  slight line by line. Node has no tail calls, so a loop is fine where
  slight would use a tail-recursive function. Reassign only locals, and
  never mutate shared data. Prefer character tests to regexes, since slight
  has no regexes.
- In C headers shared with assembly, prefix every name `RT_`/`rt_` (macOS's
  `<stdlib.h>` pulls in `<sys/wait.h>`, which has a `P_PID`).
- Comments explain why, not what. Match the density of the spike's code.

## Toolchain

- **macOS on Apple Silicon:** Xcode's clang, Node ≥ 22.18. Everything runs
  natively.
- **x86-64 Linux (this is how cloud sessions develop):** cross-compile with
  clang and run under qemu:

  ```
  apt-get install -y gcc-aarch64-linux-gnu qemu-user   # sysroot + qemu-aarch64
  # clang and lld must also be installed (clang-18 works)
  ```

  `slightc` uses `clang --target=aarch64-linux-gnu -fuse-ld=lld -static`,
  and binaries run with `qemu-aarch64`. Timings under qemu are meaningless.
  Don't add `--sysroot=/usr/aarch64-linux-gnu` (the spike's Makefile has
  it): clang finds the cross toolchain without it, and with it lld can't
  link `-lm`, because the sysroot's `libm.a` is a linker script with
  absolute paths.
- Check the setup with `make -C spike/aarch64 test`. It should print five
  `ok` lines.
- Use clang's integrated assembler, not GNU `as` (it rejects some of the
  macros).
- Don't use `-Werror`; a different Apple clang version shouldn't break the
  build.

## Layout

| Path | |
|---|---|
| `bin/slightc.ts` | The driver: read, compile, write `out.S`, link with clang |
| `compiler/src/` | `sexp.ts` (the data), `reader.ts`, `classify.ts` (the `recv` rule: which functions are state functions), `codegen.ts`, `values.ts` (value encodings; must match `rt.h`), `errors.ts` |
| `compiler/tests/` | Unit tests, `node:test` |
| `runtime/` | `rt.h` (tags and offsets shared with assembly), `asm.h` (assembler macros, included by generated code), `rt_asm.S` (context switch, process entry, `apply`), `rt.c` (the core: faults, allocation, printing, equality), `process.c` (processes, run queue, stacks, heap chunks, message copying, the collector, timers, reading keys, files and sockets, `main`), `tty.c` (raw mode, decoding keys, the screen's size), `strings.c`, `numbers.c` |
| `lib/` | `prelude.slight` (compiled with every program), `test.slight` (TAP, opt-in), `fs.slight` (`slurp` and `spew`, opt-in) |
| `examples/` | ts-slight's examples, ported; each with a `.expected` is a golden test |
| `t/` | Golden tests: `NNN-name.slight` + `NNN-name.expected`; `run.sh`; `headers.c`; `models/` (Python models that produced expected output); `data/` (files the tests read; tests write under `build/t/`). A first line `; with: lib/test.slight` compiles that in too, and a line `; stdin: bytes` (printf `%b` escapes; `\033` is ESC) is the test's stdin. |
| `build/` | Output (ignored) |

## Commands

- `npm install` once, for `typescript` (used only by `make check`).
- `make test`: unit tests, the runtime header check, then the golden tests.
- `make unit`, `make golden`, `make headers`: one at a time.
  `t/run.sh t/003-int-max.slight` runs one golden test. A golden test
  that runs longer than `TIMEOUT` seconds (default 60) is killed and
  fails.
- `make check`: `tsc --noEmit`.
- `node bin/slightc.ts -o out file.slight ...`: compile and link. It writes
  `out.S` next to `out`. `-S` writes only the assembly. On x86, run the
  result with `qemu-aarch64 ./out`.
- Exit codes from `slightc`: 0 ok, 1 compile error, 2 usage or toolchain
  error. `SLIGHT_CC` overrides the C compiler command.
- `SLIGHT_POISON=1` when running a compiled program makes the collector
  fill what it frees with garbage, so a pointer it missed fails at once.
  `t/run.sh` sets it.
- `SLIGHT_CLOCK=virtual` when running a compiled program swaps the real
  clock for a virtual one: it starts at 0 and moves only when nothing can
  run, straight to the next timer. `t/run.sh` sets it too.

The runtime prints the root process's value, followed by a newline, once
nothing can run and no timer is pending. Faults are logged to stderr as they happen (`fault: ...`
in the root, `fault in #<pid N>: ...` elsewhere), and so are dead letters
and an error that ends the root (`error: ...`). The program exits 1 if
the root ended with an error or never ended (`deadlock: ...`).

The golden tests check stdout and stderr together, plus `exit: N` when the
status isn't 0. Write expected output by working it out independently (by
hand, or in Python: an output that depends on scheduling can come from a
small model of the run queue, as in `t/models/`), never by
copying what the compiler printed. Don't let a test depend on the last bit
of a libm function other than `sqrt`: macOS's and glibc's differ (D76).
