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
reply can cancel, which cancellable timers (D232) now allow.

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
a command (below; D185, golden tests 225–230; done); and reading the
environment, for a model's address or a key: `%ENV`, the top level's
second parameter (D186, golden test 231; done); and tools: a function
of the model's arguments, each call in a process of its own (D187,
golden test 232; done), and the chat with tools to read files and run
commands, the person saying yes to each command (`exec/run`, golden test
233; done); and `lib/tools.slight`, tools to read, find and change files
and run commands (D188, golden test 234; done), and `fetch`, a URL
through curl, a page made plain text in slight (D189, golden test 235;
done), and `calc`, arithmetic parsed and worked out in slight
(`lib/calc.slight`, D190, golden test 236; done), and notes kept from
one conversation to the next (D191, golden test 237; done), and other
agents asked as tools, `agent/ask` (D192, golden test 238; done), and
a fetched page's links kept, and Escape in the chat stopping a command
(D193, golden tests 233 and 235; done); and programs, and people,
calling each other between machines: `lib/rpc.slight`, Matt S
Trout's stupidrpc finished in s-expressions (D225, golden tests
277–279; done), and a listener at an address of its own, `127.0.0.1`
say, for it (D226, golden test 276; done); and running in containers, static binaries on
Linux and SIGTERM ending PID 1 (D227; done), and `rpc` for fleets, a hub
calling its devices, connections kept up and `:system` asked from
outside (D228, golden tests 280–282; done). Next, to settle with the
user: offering an agent's tools over `rpc`, and to whom a remote call's
`approve` goes;
`log`, a builtin; or 12c. TLS (12c) and a reader of server-sent events, for a hosted
model, wait (the user, Oct 2026: local models first, and curl for
HTTPS).

- An agent is an actor whose state is its context: the conversation so
  far, its tools, its model's settings. A turn comes in as a message,
  and the answer goes out as one, streamed if the model streams.
- A tool call runs in a process of its own, so one that fails ends
  alone; a tool is a function of the model's arguments, which can ask an
  actor when it needs state (D187, rather than an actor answering with
  `reply`, as first planned).
