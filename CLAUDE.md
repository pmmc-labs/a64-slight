# a64-slight

slight (an immutable, actor-based Lisp) compiled ahead of time to native
AArch64, and x86-64 as a second target, on a small actor runtime in C and
assembly. The compiler is TypeScript for now and should self-host later.

## Status

**Steps 0–9, 10a–10e and 11 of [`docs/PLAN.md`](docs/PLAN.md) are done**:
the reader, and a compiler for integers, floats, `#true`/`#false`, `()`,
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
`lib/fs.slight`; data structures as processes (a cell, a dictionary, a
queue, a channel, and `ask`) are in `lib/ds.slight` (D157). Sockets:
`connect :tcp "host:port"` and `connect :tcp/listen port` open sockets
as devices too, waited for in `select()`, and `(connect conn expr)`
hands an accepted connection to a process.
`(@include "path")` and `(@include :name)` splice in other files (each
once); `@ARGV`, the program's arguments, is the top level's parameter
(D144); and `if`, `when`, `case`, `and` and `or` are made into `cond`
(`compiler/src/expand.ts`, a pass between the reader and the compiler).
A second target, x86-64: the code generator emits through a target
interface (`compiler/src/target.ts`; `aarch64.ts`, `x86_64.ts`), and
`slightc --target x86_64` builds for it (D145, D146). Strings are bytes;
beside the byte builtins, the `utf8/` ones count characters (a bad byte
is a character of its own, as in Go), with `ord` and `chr` in the
prelude, and `string->float` (D148).
`examples/` has fifteen example programs (all but `ping-pong-tournament`
are golden tests); new ones are written when they're wanted (D149).
Step 11, tooling, is done:
the runtime is compiled once and kept, and the golden tests run in
parallel (D150). `make test` passes on x86 Linux (AArch64 under qemu,
x86-64 natively), and on macOS (Stevan runs it on his M2 Max after every
step, and reports only failures; x86-64 passes there too, under Rosetta
2, with `make golden TARGETS=x86_64`). **Next: step 12**, the groundwork
HTTP needs: 12a, ways to read a device (chunks, and JSON and
s-expressions parsed in C, D154, D155) and inline docs after Perl's POD
(D156); 12b, collecting outside `recv`; 12c, C libraries vendored as
source (D151, D152), then TLS; 12d, looking up host names in the runtime
(D153). Then where `recv` can go (13: `defactor`, or splitting functions
at `recv`), HTTP in slight (14), and agents (15: chat as actors, tools as
messages, `:exec`, models over HTTP). Each has points to settle with
Stevan first (`docs/PLAN.md`); 12b and 13 need a discussion in depth
before building. Self-hosting waits till the language settles (17).
Update this section as steps land.

## Read first, in this order

1. [`docs/LANGUAGE.md`](docs/LANGUAGE.md): slight as it is, every form
   and builtin, for someone writing programs in it.
2. [`docs/DESIGN.md`](docs/DESIGN.md): the language, runtime and compiler
   as agreed. **(open)** marks proposals that haven't been confirmed.
3. [`docs/PLAN.md`](docs/PLAN.md): the build order and the example-port
   table.
4. [`docs/DECISIONS.md`](docs/DECISIONS.md): why, and what was turned down.
   Check it before "improving" something; it may have been decided against.
5. [`docs/BACKGROUND.md`](docs/BACKGROUND.md): AVM, VM3, the spike, prior
   art, and notes on other targets, compiling to C, multiple cores,
   embedded boards and WebAssembly (parked). Optional.

The ts-slight and ts-cpi files this started from, and the AArch64 spike,
were removed in Oct 2026; BACKGROUND.md says what came from them and how
to get them back from git (`3fd71e0`).

## Working rules

- **Aggressively simple** is the main design rule. When a feature needs
  machinery, say so and offer to cut or change it.
- **Propose before building** any change to language semantics, a new
  special form or builtin, a change to the `recv` rule, or anything marked
  (open). Give options and a recommendation. Record the outcome in
  `docs/DECISIONS.md` (who decided, why, what was rejected) and update
  `docs/DESIGN.md`.
- When a special form or builtin is added or changes, update
  [`docs/LANGUAGE.md`](docs/LANGUAGE.md) (no tables there).
- Keep a working compiler with passing tests at the end of every step
  (Ghuloum). Each step adds golden tests.
- **Examples bend to the language, not the other way round.** When an
  example needs something the compiler or runtime doesn't do, change the
  example, or leave it out; don't add to the language to make it pass
  (D84).
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
- Comments explain why, not what. Match the density of the runtime's code.

## Toolchain

- **macOS on Apple Silicon:** Xcode's clang, Node ≥ 22.18. Everything runs
  natively; the x86-64 target runs under Rosetta 2
  (`make golden TARGETS=x86_64`).
- **x86-64 Linux (this is how cloud sessions develop):** the x86-64 target
  builds and runs natively. For AArch64, cross-compile with clang and run
  under qemu:

  ```
  apt-get install -y gcc-aarch64-linux-gnu qemu-user   # sysroot + qemu-aarch64
  # clang and lld must also be installed (clang-18 works)
  ```

  `slightc` uses `clang --target=aarch64-linux-gnu -fuse-ld=lld -static`,
  and binaries run with `qemu-aarch64`. Timings under qemu are meaningless.
  Don't add `--sysroot=/usr/aarch64-linux-gnu`: clang finds the cross
  toolchain without it, and with it lld can't
  link `-lm`, because the sysroot's `libm.a` is a linker script with
  absolute paths.
