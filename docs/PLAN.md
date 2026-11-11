# Plan

Ghuloum's incremental approach: the compiler works and its tests pass at
the end of every step, and each step adds one feature. Steps are sized to
be a session or two each.

**Progress:** steps 0 to 14 are done (under qemu, and natively on macOS:
Stevan runs `make test` on his M2 Max after every step), all but 12c, C
libraries, which waits, its TLS withdrawn for curl and a proxy in front
(D251); with them a second target, x86-64 (which passes on the M2 too,
under Rosetta 2), and UTF-8 builtins (both after 10e), and after 13
`match`, tuples, and recording and replaying a run (D168–D170). Agents
(15) are built as far as their section says (D180–D193, and calls
between programs, D225–D235), and 16 is taken on as needed. Since then,
each in its section below: benchmarks (D194–D203, D224, D237–D242);
strings, waiting for several processes, numbers, random numbers and maps
(D219–D223); tasks and actors (D243–D245, retired by D277); the C-style
syntax's reader (D246); types (D247) and modules (D248); toward general
purpose, steps 1 and 2 (D249, D254, D255), and numbers in bulk (D253);
more than one core, as islands (D256); floats inline and in registers,
as far as they went (D259–D262); the review of 2026-11-02's fixes
(D265–D275, `review-2026-11-02-plan.md`); one kind of process, and an
address for every message (D276–D280); the agent team (D281–D285);
`fork` taking a function (D286); keywords (D287); and `assoc` replacing
and a quiet end (D289, D290). Next: nothing agreed. Proposed, or ideas,
each to put to Stevan: simulation across islands, a living town (D263),
branching a replay, a viewer for simulations, and the standard library,
general purpose's step 3. Self-hosting (17) waits till the language
settles.

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

#### 12c. C libraries (TLS dropped, D251)

After agents have started (D180): a local model needs only plain HTTP.
TLS was to come here, for HTTPS; it won't be vendored (D251, below).

Discussed in Oct 2026 (D151, D152; BACKGROUND.md has what Odin does):

- A pure function bounded by its input (a hash) may be a builtin; a
  library with state or I/O (TLS, a database) is a device. Nothing
  blocks, and no slight value holds a C pointer.
- Vendored as source under `vendor/`, each at a pinned version with its
  licence and a `VENDOR` file; `slightc` compiles one as it does the
  runtime, and links it statically into the programs that use it.
- In C: hashing, JSON and s-expressions (12a). Database
  drivers when they're wanted; one that can only block (SQLite) would
  need the runtime's first thread, which isn't decided.

