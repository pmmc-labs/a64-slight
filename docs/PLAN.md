# Plan

Ghuloum's incremental approach: the compiler works and its tests pass at
the end of every step, and each step adds one feature. Steps are sized to
be a session or two each.

**Progress:** steps 0–9 and 10a–10e done (under qemu, and natively on
macOS: Stevan runs `make test` on his M2 Max after every step), a
second target, x86-64 (which passes on the M2 too, under Rosetta 2), and
UTF-8 builtins (both below, after 10e), tooling (11), and of the
groundwork HTTP needs (12), ways to read a device (12a), collecting
outside `recv` (12b) and looking up host names (12d), where `recv`
can go (13), and after it, `match`, tuples, and recording and replaying
a run (D168–D170). HTTP (14) on plain `:tcp` is done (D172–D174). Next: the rest of 12
(C libraries and TLS), which HTTPS needs, then agents (15).

Read [`DESIGN.md`](DESIGN.md) first. Where a step meets an **(open)** item,
propose options to the user before building (see `CLAUDE.md`).

## Proposed layout

As planned at the start; `CLAUDE.md` has the layout as built.

```
compiler/        the TypeScript compiler
    src/         one file per pass, plus the driver
    tests/       node:test, per pass
bin/slightc.ts   compile .slight files to a native binary
runtime/         C and assembly: rt.h, rt.c, process.c, rt_asm.S, gc.c, ...
lib/             prelude.slight (compiled into every program)
t/               golden tests: NNN-name.slight + NNN-name.expected, run.sh
examples/        ported ts-slight examples
Makefile         native on arm64; clang cross-compile + qemu elsewhere
```