- **Other hosts** aren't set up for. On arm64 Linux, the x86-64 target
  needs the x86-64 cross libc (`gcc-x86-64-linux-gnu`) and `qemu-user`. An
  Intel Mac, or `make` run under Rosetta, can only build x86-64
  (`TARGETS=x86_64`).
- Check the setup with `t/run.sh t/000-int.slight` and `TARGET=x86_64
  t/run.sh t/000-int.slight`. Each should end `1 tests, 0 failed`.
- Use clang's integrated assembler, not GNU `as` (it rejects some of the
  macros).
- Don't use `-Werror`; a different Apple clang version shouldn't break the
  build.

## Layout

| Path | |
|---|---|
| `bin/slightc.ts` | The driver: read and expand (with the files it includes), compile, write `out.S`, link with clang |
| `compiler/src/` | `sexp.ts` (the data), `reader.ts`, `expand.ts` (`@include`, and the forms that become `cond`), `classify.ts` (the `recv` rule: which functions are state functions), `codegen.ts` (what to emit), `target.ts` (the shapes a target supplies, and `placeArgs`), `aarch64.ts` and `x86_64.ts` (the targets), `values.ts` (value encodings; must match `rt.h`), `errors.ts` |
| `compiler/tests/` | Unit tests, `node:test` |
| `runtime/` | `rt.h` (tags and offsets shared with assembly), `asm_aarch64.h` and `asm_x86_64.h` (assembler macros, included by generated code), `rt_asm_aarch64.S` and `rt_asm_x86_64.S` (context switch, process entry, `apply`), `rt.c` (the core: faults, allocation, printing, equality), `process.c` (processes, run queue, stacks, heap chunks, message copying, the collector, timers, reading keys, files and sockets, `main`), `tty.c` (raw mode, decoding keys, the screen's size), `strings.c`, `numbers.c` |
| `lib/` | The built-ins `(@include :name)` asks for: `prelude.slight` (in every program), `test.slight` (TAP), `fs.slight` (`slurp` and `spew`), `ds.slight` (data structures as processes) |
| `examples/` | Example programs; each with a `.expected` is a golden test |
| `t/` | Golden tests: `NNN-name.slight` + `NNN-name.expected`; `run.sh`; `headers.c`; `models/` (Python models that produced expected output); `data/` (files the tests read or include; tests write under `build/t/`). A line `; stdin: bytes` (printf `%b` escapes; `\033` is ESC) is the test's stdin, and `; args: words` its arguments. |
| `build/` | Output (ignored): `runtime/`, the compiled runtime, kept by `slightc` (D150); `t/`, the golden tests' binaries and files |

## Commands

- `npm install` once, for `typescript` (used only by `make check`).
- `make test`: unit tests, the runtime header check, then the golden tests
  for each of `TARGETS` (aarch64 and x86_64 on an x86-64 machine, aarch64
  on arm64). Under a minute on a cloud session.
- `make unit`, `make golden`, `make headers`: one at a time.
  `t/run.sh t/003-int-max.slight` runs one golden test, for `TARGET`
  (`aarch64` unless set: `TARGET=x86_64 t/run.sh ...`). The golden tests
  run `JOBS` at a time (one per CPU unless set; `JOBS=1` for one at a
  time): each says `ok` or `FAIL` as it finishes, and the failures' diffs
  come at the end. A golden test that runs longer than `TIMEOUT` seconds
  (default 60) is killed and fails.
- `make check`: `tsc --noEmit`.
- `node bin/slightc.ts -o out file.slight ...`: compile and link. It writes
  `out.S` next to `out`. `-S` writes only the assembly. `--target x86_64`
  compiles for x86-64 (the default is `aarch64`). On x86, run an AArch64
  result with `qemu-aarch64 ./out`, and an x86-64 one directly. The
  runtime is compiled the first time it's needed, for each target, and
  kept in `build/runtime/` until a runtime file changes (D150);
  `--runtime` only builds it.
- Exit codes from `slightc`: 0 ok, 1 compile error, 2 usage or toolchain
  error. `SLIGHT_CC` overrides the C compiler command.
- `SLIGHT_POISON=1` when running a compiled program makes the collector
  fill what it frees with garbage, so a pointer it missed fails at once.
  `t/run.sh` sets it.
- `SLIGHT_CLOCK=virtual` when running a compiled program swaps the real
  clock for a virtual one: it starts at 0 and moves only when nothing can
  run, straight to the next timer. `t/run.sh` sets it too.

The runtime prints the root process's value, followed by a newline, once
nothing can run and no timer is pending. Faults are logged to stderr as
they happen (`fault: ...` in the root, `fault in #<pid N>: ...`
elsewhere), and so are dead letters and an error that ends the root
(`error: ...`). The program exits 1 if the root ended with an error or
never ended (`deadlock: ...`).

The golden tests check stdout and stderr together, plus `exit: N` when the
status isn't 0. Write expected output by working it out independently (by
hand, or in Python: an output that depends on scheduling can come from a
small model of the run queue, as in `t/models/`), never by
copying what the compiler printed. Don't let a test depend on the last bit
of a libm function other than `sqrt`: macOS's and glibc's differ (D76).
