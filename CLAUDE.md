# a64-slight

slight (an immutable, actor-based Lisp) compiled ahead of time to native
AArch64, and x86-64 as a second target, on a small actor runtime in C and
assembly. The compiler is TypeScript for now and should self-host later.

## Status

**Steps 0–9, 10a–10e and 11 of [`docs/PLAN.md`](docs/PLAN.md) are done**:
the reader, and a compiler for integers, floats, `#true`/`#false`, `()`,
symbols, lists (a heap per process of chunks, capped at 64 MB, and
collected when a receive function waits for a message, or at a
function's entry, with the frames as roots: D166), strings, closures
(`lambda`, functions and builtins as values, `apply`), arithmetic
(integers inline; floats and mixed in C), structural `eq?`, type
predicates, the string and math builtins (C, in `runtime/`), `cond`,
`let`, `do`, `pprint`, `tty/write`, and top-level `defun`s with calls and
tail calls (arguments in `x0`–`x7`; stack and reduction checks at every
function entry). The prelude (`lib/prelude.slight`) is compiled with every
program (with `sort`, `sort-by` and `zip`, D175); `lib/test.slight` is a TAP library. Processes: `fork`, `send`,
`recv` (anywhere a tail call could go: `split.ts` splits a function at
each one into receive functions, D167, and `classify.ts` checks the
`recv` rule),
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
hands an accepted connection to a process; the runtime looks host names
up itself, in the hosts file and then from the name servers in
`resolv.conf`, its queries waited for in `select()` too (`runtime/dns.c`,
D165). A reader (a file or a
connection) cuts its next message when its owner next waits in `recv`
(D158), and `(:read how)` switches it between lines, chunks of up to 64
KB, a count of bytes for the next message alone, JSON values (`:json`,
`:json/items`), s-expressions (`:sexp`) and source (`:source`) (D159); a
line, count or value of 64 MB or more ends the owner with `:too-big`
(D160). JSON in C (`runtime/json.c`, D162): `json/parse` gives a Result,
`json/print` compact JSON, and the same validator reads it from a device
as the bytes come. S-expressions
as data the same way (`runtime/sexp.c`, D155, D163): `sexp/parse`,
`sexp/print`, and `:sexp` on a device; a name the program doesn't
mention reads as `(:symbol "name")`. Doc blocks, after Perl's POD
(D156, D164): between top-level forms, `=doc` ... `=cut`, which both
readers skip, and `:source` reads a file's forms and doc blocks in
order, with their lines.
`(@include "path")` and `(@include :name)` splice in other files (each
once); `@ARGV`, the program's arguments, is the top level's parameter
(D144), and `%ENV`, its environment as an association list, the second
(D186); and `if`, `when`, `case`, `and`, `or` and `match` are made into
`cond` (`compiler/src/expand.ts`, a pass between the reader and the
compiler). `match` and `recv` share patterns that nest, with literals
anywhere and `&` for the rest of a list (`compiler/src/pattern.ts`,
D168), and `#(p ...)` for a tuple, which is only ever a pattern (D176),
compiled inline (D178). A message is copied in one walk, into a chunk
that holds its record too, so a send is one malloc (D177).
Tuples: immutable, fixed in size, an element in one step, printed `#(1 2)` (`runtime/tuples.c`, D169). A run can be recorded and
replayed exactly (`SLIGHT_RECORD`, `SLIGHT_REPLAY`: everything from the
world goes through `runtime/replay.c`), and tried under seeded orders
(`SLIGHT_SCHED`) (D170). `connect :system` opens a device that tells
about every process (its state, the function it's in, its mailbox, heap
and reductions) and the runtime, from a table of function names the
compiler emits, which the deadlock message uses too (D171).
A second target, x86-64: the code generator emits through a target
interface (`compiler/src/target.ts`; `aarch64.ts`, `x86_64.ts`), and
`slightc --target x86_64` builds for it (D145, D146). Strings are bytes;
beside the byte builtins, the `utf8/` ones count characters (a bad byte
is a character of its own, as in Go), with `ord` and `chr` in the
prelude, and `string->float` (D148).
`examples/` has twenty-one example programs (all but
`ping-pong-tournament`, `top` and `chat` are golden tests); new ones are
written when they're wanted (D149).
Step 11, tooling, is done:
the runtime is compiled once and kept, and the golden tests run in
parallel (D150). `make test` passes on x86 Linux (AArch64 under qemu,
x86-64 natively), and on macOS (Stevan runs it on his M2 Max after every
step, and reports only failures; x86-64 passes there too, under Rosetta
2, with `make golden TARGETS=x86_64`). Step 12a, ways to read a device
(D158–D164), is done, and so are 12b, collecting outside `recv` (D166),
and 12d, looking up host names in the runtime (D153, D165); and step 13,
where `recv` can go (D167); and step 14, HTTP (D172–D174), on plain `:tcp`
as `lib/http.slight`, a server and a client, golden tests 204–213. Step
15, agents, has begun, before TLS, with a local model over plain HTTP
(D180): a reply's body comes to its caller as it arrives, in messages,
`http/stream` (D181, D182, golden tests 217–221), and a monitor goes
when either process ends, so asks don't pile up on a structure (D183);
and a chat: an agent whose state is the conversation, on ollama's
`/api/chat` (`lib/agent.slight`, D184, golden test 224), and
`examples/chat.slight` to talk to it in the terminal; and `:exec`, a
device for running a command: `(connect :exec (list "ls" "-l") expr)`,
stdout and stderr together read as a socket's, input written with
`(:write ...)` and closed with `(:eof)`, then `(:eof p)` and `(:exited p
status)`; a command runs in a process group of its own, killed when its
device closes or the program ends (D185, golden tests 225–230); and
`%ENV` (D186, golden test 231), which the chat reads `OLLAMA_HOST`
from; and tools, `(tool/new name description params f)`: a function of
the model's arguments, each call in a process of its own, the results
going back to the model till it answers (D187, golden test 232), and the
chat with tools to read files and run commands, each command shown to
the person first (`lib/exec.slight`'s `exec/run`, golden test 233).
**Next: 12c, for a hosted model:** C libraries vendored as source
(D151, D152), then TLS, which HTTPS needs (`https://` is `:no-tls` till
then), and a reader of server-sent events. Each has
points to settle with Stevan first (`docs/PLAN.md`). Self-hosting waits
till the language settles (17). Update this section as steps land.

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
| `compiler/src/` | `sexp.ts` (the data), `reader.ts`, `expand.ts` (`@include`, and the forms that become `cond`), `pattern.ts` (`match`'s and `recv`'s patterns, as tests and bindings), `split.ts` (a `recv` anywhere a tail call could go, made the whole body of a generated receive function), `classify.ts` (the `recv` rule: which functions are state functions), `codegen.ts` (what to emit), `target.ts` (the shapes a target supplies, and `placeArgs`), `aarch64.ts` and `x86_64.ts` (the targets), `values.ts` (value encodings; must match `rt.h`), `errors.ts` |
| `compiler/tests/` | Unit tests, `node:test` |
| `runtime/` | `rt.h` (tags and offsets shared with assembly), `asm_aarch64.h` and `asm_x86_64.h` (assembler macros, included by generated code), `rt_asm_aarch64.S` and `rt_asm_x86_64.S` (context switch, process entry, `apply`), `rt.c` (the core: faults, allocation, printing, equality), `process.c` (processes, run queue, stacks, heap chunks, message copying, the collector, timers, reading keys, files, sockets and commands, `:system`, `main`), `tty.c` (raw mode, decoding keys, the screen's size), `strings.c`, `numbers.c`, `tuples.c`, `json.c`, `sexp.c`, `dns.c` (looking up host names), `replay.c` and `io.h` (every system call that brings something in, or starts or stops a command, for recording and replaying a run) |
| `lib/` | The built-ins `(@include :name)` asks for: `prelude.slight` (in every program), `test.slight` (TAP), `fs.slight` (`slurp` and `spew`), `ds.slight` (data structures as processes), `http.slight` (HTTP/1.1 on `:tcp`, a server and a client, D173, whose replies can also be streamed, D181), `agent.slight` (a conversation with a model on ollama's `/api/chat`, D184, which can call tools, D187), `exec.slight` (`exec/run`, a command run to its end) |
| `examples/` | Example programs; each with a `.expected` is a golden test |
| `t/` | Golden tests: `NNN-name.slight` + `NNN-name.expected`; `run.sh`; `replay.sh` and `replay/` (recording and replaying, and seeded orders: checks that take more than one run); `headers.c`; `models/` (Python models that produced expected output); `data/` (files the tests read or include, JSONTestSuite's among them, and `http-raw.slight`, a raw server and client for the HTTP tests; tests write under `build/t/`). A line `; stdin: bytes` (printf `%b` escapes; `\033` is ESC) is the test's stdin, `; args: words` its arguments, and `; env: NAME=value ...` its environment. |
| `build/` | Output (ignored): `runtime/`, the compiled runtime, kept by `slightc` (D150); `t/`, the golden tests' binaries and files |

## Commands

- `npm install` once, for `typescript` (used only by `make check`).
- `make test`: unit tests, the runtime header check, then the golden tests
  and the replay checks for each of `TARGETS` (aarch64 and x86_64 on an
  x86-64 machine, aarch64 on arm64). About two minutes on a cloud session.
- `make unit`, `make golden`, `make replay`, `make headers`: one at a time.
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
- `SLIGHT_GC_STRESS=n` when running a compiled program collects at the
  entry of every nth function called (n from 1 to 1,000), and at every
  `recv`, without changing when processes are preempted (D166). `SLIGHT_GC_STRESS=1
  make golden` checks the collector against every golden test; a few
  then take too long and are killed, and pass with a larger n.
- `SLIGHT_CLOCK=virtual` when running a compiled program swaps the real
  clock for a virtual one: it starts at 0 and moves only when nothing can
  run, straight to the next timer. `t/run.sh` sets it too.
- `SLIGHT_RECORD=file` when running a compiled program writes everything
  it takes from the world (the clock, keys, reads, sockets, lookups) to
  `file`; `SLIGHT_REPLAY=file` runs it again from the record, exactly,
  touching nothing but stdout and stderr (D170).
- `SLIGHT_SCHED=n` runs processes in an order picked by a generator seeded
  with `n`, the same for each `n`, to look for races (D170).
- `SLIGHT_HOSTS` and `SLIGHT_RESOLV_CONF` name files for a lookup to read
  in place of `/etc/hosts` and `/etc/resolv.conf`; there a `nameserver`
  line may give a port (`127.0.0.1:5353`), so a test can be its own name
  server (`t/186`).

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