- Agents talk to each other in messages, and between machines in
  printed s-expressions over TCP (D155), as calls (`lib/rpc.slight`,
  D225).
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
under (none yet, but the caller's timeout and `:too-big`). Settled by
tools (D187): how a tool says what it takes, `(tool/new name
description params f)`, its parameters a list of `(name type
description)`.

### 16. As needed

Left "for now" by earlier decisions, and taken on when something needs
them:

- **Display width, graphemes and normalization** (D219): a wide
  character (CJK, most emoji) takes two columns of a terminal and counts
  one, so a terminal UI can't line up such text yet; `str/width`,
  `str/graphemes` and `str/normalize`, from utf8proc, vendored as D151
  has it.
- **Containers, what's left** (after D227, D229 and D233, Oct 2026):
  each process's heap is capped at a quarter of the container's limit
  (D233), but not all of them together, so many processes can still
  take the program past it. SIGTERM ends a program at once, so a call in flight fails at its
  caller: hearing it as a message, to finish within the grace period,
  needs a way in for signals, a proposal of its own. A Dockerfile in the
  docs, a build stage and `FROM scratch`, and both targets as one
  image, since `slightc` makes both statically; health checks (a TCP
  probe, or `/healthz` from `lib/http.slight`), as a scratch image has
  no shell. And anyone on the network can call an `rpc` program: a
  token in `helo` first, then TLS (12c).
- More than 8 arguments to a function, on the stack (DESIGN.md).
- Sockets: IPv6, and more than the 1,000 or so that `select()` holds
  (D113), with `poll`, `kqueue` or `epoll`: `kqueue` on macOS and
  `epoll` on Linux, as tokio waits (D224), for everything `select()`
  waits on now, sockets, files, commands and the terminal; an HTTP
  server meets the limit first.
- Dropping exit records, and bounded mailboxes (D88).
- Optimizations, when profiling asks for them: the heap pointer in a
  register (D57), captured values read from the closure (D79), denser
  lists (CDR-coding, VLists: D23), reference counting with reuse (D24).

### Benchmarks (phases 1 to 3 done, Oct 2026)

slight against C, Go, Erlang, Node, Perl and Python (D194,
`docs/BENCHMARKS.md`, `make bench`), Chez Scheme (D201), and Rust with
tokio in the actor programs, their floor (D224).

- **Phase 1:** `fib`, `binary-trees`, `n-body`, `ping-pong`,
  `thread-ring`, `spawn`, `idle-memory` and `parallel-fib`, on the cloud
  machine and the M2 Max, the reference.
- **Phase 2:** fixing slight's worst ratios, as the profiles found them:
  tuples built in place (D195), collecting before the heap's cap (D196),
  better code for calls, arithmetic and tests (D197), float arithmetic
  inline (D198), a message with nothing to copy freed as it's taken
  (D199), and a generational collector (D200). On the M2
  (BENCHMARKS.md, "Phase 2 on the M2 Max"): `binary-trees` 2.4 times
  faster, fib, `n-body` and `parallel-fib` 1.5, `thread-ring` 1.6 and a
  hundredth of the memory.
- **Phase 3** (D201), planned as part of phase 2 and put after its fixes:
  Chez Scheme, `tak`, `nqueens`, `deriv`, `spectral-norm` and `fan-in`;
  and what its profiles found: the prelude's list functions as loops,
  and its `not` inline (D203).
- **What the numbers point at next** (none started; BENCHMARKS.md, "What
  slight should look at next", and the "Where slight lands" dashboard):
  unboxed floats, the widest gap (10 to 36 times C, Go and Node, level
  with Chez and Erlang), which changes how values are represented and
  starts with a design note; smaller objects, a two-element tuple 24
  bytes rather than 32 (`binary-trees`); slowing a sender whose
  receiver's queue is long, or counting a mailbox in its heap's cap
  (`fan-in`); and more than one core (`parallel-fib`), a design of its
  own.
- **Later, to look into: compiler-known folds** (D203's option B).
  `fold/l` or `fold/r` given a literal lambda compiled to a loop
  specialised to its call site, the lambda's body inlined and its free
  variables passed as arguments, as ts-slight's fold continuations were
  the fast path all its list functions went through. The prelude could
  then be written over folds again, and programs' own folds would gain
  too. It needs a pass of its own: free variables, generated functions
  named for `:system`, D80's namespaces, and the eight argument
  registers. Worth it once programs show folds with lambdas on their hot
  paths.
- **Later, to look into: a mailbox in blocks** (D224). A message with
  nothing to copy (an integer, a symbol: D199) still takes a malloc of
  its own (D177), 48 to 64 bytes; tokio's channel keeps messages in
  blocks of 32 slots, about 10 bytes each. Such messages in slots of a
  block, the mailbox a list of blocks, would take `fan-in`'s ten
  million from 484 MB on the M2 to about 100, and cut a malloc and a
  free from every such send. Messages with something to copy keep
  their chunks.

### Runs that branch (begun Oct 2026)

Replay as a feature of the language, not only of the tests: record a
run, branch it at a point with something changed, and keep only what the
branch changed (`new-vision.md` in ts-cpi: "record only what cannot be
recomputed"). Begun with `examples/airline.slight` (D207), which does it
in slight on today's runtime, each branch run again from the start of
the day. At the size of a small airline (D210: 300 aircraft, 1,499
legs, ten what-ifs), the whole run is under 3 s on the cloud machine,
and half the branches' time is the minutes before their changes. The
example now starts each branch from a checkpoint of its parent's, and
keeps its runs in a file a later round of branches starts from (D212),
all in slight; and recovers from a disruption, remedies tried as
branches, the best checked by crew and maintenance services and ranked
by another, over HTTP, slight keeping nothing of anyone (D213); and
makes its what-ifs, and picks its remedies, at random from a seed, as
many as asked, `bench/airline.py` running sets of them and timing them
(D214); and makes its remedies near what the disruption touched, a day
checked once however many come to it (D215); and runs its what-ifs in
shares, a copy of the program a core, 3.94 times as fast on 4 (D218).
Next, each a proposal first: an input journal of values beside
`SLIGHT_RECORD`'s log of system calls, its requests checked so that a
replay finds the first real difference; replaying to a point and then
going on live; forking a run at a point with `fork(2)`, a branch to a
core; snapshots on disk; and `clone`, a waiting process copied, for a
branch smaller than a whole run.

### Strings (done, Oct 2026)

One set of string builtins in place of the byte ones and the `utf8/`
ones (D219): `str/` counting characters, with JavaScript's methods'
names and edge cases, `bytes/` counting bytes, conversions as `a->b`,
case mapping and white space from Unicode's tables (15.1, generated
into `runtime/unicode.c`), ASCII strings marked so they're indexed in
one step, and `pprint` escaping strings. Every program moved to the new
names; golden tests 261 to 265, from `t/models/strings.py`.

### Waiting for several processes (done, Oct 2026)

`join/race`, the first of a list of pids to end, in the runtime, and
`join/any` and `join/all` in the prelude on it: JavaScript's
`Promise.race`, `any` and `all` (D220). They keep the caller's stack, as
`join` does, so the caller needn't be a state function. `http/await`
races its request against a sleeper. Golden tests 266 to 268.

### Numbers (done, Oct 2026)

The math builtins under `math/`, with the rest of JavaScript's `Math`,
`math/pow` exact on two integers, `math/mod`, `math/nan?`, `math/E` and
`math/INF`, `int->float` and `float->str` (`toFixed`); and the bit
operations, `bit/`, compiled inline (D221). Golden tests 269 to 272.

### Random numbers (done, Oct 2026)

A generator in each process, SplitMix64 as Java's `SplittableRandom`
has it: `rand/int`, `rand/float` and `rand/seed` in the runtime,
`rand/pick` and `rand/shuffle` in the prelude; the run seeded by
`SLIGHT_SEED` or the system, the seed recorded for a replay, and a
fork's generator split from its parent's, so a process's numbers don't
depend on the order processes run in (D222). Boids draws its flock with
them. Golden test 273, and the replay checks. Cryptographic randomness
waits for TLS.

### Maps (done, Oct 2026)

A persistent map, a hash trie of tuples, in slight: `lib/map.slight`,
with `tuple/insert` and `tuple/remove` in the runtime for its nodes
(D223). Golden tests 274 and 275, from `t/models/map.py`. The airline
finds its runs, its shares' trees and its remedies' days with it.

### Later: the rest of the October 2026 list

The other changes from that list (above, "After 13"), for later, before
self-hosting, since most change the language. Each has points to settle
with Stevan first.

- **Numerical vectors and matrices,** only if a program needs them
  (D179, D221): embeddings for the agent's notes are the likeliest.
  Floats that need no box would come first.
- **A `recv` with a timeout.** Today it's `after` and a message, and
  `timer/cancel` once the answer has come (D232), which says whether the
  timeout came first, to be told apart by hand. A clause such as
  `(:after ms body...)`, as Erlang's, would do that itself. To settle:
  the syntax; that a receive function starts again from the top
  after each message, dead letters included, so the deadline must be set
  once per wait, not per start; and how it goes through `split.ts`.
- **Timers that can be cancelled** (done, D232): both. `after` gives its
  timer's number, which `(timer/cancel t)` takes back; and a timer whose
  process has ended no longer keeps the program running, which frees
  `ask-within` (D157), `http/request-within` (D173) and a closed keeper
  (D228). And so the `run` tool's time limit (D187) is in too (D234);
  HTTP's default timeout (D173) was, and was taken out again (D236).
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
- **Distribution, as calls, not remote pids** (settled by D225). Programs
  on other machines reach each other through `lib/rpc.slight`: calls,
  streams, `stop`, and a failure when the connection ends (D225), a hub
  calling the devices that dial it, and connections kept up (D228).
  Turned down there: remote pids, a process standing for one on another
  machine as in Termite Scheme or Erlang's distribution, which bring
  monitors across machines, names, and what a pid means after a
  reconnect. What's left is under 15 and 16: to whom a remote call's
  `approve` goes, a token in `helo`, then TLS (12c).
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
  arithmetic in C and the boxes it makes. (The arithmetic is inline now,
  D198, and the boxes allocated inline; the boxes themselves remain.) The fix that helps every
  program is in the representation: floats that need no box (NaN-boxing,
  or a tagged short float), or a compiler that keeps a float unboxed
  within a function. Either is a proposal of its own; boids is the
  benchmark. Not this way (D179): fixed point in the example, or a type
  of numerical vectors.
- **Exit records (settled, D244: a task's value kept till its parent has
  it, the table in pages, no `detach`; built as D243's step 5).** As
  talked over in Oct 2026:
  Every process's record is kept for good (D88), its 40-byte entry and
  a copy of the value it ended with, outside every heap, so outside
  every cap (D202, D233). D88 was decided for batch programs of small
  results; since then slight has come to run for good, in containers
  (D227, D229, D233), for fleets (D228), as an HTTP server (D173) and an
  agent (D184 on), and these fork for every request or call. Measured:
  `bank` (D241) keeps 40 MB a million customers; an HTTP server and its
  client in one program keep about 22 KB a 10 KB request, each end a
  copy of the response (12.5 MB at 500 requests, 89.5 at 4,000), which
  at 100 requests a second would be about 1 MB a second; the airline's
  runs about 0.15 MB each (D216). What a rule has to keep: sends, kills
  and timers to an ended pid, and monitors and `join/race` (D92, D183,
  D220, D232), need to know only that a pid has ended, and pids are
  never reused, so a pid forgotten still reads as ended; and a rule
  must be decided by what the program does, never by when the collector
  runs, for replay, `SLIGHT_SCHED` and `SLIGHT_GC_STRESS` (D170). Every
  join in `lib/` and the airline is a process joining what it has just
  forked or connected. The ways weighed:
  - **E2+V2, Unix's zombies:** a record kept till its parent joins it or
    ends, then forgotten, a later `join` an error; joiners waiting then,
    and monitors, told as now.
  - **Erlang's, as far as slight can go** (Claude's recommendation):
    Erlang keeps nothing and tells only those who asked first, values
    coming back as messages picked out by selective receive, which
    slight turned down (D8), so `join` and a record held for the parent
    stay. After a process ends only its parent can join it, once;
    anyone else gets `(:error :noproc)`, and a monitor then
    `(:exit pid (:error :noproc))`. Stricter than E2, so that a late
    join depends only on whether the process had ended, not on what its
    parent has done; it changes t/122, t/125 and t/266, and a monitor
    set after the end no longer gets the result.
  - **For children nobody joins** (`bank`'s customers, a server's
    connections, `rpc`'s calls), under either: `(detach pid)`, as
    `pthread_detach`, the parent saying it won't join, so the record
    goes as the child ends, for `fork` and `connect` alike (Claude's
    recommendation); or `spawn`, as Erlang's, a fork nobody can join,
    with `connect` needing one of its own; or the libraries reaping
    their own, monitoring each child and joining it at its `:exit`.
  - **What it takes:** the table in pages, each freed once every pid in
    it is forgotten, a parent link, and each process's children not yet
    joined; the value let go at the join (V2) is the small part. Without
    a rule, entries could still shrink to a word each, 8 bytes for a
    process that ended with an immediate, with no change to what any
    program sees (E1).
  - **To settle:** strict `noproc` or E2's looser rule; `detach`,
    `spawn` or reaping; and the error's name. Rejected on the way: a
    record forgotten once nothing can name its pid, which programs
    couldn't see but needs a walk over every heap, mailbox, record and
    timer, and fails silently if one is missed; and Erlang's model
    as it is, which without selective receive would make every caller
    of `(join (fork ...))` a state function.

### Tasks and actors (agreed, D243; built, with D244)

Claude's proposal, after Stevan's "there are two forms of concurrency
here ... perhaps we should spawn actors instead of fork them"; agreed
with every point below settled as Claude recommended (D243), and the
rest of exit records with D244. Built: the order is at the end.

**Why.** slight has two models of concurrency behind one `fork`:

- **Tasks:** `fork`, `join` and `yield`. A computation with a value,
  plain code that can block anywhere, `join` keeping its stack (D19).
  `author/meta-circular.slight` is the pure form.
- **Actors:** `send` and `recv`. A loop with state and a mailbox, that
  runs till it's stopped and has no value anyone joins. The `recv` rule
  (D8) suits it, since an actor is a loop anyway.

Where the two meet, a process is both, and that's where the costs found
in the review of Oct 2026 come from:

- `ask`'s box (D157), `rpc/call`, `agent/ask`: a process whose `recv`
  takes one reply and ends with it, so a task can have an actor's
  answer. A fork, an exit record kept for good (D88), and a stack held
  in `join`, per question.
- `(join (connect ...))` (`slurp`, `exec/run`, `http/request`): a
  process that receives a device's messages and ends with a value.
- Exit records keep every process's value, since any process may be
  joined, though most actors never are (D216, "Exit records" above).
- A helper's `recv` takes its caller's messages, since every process has
  one mailbox; that's why the boxes are processes.
- A wait spreads up the call chain: a function that waits can be only
  tail-called, so can its callers (D167). That's actor code's shape,
  and a burden on code that only wants an answer.
- D8's "no call-cycle deadlocks between handlers" no longer holds once
  a handler can `ask` (D192's agent asking itself).
- 40,000 processes blocked in `join` stop the runtime (`rt: can't
  allocate a stack`, exit 2), where D167 turned down stacks kept in
  `recv` for the same limit, about 32,000.

**The census** (`docs/processes-2026-10-23.md`): of the 96 `fork`s and
`connect`s in `lib/` and the examples, 49 are actors (39 loops, 7 that
give the root a summary at the end, 3 watchers), 14 are tasks, and 33
are both. Of those 33, 9 wait for one reply, 18 read a device to its
end for a value (11 in the libraries, 7 terminal programs whose last
state the root joins), and 6 coordinate: they start actors and gather
several messages (the tournaments' games, ping-pong's pinger, the
airline's desk, an agent's turn). Of the 14 tasks, only 2 are parallel
work joined for its value; the rest stand in for `try` (3), a timeout
or a failure to race (2), a call whose value is told by message or
monitor (2), and work never joined (5).

**The proposal.** Two kinds of process, and a task's ways to wait for
something other than a process's end.

- **`(spawn expr)` starts an actor.** The `recv` rule is an actor's
  alone, as it is now (D8, D167). It can be sent to, monitored and
  killed, not joined: `join` of an actor faults (`:not-a-task`). When it
  ends, its monitors are told `(:exit pid result)` as now, and its value
  isn't kept: its exit record is only that it has ended. `connect`
  starts an actor too, owning the device, as now.
- **`(fork expr)` starts a task**, as it does now, but a task can't
  `recv`: the compiler refuses a `fork`'s expression, or the top level,
  that waits (a state function, D167), and the runtime faults a `recv`
  in a task as a backstop. It can't be sent to (`:not-an-actor`). It can
  be joined, monitored and killed. The root is a task: the program's
  value is its value, as now (D93).
- **`(address)` makes a place for a task to be sent to.** An address is
  a value naming the task and a number of its own, so many can be in
  use at once. `send` and `after` take an address where they take a pid,
  so an actor answers without knowing it's an address: `lib/ds`'s
  `(reply to v)` is unchanged. `disconnect` closes an address: what's
  sent to it after goes nowhere, as to an ended process (D92), and
  what's waiting is freed. One sent to an ended task goes nowhere.
- **`(read src)` waits for the next message from `src`,** keeping the
  stack, as `join` does: an address of the task's, or a device it owns.
  Messages from anywhere else can't be taken by it, so a helper can't
  take its caller's, and there are no dead letters in a task.
- **`(read/any srcs)` waits for the first of several,** giving `(src
  msg)`: addresses, devices, and pids, a pid's "message" being its end
  (`join`'s result for a task). `join/race` (D220) is `read/any` over
  pids, and can become a prelude function over it.
- **`ask` is slight,** in the prelude, where `lib/ds` has it now:

        (defun ask (pid msg)
            (let r (address))
            (send pid (append msg (list r)))
            (let got (read/any (list r pid)))
            (disconnect r)
            (match got
                ((_ (:reply v)) v)
                ((_ result)     (raise (list :ended pid result)))))

  `ask-within` adds `(after ms r :timeout)`. No process, no exit
  record, and nothing held but the stack while it waits.
- **Gathering** is one address given to many: the airline's desk sends
  each aircraft its tick with one `r`, and takes as many answers with
  `read`. A stream (`http/stream`, `rpc/stream`, an agent's pieces) is
  an address the stream sends to till it ends.
- **`(open how ...)` opens a device the calling task owns,** with the
  same sources as `connect` (`:fs/read path`, `:tcp "host:port"`,
  `:exec cmd`, ...), and no process forked. It gives `(:ok d)` once the
  device is open (a connection connected), or `(:error reason)`.
  `(read d)` gives the device's messages as an owner gets them now,
  `(:line d s)`, `(:chunk d s)`, `(:eof d)` and the rest, its next one
  cut at the `read` (D158's cut, at a `read` in place of a `recv`), and
  `(:read how)` and `(:write ...)` are sent as now. A device that fails
  gives `(:error d reason)` from `read`, where it ends an actor that owns
  it (D128): a task asked for the device, and can be told. So `slurp`:

        (defun slurp (path)
            (match (open :fs/read path)
                ((:ok f)         (slurp-lines f ()))
                ((:error reason) (list :error reason))))

        (defun slurp-lines (f lines)
            (match (read f)
                ((:line _ s)  (slurp-lines f (cons s lines)))
                ((:eof _)     (list :ok (reverse lines)))
                ((:error _ r) (list :error r))))

**What it settles.** An actor's end keeps no value, which is most of
what exit records hold (`bank`'s customers and a server's connections
would be actors). The library's box and reader processes go: an `ask`
costs an address and a wait, and `slurp` a device. A task's code never
meets the `recv` rule; an actor's meets it where it's a loop already.
Nothing is taken from a mailbox by a helper, since tasks have none. And
an actor that can't `ask` (below) can't wait on another, so D8's
"no call-cycle deadlocks between handlers" holds again for actors.

**What doesn't change.** The collector, message copying, the scheduler,
timers, the assembly, recording and replaying (no new system calls),
`split.ts` (which matters only where a `recv` is), monitors and `kill`.
A message's record already says what device it came from
(`rt_msg_t.device`), which serves for an address too.

**Size.** In the runtime, about 350 to 450 lines of `process.c`'s 3,400:
the kind of a process (a flag in the padding of `rt_proc_t`) and its
rules, actors keeping no value, addresses (`send`, `after` and
`disconnect` taking them), `read` and `read/any` (a wait that keeps the
stack, like `join`'s, woken by a message from its source; `join/race`'s
watches for pids), `open`, a device's failure told to a task (`fail()`'s
15 callers), `kill` of a task while it reads, and `:system` and the
deadlock message (`:kind`, `:reading`). In the compiler about 150:
`spawn`, the builtins, and `classify.ts` refusing a wait in task code.
Most of the work is elsewhere: the 33 hybrids in `lib/` and the
examples; the golden tests, 142 of 265 using `fork` or `connect` (most
only `fork` to `spawn`; those that join an actor, monitor it; 9 with a
`recv` at the top level give it an actor); and the docs.

**Settled (D243), each as Claude recommended:**

1. **Names:** `spawn` and `fork`; `address`, `read`, `read/any` and
   `open`.
2. **What an address is:** an immediate under the pid tag (the task's
   number and the address's), printed `#<address 3.2>`, nothing to copy
   or collect, and not to be made from data, as a pid can't be; or a
   tuple `#(pid n)`, plain data, which can be printed, read back and
   faked. Claude recommends the immediate.
3. **Can an actor ask?** Claude recommends no: an actor makes no
   address, so it can't `read`; it sends and handles the answer as a
   message, or forks a task to wait. That's what keeps handlers from
   waiting on each other. A rule users will meet, but one with a reason
   in the model, not in how it's compiled.
4. **Can an actor block in `join` or `sleep`?** Today's examples do
   (`top`'s consumer sleeps). Claude recommends yes, as now.
5. **An actor's end, told late:** with no value kept, `monitor` of an
   actor that has ended, or `read/any` of its pid after its end, can't
   say how it ended: `(:exit pid :ended)`. A change to D101 for
   actors.
6. **`:keypress`** isn't a device (D131), so a task can't `read` keys.
   The 7 terminal programs whose last state the root joins stay actors
   that tell their monitors; or `:keypress` becomes a device. Settled:
   they stay actors, and D131 stands.
7. **Tasks as `try`:** 3 of the 14 tasks fork only to turn a fault or
   a `raise` into a Result. They can stay so (`join` still gives `(:error
   reason)`), or this is when D17's "no `catch`" is looked at again.
   Settled: they stay so.
8. **`ask` and failure:** it raises, as D157's does; or gives a Result,
   as `agent/ask` (D192) and `http/request` do. Settled: it raises.
9. **The rest of exit records:** a task's value is kept till it's joined;
   when it goes after that, and when a pid's entry goes for either kind,
   is the item above.

**The order, each step with passing tests:** (1) `spawn`, the two
kinds, and the rules that hold without the rest: a task can't wait, nor
be sent to, timed or monitor, and the root is a task (done, D243's
"Step 1"); (2) addresses, `read` and `read/any`, `ask` in the prelude,
and `lib/ds`, `rpc/peer`, `rpc/connect` and `agent/ask` moved to them
(done, D243's "Step 2"); (3) `open`, a device's failure told to a task, and `lib/fs`,
`lib/exec` and `lib/tools` moved (done, D243's "Step 3"); (4) an actor can't be
joined and keeps no value, told late as `:ended`, with HTTP's client,
`rpc/call`, `rpc/stream` and the coordinators (the games, the airline's desk, an
agent's turn) moved off joined actors first (done, D243's "Step 4"); (5) the rest of exit
records (done, D244). (4) was part of (1) as agreed, and moved: until (2) and (3)
give a task other ways to have an answer, the hybrids (`ask`'s box,
`slurp`, an HTTP request) are actors that are joined.

Replaces, if taken, parts of: D8 and D167 (the `recv` rule is an
actor's), D19 (`join` is a task's), D88 and D216 (an actor's value isn't
kept), D124 and D128 (a task opens a device, and is told its failure),
D157 (`ask` without a box), and D220 (`join/race` is `read/any` over
pids).

### A C-style syntax (agreed, D246; the reader built, Oct 2026)

A second way to write slight, `.sly`, read into the same s-expressions
(`docs/SYNTAX.md`). In steps, each with passing tests:

1. **The reader** (done): `compiler/src/sly.ts`, chosen by the file's
   extension in `expand.ts`; unit tests against `reader.ts`, and golden
   tests 303 to 307 (306 and 307: game-of-life-actors and boids,
   converted, to the examples' own output).
2. **The warning** for a tag used both bare and as a tagged list in one
   program (D246), which helps the s-expressions too: done for a send
   to an actor whose recvs are known, by the type checker (D247).
3. **The converters,** `slightc --to-sly` and `--to-slight`, which keep
   comments and doc blocks, and lay a program out by rules of their own.
4. **The guide in both syntaxes:** every program in `docs/guide/code/`
   converted, and run to the same `.expected`; the pages could then show
   either.

Each of 2 to 4 waits for Stevan to have written some `.sly` and to say
it's worth keeping.

### Types (agreed and built, D247, Oct 2026)

A checker that warns before a program is compiled (`docs/TYPES.md`), in
the steps that document gives, each with passing tests:

1. **The lint** (done): `types.ts`, `check.ts`, `builtins.ts`; what's
   certain from literals, builtins, patterns and tests; `slightc
   --check`; `make types` over every program.
2. **Processes** (done): task, actor and address types, mailboxes from
   `recv`, dead letters, an actor joined, the keyboard.
3. **Annotations** (done): `deftype`, `sig`, `the`, their `.sly`
   syntax, generics, and the prelude's sigs.
4. **`--strict`** (done).
5. **Types in code generation:** waits, till the checker has been used
   a while, and then only with benchmarks to show it's worth it.

Also waiting: typed devices beyond the keyboard, parameters' types from
their use (success typings), and contracts checked at run time.

### Modules (built, D248, Oct 2026)

A file with a header (`X.sli`, `X.slyi`) is a module: its includers see
what the header declares, and the rest is its own; `modules.ts` links by
renaming. The prelude and every library have headers. Waiting, till
Stevan has used types and `.sly` more: whether a richer module system
(imports under other names, or of some names) is wanted; real types in
the libraries' headers beyond `fs`, `exec`, `calc`, `map` and `test`;
and the libraries checking clean under `--strict` on their own.

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
  static binaries already, and so do containers, `FROM scratch` (D227).

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