Then TLS was to be the first vendored library, a `:tls` socket device
on mbedTLS. Withdrawn (D251): vendored code is only what never needs a
security fix (math, hashes, Unicode's tables), and what does is left to
programs the system keeps up to date, run as processes. A server sits
behind a proxy that ends TLS (nginx, Caddy, a cluster's ingress), on
plain HTTP or a Unix socket (D231); a client's `https://` goes through
curl, run by `lib/http.slight` with `:exec`, with the system's
certificates and proxies; and between machines, ssh (`rpc/exec`,
D225). Built (Oct 2026): `https://` through curl in `http/request`,
`http/request-within` and `http/stream`, curl asked for the raw HTTP/1.1
reply, which the same code reads as a socket's; curl's exit code as the
error, in the device's words where there are some, and a reply given
only once curl has ended with 0. Golden tests 309 (a stand-in for curl,
`t/data/curl/curl`), 211 and 220 (real curl, refused), 310 (a compiler
bug it met: `connect`'s source compiled as a tail call when `connect`
was one), and `t/live.py`'s https, real curl over real TLS.

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
`log`, a builtin; a reader of server-sent events, for a hosted model,
now that `https://` goes through curl (D251, golden test 309); and
`llama-server` run by the program, with the OpenAI-compatible protocol
in `lib/agent.slight` beside ollama's (D251).

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
  reading JSON makes one message each), a hosted API over HTTPS (through
  curl, D251),
  and a Jev-compatible server for small "System One" models, which give
  a decision (yes or no, a choice, a score) with its probability rather
  than text. JevOS, an open one, runs a 1B model on the CPU in 25-110 ms
  a decision. Not in the runtime, even a small one: inference is CPU
  work that would hold up every process without a thread of its own
  (DESIGN.md's open question 2), the libraries are large and in C++, and
  a crash in one would end every actor. A local server costs an HTTP
  request on top, a fraction of a millisecond against the model's tens.
  And no server to install apart (D251): llama.cpp's `llama-server`,
  started by the program with `:exec` and watched by a monitor, serves
  the OpenAI-compatible protocol on localhost, which `lib/agent.slight`
  would speak beside ollama's, as vLLM, LM Studio and the hosted APIs
  do. Linking llama.cpp in was turned down: C++, a thread, no GPU in a
  static Linux binary, a crash ending every actor, and a project that
  changes daily to follow.
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
  token in `helo` first; for encryption, ssh (`rpc/exec`) or a proxy
  in front (D251).
- More than 8 arguments to a function, on the stack (DESIGN.md).
- Sockets: IPv6, and more than the 1,000 or so that `select()` holds
  (D113), with `poll`, `kqueue` or `epoll`: `kqueue` on macOS and
  `epoll` on Linux, as tokio waits (D224), for everything `select()`
  waits on now, sockets, files, commands and the terminal; an HTTP
  server meets the limit first. Waits till a program needs it: slight
  leans to simulation, not servers (D257).
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
them. Golden test 273, and the replay checks. Cryptographic randomness:
random bytes from the system, through `replay.c` ("Toward general
purpose", 2; D251).

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
  HTTP's default timeout (D173) was, and was taken out again (D236),
  since the virtual clock moved to it while a reply crossed macOS's
  loopback; the clock waits for such a reply now (D288), which the
  agent team's budgets needed.
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
  `approve` goes, a token in `helo`, and ssh or a proxy for encryption
  (D251).
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
  D198, and the boxes allocated inline, and comparisons, D259; the boxes
  themselves remain: "Floats without boxes", below, proposes what's
  next.) The fix that helps every
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

### Tasks and actors (agreed, D243; built, with D244; retired by D277)

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
(imports under other names, or of some names) is wanted. Every library
checks clean under `--strict` on its own, which `make types` holds them
to. Every library's
header is typed (`ds`, `http`, `rpc`, `agent` and `tools` after the
first five, with the types their docs name: `Response`, `Handler`,
`Tool`, ...; `t/types/010`).

### Toward general purpose (order agreed, D249, Oct 2026)

What slight lacks before someone can write a command-line tool, a web
service, a script over data or a daemon in it without shelling out for
the basics. In this order, each step's builtins and devices proposed
before they're built, as the working rules say. Numbers in bulk (below,
D253) come after step 1, and more than one core (7) after them. Two
points that shaped the rest were settled in Oct 2026 (D252):

**C libraries: vendored only, no FFI** (D252). D151 and D152 have
vendored libraries, each a builtin or a device that `slightc` knows
about, and D251 narrows them to code that never needs a security fix.
An FFI call could block, keep pointers, and do I/O that isn't recorded,
so a run could no longer be replayed whole (D250's rule). What can't be
vendored runs as a process (D251).

**Regular expressions: a small engine of our own** (D252), in C, a
subset of RE2's syntax, its time linear in its input, so no pattern can
stall a process and there's nothing to patch for one (D251); a builtin,
as D151 allows a pure function bounded by its input. Its syntax and
builtins are to be proposed with the standard library (3).

1. **What the runtime is missing,** small things each:
   - The time of day: seconds (or microseconds) since the epoch, read
     through `replay.c` so a run replays, and on the virtual clock its
     start plus the virtual time. Calendar dates, formatting and time
     zones after it, in slight, as a library.
   - Files beyond reading and writing: listing a directory, a file's
     size, kind and times, deleting, renaming, making a directory. Today
     they go through `:exec` (`lib/tools.slight` does).
   - Binary data: integers and floats packed into bytes and read back,
     by width and byte order, and a way to build a string of bytes
     without copying it at each step. Binary protocols (PostgreSQL's,
     WebSockets', msgpack) need it.
   - A log device: levels, structured lines to stderr, recorded.

   Proposed below, "Step 1, proposed", and agreed (D254): `clock/now`
   and `lib/time.slight` (done, golden test 311, from
   `t/models/time.py`, and `t/replay/now.slight`), `fs/` builtins
   (done, `runtime/files.c`, golden test 312, and trees in
   `lib/fs.slight`, 313, and `t/replay/world.slight`), `bytes/pack` and
   `bytes/unpack` (done, golden test 314, from `t/models/pack.py`),
   and `log` (done, golden tests 315 and 316). Step 1 is done.
2. **HTTPS, and what TLS would have brought** (D251, in place of TLS
   on mbedTLS; done, D255, golden test 317): `https://` through curl in
   `lib/http.slight` (golden test 309), and a
   server behind a proxy that ends TLS; SHA-2 and HMAC in C of our own,
   small and fixed, which never need a security fix; base64, and random
   bytes from the system for keys and tokens, through `replay.c` (not
   `rand/`'s, which are for simulation, D222). Hex and URL encoding are
   slight's.
3. **The standard library,** in slight where that's practical (D152):
   `format`, CSV, parsing `@ARGV`, a set, sorted maps beside the hash
   map (D223), dates (from 1), and regular expressions (D252). No
   mutable array; arrays of numbers, immutable, are Numbers in bulk's
   (D253).
4. **Supervisors and a registry,** an OTP's least, in slight on
   `monitor`: restarting what fails (one for one, the rest after one),
   with a limit on restarts in a time, and processes found by name, so
   one restarted can be found again (`rpc` users build their own
   today). Shutting down in order on SIGTERM needs signals as messages
   (16, Containers).
5. **Tooling:** a formatter for both syntaxes, which is also the
   converters (A C-style syntax, 3); a language server on `check.ts`
   (its warnings as diagnostics, a name's type on hover, a definition
   from `modules.ts`); a debugger that steps through a recorded run
   (D170); a profiler; docs from `=doc` blocks, as perldoc does them.
6. **Packages:** `@include` takes a path. Dependencies by git URL and
   hash in a lockfile, a place to fetch them to, and a search path for
   `@include :name`.
7. **More than one core** (Parked, Parallelism): a ceiling on servers,
   not a bar to being general purpose; separate heaps make it easier
   than in most languages, and the run queue and timers are the work.
   Moved up by D253 to follow Numbers in bulk, since numbers in bulk on
   one core aren't worth the bother. Agreed as islands (D256, below,
   "More than one core: islands").

And, though not features: a version number and a core that holds still
for it; one syntax the docs are written in, the other made from it.

**What vendoring takes out of the runtime.** Little, by the look of
it: what the runtime's C does is tied to its heap, `select()` and
`replay.c`, which no library does for it. `json.c` and `sexp.c` are
push parsers that build slight values as bytes arrive (D162, D163),
which the usual libraries (yyjson, cJSON) don't do; `dns.c` is 353
lines in `select()`, where c-ares is far bigger and has its own loop.
`unicode.c`'s 3,600 lines are tables made from the UCD, not code to
keep; if utf8proc comes for display width and graphemes (16), they
might shrink to SpecialCasing's mappings, which utf8proc lacks
(JavaScript's case mapping, D219, takes `ß` to `SS`), if its
properties give "cased" and "case-ignorable" as D219 needs them. To
check when utf8proc comes. What vendoring saves is the code not yet
written: graphemes, compression. TLS goes to curl and a proxy instead,
and hashes, base64 and random bytes are ours (D251). SQLite waits on
D151's open point, a thread for a library that can only block, or for
its command-line tool through `:exec`, clumsier but with neither.

#### Step 1, proposed (agreed, D254, Oct 2026)

The four pieces of step 1, each with options and a recommendation;
Stevan took each recommendation (D254). All four are builtins in C, recorded through `replay.c`
where they take from the world, so a run still replays (D250's rule).

**1. The time of day.**

`clock/us` is the microseconds since the program started, on the clock
timers go by, and never the time of day (D208). Nothing gives the date.

- (a) `(clock/now)`: the microseconds since 1970-01-01T00:00:00Z, an
  integer, as `clock/us` is (63 bits hold them for 146,000 years),
  POSIX time, so without leap seconds. On the real clock it's the
  system's (`CLOCK_REALTIME`), through a new `rt_io_realtime`, recorded;
  it can jump when the system's clock is set, which is why timers stay
  on `clock/us`. On the virtual clock it's the virtual time plus a
  start: `SLIGHT_EPOCH`'s, in seconds, if it's set, or the real time of
  day when the program starts, recorded either way, as `SLIGHT_SEED`
  is for random numbers (D222). `t/run.sh` sets `SLIGHT_EPOCH`, so a
  test's dates are the same every run.
- (b) As (a), in milliseconds, JavaScript's `Date.now()`, which `str/`
  and `math/` follow elsewhere (D219, D221): familiar, but a second unit
  beside `clock/us`.
- (c) Seconds, as a float: precise to a microsecond only till 2255, and
  floats print with an exponent.

Then the calendar, in slight, `lib/time.slight`, a module: a time made
a UTC date and back, `(time/utc us)` giving a keyed list (`:year`
`:month` `:day` `:hour` `:minute` `:second` `:us` `:weekday`) and
`(time/from-utc fields)`, by Howard Hinnant's days-from-civil, exact
over any year; `(time/iso us)`, ISO 8601 (`"2026-10-29T14:03:07.123456Z"`),
and `(time/parse-iso s)`, a Result; `(time/http us)`, the HTTP date
(`"Thu, 29 Oct 2026 14:03:07 GMT"`), which `lib/http.slight`'s
missing Date header wants; and a fixed offset from UTC, in minutes, for
`time/iso` to write. Not time zones by name: they need the system's
zone database, which changes (D251's kind of thing to leave to the
system); reading `/etc/localtime` in slight is a later option.

**Recommendation: (a),** and `lib/time.slight` as above. Golden tests
from Python's `datetime`, and `t/replay/` for a recorded run's dates.

**2. Files beyond reading and writing.**

Only `:fs/read`, `:fs/write` and `:fs/append`; anything else goes
through `:exec` (`ls`, `rm`, `mv`), as `lib/tools.slight` does.

- (a) Builtins, each a system call made where it's called, as a file
  device's `open` already is: `(fs/list dir)`, the names in it, sorted
  by their bytes so the order is the same on every file system, without
  `.` and `..`; `(fs/stat path)`, a keyed list, `:kind` (`:file`,
  `:dir`, `:link` or `:other`), `:size`, `:modified` (microseconds, as
  `clock/now`), `:mode`, following a link as `stat` does;
  `(fs/delete path)`, a file or an empty directory, as C's `remove`;
  `(fs/rename from to)`; `(fs/mkdir path)`, one level, its mode 0777
  less the umask. Each gives `(:ok value)` or `(:error (name path))`,
  errno's names as a device's are (`:enoent`, `:eacces`, `:enotempty`,
  ...), and goes through `replay.c`: recorded with what it gave, and in
  a replay not made, as no write is.
- (b) An `:fs` device that answers requests as messages, as D151 says
  of anything with I/O. But with no thread in the runtime, it would
  make the same system call on the scheduler's stack, blocking the
  same way; it only makes each call a message and a reply.
- (c) As now, `:exec`: a process per call, and output to parse.

**Recommendation: (a).** A call waits as long as the disk does, as
opening a file does now; on a network file system that can be long,
which a thread for the file system would fix, as D151's open point
would for SQLite. A whole tree (`mkdir -p`, `rm -r`, a walk) is
slight's, in `lib/fs.slight`, on these. Golden tests in `build/t/`, as
the tests that write files do.

**3. Binary data.**

Strings are bytes, and `bytes/at`, `bytes/slice` and `bytes/index-of`
read them, but nothing turns a number into its bytes or back.

- (a) `(bytes/pack specs values)` and `(bytes/unpack specs s at)`, the
  specs a list of keywords: `:u8` and `:i8`; `:u16be`, `:u16le`,
  `:i16be`, `:i16le`, and the same for 32 and 64 bits; `:f32be`,
  `:f32le`, `:f64be`, `:f64le`; and `(:bytes n)`, `n` bytes as they
  are. `pack` gives a string; a value out of its spec's range faults
  (`:out-of-range`), and an `f32` is rounded to the nearest float, as
  IEEE says. `unpack` gives `(:ok values next)`, `next` the index after
  them, or `(:error :short)` when `s` hasn't all of them yet, so a
  reader can wait for more; a `u64` that doesn't fit slight's 63 bits
  faults (`:overflow`), as arithmetic does. Specs are data, so a
  protocol's are a list it names once.
- (b) Perl's `pack` and `unpack`, a template string (`"n N a*"`):
  terse, and familiar to anyone who knows Perl, but a little language
  to parse, and letters to look up.
- (c) A builtin for each width and order (`bytes/u32be`,
  `bytes/get-u32be`, ...): forty names, where (a) has two.
- (d) Erlang's bit syntax, in `match`'s patterns: the best to read, but
  new syntax, and a change to patterns (D168), the most machinery.

Building a string without copying at each step needs nothing new:
`str/join` and `~` copy once, at the end, over a list of pieces.

**Recommendation: (a).** Golden tests from Python's `struct`, which has
the same widths and orders.

**4. A log.**

There's no way for a program to write to stderr: `pprint` and
`tty/write` write to stdout, and only the runtime's own faults, dead
letters and errors go to stderr.

- (a) A builtin, `(log level msg fields)`: `level` one of `:debug`,
  `:info`, `:warn` and `:error`; `msg` a string; `fields` an
  association list, `(list (list "port" 8080))`, or `()`. It writes one
  line to stderr, as JSON: `{"time":"2026-10-29T14:03:07.123456Z",
  "level":"info","pid":3,"msg":"listening","port":8080}`, the time
  `clock/now`'s, the fields as `json/print` writes them. Lines below
  `SLIGHT_LOG`'s level (`info` unless it's set) aren't written. JSON
  lines are what log collectors read (a container's logs, Loki,
  CloudWatch), and slight reads them back with `:json`. In a replay
  they're written again, as stdout is, so a recorded run's log comes
  back with it.
- (b) As (a), but s-expressions, one a line, which slight reads back
  with `:sexp`: slight's own, but nothing else reads them.
- (c) A `:log` device, a process that owns the log, which programs send
  lines to: a process and a mailbox for what a write does.
- (d) Just `(eprint x)`, `pprint` to stderr, and logging a library in
  slight on it.

**Recommendation: (a).** The runtime's own messages (`fault in ...`)
stay as they are: golden tests say them, and they're for people. A
log's lines on the virtual clock, with `SLIGHT_EPOCH` set, are the same
every run, so golden tests check them.

### Numbers in bulk (agreed, D253, Oct 2026)

Three proposals, for simulation, agreed as written with the points to
settle settled (D253): actors as agents, the virtual clock as simulated
time, seeded orders as Monte Carlo over schedules, and replay as
reproducibility are there already; what such programs lack is numbers
in bulk, the physics, statistics and fields over a grid. So D250's "not
for" loses numbers in bulk, slight becoming a language for services
*and simulations* that can be replayed, and D249's order gains these
after its step 1, with more than one core moving up after them, since
numbers in bulk on one core aren't worth the bother. Tensors, autodiff and a GPU stay out (a GPU's results differ by
card and driver, so replay would have to record them all, and its
kernels would need a language of their own).

In the order they'd be built, each its own step with golden tests, and
each useful without the ones after it.

#### 1. A libm that gives the same bits everywhere

**The problem.** `math/sin` and the rest call the system's libm, and
macOS's and glibc's differ in the last bit (D76): `(math/tan 1.0)` is
`1.557407724654902` on one and `1.5574077246549023` on the other. So a
run recorded on the Mac doesn't replay on Linux once it computes with
them, the golden tests compare them to 12 places, and `forest`, `coral`
and `waves` aren't golden tests at all. The runtime's C is compiled
with clang's default `-ffp-contract=on`, which may fuse `a * b + c` into
one rounding on AArch64 but can't on x86-64's baseline, a second way
the two targets could differ.

**Options.**
- (a) As now: D76, and replay on the machine that recorded.
- (b) Vendor CORE-MATH (MIT, plain C, D152): every function correctly
  rounded, so a result is *the* double nearest the true value, the same
  everywhere, and what an independent model (Python's `mpmath`) says.
- (c) Vendor musl's libm (MIT, fdlibm's lineage): the same code on
  every machine, so the same bits, but not correctly rounded, so a
  test's expected value has to come from musl itself.
- (d) LLVM's libc math: correctly rounded too, but C++, and a bigger
  build.

**Recommendation: (b),** with musl's version of any function CORE-MATH
lacks (as far as I know it has all twenty-one that `math/` needs from a
libm: `sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh
exp expm1 log log2 log10 log1p pow cbrt hypot`; to confirm when it's
vendored). `sqrt`, `floor`, `ceil`, `round` and `trunc` are exact
already. And `-ffp-contract=off` for the runtime and everything
vendored; the compiled code already does a multiply and an add as two
instructions. Then D76 is retired for `math/`: its golden tests compare
exactly, from `mpmath`, and `forest`, `coral` and `waves` can be golden
tests. Parsing floats (`strtod`, correctly rounded on both systems) and
printing them (our own, shortest) are the same everywhere already, and
`hash` makes every NaN alike (D209).

**What it costs.** It would be the first vendored library (TLS won't
be one, D251), so it builds D152's machinery: `vendor/`, its `VENDOR`
files, compiled once per target and kept, as the runtime is (D150).
CORE-MATH's functions are a little slower than glibc's (a correctly
rounded result sometimes needs a second, slower step); `bench/` would
say whether `n-body` and `spectral-norm` notice. Nothing in the
language changes.

**Settled** (D253): (b), CORE-MATH, with musl's version of any function
it lacks; linked into every program, as the system's libm is now, since
it's small and the runtime's own C calls it.

**Built** (Oct 2026): the runtime compiled with `-ffp-contract=off`
(`bin/slightc.ts`'s `CFLAGS`), and CORE-MATH's 21 functions vendored in
`vendor/core-math/` (its `VENDOR` file says from where and how to
update them), `runtime/numbers.c` calling them; golden test 319, each
compared exactly with mpmath (`t/models/math.py`). Still to do: `forest`,
`coral` and `waves` as golden tests, each needing a model of its
picture.

#### 2. Shared buffers: big immutable data off the heap

**The problem.** A message is copied into its receiver (D177, D240),
so an array of a million floats, or a 10 MB string, costs a copy at
every send, and its full size in every heap it reaches, against each
one's cap (D202, D233). A device's read is copied too, from the
runtime's buffer into its owner's heap.

**Options.**
- (a) As now: copy everything.
- (b) Erlang's reference-counted binaries: a string or array of at
  least a threshold's size lives off the heap, in a buffer with a count
  of references; the heap holds a small handle to it; a send copies the
  handle and adds one to the count; nothing is ever copied, since
  nothing changes.
- (c) A heap that all processes share, for big values, with a collector
  of its own: a second collector, and pauses that cross processes.
- (d) Moving a value on send, the sender losing it: needs the checker
  to know a value is used once, a linear type; machinery D247 left out.

**Recommendation: (b),** and it's safe here for the reason D200's
collector needs no write barrier: what's shared never changes, and
holds no pointers (only strings and arrays of numbers go off the heap,
never lists, tuples or closures), so no buffer points into any heap,
and no heap's collector has to know about another's.

How it works:
- A handle is a box of three words: the header (its type, string or
  array, and a flag saying it's shared), the buffer's address, and an
  offset, so a slice of a shared string or array is a handle too, made
  without copying. `rt_string_bytes` and the like read through it, a
  branch on the flag; nothing in the compiled code reads a string's
  bytes, so the change is the runtime's alone.
- Each process keeps a list of its handles, young and old as its heap
  is (D200). After a collection, a handle the collector didn't copy is
  garbage, and its buffer's count goes down; a buffer at zero is freed.
  A process's end lets go of all of its handles.
- A message's chunk (D177) keeps the handles it carries, and they join
  the receiver's list when it takes the message; a message never taken
  (a dead letter, a mailbox at the end) lets go of them.
- Erlang's lesson: a process that allocates little never collects, so
  it can hold dead buffers for ever. So the bytes a process's young
  handles refer to count toward collecting it, as if they were in its
  nursery, and all its handles' bytes count toward its cap.
- A device reads anything at the threshold or more straight into a
  buffer, and its owner gets the handle; writing a shared string writes
  from the buffer.
- `SLIGHT_POISON` fills a freed buffer; `SLIGHT_GC_VERIFY` checks each
  buffer's count against the handles that refer to it; `:system` says
  how many bytes of buffers each process holds, and the runtime in all.
  Replay isn't touched: where a value lives is never seen.
- One core now, so the counts are plain integers; with more cores
  they'd be atomic, and a buffer, never changing, can be read from any.

**What it costs.** Most of the work is in `process.c`: allocating
buffers, the handle lists, the collector and the message copy, and a
process's end. Strings gain from it at once, before there are arrays.

**Built** (Oct 2026, golden test 318, `t/live.py`'s memory: a 32 MB
string held by 40 actors peaks at 33 MB, and 600 strings of 1 MB made
and dropped at 13 MB): strings, the arrays still to come; not yet a
slice sharing its string's buffer, nor a device's read made a buffer as
it comes, nor the benchmark to tune the threshold with.

**Settled** (D253): the threshold 4 KB to start, below which copying
costs less than counting, tuned with a `payload` of big strings; a
handle counts its whole buffer toward the cap, since that's what it
keeps alive; and a slice is shared only when it's at the threshold, and
copied below, against Erlang's trap of a small slice keeping a big
buffer alive.

#### 3. Arrays of numbers

**The problem.** A vector of floats today is a list (16 bytes a cell,
and a 16-byte box for each float) or a tuple (8 bytes for a pointer to
each box): three to four times the memory of the numbers, a pointer to
follow for each, and every result a new box.

**Options.**
- (a) Arrays of anything, as tuples are: no gain for numbers, which
  stay boxed.
- (b) Arrays of one kind of number, unboxed: `:float` (IEEE doubles)
  and `:int` (slight's 63-bit integers, so an element always reads
  back), as SRFI 4's vectors and NumPy's arrays are.
- (c) (b) with shapes, n-dimensional, in the runtime: NumPy's model,
  and most of its machinery.

**Recommendation: (b),** one-dimensional, immutable, with shapes and
matrices a library in slight on top. An array of numbers holds no
pointers, so the collector never walks it (D240), and filling a fresh
one in place can't break D200's rule that nothing old points at
anything young.

- Printed `#float(1.0 2.5)` and `#int(1 2)`, in slight's names rather
  than SRFI 4's `#f64`; `sexp/print` writes that and `sexp/parse` reads
  it back (D163), `json/print` writes a JSON array. No literal in
  source, as there's none for a tuple (D176).
- `eq?` and `hash` compare kind and elements, as for tuples, an element
  as `eq?` compares floats; an `#int` array is never `eq?` to a
  `#float` one.
- Types: `(array float)` and `(array int)` (`array<float>` in `.sly`).
- The builtins, under `array/`, few, named as `tuple/`'s are where
  they do the same:
  - making one: `(array/make :float n x)`, `(list->array :float xs)`,
    `(array->list a)`, as `tuple/make`, `list->tuple` and `tuple->list`;
  - reading: `array/len`, `(array/get a i)` (`:out-of-range`, as
    `tuple/get`), `(array/slice a from to)`, `array/concat`;
  - `(array/set a i x)`: a copy with one element changed, O(n), as
    `tuple/set` is, and not for loops;
  - element by element, each operand an array of the same kind and
    length or a number: `array/add`, `array/sub`, `array/mul`,
    `array/div`, faulting as `+` and the rest do (an `#int` sum that
    overflows faults `:overflow`);
  - reductions: `array/sum`, `array/dot`, `array/min`, `array/max`,
    in order, left to right, so the answer is the obvious loop's, and a
    Python loop is a model of it; clang doesn't vectorize a float
    reduction without being told it may reorder, so the order holds,
    while element-by-element loops are free to vectorize.
- In the prelude: `(array/generate :float n f)`, an array of `(f i)`,
  written in slight, filling a fresh array through a builtin only
  `lib/` may call (C can't call back into slight with the collector
  walking frames, D166, so it can't be a C loop); and `array/map` and
  `array/fold` on it.
- Shared buffers (2) make a big one free to send; without them arrays
  work, copied, as strings do now.
- Matrices: `lib/matrix.slight`, a module, a matrix a tuple of its rows,
  its columns and an array; the one operation too slow in slight,
  multiplying, a builtin, `(array/matmul a b n k m)`, our own loop, each
  element summed in order. Not BLAS: its order changes with its blocking
  and its threads, so its bits do.

**What it costs.** A new box type in `rt.h` and `values.ts`, about
twenty builtins in C (`runtime/arrays.c`) with their types in
`builtins.ts`, `sexp.c` reading and printing them, and the prelude's few.
Floats stay boxed elsewhere: unboxing them in a loop is TYPES.md's step
5, types in code generation, which can come after, with benchmarks.

**Settled** (D253): `:int` arrays and `:float`; printed `#float(...)`
and `#int(...)`; element by element never mixes an `:int` array with a
float (convert it first, through `array->list`, or a builtin for it if
that's too slow); and reductions left to right, the obvious loop's
answer, which a Python loop models for the tests.

**Built** (Oct 2026, golden tests 320 to 323, `t/models/arrays.py`):
as proposed, but for `array/generate`, which goes through a list rather
than a builtin only `lib/` may call (D253 says why); with `array/kind`,
`array?` and `array/concat`, and `array/div` always `:float`. Not yet: a
builtin to make an `:int` array a `:float` one (through `array->list`
and `list->array` for now), and unboxed floats in compiled code
(TYPES.md's step 5), each for when a benchmark asks.

### More than one core: islands (agreed, D256, Oct 2026)

**The problem.** One OS thread runs every process, so a server or a
simulation uses one core of the M2 Max's twelve. The airline's
what-ifs get more by running copies of the program, a share each
(`--shard k/n`, D218), which is the workaround this makes unnecessary.

**What was agreed** (D256, after a first proposal of one scheduler over
every core, with stealing, that Stevan turned down for something more
isolated, as `rpc` between programs is): **islands**. Each island is
today's single-threaded runtime on a thread of its own: its own process
table, run queue, timers, `select()` loop, stack pool and record.
Nothing is shared between islands but an inbox each, and the shared
buffers, which never change.

- **A fixed set, made when the program starts:** `(islands)` of them,
  one per core unless `SLIGHT_ISLANDS` says, island 0 the main one; an
  island's thread starts the first time something is placed on it.
  Nothing moves between islands on its own: where an actor runs is the
  program's choice, `(spawn/on k expr)`, k from 0 below `(islands)`,
  and `(island)` says which one a process is on. A program spreads its
  work with `(spawn/on (% i (islands)) ...)`, and runs the same on one
  core or twelve; `SLIGHT_ISLANDS=1` is today's runtime exactly.
  `fork` stays on its parent's island, so a `join` is always local.
- **Pids carry their island,** and a send goes wherever its pid says:
  within an island, today's send, with no atomics; to another, the
  message copied into a chunk, as now, and the chunk pushed onto that
  island's inbox, a queue many push to and one pops, which wakes it,
  its scheduler delivering into the local mailbox. Messages from one
  sender to one receiver still arrive in order. Monitoring, killing
  and an actor's end cross the same way, as messages. Each island
  numbers its own pids, so each is the same from run to run, whatever
  the others do. A pid prints `#<pid 5@2>`, pid 5 at island 2, and
  island 0's as now, `#<pid 5>`; an address `#<address 3.1@2>`.
- **The bits:** 8 of island (up to 256), below the address bit, so a
  pid keeps 52 bits of its number, and an address its task's 32 and
  20 of its own number (from 28), which wraps past a million, skipping
  any still open (a task's open ones are few).
- **Shared buffers cross islands without a copy** (D253): a big string
  or array is sent as its handle, its reference count atomic. So a
  frame of a simulation, an array of a million floats or a screen's 4
  MB, goes to the island that draws it for the price of a handle.
- **Island 0 owns the signals, the terminal and `:system`:** the other
  islands' threads block signals, so they come to island 0's (`SIGCHLD`
  wakes each island that has commands, each reaping its own); `:keypress`
  and `:system` are island 0's devices wherever they're connected, their
  messages crossing to an owner elsewhere; and output, from any island,
  goes out a whole value or line at a time, under a lock. So a TUI on
  island 0 answers keys and redraws at idle latency (D238) however hard
  the other islands compute, its collections touching only its own
  small heaps: real-time pictures of a running simulation.
- **`:system` counts what crosses:** each actor's messages and bytes to
  and from other islands, and each island's totals, run queue and
  shared bytes, asked of each island through its inbox.
- **Replay per island** (D170): what comes through an island's inbox is
  input from outside it, recorded as a socket's read is; each island
  records its own run (`SLIGHT_RECORD=f` writes `f` for island 0, `f.2`
  for island 2 and so on), and any island replays exactly, on its own or
  with the rest. `SLIGHT_SCHED` seeds each island's order apart. A
  simulation spanning islands carries its time in its messages, since
  each island's virtual clock is its own.
- **The end, and deadlock:** the program ends when every island is idle,
  every inbox empty, and nothing pending anywhere that could make a
  process ready, island 0 printing its root's value as now; one shared
  count of busy islands and messages in flight says so, and lets the
  deadlock message see across islands.

**Rejected:** one scheduler over every core with stealing, and a lock
or an atomic in every send (the first proposal); channels between
islands as devices (`(:from c v)` wrapped around every message, where
pids that cross keep `send` as it is); actors declared shared, only they
reachable from another island (a reply needs its asker shared too, or
an exception for addresses); an island per `spawn/island` call (a
thread for each of a simulation's thousand branches); `#<pid 2.5>`,
taken by addresses, and `#<pid 0:5>` everywhere, which changes every
pid a test prints.

**The steps,** each with `make test` passing:

1. **Island state:** the runtime's globals (`process.c`'s table, queue,
   timers and devices, and `replay.c`'s, `dns.c`'s and `tty.c`'s) made
   an island's, thread-local, still on one thread; pids with their
   island's bits, always 0 yet, masked where a pid is looked up, and
   address numbers skipping open ones as they wrap. *(Done, Oct 2026:
   `RT_ISLAND`, C's `_Thread_local`, on what's an island's, the
   settings, the terminal's state and what signal handlers touch left
   global; `rt_pid_number`, `rt_pid_island` and `rt_pid_text`; golden
   test 324, an address number wrapping past one still open. Under
   callgrind, `ping-pong` runs 2.2 per cent more instructions and
   `fan-in` 0.16; on macOS, where a thread-local is reached through a
   call, the M2 Max says.)*
2. **Islands as threads:** `SLIGHT_ISLANDS`, `(islands)`, `(island)`,
   `(spawn/on k expr)`, the inboxes, sends, monitors, kills and ends
   across them, and `#<pid 5@2>`; `t/run.sh` sets `SLIGHT_ISLANDS=4`, so
   `(islands)` is the same on every machine. *(Done, Oct 2026, golden
   tests 325 to 330, with step 4's end and deadlock, which it couldn't
   do without, and some of 3's and 5's: shared buffers' counts atomic;
   `:keypress` and `:system` placed on island 0 from any island, keys
   read only there, and `SIGCHLD` passed to every island's pipe, each
   reaping its own commands; `pprint` in one write. A spinning wait
   before an island sleeps, which took 200,000 round trips from 10 s to
   0.68. Still to do there, then: PID 1's orphans with commands on more
   than one island, and `:system` telling of other islands, both done
   in step 5; and a run recorded or replayed is on one island, a
   `spawn/on` elsewhere ending it, till step 6.)*
3. **Shared buffers across islands:** atomic counts. *(Done with step
   2.)*
4. **The end and deadlock** counted across islands. *(Done with step
   2.)*
5. **Island 0's:** signals, `:keypress`, `:system` and its counts of
   what crosses, and output under a lock. *(Done, Oct 2026, golden test
   331: `:system` tells of every island, `(:processes k)` of one, each
   process's `:island`, `:sent` and `:received`, and `(:runtime)`'s
   `:islands`, other islands asked through their inboxes, answers in
   the order asked; output a value or line in one write, stdio's lock
   keeping them whole, and `log` saying a process's island; PID 1's
   orphans told from every island's commands, t/live.py's signals.)*
6. **Replay per island,** and `SLIGHT_SCHED` per island. *(Done, Oct
   2026, as D256's option (c): the islands replay together, each from
   its own record, `f` and `f.k`, which names the items its inbox took
   and when rather than keeping them; one island alone is put off, as
   (b). `SLIGHT_SCHED` was already per island, from step 2. t/replay.sh's
   islands checks.)*
7. **Checks and numbers:** golden tests whose output doesn't depend on
   the order islands run in, the same on one island or four (the VM3
   sketch's test); ThreadSanitizer over them; `SLIGHT_GC_STRESS` with
   islands; `bench/` with `slight-mc`, `parallel-fib`, `fan-in` and
   `spectral-norm` placed across islands, in D237's every-core table; an
   example, a simulation on islands drawn live on island 0; and
   LANGUAGE.md, DESIGN.md and the guide. *(Done, Oct 2026: golden tests
   332 and 333, one program on four islands and on one; `make tsan`
   (`t/tsan.sh`), clean, and a race planted in `push` caught; every
   island test under `SLIGHT_GC_STRESS=7` and `SLIGHT_GC_VERIFY=1`;
   `slight-mc` in `bench/`; `examples/reaction.slight`, Gray-Scott in
   bands across islands, drawn live on island 0, its quiet mode a golden
   test from `t/models/reaction.py`; and the guide's "Use every core".
   Islands are built.)*

qemu runs threads, but on this x86-64 machine the memory model isn't
AArch64's, so ordering bugs show only on the Mac, where each step is
proven.

### Simulation across islands (proposed, open, Oct 2026)

Proposed by Claude after islands (D256) and D257 (simulation before
servers); Stevan asked for it written down, to pick up later. Not yet
agreed: the options and the recommendation stand as offered.

**The problem.** On one island slight simulates well: agents are
actors, time is the virtual clock, `after` and `sleep` are events, and
`bank` comes to SimPy's waits (D241). Across islands each island's
virtual clock is its own, so an agent on island 2 can't safely send
another one a message timed for an island whose clock may be past it.
`examples/reaction.slight` steps everything in lockstep by hand. The
library should give that a shape, and the same answer on any number of
islands.

- **(a) Time-stepped, a library alone (`lib/sim.slight`).** The world
  cut into regions, an actor each, placed across the islands; each step
  the library hands every region its neighbours' edges, the region's
  step function gives its new state and the edges to send on, a barrier
  gathers them, and every kth step an observer gets their outputs:
  `(sim/grid regions init step edges)`, `(sim/run g n every observe)`.
  Cellular automata, diffusion, agents in synchronous ticks; the same on
  any number of islands, as `reaction` is. No runtime change.
- **(b) Discrete events, in conservative windows (one new builtin).**
  Agents stay actors with `after`, `sleep` and `recv`, with one rule: an
  interaction takes at least a lookahead L of simulated time. Every
  island runs to the window's end T+L, exchanges what crossed, meets the
  rest at a barrier, and goes on. The runtime would need
  `(clock/hold t)`: the island's virtual clock held at t, and once
  nothing there can happen before t (every actor waiting, the next timer
  at t or later, the inbox empty), moved to t and `(:held t)` sent to
  the caller. The library: `(sim/after delay pid msg)` for a timed
  message, delay at least L; a keeper per island that takes them and
  sets them as local timers; and a barrier that counts what each island
  sent, so no window starts before every keeper has its own. For the
  same answer on any number of islands, events due together fire in an
  order that doesn't depend on placement: every `sim/after` goes through
  a keeper, the same island's too, sorted by time, the agent's own sim
  id (not its pid, which says its island) and its sequence. Its
  `clock/hold`, and how a held clock records and replays, would be a
  proposal of their own.
- **(c) Both,** (a) first, sharing its regions and barrier with (b).

Rejected as offered: optimistic simulation (Time Warp: running ahead
and rolling back), which immutable state makes cheap to save but whose
anti-messages and global virtual time are a lot of machinery; and one
virtual clock for every island, which would make every timer a
conversation between islands.

**Recommendation: (c).** (a) is small, needs no runtime change, and
makes `reaction` shorter; (b) is what makes slight a simulation
language: SimPy's style, agents as actors, on every core, the same
results on 1 island or 64.

**An example to build it around: protein folding on a lattice.**
Stevan's suggestion, after a friend at a genetics company asked whether
slight could fold proteins. Not structure prediction (AlphaFold's deep
learning on GPUs, which slight would only drive, as a command), nor
all-atom molecular dynamics (GROMACS', OpenMM's: a floating-point inner
loop over pairs of atoms, where slight would be far behind C and further
behind a GPU); but the HP model, each residue a bead on a 2D or 3D
lattice, hydrophobic or polar, the energy minus one for each pair of H
beads touching but not bonded, explored by Monte Carlo moves (end,
corner and crankshaft moves, or pull moves) with replica exchange: a
replica an actor at its own temperature, spread across islands, a swap
between neighbouring temperatures a message, accepted by Metropolis's
rule from the replicas' seeded generators. It's checkable (the lowest
energies of the standard benchmark sequences are published, and a
short sequence's can be found by brute force, in a Python model), the
same to the last bit on any machine and any number of islands (seeded,
split random numbers, CORE-MATH), replayable, and drawable live on
island 0 as `reaction` is. It would want (a)'s regions and barrier, a
swap every k sweeps being a time step, and none of (b). A later step:
slight as the coordinator of real tools' runs, many short trajectories
run as commands and chosen adaptively, as Folding@home's Markov state
models are.

**A second example: a town's talk, after Talk of the Town** (now taken
into "A living town", below, D263). Also from
Stevan's friend. James Ryan and colleagues' Talk of the Town (UC Santa
Cruz, about 2016) simulates a small town over a century: characters
born, married, employed, befriended and estranged, each holding beliefs
about the others that spread in conversation, get distorted, fade from
memory, and can be simply wrong; its player solves a mystery from what
the townsfolk believe, not from what's true. In slight a character is
an actor, its beliefs its own state (a persistent map, `lib/map.slight`);
a conversation is messages, and a message is a copy, which is just what
a belief told needs: the hearer's copy is theirs to misremember, the
teller's untouched. Days and years pass on the virtual clock, `after`
saying who meets whom; a town's history replays exactly, and a branched
replay (below) asks what if a rumour never started. Neighbourhoods on
islands, most talk local and travel between them crossing, the time a
journey takes being (b)'s lookahead; at some 600 bytes an idle actor,
millions of townsfolk, not hundreds. Its interest is in a run's
history, not a final number, and its messages carry structured data, so
it tests what lattice folding doesn't. A cousin: Stanford's Generative
Agents (Smallville, 2023), whose characters talk through a language
model, as a few could here through `lib/agent.slight`, on a town run by
rules.

**The flagship: a country of towns, with collectors.** Stevan's, the
two examples above together. Most of what happens in a town stays in
it: people meet neighbours and workmates thousands of times a day, and
few go between towns, fewer between regions. So the computing grows with
the people, inside each town, which islands spread, and the talking
between places with what crosses (travellers, trade, news), which is
little: a simulation shaped so scales where a flat one of a country
doesn't. Done elsewhere as metapopulation models (GLEaM: cities as
patches joined by real journeys), federated simulation (HLA, IEEE 1516),
and a game's level of detail.

- **A tree.** Towns are actors (a few for a big city), over islands;
  county collectors above them, then states, then a root. Each step two
  waves: down, `(:step t signals)`, what the level above decides or
  knows (prices, policies, news, the season); up, `(:done t summary)`,
  each town's counts for the step.
- **The tree is the barrier.** A collector waits for every child's step
  t, merges their summaries, reports up; the root, having them all,
  starts the next step down: a tree barrier, synchronization and
  aggregation in the same messages.
- **Summaries that merge:** counts, sums, histograms, minima and maxima;
  sketches where those can't serve (HyperLogLog for distinct counts,
  t-digest for quantiles). Children merged in a fixed order, by their
  ids, not as they come, so float sums, and the run, are the same to the
  bit every time.
- **Travellers are messages,** a person's state (who, what they
  believe, whether infected) sent to the town they reach, straight to
  its pid, or up and down the tree between machines. The shortest
  journey is the lookahead: each town runs that far on its own between
  barriers, coarse windows and little waiting.
- **Fidelity where it matters:** the towns of interest with Talk of the
  Town's minds, the rest lean rules, or a compartment model's equations.
  Zooming a town in mid-run, statistics made into people consistent
  with them, is a research problem; out is easy.
- **More than one machine without slight spanning machines:** only the
  top of the tree crosses, with summaries and the few travellers between
  regions. A machine runs one program for a region, its towns on its
  islands and its own collectors, and region roots meet each step over
  `lib/rpc.slight`, which exists. Each machine records and replays its
  own; a branched replay asks what if a region closed its borders on
  day 30, from a checkpoint.
- **What to watch:** what the collectors keep is all that can be asked
  later (keep counts, and one person's path is gone), so choose, or log
  each town's events to files; effects between levels (national news
  changing every town) go down the tree explicitly; and validating it,
  calibration against data and ensembles of runs, is the real work.

A dashboard on island 0, fed by the root, shows it running.

Measured alongside, for whoever picks this up: a message crossing
islands is one allocation now, and `fan-in-mc` four times faster on the
cloud machine; a ping-pong whose halves are on neighbouring islands
costs a few microseconds a round trip on the cloud machine's 4 vCPUs,
worse with more islands, which the M2 Max should measure before more
work goes into crossing latency (`docs/BENCHMARKS.md`, 31 Oct).

### A living town: an epidemic you watch and poke (proposed, Nov 2026)

Stevan's, after looking at Imperial's CovidSim (Report 9) and deciding
not to compete with C++ and OpenMP at batch runs over a country: "The
Sims: Covid edition", a world that runs continuously, actors for
people, watched and changed while it runs. It takes in "a town's talk"
above. Agreed so far (D263): the clock below, (b); a start of 10,000
people on one island; and measuring how far it scales on one island and
across several. Nothing built yet.

- **People are actors:** a home, a job or school, habits, a health state
  and beliefs (a persistent map). Most of the time a person waits on a
  timer for the next thing in their day (the runtime's timers are a
  binary heap, so a million waiting is fine; an idle actor is about 600
  bytes, D257).
- **Places are actors:** homes, a school, workplaces, shops. People send
  `(:arrive ...)` and `(:leave ...)`; every few simulated minutes a place
  works out who was near whom, for how long, and whether the virus
  passed, from seeded draws.
- **An infection is an actor:** born when someone catches it, it lives
  through the illness (latent, infectious, symptoms, recovery), and the
  infections it causes are its children, so the transmission tree is a
  tree of processes and contact tracing walks it.
- **Behaviour spreads too:** people talk where they meet, so fear,
  rumours and willingness to isolate travel the same contacts as the
  virus, and change it.
- **Watched and poked:** in the terminal (a map coloured by health state,
  live counts, following one person, speed and pause) with controls
  (close the school, test and isolate, start a rumour); a browser view
  later ("Seeing a simulation", below). Every input is recorded, so a
  session replays exactly; with "Branching a replay", what if the café
  had shut on Tuesday.

**The clock (agreed: (b)).** (a) The real clock, scaled: no runtime
change, replayable from a record, but the same seed doesn't give the
same town. (b) A virtual clock with a pace: the virtual clock, which
today jumps straight to the next timer, held to no more than the wall
clock times a rate the program sets (and changes, for pause and speed),
so the same seed and the same keys give the same town, and a run is a
golden test (its keys from `; stdin:`, its pace unlimited there). A
runtime change, to be proposed on its own: a builtin or a setting for
the rate, how keys are stamped with the virtual time they arrive at,
and how it records and replays; close to `clock/hold` above, which
crossing islands would need anyway.

**Phases.** 1: one island, 10,000 people, homes, a school, workplaces
and shops, disease only, watched in the terminal, with a Python model
of a small town for its golden test. 2: beliefs and behaviour, and the
controls. 3: scale, measured: how many people one island holds and at
what pace, then neighbourhoods across islands, people travelling between
them, on "Simulation across islands" (b)'s windows, and how far that
goes. 4: branching what-ifs, and a browser view.

**Questions.** Each phase's model (contact rates, the illness's
periods) from published values (Report 9's, Ferguson et al. 2005 and
2006), rebuilt rather than ported (CovidSim is GPL-3.0; this
repository is Apache-2.0, D264); how many people the terminal can
show (a map of places rather than people, past a few thousand); and
the rate's proposal before phase 1.

### Floats without boxes (Oct 2026: (a) measured slower, stopped, D260; (c)'s step 1 done, D261, and there it rests, D262)

Proposed by Claude after D258 and D259, when Stevan asked to "fix the
floats". Stevan agreed to (a), symbols giving up the bit, a prototype
first, and (c) planned now (D260). The prototype made things slower, so
(a) has stopped; see "Measured", below.

**The problem.** A float is a 16-byte box. D198 made `+`, `-`, `*` and
`/` on two floats inline, and D259 comparisons, but each result is
still allocated, and everything that makes a float in C does it through
`rt_new_float`: `array/get` on a `:float` array is a call into C and a
box each time (13 per cent of boids, D258), as is every `math/`
function. In `n-body`'s inner loop a float subtraction is about 30
instructions on x86-64: the integers' check, a tag-and-header check on
each operand, an allocation, the operation, the box's two stores, the
result to its slot. And a tuple or a message of floats is a word and a
box for each, so a simulation's state is three times the size it need
be, and copied as such.

**The options.**

- **(a) Floats as immediates, most of them** (Pharo's Spur VM has done
  this since 2014, "SmallFloat64"). A float whose exponent fits in 8
  bits, |x| from about 1.2e-38 to 3.4e38 (single precision's range,
  with all 52 bits of the fraction), and ±0, is the word itself: its
  bits rotated so the sign is lowest, the exponent offset taken off,
  shifted up three, under a tag of its own. The rest, infinities, NaN,
  subnormals and the very large and very small, stay boxed, so every
  double is still a float, to the bit; a value has one form, so `eq?`,
  `hash` and printing don't change. Decoding is a shift, an add and a
  rotate; encoding the reverse, with a branch to the box when it
  doesn't fit. The tag has to come from somewhere: all four odd ones are
  taken, so pids would move under the symbols' tag, `101`, a bit above
  it saying which (symbols `0101`, pids and addresses `1101`), and
  floats would take `111`. Nothing a program can see changes. The
  collector and the message copy don't change either: an immediate
  isn't a pointer, as a pid isn't now.
- **(b) NaN-boxing**, every value inside a double's NaN space, as
  JavaScript engines do. Rejected: integers would drop to 48 or 51
  bits, a change to the language (63-bit integers, D37), and every
  pointer would need masking.
- **(c) Unboxed within a function**, kept in floating-point registers
  where the compiler knows a value is a float, boxed only where it
  leaves (a call, a tuple, a message, the result). This is TYPES.md's
  step 5. It's the one that could close most of `n-body`'s gap (10 to
  36 times C), but it's the most machinery: types in code generation,
  a second register class on both targets, boxing at the edges, and
  knowing: in `n-body` the floats come out of tuples, typed `any`, so
  it needs `sig`s the code generator trusts, checked once at the
  function's entry, or a float check per value then kept.
- **(d) Smaller inlines, with no change of representation**:
  `array/get` on a `:float` array (its shared form too) and `math/sqrt`
  and `math/abs` inline, allocating as `+` does. About a tenth of
  boids, a per cent or two of `n-body`.

**Recommended, and agreed: (a), then (c) if the benchmarks still ask**,
on an estimate of 15 to 30 per cent for (a), which the prototype didn't
bear out.

**Measured (D260).** Step 1, symbols and pids sharing the tag `101` so
floats could have `111`, is built (every test passing as before). Step
2, the prototype: `rt_new_float`, `rt_float_value` and `rt_is_float` on
immediates (all 362 golden tests passing on both targets with C alone
making them), and x86-64's inline float paths (D198's arithmetic,
D259's comparisons, literals as words). On x86-64, against D259, in
instructions: `n-body` 24 per cent more, `spectral-norm` 10 per cent
more, boids 7 per cent more; in time, `n-body 1000000` 1.88 s to 2.36,
`spectral-norm 1000` 1.28 to 1.53 (the best of five, one island). No
result went to C but `sqrt`'s. The decode, shift, add the offset unless
it's ±0 (a `cmova`), rotate, move to `xmm`, is 8 instructions an
operand, and the encode, rotate, ±0 by a branch, the exponent's range
checked, the offset taken off, shifted and tagged, 13; the box it
replaced was a bump of the nursery's pointer and two stores, about 5,
in memory the cache already holds, and its collections cost next to
nothing. A leaner encoding (±0 boxed, no special case) would come to
about what a box costs, not less. So immediates don't pay for
themselves here: the cost of a float operation is going through the
generic word at all, checked, taken apart and put back, between one
operation and the next, whatever the word is. (a) has stopped, and step
1 is reverted, since it bought nothing without (a).

**(c), planned.** Floats kept as doubles in registers between
operations, boxed only where they leave, in steps, each measured:

1. **Float expressions.** A tree of float operations (`+`, `-`, `*`,
   `/`, comparisons, and `math/sqrt` and `math/abs`, which are exact),
   its leaves locals, literals and other expressions, compiled twice:
   once as now, and once with every leaf checked to be a float at the
   start (a box's header, or an integer made a double where it may be
   one, as today's float path does), the tree worked in `xmm` or `d`
   registers, and the result boxed once. The checks failing goes the
   way it goes now. `n-body`'s `pull` has 30 float operations in 11
   such trees: 11 boxes in place of 30, and each leaf checked once in
   place of at each use. No types, no new frame layout, no change of
   representation. Estimated, from that count: `pull`'s float work
   about 2.5 times fewer instructions, `n-body` 1.5 to 1.8 times
   faster; boids less (its arithmetic is shallow, and the rest is
   lists and sorting). **Done (D261):** `n-body` 1.40 times faster
   (29 per cent fewer instructions), boids 9 per cent; nothing else
   slower.
2. **Float locals.** A `let` bound to a float expression kept as a raw
   double in a frame slot the collector doesn't see (the frame split
   into its values and its doubles), so `dx` crosses from its `let` to
   its uses unboxed; boxed only where it's passed, stored or returned.
   **Found, before building (Oct 2026):** the frame needn't change (a
   double can sit in two slots as two integers, its bits but the lowest
   and the lowest, which the collector passes over), but `dx` can't be
   kept raw: `(- x ox)` is an integer when `x` and `ox` are, and in
   `n-body` they come out of tuples, so when the tree's check fails and
   the `let` goes the old way, what follows can't know which `dx` is.
   Only a `let` that's a float whatever its leaves are (`/`,
   `math/sqrt`, a float literal or a raw local in it) can be raw:
   `n-body`'s `dist` and `mag`, not `dx`, `dy` or `dz`. Counted in
   `pull` (about 800 instructions a call, after D261): all five `let`s
   boxed and checked at their 16 uses cost about 150, so step 2 could
   save at most 14 per cent of `n-body` if all five were raw, and about
   6 per cent with only `dist` and `mag`; boids nothing (its `d2` is
   compared and stored, which box it anyway). Keeping `dx` raw needs
   knowing `x` is a float: types (step 3's `sig`s, for a tuple's
   elements too), or compiling the function twice and leaving the fast
   copy for the slow one, its raw locals boxed, where a check fails
   (deoptimizing, as JITs do), which is machinery. **Not built
   (D262):** step 1 is good enough for now; if floats need to be
   faster again, the way is types in code generation (TYPES.md's step
   5), proposed on its own, not this step or deoptimizing.
3. **Float parameters.** A function whose parameters are used as
   floats compiled a second time for floats, entered when its
   arguments check at the entry, so a loop's state stays unboxed from
   one call to the next; with `sig`s, that check is what the
   signature says (TYPES.md's step 5, types in code generation).

Each step keeps every float the bits it has now, the same IEEE
operation on the same doubles, so every golden test stands as it is;
each adds golden tests at its edges (a leaf that's an integer, a
string, a boxed NaN; a tree too deep for the registers, spilled).

**Agreed** (D260): (c)'s step 1 goes ahead.

### Branching a replay (idea, open, Oct 2026)

Stevan, after the simulation proposal: "we should also look at
branching in the replay, we did it in the airlines, but that seems like
it would be a powerful feature." Not yet proposed; first thoughts, to
work into a proposal.

`airline` branches its own simulated day, by hand: a what-if from a
checkpoint, run till it rejoins the run it came from (D207, D212). D212
noted that replay (D170) couldn't do that: a replay runs the program
again, doing all its work, with what it read the first time, and can
only go the same way. Branching in the runtime would give any program
a version of it: run as recorded up to a point, then go another way
from there.

- **Where a branch starts:** at an entry of the record (`replay: ...
  at entry N` already names them), or at a moment on the virtual clock,
  for a simulation; say `SLIGHT_REPLAY_UNTIL`.
- **What changes there:** the world, from then on (the replay going
  live, and recording to a new file whose start is the old one's); or,
  better for simulations and for hunting races, something the runtime
  decides itself: `SLIGHT_SCHED` seeded from that point, so a hundred
  orders can be tried after a suspicious moment rather than from the
  start; the random numbers reseeded; or a message put in, read from a
  file of s-expressions, as an agent's what-if.
- **What it can't do:** a replay opens no real socket, file or command,
  so a branch that goes live finds none of those the recorded part
  opened; branching fits runs whose world is the clock, the random
  numbers, keys and files read whole, which is a simulation's.
- **Cost:** each branch replays its whole start, all the work of it,
  as D212 found. Many branches from one point could share the start: the
  run replayed once to the point, then a Unix `fork()` of the whole
  program for each branch, copy-on-write, each going its own way. Simple
  on one island; with islands, `fork()` copies only its own thread, so
  every island would have to stop at the point first and start again in
  each child.
- **Islands:** each island's record is its own (step 6), so a point in
  island 0's isn't a point in the others'. A branch across islands needs
  a consistent cut: the moment named on the virtual clock, every island
  stopped where its clock reaches it, with what's in flight between them
  carried over. One island first, then islands.

Likely shape, for the proposal: branching at an entry or a virtual
moment, with a new `SLIGHT_SCHED` seed, new random numbers, or messages
put in, on one island, each branch replaying its start; then `fork()` to
share the start; then islands. It would sit well with the simulation
library above, a what-if being a branch of a recorded run.

### Seeing a simulation: a viewer (idea, open, Oct 2026)

Stevan: terminal drawing is crude and bound to the terminal's speed;
Sixel and kitty's graphics protocol suit pictures that change seldom;
would raylib fit slight, and draw what we'd want? Not yet proposed;
first thoughts, to work into a proposal.

**What raylib would draw.** A small zlib-licensed C library (a handful
of files and its own window layer, not one file) for windows, 2D,
textures, text, simple 3D, shaders, keys and the mouse; raygui, a header
beside it, for sliders and buttons. What we'd want, given data in bulk
rather than a call a shape: a grid (`reaction`'s, heat maps) as a
texture each frame, coloured on the GPU, millions of cells at 60 frames
a second; agents on a map as arrays of positions and colours, drawn in
batches, hundreds of thousands smoothly; charts of the collectors'
summaries; networks of who talks to whom; a lattice protein in 3D with
a camera. Not charts for publication: those are data exported to a
plotting tool.

**Why not in the runtime.** macOS wants windows and OpenGL on the main
thread, which is island 0's scheduler, whose `select()` can't wait on a
window's events; OpenGL comes from the driver, loaded dynamically, so
not the static binary every Linux build is (D227); hundreds of
functions would each be a builtin, there being no FFI (D252), and a
window's events would have to go through `replay.c`; and a golden test
can't open a window.

**A viewer, as a process** (as curl and `llama-server` are, D251): a
small C program, `tools/slight-view`, with raylib vendored in it, that a
program starts as a command (`:exec`) or reaches on a Unix socket. To
it go frames, arrays of numbers (a grid; positions and colours) and
s-expressions for the few draw commands; from it come keys, clicks and
sliders, as messages through a device, which a run already records, so
replay holds. The runtime stays free of graphics, a viewer that
crashes doesn't take the simulation with it, and a test uses a stand-in
viewer, as `t/data/curl/curl` stands in for curl. With islands, frames
go to island 0, a big array as a handle, and on to the viewer. raylib
loads image and font formats, so it isn't entirely free of what D251
warns of, but the viewer reads only what its program sends it.

**And beside it:**

- **The browser, the same protocol:** a page with a canvas, served by
  `lib/http.slight`, for a simulation on a headless machine watched
  from a laptop; frames going to it live wants WebSockets, which slight
  hasn't, so later.
- **Files:** frames written as PNG or PPM, and `ffmpeg`, as a command,
  making a film of a long run.
- **The terminal:** Sixel or kitty's graphics protocol, for a picture
  now and then.

Likely order, for the proposal: the protocol first (what a frame and a
draw command are, and how input comes back), then the raylib viewer,
tried on `reaction`, whose drawing on island 0 is its limit (125 steps a
second on one island, 181 on four, on the cloud machine), then the
browser when watching from afar matters. It would sit with the
simulation library above: the flagship's dashboard is this viewer, fed
by the root.

### One kind of process, and an address for every message (Nov 2026: built, D276 to D280)

Proposed by Claude after Stevan went back to ts-slight ("the process
split felt kind of wrong when we did it ... I've struggled with the
`recv` rule every time I wrote slight, where the join/fork/yield
patterns felt more intuitive") and set the shape: "fork/join/yield is
the core process model; address/read/send is the core message model;
actors are a pattern of usage for those two, and `recv` is a way to
read a default address." Not yet agreed; the decision's number comes
when it is.

**Where it comes from.** In ts-slight every process was a continuation
chain and a mailbox, and every wait, `recv`, `join`, `yield` and a
syscall, parked the chain the same way. So `recv` went anywhere, a
closure included, and the idiom the examples lived on was a closure
wrapping a send and a `recv`, which made an actor callable
(`simple-db-server`'s `db-client`, `active-objects`' `PointActor`). Its
one hole was that `recv` took whatever came next, AVM's stray-reply
bug. Natively a suspended process needs a stack, and D9 chose stacks
only while running, which needs the stack empty at `recv`: D8, the
`recv` rule, taken "if it simplifies things." The stackful waits then
came back one by one as programs needed them, `join`, `sleep`,
`join/race`, and D243's census found 33 processes that were both actor
and task, and split the process into two kinds, giving tasks `address`
and `read`, the correlated reply ts-slight's roadmap had planned, and
actors nothing. So one process became two by an implementation
decision, and the wait people reach for came back under another name,
for the kind that can't be sent to.

The runtime already has the floor. An address is a word under the pid
tag, a message to one goes into its task's own mailbox marked with it,
and `read` takes the first message so marked and leaves the rest
(`process.c`, "addresses"). With the two `if (p->actor)` guards in
`rt_address` and `read_from` turned off, an actor asking another actor
in the middle of a handler, `(reply to (* 2 (ask c (list :get))))`,
ran as written, parked in `read` with its stack as it may already in
`join` and `sleep`. The one thing missing was that a plain `recv` took
the reply meant for the address, since `rt_recv` looks at no marks.

**The model.** Two sets of forms, and a pattern on top of them:

| | Forms |
|---|---|
| Processes | `fork`, `fork/on`, `join`, `yield`, `kill`, `monitor`, `$$`, `^$$` |
| Messages | `address`, `send`, `read`, `read/any`, `disconnect`, `after`, `open`, `connect` |
| Actors, as a pattern | `recv`: `(recv)` is `(read $$)`, and `(recv (p body) ...)` a `match` on it |

- **One kind of process.** `(fork expr)` starts one, on this island, and
  `(fork/on k expr)` on island `k`; `spawn` and `spawn/on` go. Every
  process keeps its stack across every wait, and can wait anywhere: in a
  helper, in a lambda, in the middle of a body, in the root. Its value
  is kept for its parent as a task's is (D244), till the parent has it
  or ends. It can be joined, monitored, killed, and sent to.
- **A pid is its process's inbox.** `(send pid msg)` goes into the
  process's mailbox as now, unmarked; `(read $$)` takes the next unmarked
  message, which is what `(recv)` is. Reading any other pid faults:
  only a process reads its own inbox, as only a task reads its own
  addresses. A process's end is no longer something `read` waits for.
- **An address is an inbox of its own.** `(address)` makes one, owned by
  the process that made it, read only by it, closed by `disconnect` or
  the process's end. `send` and `after` take one where they take a pid.
  `read` takes the first message marked for it and leaves the rest;
  `read/any` the first of several sources, in their order, giving
  `(src what)`. Nothing can be taken for an address but what was sent to
  it: no stray reply, and no dead letter. `recv` skips what's marked for
  an address, the one change to `rt_recv`.
- **An end is a message.** `(monitor pid)` tells the caller's inbox
  `(:exit pid result)` when `pid` ends, as now; `(monitor pid to)` tells
  the address `to` instead, so a reply and the end of the process asked
  come to one place. That's ts-cpi's `process::watch pid watcher`, "a
  PID or an address." `ask` becomes: an address, a monitor to it, a
  send, a read. `join` stays for a parent waiting on a child's value,
  keeping its stack as now, and works across islands through the watch
  that `read/any` of a far pid uses today.
- **`recv` stays a word,** because the checker takes an inbox's type
  from its patterns, and because `(recv (p body) ...)` reads better
  than the `match` it stands for. It can go anywhere now.
- **Devices are sources.** `(open how ...)` in any process gives a
  device it owns, read with `read`, its failure a message; `(connect
  ...)` starts a process that owns one and takes its messages in its
  inbox, as now.

**What goes.** The `recv` rule, D8 and D167, and with it `split.ts`
(542 lines), `classify.ts` (85), state functions, receive functions,
the stackless path in `rt_recv` (`p->code`, `p->args`, a function
called again from the top), the stack given back at `recv`, the two
kinds (D243's flag, `rt_spawn`, `:not-a-task`, `:not-an-actor`,
`:kind`), `spawn` and `spawn/on`, `read` of a pid as its end, the
checker's warnings about a task that waits or an actor that's joined,
the guide's "The recv rule" and "Tasks and actors" pages, and review
H2, since the pass it's in is gone. And the dance: `(join (fork (ask
...)))` appears 139 times in `lib/`, the examples, the tests and the
guide's code, most of them a process forked to hold an address.

**What stays.** Replay, seeded orders and islands: a read is a pure
function of the mailbox, and the mailbox is the same in a replay.
The collector: D166 walks stacks as roots at a function's entry
already, and can at a `read`. Patterns, `match`, the virtual clock,
timers, devices, exit records, `:system`, the deadlock report (a
process "waiting in f for a message to #<address 1.0>"), and the
checker, whose inbox types still come from the `recv`s a function can
reach, without the tail-position restriction.

**What it costs.** A waiting process holds a stack, which is D9's
trade reversed:

| | In `recv` today | Every wait, proposed |
|---|---|---|
| Resident | a few hundred bytes | the pages touched, 4 to 16 KB |
| Virtual | nothing | 8 MB (`STACK_BYTES`) |
| Ceiling | millions | about 32,000 with a mapping per stack on default Linux; hundreds of thousands if stacks are carved from one region, since the entry check catches overflow without a guard page (carved, D291: millions, memory allowing) |

`million-forks` and `bank`'s million customers no longer run as they
are; `ring-benchmark`'s ten thousand do, and so does a town of ten
thousand. The agent, the chat, `rpc` and `http` are nowhere near it.
To measure before it's agreed: `ring-benchmark` and `fan-in` on the
Mac, stackful against today.

**Rejected as offered:**

- **Selective receive by pattern,** Erlang's, scanning the mailbox for
  the first match. Turned down four times already (D8, D85, after boids,
  exit records), never for determinism, which it doesn't touch: a scan
  in arrival order is a pure function too. Its costs are that a message
  no clause takes stays in the mailbox instead of being a dead letter
  reported at once, Erlang's silent leak, and that a wait costs the
  mailbox's length. Selection by destination, an address, does the
  request-and-reply case with neither.
- **Pids and addresses disjoint,** ts-cpi's way (`process::address
  pid`, `actor::self` an address). Cleaner on paper, and every send to
  an actor takes a step more, in 300 programs, for nothing a program
  can see.
- **Several readers on one address,** ts-cpi's shared mailboxes, which
  brought durable and non-durable mailboxes, closed ones, a wake rule,
  a thundering herd, and "mailbox ownership is underspecified." A work
  queue is a process that hands jobs out, in `lib/ds.slight`.
- **Continuations on the heap** for the waits, so that a waiting
  process holds no stack and `recv` still goes anywhere: D167 and D243
  turned it down, and it's a compiler of a different shape.
- **The rest of ts-cpi:** a scheduler written in the language, roles
  and grants, traps, parking and checkpoints. The runtime schedules
  natively and deterministically, and replay is its journal.

**The steps,** each with `make test` passing:

1. **`recv` keeps its stack.** `rt_recv` waits as `read_from` does, in
   a new state, and skips messages marked for an address. The compiler
   emits a `recv` as a call that returns, its clauses the `match` they
   are. `split.ts` and `classify.ts` go; `make test` still passes,
   since every program that obeyed the rule still does. *Built (D276):
   the only expected outputs that changed are the five the change
   explains, two deadlock messages, `:system`'s stacks, the guide's
   memory demo and `million-forks`, now `ten-thousand-forks`; no
   program's order of events moved.*
2. **One kind.** The actor flag, `spawn`, `:not-a-task` and
   `:not-an-actor` go; `address`, `read`, `read/any` and `open` work in
   every process; `read` of a pid faults unless it's `$$`; `monitor`
   takes an address; `ask` is rewritten on it. Golden tests 296 to 301
   are rewritten for one kind. A script turns `spawn` into `fork` (179
   uses in `t/`, 75 in the examples, 21 in the guide's code, 28 in
   `bench/`, 14 in `lib/`), and the expected outputs that named a kind
   (8 files) are worked out again by hand. *Built (D277): as written,
   but `read` of a pid faults for every process, `$$` and one that has
   ended included, a process being no source; `monitor` takes the
   address as a second argument, `(monitor pid to)`; `join` and
   `join/race` wait across islands, an end crossing as an item, since
   it no longer comes as a readable message; `ask` monitors to its
   address, so one `read` hears the reply or the end; the types are
   one, `pid<M>`; and 268, 325 and the guide's deadlock page changed
   with 296 to 301. The guide's pages say one kind, each in place;
   folding the two concept pages is step 4's.*
3. **The dance comes out.** `agent/ask`, `lib/ds.slight`, `rpc/call`,
   `http/request` and the examples that forked a task to hold an
   address ask directly. The checker's two warnings go; one comes:
   `ask` in a function that handles messages is fine, a bare `(recv)`
   after a `send` in one is worth a word. *Built (D278): as written,
   but the word is under `--strict`, since a send then a bare `recv` is
   right whenever nothing else sends to the process, which the checker
   can't tell; `rpc/call` names its caller beside its address, so the
   connection still stops a call whose caller ends; and the acceptors
   monitor every connection, since one kind of process keeps a result
   for a parent that never joins.*
4. **The docs.** LANGUAGE.md's Processes and Addresses, DESIGN.md's
   process model, TYPES.md's process types, the guide's two concept
   pages folded into one, "Processes and messages," and the tutorial's
   "Asking." *Built (D279): the reference docs were rewritten in place
   with step 2; the guide's two pages are one, `concepts/processes.md`,
   and chapter 4 answers at an address, then at a pid, and says what
   the address buys.*
5. **ts-cpi's Life programs** that use only processes and messages,
   ported as golden tests, each to `01-reference`'s frames: 02 actors
   (an address per generation does the buffering), 03 row workers (a
   barrier is one address read eight times), 04 metronome and 12 timer
   wheel (`after` on the virtual clock), 07 literal death (`kill` with a
   reason, an undertaker reading exit notices at one address), 11 join
   dataflow (576 processes joining nine inputs each), 13 let it crash
   (restart on `monitor`, the state in the supervisor's hands), and 18
   hostile scheduler, whose adversary is `SLIGHT_SCHED`. The others
   (traps, parking, checkpoints, pads, tick metering, multi-shot
   continuations, a deliberate deadlock) need a scheduler with its
   hands inside a process, and don't port; 08's forked timelines are
   "Branching a replay." *Built (D280): golden tests 367 to 374, on
   `t/data/life.slight`, their frames and commentary from
   `t/models/life.py`; `kill` carries no reason, so a cell told to die
   raises it; ts-cpi's shared job queue is a process; and 374 runs under
   five seeds in t/replay.sh too.*

**To settle:**

- **A pid is its inbox** (above), or pids and addresses disjoint.
  Recommended: a pid is its inbox.
- **The process types.** `task<T>` and `actor<M>` become one: `pid<M>`,
  what its inbox takes, inferred from its `recv`s as `actor<M>` is. For
  `join`, the checker carries the value type of a local bound to a
  `fork` without a name for it in the syntax, so `(let p (fork e))
  ... (join p)` is typed and a pid from elsewhere gives `any`.
  Recommended.
- **Stacks from one region,** without a mapping each, so that the
  ceiling is memory rather than the kernel's map count. Only if a
  program needs it; `ring-benchmark` says whether one does. Built
  (D291), once `bench/idle-memory` and `thread-ring` did: slabs of 64,
  coloured so their tops don't share cache sets.
- **A bare `(recv)` inside a handler,** the one way left to take a
  message meant for someone else. A `--strict` warning, when it follows
  a `send` in the same body; not a rule.

### A waiting process copied off its stack (proposed, Nov 2026)

**Why.** One kind of process (D276) gave up scale: many processes
waiting at once, the shape of a server's connections and a simulation's
actors. A waiting process keeps its stack, so it holds at least a page
of it (4 KB on Linux, 16 KB on the M2 Max), and the first touch of that
page is a fault. Carving and colouring the stacks (D291) took back the
ceiling, the system calls and the ring of 503, but not the page.
Against before D276 (`docs/BENCHMARKS.md`, 11 Nov):

| | before D276 | now |
|---|---:|---:|
| A waiting process, resident | 614 B (M2), 441 B (cloud) | 8,913 B (M2), 4,553 B (cloud), and a page table's page |
| A million started to wait, asked, ended | 0.31 s (M2), 0.78 s (cloud) | 10.5 s (M2), 8.9 s (cloud) |
| A hop round a ring of 4,000 waiting (cloud) | 63 ns | 294 ns |
| A hop round a ring of 30,000 (cloud) | 73 ns | 685 ns |

A goroutine waits in about 2.9 KB, an Erlang process 2.7 and a tokio
task 1.1: slight went from a fifth of the first two to three times
them on the M2. A process per connection, per person or per thing in a
simulation (PLAN.md's town, D263) pays it for every one.

**The floor is the page.** No arrangement of stacks gets a waiting
process below a page while its stack is its own memory; smaller stacks
would save only the page tables (D291). Go gets under a page by making
stacks heap memory, several to a page, grown by copying. What a
waiting slight process needs to keep is far less than a page: its
frames from the base to the wait, the trampoline's and a few of the
program's, a few hundred bytes for most (the spike measured about 130).

**What makes it possible.** Three things already true:

1. A slight frame holds only values, the frame pointer of the one below
   and a return address. Compiled code keeps nothing in a register
   across a call, and a slot only ever holds a value; the collector
   relies on both (D166).
2. C never calls compiled code (`apply` jumps), so below a wait there
   are only slight frames and the trampoline's: no C frame that might
   hold a pointer into the stack.
3. A wait is a call from compiled code into the runtime (`rt_recv`,
   `rt_join`, `read_from`, `rt_sleep`, ...), whose C frames are on top.

**The proposal.** When a process waits, and the stack below the wait
is small (a page or less), the runtime copies the slight frames, from
the wait's caller to the base, into a buffer on the process's side,
throws away the wait's own C frames, and gives the stack back warm.
When the process is woken, it takes any warm stack, copies the frames
back to its top, moves each saved frame pointer by the difference in
addresses, and starts the wait again from its call site: an assembly
stub per target, in `rt_asm_*.S`, that calls the wait's C function
afresh with the moved frame and returns its value to the saved return
address. A wait is then a function that can be run again: what it needs
after waking is in the process, not its C locals, which the waits
nearly are already (`recv_slow` loops, `rt_join` loops on `far_end`).
A process whose stack below the wait is bigger keeps its stack, as now:
deep recursion that waits pays the page, nothing else does.

**When.** Not at every wait. Copying out and back is a few hundred
bytes and a few frame pointers each way, cheap against a page fault and
dear against `ping-pong`'s 70 ns. So a process copies out only when its
island holds more than `STACKS_KEPT` stacks of waiting processes;
below that, waits keep their stacks, and `ping-pong`, `payload` and a
ring of a few hundred run as now. `idle-memory` and the rings of
thousands copy out, and should come back near their times before D276.

**Options:**

- **A. Copied-out waits** (above). Gives back most of the scale where
  it's spent, keeps `recv` anywhere and every wait keeping what it
  needs, and is contained in the runtime: no compiler change, no change
  a program can see but `:system`'s `(:stack held)`. Recommended.
- **B. Growable stacks, Go's:** a small stack from the heap, copied to
  a bigger one when the entry check finds it full. Gets a waiting
  process near Go's 2.9 KB, not under it, makes every process pay the
  copy as it grows, and needs the entry check's failure path to be able
  to continue (both code generators) and every C call on the stack to
  be one whose frame can be moved. More machinery for less.
- **C. Smaller stacks,** packed closer: saves the page tables and some
  of starting a waiting process (a third, at 256 KB, D291), not the
  page; and changes how deep a program may recurse (LANGUAGE.md, 8 MB).
- **D. Accept the cost,** and say so in the guide: a process that waits
  costs a page.

**Risks, to settle in a prototype:**

- **Every pointer into a stack must be known.** The saved frame
  pointers, and the process's context; nothing else, if (1) and (2)
  hold. `rt_apply`'s frame, the trampoline's and the fork entry's
  (`rt_apply_start`) must be ones the walk recognises; a frame it
  doesn't recognise makes the process keep its stack, so a mistake
  costs memory, not correctness.
- **Each wait restartable.** An inventory of the waits (`recv`, `join`,
  `join/race`, `read` and `read/any`, `sleep`, `fork/on`'s wait for its
  pid, `timer/cancel` across islands), each made to keep its state in
  the process. A wait that can't be is left keeping its stack.
- **The collector.** A copied-out process's roots are in its buffer
  while it waits; nothing collects a waiting process's heap now, so
  nothing changes, but the collector's frame walk must run only on
  frames back on a stack. `SLIGHT_GC_STRESS` and `SLIGHT_GC_VERIFY`
  with copying out forced at every wait (a `SLIGHT_` setting for tests)
  over every golden test.
- **Two targets.** The resume stub and the frame walk, for AArch64 and
  x86-64, the context's shape differing (x86's return address on the
  stack, AArch64's in x30).
- **Determinism.** No change to what runs when: replay and seeded
  orders hold as they are.

**To measure before it's agreed,** a prototype for `recv` alone, on
the M2 Max: `idle-memory 1000000` (time and bytes a process), rings of
503, 4,000 and 30,000, and `ping-pong`, `payload` and `thread-ring`
unchanged. If `idle-memory` isn't back within a few times its 0.31 s
and a few hundred bytes a process, or the gate costs `ping-pong`
anything, it's not worth its machinery, and D is the answer.

### An agent team: a design exercise (agreed, D281; built, D281 to D285, Nov 2026)

Stevan, after the Life ports (D280): "So what would be a good design
exercise? Should we flesh out an example to a larger app? Or do
something new?" Claude's answer, written up here: something new,
mid-sized, and dense with conversations between long-lived processes,
since that's the one shape the ports didn't exercise and the one D277
was for. Proposed, not agreed; nothing built.

**What it is.** `examples/team.slight`: a lead agent is given a job, in
text. It asks its model to break the job into tasks, forks a worker
agent for each, with a role, tools (read, list, search, calc, a command
run within a limit, a fetch) and a budget (turns, and milliseconds),
hands each its task, gathers the answers, asks its model to combine
them, and answers. At most k workers run at once: a pool, forking till
k are running and `join/race` for the first to end. A worker that
overruns its time is killed, its command with it (D193); one that
faults or raises is started again once, with why it ended, from its
monitor's notice, in its new task; a worker may ask another, and the
lead may ask a reviewer a second round. The person watches in the
terminal as `chat` does, or asks the team from `chat`, as a tool,
`ask_team`. Everything the run took from the world, the model's
answers, the commands' output, the fetches, goes in the record, so a
run replays exactly with no model and no network, and `SLIGHT_SCHED`
runs the same job under other orders, the answer the same.

**Why this one.**

- Every actor in it holds a conversation in the middle of handling a
  message: the lead asks its model while workers report, a worker asks
  its model and its tools while the lead may be telling it to stop.
  That's the case the old model made a dance of (D243's fork to hold
  the address, out since D278), and nothing yet exercises it at size.
- It uses `lib/agent`, `tools`, `exec`, `http` and `rpc` together,
  which nothing does.
- Supervision, cancellation and timeouts, which the Life ports barely
  touched: `ask-within`, `kill` and what it takes with it, a restart
  from a monitor's notice, a pool.
- Replay meets its most plausible use: an agent run recorded and
  replayed, the same answer under any schedule, and a transcript a test
  can hold.
- It ends in something a person can run.

**What it isn't.** Not a product, and not the chat's replacement:
`chat` stays the one-agent conversation, and the team can be a tool in
it. The question it answers is whether slight is a good language to
write an agent harness in, and the deliverable is the friction list,
not the program: everything reached for that isn't there, with what was
done instead, as the Life ports gave three entries in a day (`/` giving
a float where an index was wanted, `$$` in a `fork`'s body being the
child's, a `sleep`'s wake needing a turn where a timer's message lands
at once). Each entry is then proposed on its own, or left. "No, it
isn't" is an acceptable answer at a thousand lines, and the safeguard
against the trap Stevan named: a solution looking for a problem.

**Shape.** 500 to 1,000 lines. Messages: `(:job text to)` to the lead;
`(:task id text to)` to a worker; `(:done id answer)` and `(:failed id
why)` back; a worker's tools approved by a function, as `chat`'s are.
The budget: turns per worker, which `lib/agent`'s rounds give; ms per
task, `ask-within`; k at once, the pool. Output: the terminal as `chat`
writes it, and a transcript written as s-expressions (`spew`), which the
tests compare.

**Tests.** A golden test on a fake model, as test 224's: a fake ollama
that answers from a script keyed on the question, so the lead's plan,
the workers' answers and the combination are fixed; one worker told to
overrun, its command a `sleep`, killed at its limit on the virtual
clock; one told to fail, started again once; the transcript worked out
by hand from the script. In t/replay.sh: a recorded run with the fake
model replayed without it, and the same job under five `SLIGHT_SCHED`
seeds giving one transcript. In t/live.py: the team on a real local
model when there is one, as the chat's live check does.

**Steps.** 1. The lead and one worker, no pool and no budget: a job
split and answered; the fake model and the golden test. *Built
(D281): `examples/team-agents.slight` and `examples/team.slight`,
golden test 375.* 2. The pool,
the budgets, the kill on overrun and the restart on failure, each
tested. *Built (D282): `team/new`'s options, the CLI's flags and
approvals, golden test 376; a kill doesn't take a process's children,
so the lead keeps each agent's pid.* 3. Workers asking each other, the review round, and `ask_team`
in `chat`. *Built (D283): golden test 377; a worker's agent lives till
the job's end, for its teammates, and reaches them through the lead.*
4. Record and replay, and the seeds, in t/replay.sh; the
live check. *Built (D284): `--transcript`; 375 recorded and replayed,
375 and 376 under five seeds; the team on t/live.py's model.* 5. The friction list written up in the decision, each entry
weighed: a library function, a language change to propose, or nothing.
*Done (D285): below.*

**Settled (D281), as recommended:** before the simulation's decisions;
workers' changing tools off, what they would have done reported, and
one approval a job; one model, a worker's an argument; the terminal and
a transcript file.

**What it found (D285).** Eleven entries on the friction list, and
three from the Life ports before it; not one is the process model's.
Nothing to do for most: a test sorts concurrent workers' events; a
module's header shows what it says (D248); a table of records is `(id
(record))`; a function takes 8 parameters, as LANGUAGE.md says; a tool
is a closure, and one over an address is the capability; keywords print
bare. Done here: `tools/string` exported. To propose, each on its own:

- **A replacing `assoc`** (built, D289): it added a pair at the front
  and never removed the old, so an update was `assoc` after `remove`,
  which a test got wrong. Now it replaces the first pair for the key
  where it was, or adds at the front; `lookup` sees no difference; a
  table walked or measured sees the right one. The dictionary's `put`
  and `update`, and two tests, lost their `remove`. Golden test 381.
- **Ending a process's children with it** (parked, Nov 2026, till a
  second program needs it): killing a worker leaves its
  agent, the agent's turn and a running command, so the lead keeps each
  agent's pid and the agent library's monitors do the rest. Erlang's
  links, or a kill that takes a process's descendants, would do it in
  the runtime; it cost three lines here, so it waits for a second
  program that needs it.
- **A quiet end for a program people run** (built, D290): the runtime
  printed the root's value after the program's own output, so `team`
  ended with `done` and `chat` with `bye`. A root ending with `()`
  prints nothing now; `chat` says its own goodbye, and `team` ends
  with `()` or raises. Golden test 382.

**The answer:** yes, for this shape of program, with the reservations
above; D285 says why, and what the exercise didn't test.

**Rejected, for this exercise:** growing `airline`, which is fork-and-join
shaped and was right under the old model, so it would prove nothing
about the new one; growing the Life ports, the same at small scale; the
living town now, which needs the simulation decisions first and is
`fork`, `send` and `recv` at scale, which the ports just did; and a
supervision library first, OTP's shapes, since the way to find what
repeats is to write the program and look.

### `fork` takes a function and its arguments (agreed and built, D286, Nov 2026)

From the talk after D285 (Stevan: "Yes this is what I was thinking for
fork and `$$`. It makes it much more clear."). Agreed and built, D286:
`(fork f arg ...)`, `(fork/on k f arg ...)` and `(connect ... f arg
...)`, with `(fork 42)` refused by the compiler ("`fork` should always
take a function in its first position"), a bad function faulting the
caller, `fork f(a, b)` and `fork { ... }` in `.sly`, every program
migrated, golden test 378. What follows is the proposal as it was put.

**The itch.** In `(fork expr)`, `expr` runs in the child, so `$$` in it
is the child, and a parent that wants the child to know it binds
itself first, `(let me $$)`, thirty times across the repo; and an
argument that should be worked out once, in the parent, is worked out
in each child unless it's bound first. The rule is in the tutorial and
in LANGUAGE.md, and the Life ports still read it wrong (D280). It comes
of `fork` being the one form whose argument runs somewhere else.

**The proposal.** `fork` takes a function and its arguments, as Erlang's
`spawn/3` does:

    (fork f arg ...)                 ; was (fork (f arg ...))
    (fork/on k f arg ...)
    (connect :keypress f arg ...)
    (connect :tcp where f arg ...)
    (connect dev f arg ...)

`f` and the arguments are evaluated in the parent, deep-copied into the
new process (a closure with what it captured, as a message carries
one), and the child calls `f` with them; the call's value is the
process's, as now. So `$$` anywhere in a `fork` form is the parent, and
`(fork worker $$ job)` tells the worker who started it. A body that
isn't a call is a lambda: `(fork (lambda () (do (sleep ms) (send p
:tick))))`. There's nothing else to learn: a fork is a call, made in a
new process.

**What goes.** The compiler's hidden entry function at each fork site,
and the free-variable walk that finds its parameters (`compileFork`);
the rule that a fork takes at most 8 locals into the child (a lambda
captures what it needs, and arguments are at most 8 as in any call);
and `$$` meaning two things in one form. The checker types a fork as
the call of `f` it is, so its arity and arguments are checked as any
call's are, and the process's messages come from `f`'s recvs, as they
come from the body's call now (`forkType`). The runtime's `rt_fork`
takes the function and the argument values where it took a code
address and the copied locals, and the child's entry applies it, as
`apply` does; `:system` says a process is in `f`, which already has a
name. What's copied is what's passed, which is the rule a program wants
anyway: `(fork worker (part-of big i))`, not `big`.

**Migration.** 682 forks and 149 connects with a body, across lib, the
examples, the tests and the guide. About 630 are a direct call of a
function, `(fork (f a b))`, which a script makes `(fork f a b)`,
asserting each; 33 whose body is a special form (`do`, `match`, `cond`,
`recv`, ...) and 23 whose body is a bare value become lambdas, by hand;
the 30 `(let me $$)` before a fork go. An argument that mentions `$$`
changes meaning, from the child to the parent: the script lists each
one, to be read. The checker catches an old-style fork that's left:
`(fork (worker mine))` is then `fork` of what `worker` returns, which is
no function, and it says so where it can tell. A day, with the docs
(LANGUAGE.md's fork, fork/on and connect, DESIGN.md's table, the guide's
tutorial and concepts pages) and a decision record.

**To settle.**

1. `(fork v)` where `v` isn't a function. Recommended: it faults,
   `:not-a-function`, in the parent, as `(apply v ())` would, and no
   process starts; the checker warns of it when it can see it. The
   other way, a value as a process's value, `(join (fork 42))` giving
   `(:ok 42)`, is one fault fewer and two meanings for `fork`, and it
   hides an old-style `(fork (f x))` whose `f` returns a value.
2. Nothing shorter than `(fork (lambda () ...))` for a body that isn't
   a call. Recommended: nothing; it's the honest spelling, and the
   short bodies in the repo are `sleep`s and `send`s, which read as
   well as `(fork sleep ms)`.

### Keywords, a type of their own (agreed and built, D287, Nov 2026)

From the same talk (Stevan: "perhaps we should have a new type, a
'keyword' which is a symbol which evaluates to itself. They would not
share the same namespace as `let` or `defun` defined symbols, and it
would always print the `:` when it was serialized. This should decouple
the two symbol types and I hope, simplify things. I know these are deep
changes, but I think they are worthwhile."). Agreed and built, D287:
K1, the colon printed everywhere, `sym?` false of the booleans; the
keyword type beside `sym`; `rpc`'s words symbols; every expected
output given its colons, by a verified patch or its model run again;
golden test 379. What follows is the proposal as it was put.

**The itch** (D284; D285's entry 11, filed under nothing, which this
revises). `:ok` and `'ok` are one symbol, printed `ok` (D52), so printed
data can't say which it was: a transcript's `(plan ...)` reads back as
the symbol `plan`, or as `(:symbol "plan")` when the program doesn't
mention it (D155), and `:zzz` and `zzz` in text read as one thing. A
keyword is a tag; a symbol is a name. The language uses the first in
every message and result, and the second almost nowhere: quoted names
appear in a dozen tests, and in data read from text, `rpc`'s words and
handler names, and source read with `:source`.

**The proposal.** A keyword is a type of its own. `:name` evaluates to
itself and is `eq?` to no symbol, string or anything else. It prints
`:name` wherever it's printed: `pprint`, `str` and `tty/write`,
`sexp/print`, and `log`'s fields (`":name"` in the JSON, as `str`
writes it). It reads back as itself from `sexp/parse`, `:sexp` and
`:source`. A symbol is a name as data: `'name`, the names in a quoted
list, and a bare word in text read as data. It prints bare, as now, and
is for programs that handle programs, and for text protocols made of
words. In patterns, `:tag` matches the keyword and `'name` the symbol,
both allowed today. Predicates: `keyword?`, and `sym?` for symbols
alone; `#true` and `#false` are neither, `bool?` alone, where D53 had
`sym?` true of them. Conversions: `keyword->str` and `str->keyword`
beside `sym->str` and `str->sym`. Types (TYPES.md): `keyword` where the
table has `sym` as "every symbol, keywords and booleans included", `:ok`
the type of that keyword alone, as now, and `sym` the symbols. The
reader: `:name` is a keyword literal, not `(quote name)`, so `'(:a b)`
is the keyword `:a` and the symbol `b`, where today it's `((quote a)
b)`; `:12`, `::x` and a lone `:` stay errors. `.sly` spells both as it
does, `:name` and `'name`; its grammar doesn't change.

**How, in the runtime.** One tag, one word: `id << 4 | kind << 3 |
RT_TAG_SYMBOL`, the kind bit set for a keyword. A keyword and a symbol
of one name share the compiler's table and differ in that bit, so `eq?`
is still word equality and a pattern still compares a word; the
compiler emits every name mentioned as `:name` or `'name` once, as now.
`#true` and `#false` stay ids 0 and 1, printed as themselves. `hash`
mixes the kind in with the name (D209), so `:a` and `'a` hash apart,
and the same on every machine still. `sexp.c` reads `:name` as the
keyword and `name` as the symbol, and prints each its way.

**Names the program doesn't mention.** Both kinds are compile-time ids
(D14), so a name read from text that the program never wrote needs a
value. Two ways:

- *K1, D155's rule, in both namespaces:* an unknown keyword reads as
  `(:keyword "name")`, as an unknown symbol reads as `(:symbol "name")`,
  and each prints back as it was read, `:name` and `name`, so data
  passes through a program unchanged, and a program can only compare or
  match what it mentions anyway. No table at run time; nothing an input
  can fill.
- *K2, interning at run time:* a table of names shared by the islands
  under a lock, with a cap (a million names, say, past which a reader
  gives `(:error (:too-many-names))`), so every `:name` read is `eq?` to
  the program's whether it mentions it or not. It reopens D14 and D155
  (Erlang's atom table, which untrusted input fills); ids differ between
  runs, which `hash` by name hides and the collector doesn't see.

Recommended: K1 now. It's the rule we have, in two namespaces where it
was in one, and a keyword's colon never goes missing again, which was
the complaint; a program that never looks inside `(:keyword "name")`
wouldn't notice K2 later, so K2 waits for a program that needs it.

**What changes, and the cost.**

- The runtime: the printers, `sexp.c`'s reader and printer (a
  `RT_SEXP_KEYWORD` beside `RT_SEXP_SYMBOL`), `str->sym`, `sym?`, the
  new builtins, `hash`.
- The compiler: the reader, `values.ts`, the constants codegen emits,
  the checker's types and `builtins.ts`.
- `lib/rpc.slight`: stupidrpc's words are bare, so its wire words become
  symbols, `(list 'helo 'rpc 1)` sent and `('helo 'rpc 1)` matched; a
  barf's reason, `(:unknown-protocol ...)`, prints with its colons
  inside. `lib/http.slight` takes a method as a symbol or a string;
  keywords too, or keywords alone.
- The golden outputs: about 300 of the 408 print a keyword somewhere,
  and each gains its colons. The rule for changing them: the 25 Python
  models print the colons and are run again; for the rest, the new
  compiler's output is diffed against the old expected, and the diff is
  read against the program: a colon before a word the program writes
  as a keyword is the change, and anything else is a bug, in the
  program or in the compiler. The diff is the review, not a copy. A
  program that writes a keyword for a person to read, `(tty/write
  :done)`, bends, and writes a string (D84).
- The docs: LANGUAGE.md's reader, values, patterns, strings, `sexp/` and
  output sections; DESIGN.md's values; TYPES.md's table; SYNTAX.md; the
  guide's tour and `01-values.md`, which say "without its colon", and
  every output block that shows a keyword; the Sublime syntaxes; D52,
  D53, D155 and D163 superseded in part, by the decision record.

Two days, after `fork`'s day; each its own decision, with the suite
green at the end.

**Rejected, or to be:** one type still, only printed with its colon,
which keeps `(eq? :ping 'ping)` true and a bare word in text reading as
a tag; no symbols at all, names in data as strings, which quoted lists
and the readers of source can't do without; and K2 now, above.

**To settle:** K1 or K2; whether `str` and `tty/write` print the colon
(recommended: yes, as Clojure's `str` does, `keyword->str` giving the
bare name); and `sym?` false of the booleans (recommended: yes).

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

- Multiple cores: agreed as islands (D256, "More than one core:
  islands", above), not the scheduler over every core, with stealing,
  that BACKGROUND.md's "Multiple cores" sketches.

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
| `ping-pong`, `ring-benchmark`, `million-forks` | ported in step 7 (`examples/`, D97); `million-forks` is `ten-thousand-forks` since D276, a waiting process keeping its stack |
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