Borrow from the spike (BACKGROUND.md): the Makefile's cross-compile logic,
`t/run.sh`, `t/headers.c` (the macOS `P_PID` lesson: prefix everything in
headers with `rt_`/`RT_`), `rt_switch`, the `LOADADDR` macro (Apple's
`@PAGE`/`@PAGEOFF` vs ELF's `:lo12:`), and the `RT_ASM()` symbol-naming
macro (macOS adds a leading underscore to C symbols).

## Steps

### 0. Scaffold (done)

What landed matches the layout above, except that `lib/` and `examples/`
don't exist yet and the runtime has no Makefile of its own: `slightc` passes
`runtime/rt.c` and `runtime/rt_asm.S` to clang along with the generated
assembly, so there's nothing to build first. If that gets slow, it can
become a prebuilt archive.

- `package.json`, `tsconfig.json` (copy ts-cpi's: strict,
  `erasableSyntaxOnly`, `allowImportingTsExtensions`), `node --test`
  script.
- The reader: port ts-cpi's (it has source positions), with its
  tests.
- `bin/slightc.ts file.slight -o out`: read, emit `.S`, call clang to
  link with the runtime.
- A minimal runtime: `main` sets up `x28` with a process struct whose
  reduction counter never runs out, calls the compiled entry, prints the
  result.
- The golden test runner. **Done when** `42` compiles, runs and prints
  `42`, natively on macOS and under qemu on x86 Linux.

### 1. Immediates and control (done)

`pprint` came forward from step 3 (for immediates only, so far), so that
a golden test can check many values. Faults print a line to stderr and
exit 1 for now, and the golden tests check that output too.

- Integers (63-bit, overflow faults), `#true`/`#false` (reserved symbol
  ids), `()`.
- `+ - *` with overflow checks, `== != < <= > >=`.
- `cond`, `let`, `do`.
- The runtime's printer for these.

### 2. Functions and tail calls (done)

Calling convention B (D47): arguments in `x0`–`x7`, so at most 8. Every
function entry also checks the stack against a limit (D48), so deep
non-tail recursion faults cleanly. The 10⁸-iteration loop is golden test
032 (about 2 s under qemu).

- `defun`, calls, arity checks at compile time.
- Tail calls as jumps; a loop of 10⁸ iterations runs in constant stack.
- The calling convention for compiled functions: settled as D47,
  arguments in `x0`–`x7`, the result in `x0`, and a closure in `x9`.
- The reduction check at entries and tail calls. Until step 7 the
  preempt handler just resets the counter.

### 3. Symbols and quote (done)

`int?` and `nil?` came along with `sym?` and `bool?`. Quoted lists wait
for step 4, which also has to make `eq?` structural. Symbols print without
the colon, as in ts-slight (D52).

- The compile-time symbol table; `:kw` and `'sym`; symbol names in the
  data section for printing.
- `eq?`/`ne?` on immediates; `sym?`, `bool?`.

### 4. Heap and lists (done)

`c[ad]r` goes up to four letters (Common Lisp's set, which covers the
examples). `eq?` calls the runtime's `rt_equal` only when the words differ
and neither side is a literal immediate.

- A per-process heap: chunk chain, bump allocation. No GC yet: one big
  chunk and a limit that faults.
- `cons car cdr list` and the `cadr` family, `nil? cons?`, structural
  `eq?`, quoted list constants in static data, `pprint` for lists.

### 5. Strings and floats (done)

Strings landed first, as 5a; floats are 5b. The string builtins are in
`runtime/strings.c`, the numeric ones in `runtime/numbers.c`. The ASCII
case mapping and the padding and searching helpers (`uc lc pad-start
pad-end str-repeat starts-with ends-with`) wait for the prelude in
step 6. Agreed for floats: `div` and `%` truncate
toward zero, and dividing by zero faults; floats print as `3.0`, or the
shortest form that reads back the same; `(== 1 1.0)` is `#true` and
`(eq? 1 1.0)` is `#false` (D68–D70).

- Boxed objects with headers. String literals in static data.
- The string builtins in C (see DESIGN.md), `concat`/`~`, `tty/write`.
- Floats: literals, arithmetic with promotion, `/` gives a float, `div`
  and `%` (D68), rounding to integers, libm wrappers.

### 6. Closures and the prelude (done)

`apply` became a builtin (D77), and the prelude's names were tidied
(D78). The five ported examples are in `examples/`, each with a
`.expected` that `t/run.sh` checks.

- `lambda`, closure conversion, indirect calls with an arity check,
  static closures for `defun`s used as values.
- `lib/prelude.slight`: `map filter fold/l fold/r reverse append length
  nth range member? find assoc lookup ...`, `and or not`, string helpers.
- A TAP-style test library in slight, after ts-slight's `Test.slight`.
- Port the pure examples as golden tests: `fib`, `fold-konts`,
  `closure-objects` (minus `gensym`), `simple-crappy-adts`,
  `game-of-life` (the non-actor one).

### 7. Processes (done)

Settled with the user: the `recv` patterns (D85), a FIFO run queue (D86),
and the error reasons step 8 will build (D87). The heap became a chunk
chain now rather than in step 9 (D89), since received messages join it as
chunks. `million-forks` came forward from step 8: a million processes
run in a few seconds under qemu. Faults still end the whole program, the
deadlock report is the simple one (D93), and a process's value is dropped
(the root's is printed): `(:ok value)` arrives with exit records in step 8.

- The process struct; a FIFO run queue; `rt_switch` from the spike.
- A stack pool: `mmap`ed stacks with guard pages, taken when a process
  runs, returned at `recv` and at exit.
- The classify pass: receive, state and plain functions; enforce the
  `recv` rule with good error messages.
- `fork` (hidden entry functions, deep copy of free variables), `send`
  (deep copy into a message chunk), `recv` codegen (pattern clauses,
  dead-letter log), `$$`, `^$$`.
- Preemption from the reduction counter; `yield`.
- A process ends with `(:ok value)`.
- **Done when** a ported `ping-pong` and `ring-benchmark` pass.

### 8. Process lifecycle (done)

Agreed: the fault kinds (D98), how the root's result shows (D99), and
logging faults from every process (D100, narrowed to faults: `raise` and
`kill` are logged only in the root). An ended process keeps just a
32-byte exit record (D102), and `kill` frees a process wherever it is
(D103). `ping-pong-tournament` waits for `sleep`.

- `join` (blocking, exit records), `monitor`, `kill`, `raise`.
- Faults (overflow, bad types, arity, heap limit) become `(:error ...)`.
- Deadlock detection, beyond step 7's "the root is waiting and nothing
  can run".
- Port the tournaments, `pub-sub`, `even-odd-actors`.

### 9. GC (done)

Agreed: collect only at `recv` (D105), once the heap in use has doubled
since the last collection and is at least 256 KB, with the 64 MB limit
kept (D106); and test by volume, with no heap-size builtin (D108).
Cons cells have no header, so the copy uses a work stack rather than
Cheney's scan (D107). `SLIGHT_POISON` makes missed pointers fail loudly.

- Cheney copying at `recv` and at tail calls from state functions; the
  "heap doubled" trigger; the per-process heap limit.
- Tests: a long-lived data-structure actor keeps a bounded heap; a nested
  allocating loop hits the limit and faults cleanly.

### 10. Devices and I/O

Agreed after step 9 (D109–D113): three parts, each committed with its
tests; a virtual clock for tests, chosen by `SLIGHT_CLOCK=virtual` (which
`t/run.sh` sets), that moves only when nothing can run; `after` and
`sleep` return `()`, and `slurp` and `spew` Results; a timer whose process
has ended is dropped when it's due; and the runtime waits in `select()`
on both platforms.

#### 10a. Timers (done)

`(after ms pid msg)` and `(sleep ms)`, timers in a binary heap, and the
scheduler waiting for the next one when nothing can run. A pending timer
counts as something that can still happen, for deadlock and for the end
of the program. Due timers also fire when a process is preempted, so a
busy process can't hold one up (D114). `ping-pong-tournament` is ported,
without a golden test: on the virtual clock its players never stop
(D115).

#### 10b. The terminal (done)

`connect :keypress`, raw mode, decoding keys (`runtime/tty.c`),
`tty/screen/rows` and `tty/screen/cols`; stdin joins the `select()`.
Agreed (D116–D119): a test gives its keys on a `; stdin:` line; Ctrl-C
puts the terminal back and exits 130; when stdin ends, the keys stop; and
every connected process gets every key. On the virtual clock a key comes
each time nothing can run (D122). `key-catcher`, `divisions` and
`tail-chase-game` are ported, with golden tests (D123).

#### 10c. Files (done)

Stevan's design (D124–D129): files are devices, opened with
`(connect :fs/read path expr)` (and `:fs/write`, `:fs/append`), so open
and close are `connect` and `disconnect`. A device is a pid; its first
message to its owner is `(:open f)`; a reader sends `(:line f s)` one at
a time, then `(:eof f)`; a writer takes `(:write x ...)` from anyone. A
failure ends the owner with `(:error (name path))`, errno's name. `slurp`
and `spew` are slight, in an opt-in `lib/fs.slight`, and `slurp` gives
lines. `:keypress` stays as it is (D131).

#### 10d. The network (done)

TCP in the runtime (D132–D139): `(connect :tcp "host:port" expr)` and
`(connect :tcp/listen port expr)` open sockets as devices, waited for in
`select()`. Lines for now, one at a time (D134); writes are buffered, and
closing waits for them (D135); errno's names plus `:enotfound` (D136); a
listener says which port it got, so tests can listen on 0 and run server
and client in one program (D137); `(connect conn expr)` hands an accepted
connection to a process of its own (D138).

#### 10e. Includes and the expanded forms (done)

Before HTTP, two things Stevan asked for (D141–D143): `(@include
"path")` and `(@include :name)`, expanded in place, each file once, with
the prelude an implicit `(@include :prelude)`; and `if`, `when`, `case`,
`and` and `or` back, made into `cond` by a new expander pass, with `and`
and `or` short-circuiting. Then `@ARGV` (D144), for the text editor: the
program's arguments, as the top level's parameter.

#### A second target: x86-64 (done, Oct 2026)

Taken out of order, at Stevan's request, as BACKGROUND.md ("Other
targets") planned it (D145, D146). First the code generator was put
behind a target interface (`target.ts`), checked by the generated
assembly of all 150 programs coming out the same byte for byte; then
`x86_64.ts` and the runtime's x86-64 half (`rt_asm_x86_64.S`, `rt_ctx_t`
and `start` per architecture), and `--target` in the driver, `t/run.sh`
and the Makefile. Every golden test passes on both, natively on x86-64,
with the same expected output. A third target (RISC-V, say) is now a
target file and its runtime assembly. Both targets pass on Stevan's M2
too (Oct 2026): AArch64 natively, and x86-64 under Rosetta 2, with
`make golden TARGETS=x86_64`.

The adversarial review that followed (differential tests of some 400
programs on both targets, the ABI shape by shape, the interface's
contract) found no difference in the x86-64 code. It found some older
bugs, fixed with tests: a name ending in a backslash, written into an
assembly comment, made the preprocessor swallow the next instruction
(160); `min` and `max` compared two integers as doubles (161); a lambda's
stack fault named its position twice. And one older than all of it:
printing, copying (a message, or a fork's values) and `eq?` recursed in C
down a list's cars, so a value nested some 50,000 deep in its cars
crashed the whole program, at a depth that differed between targets. They
now keep a work stack, as the collector does (D147, 162).

#### UTF-8, enough for JSON (done, Oct 2026)

At Stevan's request, part of DESIGN.md's level 2 strings (D148): a second
set of builtins beside the byte ones, counting characters (`utf8/len`,
`utf8/substring`, `utf8/index-of`, `utf8/chars`, `utf8/code`,
`utf8/char`, `utf8/valid?`), `ord` and `chr` in the prelude, and
`string->float` for JSON's numbers. A bad byte is a character of its
own, as in Go. Tests 164–168; 168 is a JSON parser and printer in
slight, checked against Python's `json` module, which could become
`lib/json.slight` when HTTP (14) wants one; since then, JSON's parser is
to be C (D154). Display width is still to come (16).

#### Data structures as processes (done, Oct 2026)

At Stevan's request, DESIGN.md's first goal as a library in slight
(D157): `lib/ds.slight`, with a cell, a dictionary, a queue and a
channel, each a process, and `ask` and `reply` for structures of one's
own. No change to the language. Test 169.

### 11. Tooling (done, Oct 2026)

First, so that every step after it gets a quicker loop (D150). `make
test` went from 7.5 minutes to 46 seconds on a cloud session:

- `slightc` compiles the runtime once per target, into `build/runtime/`,
  under a hash of the compiler, the flags and the runtime's files, and
  links programs against it; `--runtime` builds it alone.
- `t/run.sh` runs the golden tests in parallel, `JOBS` at a time, with
  the failures' diffs at the end; a test the watchdog kills says so.
- `compileBody` and the expander's `mapList` loop over a list rather than
  recursing (D39), so a long body no longer runs Node out of stack; the
  assembly of every program came out the same.

Left for when it gets in the way: better compile errors, say.

### 12. Groundwork for HTTP

What HTTP needs underneath it, from the things left "for now" so far.

#### 12a. Ways to read a device: chunks, JSON, s-expressions, source (done)

Files and sockets deal only in lines (D134: "just lines for now, and
move to chunks later, just like with :fs"). An HTTP body is read by its
length, not by lines, and binary data has none. So a device that
delivers bytes will choose how to cut them into messages: lines, chunks,
JSON values or s-expression forms (D154, D155), the last two parsed in C
by push parsers of our own, with the builtins `json/parse`, `json/print`,
`sexp/parse` and `sexp/print` for text already in hand. Decided: one
message per top-level value, and a JSON "items" mode; the same
backpressure as lines; JSON's shape in slight (`t/168`'s); symbols the
program doesn't mention read as `(:symbol "name")`; `sexp/print` reads back.
Also inline documentation, after Perl's POD (D156): `=doc` ... `=cut`
blocks of Markdown between top-level forms, which the compiler's reader
(TypeScript) and the C reader both skip, and a `:source` way to read a
file that gives its forms and doc blocks in order, with line numbers.
Settled with Stevan (D158–D161): a device cuts its next message when
its owner next calls `recv`, not when it takes the last, so whatever the
owner does while handling a message applies to the next one; `(:read
how)` changes the way of reading, `how` being `:lines`, `:chunks`,
`:json`, `:json/items`, `:sexp`, `:source`, or a count of bytes for the
next message alone, and `(:chunk f s)` is what chunks and counts send;
`json/parse` and `sexp/parse` give Results; bad JSON on a device ends
its owner with `(:error (:bad-json where))`; anything 64 MB or more ends
it with `(:error (:too-big where))`; `json/print` is compact, on one
line. In four pieces, each with golden tests:

1. Chunks, counts and switching: the runtime only. Done (Oct 2026):
   golden tests 170–174; and 145 and 147 changed, as D158 says.
2. JSON: the push parser, `json/parse`, `json/print`, `:json` and
   `:json/items`. JSONTestSuite's files under `t/data/` with their
   licence, Python's `json` as the model, and inputs cut at every point
   (not at random ones, so the tests stay the same every run). Done (Oct
   2026), as a validator and a builder (D162): `runtime/json.c`, golden
   tests 175–179, and 168 checks its parser in slight against the C one.
3. S-expressions: `sexp/parse`, `sexp/print` and `:sexp`. Done (Oct
   2026), as JSON was (D163): `runtime/sexp.c`, golden tests 180–183.
4. Doc blocks in both readers (golden tests that must compile as if they
   weren't there), and `:source`. Done (Oct 2026, D164): golden tests
   184 and 185, and the reader's unit tests. 12a is done.

#### 12b. Collecting outside `recv` (done)

DESIGN.md's last open question (D105). Only a receive function collects,
when it waits for a message, so the root (unless it ends in one) and a
process that works without waiting never collect, and stop at 64 MB.
HTTP will meet it: a handler that parses a large request in one go, a
client that reads a big response in the root (a JSON text split there
tops out at about 2 MB, D148). To talk through: collecting at tail calls
between state functions, at tail calls from the bottom of the stack (a
runtime check of `sp`), or anywhere, with the compiler's help (the stack
maps D24 kept out). Done (Oct 2026, D166): at any function's entry, with
the frames as roots, read as they are, and the slots of a function that
calls another zeroed at its entry; golden tests 187–190, and 134
changed.

#### 12c. C libraries, and TLS

After agents have started (D180): a local model needs only plain HTTP,
and TLS waits for the first hosted one. HTTPS needs this.

Discussed in Oct 2026 (D151, D152; BACKGROUND.md has what Odin does):

- A pure function bounded by its input (a hash) may be a builtin; a
  library with state or I/O (TLS, a database) is a device. Nothing
  blocks, and no slight value holds a C pointer.
- Vendored as source under `vendor/`, each at a pinned version with its
  licence and a `VENDOR` file; `slightc` compiles one as it does the
  runtime, and links it statically into the programs that use it.
- In C: TLS, hashing and crypto, JSON and s-expressions (12a). Database
  drivers when they're wanted; one that can only block (SQLite) would
  need the runtime's first thread, which isn't decided.

Then TLS, the first vendored library: a `:tls` socket device, on mbedTLS
unless something better turns up. To settle: confirming mbedTLS, where a
client finds its CA certificates (macOS keeps them in the Keychain), and
how a server is given its certificate and key.

#### 12d. Looking up host names (done)

`getaddrinfo` blocks the whole runtime. As Odin's `core:net` does, the
runtime will read `/etc/hosts`, query the servers in `/etc/resolv.conf`
itself over UDP, and wait for the answer in `select()` (D153). Done (Oct
2026, D165): `runtime/dns.c`, and golden test 186, with a name server
written in slight.

### 13. Where `recv` can go (done)

Done (Oct 2026, D167): Stevan chose splitting functions at `recv`, below,
over `defactor`, which it covers. `compiler/src/split.ts`, its unit tests,
and golden test 191. What follows is the discussion as it was.

Before HTTP, the biggest library yet in slight, since how it's written
depends on this.

**`defactor`.** Stevan's idea: one form for an actor whose body does some
work and then ends in a `recv`, expanding to the two functions the `recv`
rule needs now (one that does the work and tail-calls one whose body is
just the `recv`; see `tail-chase-game`). The `recv` would have to come
last. Open: in the expander or the compiler; how
the expanded functions are named; whether more than one `recv` (a chain of
states) makes sense; what a tail call to the actor from inside its `recv`
means (back to the work, as now). Talk it through in depth first.

**An alternative to weigh with it: split functions at `recv`.** (Claude,
Oct 2026, after Stevan asked whether compiling to C would allow `recv`
anywhere. It wouldn't: the rule is about runtime costs, not the target. A
process waiting mid-function keeps its stack (8 MB of address space, at
least a page of memory, and two mappings, so Linux's default limit of
65,530 allows about 32,000 waiting processes), and the collector would
have to find roots in its frames, which our own assembly could do but C
couldn't, short of a shadow stack. Since step 12b the collector does read
frames, at a function's entry (D166), so of those two costs the stack is
what's left.) Instead the expander makes a
mid-function `recv` into what the rule wants: at a `(recv)` (as a body
form, or `(let x (recv))`), it splits the function into one that does the
work before it and tail-calls a generated receive function holding the
rest, passing the variables the rest still needs as arguments. That's the
rewrite done by hand in the ported examples and the editor:

    (defun editor (cfg cursor lines offset msg)
        (refresh cfg cursor lines offset msg)
        (let key (recv))
        (cond ...))

becomes

    (defun editor (cfg cursor lines offset msg)
        (refresh cfg cursor lines offset msg)
        (editor-after-recv cfg cursor lines offset msg))

    (defun editor-after-recv (cfg cursor lines offset msg)
        (recv
            (key (cond ...))))

(The generated name would have spaces in it, as `case`'s topic does.)

- The rule becomes "`recv` can go anywhere a tail call can": a body form
  or a `let`'s expression in any body in tail position, a `cond`
  clause's included. Nested uses, like `even-odd-actors`'
  `(pprint (list (recv) (recv)))`, are flattened into `let`s first.
- No runtime cost: waiting processes still hold no stack, and the
  collector still runs at `recv` with just the arguments as roots. That
  no longer makes it easier to compile to C than a `recv` mid-function:
  since step 12b a C target needs a shadow stack either way, or has to
  give up collecting at a function's entry and collect only at `recv`,
  where the split would still spare it one (D166).
- Unchanged: a function that waits is still called only in tail
  position; a `recv` inside a lambda (`simple-db-server`'s `db-client`,
  called from `map`) still can't work, since `map` has work pending on
  the stack; and more than 8 variables still needed after a split would
  have to be bundled into a list.
- About 100–200 lines in the expander; working out a lambda's free
  variables, which the compiler already does, is most of the analysis.
- `defactor` is one shape of this split; this is the general version,
  without a new special form, and may replace it. It changes the `recv`
  rule, so it's Stevan's decision.

### After 13: patterns, tuples, replay, `:system` (done, Oct 2026)

Four of the changes Claude listed when Stevan asked what would make
slight more useful (BACKGROUND.md has Misty, which started the talk);
Stevan picked three to do at once, and `:system` after them, and the
rest are under "Later", below.

- **`match`, and nested patterns for `recv`** (D168): one lowering of
  patterns to tests and bindings (`compiler/src/pattern.ts`), used by the
  expander for `match` and by the code generator for `recv`. Golden tests
  194–196.
- **Tuples** (D169): `runtime/tuples.c`; copying, the collector,
  printing and `eq?` know a box that holds values. Golden tests 197 and
  198.
- **Recording and replaying a run, and a seeded schedule** (D170):
  `runtime/replay.c`, `SLIGHT_RECORD`, `SLIGHT_REPLAY` and
  `SLIGHT_SCHED`; `t/replay.sh` (`make replay`) and golden test 199.
- **`:system`** (D171): a device that tells about every process (its
  state, where it is, its mailbox, heap and reductions) and the runtime,
  one process at a time; reductions counted per process; a table of
  every compiled function's entry and name, which the deadlock message
  uses too. Golden tests 200–202, and `examples/top.slight`.

### 14. HTTP in slight (done, Oct 2026)

On plain `:tcp`, before 12c (D172), as `lib/http.slight`, opt-in (D140).
The details were settled before any code (D173), and D174 has what was
built: a server (`http/serve`) that runs each handler in a process of its
own, and a client (`http/get`, `http/request`, `http/request-within`), with
chunked bodies both ways, one request to each connection, and the limits
the head and body need. Golden tests 204–213, with a raw server and client
in `t/data/http-raw.slight`. Checked by hand against Python's `http.server`
and `http.client`, and against curl, outside the tests.

Left for later: keep-alive; bodies streamed in pieces, which agents need
(15: ollama streams NDJSON over chunked encoding; done there, D181, D182);
HTTPS (12c); the limits
as parameters; a lingering close, so a server that answers before it has
read the whole request doesn't reset the connection; and a timeout that a
reply can cancel, which needs cancellable timers (see "Later").

### After 14: what the boids example led to (done, Oct 2026)

`examples/boids.slight` (a flock in the terminal, a process for each
bird, all-to-all, the same flight under any schedule; `t/models/boids.py`)
asked for these, and its profile for the last two:

- **`sort`, `sort-by` and `zip`** in the prelude (D175). Golden test 214.
- **Tuple patterns**, `#(p ...)`, only as patterns; `sexp/parse` and
  `sexp/print` read and write tuples (D176). Golden tests 215 and 216.
- **A message copied in one walk, and sent in one malloc** (D177).
- **A tuple pattern compiled inline**: one compare of the header, a load
  for each element (D178).

Over this work (the example's own changes, then D177 and D178), 250
birds went from about 210 ms a tick to about 90 on the cloud machine,
in wall time, which moves by a tenth from run to run; D179 says what was
settled besides.

### 15. Agents

Stevan's aim for slight (Oct 2026), once HTTP (14) and JSON (12a) are
in: chat, person to agent and agent to agent, written as actors. Begun
before TLS (D180), with a local model over plain HTTP. What it needs
first, in order: HTTP bodies streamed to the caller as they arrive,
`http/stream`, its pieces as messages (D181; done, D182, golden tests
217–221; and a monitor now goes when its watcher ends, D183); then,
before `:exec`, a chat: an agent whose state is the conversation, on
ollama's `/api/chat`, and a terminal to talk to it (D184, golden test
224, `examples/chat.slight`; done); then `:exec`, a device for running
a command (below; D185, golden tests 225–230; done). Next, reading the
environment, for a model's address or a key (a builtin, to propose);
then tools as messages. Then, for a hosted model: TLS (12c), and a
reader of server-sent events.

- An agent is an actor whose state is its context: the conversation so
  far, its tools, its model's settings. A turn comes in as a message,
  and the answer goes out as one, streamed if the model streams.
- A tool call is a message to an actor, answered with `reply`
  (`lib/ds.slight`'s `ask`), so a tool is a process like any other, and
  one that fails ends alone.
- Agents talk to each other in messages, and between machines in
  printed s-expressions over TCP (D155).
- Models run outside slight, behind HTTP: a local server such as ollama
  for generative models (it streams JSON lines, which 12a's way of
  reading JSON makes one message each), a hosted API over HTTPS (12c),
  and a Jev-compatible server for small "System One" models, which give
  a decision (yes or no, a choice, a score) with its probability rather
  than text. JevOS, an open one, runs a 1B model on the CPU in 25-110 ms
  a decision. Not in the runtime, even a small one: inference is CPU
  work that would hold up every process without a thread of its own
  (DESIGN.md's open question 2), the libraries are large and in C++, and
  a crash in one would end every actor. A local server costs an HTTP
  request on top, a fraction of a millisecond against the model's tens.
- `:exec`, a device for running commands, for tools: `(connect :exec
  (list "ls" "-l") expr)` runs the command, its owner gets the output as
  other devices give theirs (stdout and stderr together), then `(:eof
  p)` and `(:exited p status)` (D185; not `:exit`, which is
  `monitor`'s). A command is a list of arguments, with no shell unless
  asked for (`(list "sh" "-c" cmd)`), so nothing is quoted wrongly.

Settled by the chat (D184): what a turn and an answer look like as
messages, and how streamed output arrives. Settled by `:exec` (D185):
its stderr (with its stdout), its stdin (a pipe, closed with `(:eof)`),
when the child is reaped (SIGCHLD, through a pipe `select()` waits on),
its last message, `(:exited p status)`, and the limits a command runs
under (none yet, but the caller's timeout and `:too-big`). Still to
settle: how a tool says what it takes.

### 16. As needed

Left "for now" by earlier decisions, and taken on when something needs
them:

- **Display width** (the rest of DESIGN.md's level 2 strings): a wide
  character (CJK, most emoji) takes two columns of a terminal and counts
  one, so a terminal UI can't line up such text yet.
- More than 8 arguments to a function, on the stack (DESIGN.md).
- Sockets: IPv6, and more than the 1,000 or so that `select()` holds
  (D113), with `poll`, `kqueue` or `epoll`.
- Dropping exit records, and bounded mailboxes (D88).
- Optimizations, when profiling asks for them: the heap pointer in a
  register (D57), captured values read from the closure (D79), denser
  lists (CDR-coding, VLists: D23), reference counting with reuse (D24).

### Later: the rest of the October 2026 list

The other changes from that list (above, "After 13"), for later, before
self-hosting, since most change the language. Each has points to settle
with Stevan first.

- **Bit operations on integers.** Hashing (a hash trie of tuples, for a
  fast dictionary), random number generators (and with them property
  tests), and binary protocols all need them, and slight has none. And,
  or and xor work on tagged words as they are, so they can be inline;
  shifts untag and retag. To settle: the names (`bit/and` and so on, as
  `utf8/`), what a shift does past 63 bits (fault, as arithmetic
  does?), and negative counts.
- **A `recv` with a timeout.** Today it's `after` and a message, and a
  late timeout can arrive after the answer, to be told apart by hand. A
  clause such as `(:after ms body...)`, as Erlang's, needs timers that
  can be cancelled (the runtime has them for its own, `unset_alarm`). To
  settle: the syntax; that a receive function starts again from the top
  after each message, dead letters included, so the deadline must be set
  once per wait, not per start; and how it goes through `split.ts`.
- **Processes as data.** A waiting process is a function, its arguments
  and a mailbox (D8), and its data has no cycles, so it can be written
  out, with functions and closures by name. For: saving and restoring a
  whole program; upgrading between builds by stopping, rebuilding and
  going on with the old state, which is the ahead-of-time answer to hot
  reload (D6); and moving a process to another machine, as Termite
  Scheme does. To settle: what happens to devices and pids in a saved
  process; how a closure keeps its identity across builds (a function's
  name, and a lambda's position, which moves when the source does);
  and what's saved: one process, a group, or everything.
- **Large strings shared, not copied.** Sending a 10 MB string copies
  it (D25); being immutable, it could be shared, off the heaps, with a
  count of its users, as the BEAM's large binaries are. To settle: the
  size from which a string is shared, and how the count goes down when a
  heap is collected or freed.
- **Distribution, as a library first:** a process that stands for one on
  another machine, forwarding messages as s-expressions over TCP (D155),
  as Termite Scheme does, with no runtime change until it's proved
  itself. To settle: what a remote pid looks like, monitoring across
  machines (the connection ending), and what can't be sent (a closure,
  between different builds).
- **Smaller, left out of D168 and D169:** guards in `match` and `recv`;
  and what `#(...)` means outside a pattern (D176 left it open): a tuple
  made of its elements' values, as Clojure's vectors, or a constant, as
  Scheme's, which needs static tuples.
- **Selective receive, narrowly, with bound-variable patterns** (talked
  over after boids, Oct 2026). Boids kept messages for later by hand
  twice (a tick's, from a bird already on the next; and tick 0's, before
  a bird knows its flock), which Erlang's `receive {at, Tick, ...}`, the
  tick bound, does by leaving them in the mailbox. A bound variable
  alone (`(:at ^tick id s)`) doesn't do it: in slight a message no clause
  fits is a dead letter, so it would drop what it should keep. Selective
  receive reopens D8, the decision everything leans on (a mailbox
  searched, dead letters no longer reported at once), so: wait for a
  second program that needs it. Join calculus (BACKGROUND.md, "Prior
  art") goes further still; as a library, a join object is a process,
  like boids' round-up (D179), and a `barrier` in `lib/ds.slight` is the
  likely first one.
- **Faster floats.** Boids spends about a sixth of its time on float
  arithmetic in C and the boxes it makes. The fix that helps every
  program is in the representation: floats that need no box (NaN-boxing,
  or a tagged short float), or a compiler that keeps a float unboxed
  within a function. Either is a proposal of its own; boids is the
  benchmark. Not this way (D179): fixed point in the example, or a type
  of numerical vectors.
- **Comparing strings**, a builtin, so `sort` can order them without a
  `less?` written for it (D175).

### 17. Self-host

Once the language has settled. Port the compiler to slight (it's written
slight-shaped for this), then the three-stage bootstrap from DESIGN.md.
If compiling to C (below) ever happens, it comes first, so that the
compiler isn't ported twice.

## Parked

Discussed, but neither planned nor ruled out: each needs more talk
first. Grouped so that related ideas can join them as the language
grows. BACKGROUND.md has the discussions so far.

### New compilation targets

- WebAssembly, and the browser ("WebAssembly, and the browser"): 4–6
  sessions for WASI, and 2–3 more for a browser host.
- Compiling to C instead of assembly ("Compiling to C instead"): 2.5–3
  sessions.
- RISC-V, RV64 ("Other targets"): about 2 sessions.

### New platforms

- 32-bit microcontrollers (RP2040, STM32, ESP32, ...): a 32-bit value
  layout, and budgets of a few hundred KB ("Embedded boards", "Other
  targets"). AArch64 Linux boards, a Raspberry Pi say, run slight's
  static binaries already.

### Parallelism

- Multiple cores: a scheduler thread per core, with run queues that
  steal from each other ("Multiple cores").

## The ts-slight examples

Porting stopped in Oct 2026 (D149): fifteen are ported (`examples/`),
enough to have tried the language on, and new examples are written for
this version. The survey below is kept as a record. After `match`,
tuples and `recv` anywhere (D167–D169), three more fit and were ported,
and every example was rewritten to use them where they read better
(below).

Not every example has to be ported, and an example can change as much as
it needs to: if one calls for a change to the compiler or runtime, change
the example instead, or leave it out (D84).

From a survey of ts-slight's 27 examples. Almost all of them need
mechanical changes: `if`/`when`/`case` become `cond`; `head`/`tail` become
`car`/`cdr`; `sys/io/print-ln` becomes `pprint` or `tty/write`; `grep`
becomes `filter`, and `(range a b 1)` becomes `(range a (+ b 1))` (D78);
`meta-circular`'s own `apply` needs another name, now that `apply` is a
builtin. Beyond that:

| Example | Needs |
|---|---|
| `fib`, `fold-konts`, `game-of-life`, `simple-crappy-adts`, `closure-objects` | ported in step 6 (`examples/`) |
| `scratchpad` | nothing else (pure) |
| `ping-pong`, `ring-benchmark`, `million-forks` | ported in step 7 (`examples/`, D97) |
| `pub-sub`, `even-odd-actors`, `fixed-tournament` | ported in step 8 (D104) |
| `ping-pong-tournament` | ported in step 10a, without a golden test (D115) |
| `simple-db-server`, `game-of-life-actors` | ported after step 13: `recv` goes in the middle of a function now (D167); `simple-db-server`'s client asks with `lib/ds.slight`'s `ask`, as a lambda can't wait; `game-of-life-actors` takes its count and its change in two steps, the root waiting for every cell between them, and is on `game-of-life`'s glider, so it prints just what that does |
| `active-objects` | ported after step 13, as `simple-db-server` (with `ask`), and with names in place of `gensym` |
| `key-catcher`, `divisions`, `tail-chase-game` | ported in step 10b (D123) |
| `window-manager`, `better-window-manager` | as above, plus `connect :keypress` and the screen size (step 10b); Stevan's port, of both, is in `author/window-manager.slight` |
| `meta-circular` | `join`/`yield`/`apply`; no `recv`, so likely as-is; Stevan's port is in `author/meta-circular.slight` |
| `more-oop` | local `defun`s lifted to top level; it uses `slight/eval` to look up methods, which has to go; without it, it's `closure-objects` again, so not ported |
| `text-editor` | local `defun`s lifted; drop the `slight/parse`/`slight/expand` feature; files (`lib/fs.slight`'s `slurp` gives lines) |
| `repl` | no `eval`, so it becomes a line-editor demo (or evaluates a tiny calculator language written in slight) |
| `eval-string`, `hot-code-reload` | out: they exist to show `eval` and hot reload |
