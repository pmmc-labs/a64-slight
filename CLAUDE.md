# a64-slight

slight (an immutable, actor-based Lisp) compiled ahead of time to native
AArch64, and x86-64 as a second target, on a small actor runtime in C and
assembly. The compiler is TypeScript for now and should self-host later.

## Status

**Steps 0–9, 10a–10e and 11 of [`docs/PLAN.md`](docs/PLAN.md) are done**:
the reader, and a compiler for integers, floats, `#true`/`#false`, `()`,
symbols, lists (a heap per process of chunks, capped at 1 GB or what
`SLIGHT_HEAP_MAX` says, D202, in two generations, D200, and collected when a receive function waits for a
message, or at a function's entry, with the frames as roots: D166),
strings, closures
(`lambda`, functions and builtins as values, `apply`), arithmetic
(integers inline; floats inline too, D198, and mixed in C), structural `eq?`
and `hash`, the same for values that are `eq?` and on every machine (D209), type
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
`monitor`, `kill`, `raise`, `join/race` (D220), exit records, and faults that end just their
process with `(:error (kind value site))` (`runtime/process.c`). Timers:
`after`, `sleep`, and a virtual clock for tests (`SLIGHT_CLOCK=virtual`);
`clock/us`, that clock in microseconds (D208); `timer/cancel`, and a
timer whose process has ended no longer holding up the program's end
(D232).
The terminal: `connect :keypress`, raw mode, decoded keys, the screen's
size (`runtime/tty.c`). Files: `connect :fs/read` (`:fs/write`,
`:fs/append`) opens a file on a device, a pid the runtime serves, and
`disconnect` closes it; `slurp` and `spew` are slight, in an opt-in
`lib/fs.slight`; data structures as processes (a cell, a dictionary, a
queue, a channel, and `ask`) are in `lib/ds.slight` (D157). Sockets:
`connect :tcp "host:port"` and `connect :tcp/listen port` open sockets
as devices too (and `:unix` and `:unix/listen` at a path, D231), waited for in `select()`, and `(connect conn expr)`
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
`slightc --target x86_64` builds for it (D145, D146). Strings are bytes,
UTF-8 by convention: the `str/` builtins count characters (a bad byte is
a character of its own, as in Go), and the `bytes/` ones count bytes
(D148, D219).
`examples/` has twenty-nine example programs (all but
`ping-pong-tournament`, `top`, `chat`, `airline-gen`, `forest`, `sieve`,
`snails`, `coral`, `waves` and `etch` are golden tests; `etch`, an
Etch A Sketch on the keyboard, whose every step is a persistent map kept
for undo and redo, is interactive; `waves`, strings on sine
waves, a scroller, a case wave and a Lissajous snake, draws with
`math/sin`, D76; `coral`, Collatz's ways to 1
walked out from 1 and drawn in Braille, draws with `math/sin`, D76; `snails`, a race whose winner an oracle
of ghost snails, forked from the same seed, foretells, runs on the real
clock; `forest`, a forest grown by a process per
branch, drawn in Braille, the same picture under any schedule, draws
with `math/sin`, whose last bits differ between machines, D76; `sieve`,
the prime sieve as a chain of processes, a process per prime, drawn as
Ulam's spiral in half blocks, writes its pixels in the order its
messages come), and `airline-services.slight`, which `airline` includes; new ones are written when they're wanted (D149), the latest
`airline`, a simulated day branched with what-ifs, each run till it
rejoins the run it came from (D207), and `airline-gen`, a day for it
the size of a small airline's, 300 aircraft and 1,500 legs, which it
runs, timed with `--time`, in under 3 s (D210); with `--json` it
writes every run's minutes and flights out, for charts (D211); and it
starts each branch from a checkpoint, and keeps its runs in a file for
a later round of branches to start from (`--keep`, `--from`, D212); and
with `--recover`, tries remedies for a disruption, cancellations and
swaps among them, has services check the best and rank those that pass,
over HTTP, with stand-ins for the services in
`examples/airline-services.slight`, which it includes (D213, golden test
256); and makes what-ifs at random from a seed, as many as asked
(`--random n --seed s`), and picks remedies the same way (`--remedies
n`), with `bench/airline.py` to run sets of these and time them (D214,
golden tests 257 and 258); and makes its remedies only near what the
disruption touched, checks the candidates that come to the same day
once, and keeps a remedy only as how its day differs (D215, golden test
259); and with `--json` writes a recovery's candidates, rounds and
ranking, for charts (D217); and runs a share of its what-ifs with
`--shard k/n`, so `bench/airline.py --shards` can run copies of it at
once, a core each (D218, golden test 260).
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
the person first (`lib/exec.slight`'s `exec/run`, golden test 233); and
`lib/tools.slight`, tools to read, find and change files and run
commands, those that change something asking an `approve` function
first (D188, golden test 234), and `fetch`, a URL through curl, asking
first every time, a page made plain text in slight (D189, golden test
235), and `calc`, arithmetic as people write it, parsed and worked out
in slight (`lib/calc.slight`, D190, golden test 236), and notes kept
from one conversation to the next, a store that writes them to a file
as an s-expression (D191, golden test 237), and other agents asked as
tools, `agent/ask` (D192, golden test 238); and a fetched page's links
kept, `[text](url)`, and a command ending with the process that ran it,
so Escape in the chat stops one (D193, golden tests 233 and 235).
Benchmarks, phase 1 (D194, `docs/BENCHMARKS.md`): slight against C,
Go, Erlang, Node, Perl and Python, on the cloud machine and the M2 Max
(the reference); phase 2, fixing what the profiles found: tuples built
in place (D195, golden test 239), collecting before the heap's cap
(D196, 240), better code for calls, arithmetic and tests (D197,
241), float arithmetic inline (D198, 242), a message with nothing to
copy freed as it's taken (D199, 243), and a generational collector,
with no write barrier, since nothing old can point at anything young
(D200, 244); and phase 3, Chez Scheme and five more programs (`tak`,
`nqueens`, `deriv`, `spectral-norm`, `fan-in`, D201), and what its
profiles found: the prelude's list functions as loops, and its `not`
compiled inline (D203, golden tests 247 and 248); and Rust, with tokio,
in the actor programs, their floor as C is the sequential ones' (D224);
and `payload`, ping-pong with 100 integers in each message, what copying
a message costs, and one core as the comparison, every core a table of
its own in the summary (D237); and `latency`, how long a message waits
while other processes work: idle, beside a process that never waits,
and beside a big heap (D238); and the collector's walk, its state in
registers and a list's spine copied without its stack, 21 instructions a
cell where it took 52 (D239, golden test 293); and the message copy's
walk made the collector's, 41 instructions a cell where it took 68, and
values with no pointer in them not walked at all (D240, golden test
294); and `bank`, a simulation, SimPy's bank example, slight's on the
virtual clock against SimPy's and an event list in C, all coming to the
same waits (D241), and the virtual clock no longer calling `select()` at
each step when nothing's open, 1.55 times faster there (D242, golden
test 295).
Strings, cleaned up
(D219): one set of builtins in place of the byte ones and the `utf8/`
ones, `str/` with JavaScript's methods' names and edge cases over
characters, `bytes/` over bytes, conversions as `a->b`, Unicode's case
mapping and white space (`runtime/unicode.c`), ASCII strings marked so
they're indexed in one step, and `pprint` escaping strings; golden
tests 261 to 265, from `t/models/strings.py`. Waiting for the first of
several processes (D220): `join/race` in the runtime, and `join/any` and
`join/all` in the prelude, JavaScript's `Promise.race`, `any` and `all`,
each keeping its caller's stack as `join` does; golden tests 266 to
268. Numbers (D221): the math builtins under `math/`, with the rest of
JavaScript's `Math`, `math/pow` exact on two integers, `math/mod`,
`math/nan?`, `math/E` and `math/INF`, `int->float`, `float->str` as
`toFixed`, and the bit operations, `bit/`, compiled inline; golden tests
269 to 272. Random numbers (D222): a generator in each process,
SplitMix64 as Java's `SplittableRandom`, `rand/int`, `rand/float` and
`rand/seed`, and `rand/pick` and `rand/shuffle` in the prelude; a fork's
split from its parent's, so the numbers don't depend on the order
processes run in, and the run's seed recorded for a replay; golden test
273, from Java's own (`t/models/Rand273.java`). Maps (D223): a
persistent hash trie of tuples in slight, `lib/map.slight`, with
`tuple/insert` and `tuple/remove` for its nodes; golden tests 274 and
275, from `t/models/map.py`. Calls between programs, and people (D225):
`lib/rpc.slight`, stupidrpc (Matt S Trout's line protocol) finished in
s-expressions, `(call id name arg ...)`, `next`, `done`, `fail`, `stop`,
`helo` and `barf`, typed at `nc` as readily as sent; handlers with typed
parameters that `help` gives, each call in a process of its own,
streams, `stop`, either end calling the other, over TCP or a command
(`rpc/exec`, for ssh); golden tests 277 to 279. And a listener at an
address, `(connect :tcp/listen "127.0.0.1:7317" expr)` (D226, golden
test 276). Programs run in containers: static on Linux, and SIGTERM ends
one that's PID 1 (D227, t/live.py's signals), whose output goes out when
it waits, and which as PID 1 waits for what its commands leave (D229);
and `rpc` for fleets: a hub calls its devices over the connections they
made (a handler's `^$$`), `rpc/keep` keeps a connection up, and
`rpc/system` answers with what `:system` says (D228, golden tests 280 to
282); and, after a look at mst's Mojo-StupidRPC and Hydling, a call from
the other end stopped however its connection ends, an error included
(D230, golden test 283); and Unix sockets, `connect :unix` and
`:unix/listen`, a path's permissions saying who may connect, the socket
removed when its listener closes or the program ends, SIGTERM included,
and one left by SIGKILL replaced, which `rpc` takes wherever
`"host:port"` goes (D231, golden tests 284 and 285, t/live.py's
sockets); and timers that can be cancelled: `after` gives its timer's
number, `(timer/cancel t)` takes it back, and a timer whose process has
ended is dropped as soon as nothing else can run (D232, golden tests
286 to 288, t/live.py's timers); and in a container, each process's
heap capped at a quarter of its memory limit, so a runaway process
faults on its own rather than the kernel killing the program (D233,
t/live.py's memory limit); and a time limit for the run tool, now that
a timer no longer holds up the end: 2 minutes, `exec/run-within` and
`tools/run-within` for others (D234, golden test 290, t/live.py's run
limit; its 30 s default for an HTTP request was withdrawn, D236, since
the virtual clock moved to it while a reply crossed macOS's loopback);
and who's at the other end of a Unix socket, `(socket/peer c)`, its
uid, gid and pid as the kernel has them, and `(rpc/peer)` in a handler
(D235, golden test 291).
And tasks and actors (D243,
`docs/PLAN.md`, "Tasks and actors"), built in five steps; step 1: `spawn`
starts an actor, `fork` a task, which can't wait, nor be sent to, timed
or monitor, and the root is a task, so a top level that waits spawns an
actor and reads its end; the deadlock message follows the root's join to
where the program waits (golden tests 296, 297); and step 2: a task's
addresses, `(address)`, which `send` and `after` take, `read` and
`read/any`, which wait for a message to one or a process's end, and
`ask` in the prelude on them, without a box (golden tests 298, 299);
and step 3: `(open source arg)`, a device a task owns, no process
started, read with `read`, its failure a message rather than the
task's end, under `slurp`, `spew`, `exec/run` and `tools/read` (golden
test 300); and step 4: an actor can't be joined (`:not-a-task`), and
keeps no value: its end goes to its monitors and the tasks reading it,
a read's watch lasting till the task reads without it, and is `:ended`
after; `http/listen` and `rpc/listen` start servers from a task, and
`http/request` and `rpc/call` join a task that reads, so actors call
them too; every program restructured to match (golden test 301); and
step 5, exit records (D244): a task's value kept till its parent has it
(a join, a read, a monitor's notice, or the parent's end), then
`:ended`, and the process table in pages, each freed once nothing more
can come of its entries, so a program that runs for good holds only
what's running, open or kept (`:system`'s `(:records n)`; golden test
302, t/live.py's memory); and an actor's end kept so too, for a parent
that's a task, so a task reads one whenever it likes (D245, golden test
301). A second syntax, C-style, `.sly` (D246, `docs/SYNTAX.md`): its
reader, `compiler/src/sly.ts`, into the same s-expressions, golden
tests 303 to 305; its warning, converters and the guide in both wait
(`docs/PLAN.md`, "A C-style syntax"). **Next:** to settle with Stevan
(`docs/PLAN.md`, 15). 12c (C libraries vendored as source, D151, D152, then
TLS, which HTTPS needs: `https://` is `:no-tls` till then) waits: local
models first, and curl for HTTPS. Each has points to settle with Stevan
first (`docs/PLAN.md`). Self-hosting waits
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
  [`docs/LANGUAGE.md`](docs/LANGUAGE.md) (no tables there), and the
  guide (`docs/guide/`) where it shows it: its code is golden tests, so
  `make test` says which pages a change broke.
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
  `<stdlib.h>` pulls in `<sys/wait.h>`, which has a `P_PID`). macOS's
  `<sys/types.h>` also defines `major()` and `minor()` as macros, so no
  function in the runtime can have either name; `clang -include
  sys/sysmacros.h -fsyntax-only -Iruntime runtime/process.c` shows such
  a clash on Linux.
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
  and binaries run with `qemu-aarch64`. Native builds on Linux are static
  too (`-static`, D227), so a program runs in an empty container. Timings
  under qemu are meaningless.
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
| `compiler/src/` | `sexp.ts` (the data), `reader.ts`, `sly.ts` (the C-style syntax, `.sly`, read into the same forms, D246, `docs/SYNTAX.md`), `expand.ts` (`@include`, and the forms that become `cond`), `pattern.ts` (`match`'s and `recv`'s patterns, as tests and bindings), `split.ts` (a `recv` anywhere a tail call could go, made the whole body of a generated receive function), `classify.ts` (the `recv` rule: which functions are state functions), `codegen.ts` (what to emit), `target.ts` (the shapes a target supplies, and `placeArgs`), `aarch64.ts` and `x86_64.ts` (the targets), `values.ts` (value encodings; must match `rt.h`), `errors.ts` |
| `compiler/tests/` | Unit tests, `node:test` |
| `runtime/` | `rt.h` (tags and offsets shared with assembly), `asm_aarch64.h` and `asm_x86_64.h` (assembler macros, included by generated code), `rt_asm_aarch64.S` and `rt_asm_x86_64.S` (context switch, process entry, `apply`), `rt.c` (the core: faults, allocation, printing, equality), `process.c` (processes, run queue, stacks, heap chunks, message copying, the collector, timers, reading keys, files, sockets and commands, `:system`, `main`), `tty.c` (raw mode, decoding keys, the screen's size), `strings.c` (the string builtins, D219), `unicode.c` (Unicode's case mappings and the properties they need, made by `gen-unicode.py`; don't edit it), `numbers.c`, `tuples.c`, `json.c`, `sexp.c`, `dns.c` (looking up host names), `replay.c` and `io.h` (every system call that brings something in, or starts or stops a command, for recording and replaying a run) |
| `lib/` | The built-ins `(@include :name)` asks for: `prelude.slight` (in every program), `test.slight` (TAP), `fs.slight` (`slurp` and `spew`), `ds.slight` (data structures as processes), `http.slight` (HTTP/1.1 on `:tcp`, a server and a client, D173, whose replies can also be streamed, D181), `agent.slight` (a conversation with a model on ollama's `/api/chat`, D184, which can call tools, D187), `exec.slight` (`exec/run`, a command run to its end), `calc.slight` (`calc/eval`, arithmetic as people write it, D190), `map.slight` (a persistent map, a hash trie, D223), `rpc.slight` (calls between programs, and people, over a connection, D225), `tools.slight` (tools for an agent: files, commands, fetching, arithmetic, notes and other agents, D188–D192) |
| `examples/` | Example programs; each with a `.expected` is a golden test |
| `docs/guide/` | The guide, for learning slight: `README.md` (what's in it and what's to come), `tour.md`, `tutorial/` (a key-value store, a chapter a page), `concepts/` (the ideas, and why), `howto/` (recipes); `code/`, its programs (`tut-` the tutorial's, `con-` the concepts', `howto-` the recipes', and `data/`, files they read), each with a `.expected`, golden tests too, and the files of functions they include (`tut-table.slight`, `tut-store.slight`, `tut-ttl.slight`, `tut-log.slight`, `tut-serve.slight`); a program that writes a file takes its path from `; args:`, under `build/t/`. A `lisp` block in a page follows `<!-- code: path -->`, and an output block `<!-- output: path -->`, and `t/guide.py` checks each is in its file as it stands (README.md's too) |
| `bench/` | Benchmarks (D194, D201, `docs/BENCHMARKS.md`): `NAME/` holds a program in each language it's written in (`.slight`, `.c`, `.go`, `.erl`, `.js`, `.pl`, `.py`, `.ss` for Chez Scheme, and `.rs` for Rust, the actor programs alone, on tokio, D224); `Cargo.toml` and `Cargo.lock`, the Rust programs as one package, and `rt.rs`, the runtime they start; `run.py`, the harness, which builds, checks, times and summarizes them; `airline.py`, which runs `examples/airline.slight` with sets of options and times them (D214); `rusage.c`, which measures one run; `chez-compile.ss`, which compiles a Chez program ahead of time; `results/`, the runs worth keeping, as JSON and Markdown |
| `t/` | Golden tests: `NNN-name.slight` (or `.sly`, D246) + `NNN-name.expected`; `run.sh`; `replay.sh` and `replay/` (recording and replaying, and seeded orders: checks that take more than one run); `live.py` and `live/` (what a golden test can't see: keys on a stack all but full, on the real clock; the peak memory of reading and building values; the chat in a terminal, with a model of its own; a day the size of a small airline's; SIGTERM, as PID 1 too, and a command's orphan, as PID 1; a runaway
process in a cgroup of 256 MB, faulting at its cap; a command killed at
its time limit; timers that
nothing can come of, which don't hold up the end; a Unix socket left by
SIGKILL, replaced, and removed by SIGTERM); `headers.c`; `models/` (Python models that produced expected output, and a Java one, for the random numbers, and `timer-ids.py`, a model of `timer/cancel`'s index); `data/` (files the tests read or include, JSONTestSuite's among them, `sly/`, the tutorial's store in the C-style syntax, and `http-raw.slight`, a raw server and client for the HTTP tests; tests write under `build/t/`). A line `; stdin: bytes` (printf `%b` escapes; `\033` is ESC) is the test's stdin, `; args: words` its arguments, and `; env: NAME=value ...` its environment (`//` in place of `;` in a `.sly` test). |
| `build/` | Output (ignored): `runtime/`, the compiled runtime, kept by `slightc` (D150); `t/`, the golden tests' binaries and files |

## Commands

- `npm install` once, for `typescript` (used only by `make check`).
- `make test`: unit tests, the runtime header check, the guide's check
  (`t/guide.py`), then the golden tests,
  the replay checks and the live checks (`t/live.py`, Python 3) for each
  of `TARGETS` (aarch64 and x86_64 on an x86-64 machine, aarch64 on
  arm64). About two minutes on a cloud session.
- `make unit`, `make guide`, `make golden`, `make replay`, `make live`,
  `make headers`: one at a time.
  `t/run.sh t/003-int-max.slight` runs one golden test, for `TARGET`
  (`aarch64` unless set: `TARGET=x86_64 t/run.sh ...`). The golden tests
  run `JOBS` at a time (one per CPU unless set; `JOBS=1` for one at a
  time): each says `ok` or `FAIL` as it finishes, and the failures' diffs
  come at the end. A golden test that runs longer than `TIMEOUT` seconds
  (default 60) is killed and fails.
- `make check`: `tsc --noEmit`.
- `make bench`: the benchmarks, slight against C, Go, Erlang, Node, Perl,
  Python, Chez Scheme and Rust, and SimPy in `bank`, if it's installed (`bench/run.py`; `BENCH=--quick` for small sizes, and see
  `--help`). Not part of `make test`. Measure on the Mac: timings under
  qemu mean nothing, and the cloud machine's are noisy.
- `bench/airline.py`: `examples/airline.slight` on a made-up day, with
  lists of what-ifs, seeds, checkpoint spacings or remedies swept, a
  table of what each run says of its times and what it cost (see
  `--help`). Not part of `make test` either.
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
  fill what it frees (a nursery started again included) with garbage, so
  a pointer it missed fails at once. `t/run.sh` sets it.
- `SLIGHT_GC_STRESS=n` when running a compiled program makes a minor
  collection at the entry of every nth function called (n from 1 to
  1,000), and at every `recv`, and every eighth of those a major one,
  without changing when processes are preempted (D166, D200).
  `SLIGHT_GC_STRESS=7 make golden` checks the collector against every
  golden test; a few then take too long and are killed, and pass with a
  larger n (059, 130 and 240, which fill a 64 MB heap, never finish).
- `SLIGHT_GC_VERIFY=1` when running a compiled program walks everything
  reachable before each minor collection, and aborts at an old object
  pointing at a young one, which the collector depends on never
  happening (D200). `SLIGHT_GC_VERIFY=1 make golden` after changing the
  collector or how anything is allocated; no test sets it, since it's
  slow.
- `SLIGHT_HEAP_MAX=n` (bytes, or with `K`, `M` or `G` after it, at least
  `1M`) when running a compiled program caps each process's heap there,
  in place of 1 GB, or a quarter of the cgroup's memory limit if that's
  smaller, as in a container (D202, D233). `t/run.sh` sets `64M`, the cap the tests were
  written for. A device's line, count or value is held to 64 MB however
  big the heap.
- `SLIGHT_CLOCK=virtual` when running a compiled program swaps the real
  clock for a virtual one: it starts at 0 and moves only when nothing can
  run, straight to the next timer. `t/run.sh` sets it too.
- `SLIGHT_RECORD=file` when running a compiled program writes everything
  it takes from the world (the clock, keys, reads, sockets, lookups) to
  `file`; `SLIGHT_REPLAY=file` runs it again from the record, exactly,
  touching nothing but stdout and stderr (D170).
- `SLIGHT_SCHED=n` runs processes in an order picked by a generator seeded
  with `n`, the same for each `n`, to look for races (D170).
- `SLIGHT_SEED=n` seeds the random numbers (`rand/`); without it they
  come from the system's entropy, different every run, and are recorded
  for a replay either way (D222). `t/run.sh` sets `1`.
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
