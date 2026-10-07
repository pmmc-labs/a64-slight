# Plan

Ghuloum's incremental approach: the compiler works and its tests pass at
the end of every step, and each step adds one feature. Steps are sized to
be a session or two each.

**Progress:** steps 0–9 and 10a–10e done (under qemu, and natively on
macOS: Stevan runs `make test` on his M2 Max after every step), and a
second target, x86-64 (below, after 10f). 10f, HTTP in slight, is next.

Read [`DESIGN.md`](DESIGN.md) first. Where a step meets an **(open)** item,
propose options to the user before building (see `CLAUDE.md`).

## Proposed layout

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

Borrow from `spike/aarch64/`: the Makefile's cross-compile logic,
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
- The reader: port `reference/ts-cpi/src/reader.ts` (it has source
  positions), with its tests (`reference/ts-cpi/tests/reader.test.ts`).
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
- Settle the calling convention for compiled functions **(open)**: the
  proposal is args in `x1`–`x7`, closure in `x0`, result in `x0`.
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
`runtime/strings.c`, the numeric ones in `runtime/numbers.c`. The ASCII case mapping and the padding and searching
helpers (`uc lc pad-start pad-end str-repeat starts-with ends-with`) wait
for the prelude in step 6. Agreed for floats: `div` and `%` truncate
toward zero, and dividing by zero faults; floats print as `3.0`, or the
shortest form that reads back the same; `(== 1 1.0)` is `#true` and
`(eq? 1 1.0)` is `#false` (D68–D70).

- Boxed objects with headers. String literals in static data.
- The string builtins in C (see DESIGN.md), `concat`/`~`, `tty/write`.
- Floats: literals, arithmetic with promotion, `/` gives a float, `div`
  and `%` **(open: names)**, rounding to integers, libm wrappers.

### 6. Closures and the prelude (done)

`apply` became a builtin (D77), and the prelude's names were tidied
(D78). The five ported examples are in `examples/`, each with a
`.expected` that `t/run.sh` checks.

- `lambda`, closure conversion, indirect calls with an arity check,
  static closures for `defun`s used as values.
- `lib/prelude.slight`: `map filter fold/l fold/r reverse append length
  nth range member? find assoc lookup ...`, `and or not`, string helpers.
- A TAP-style test library in slight, after `reference/ts-slight/lib/Test.slight`.
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

**To discuss before building: `defactor`.** Stevan's idea: one form for an
actor whose body does some work and then ends in a `recv`, expanding to
the two functions the `recv` rule needs now (one that does the work and
tail-calls one whose body is just the `recv`; see `tail-chase-game`). The
`recv` would have to come last. Open: in the expander or the compiler; how
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
couldn't, short of a shadow stack.) Instead the expander makes ts-slight's
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
  collector still runs at `recv` with just the arguments as roots, so it
  suits compiling to C as well as assembly.
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

#### 10f. HTTP in slight

`lib/http.slight`, opt-in (D140): an HTTP/1.1 client and server on `:tcp`,
without TLS. To settle when it starts:

- What the library looks like to a program: a server as a function of a
  request that returns a response? A client as a call that gives
  `(:ok response)` or `(:error reason)`?
- Request bodies: a body that doesn't end in a newline can't be read
  until connections can send chunks (D134). Add chunks first, or start
  with GET and bodies that end in newlines?
- `Connection: close` (one request per connection) to begin with, or
  keep-alive?
- Keep the API independent of what's underneath: in a browser, HTTP
  would be a device over `fetch`, not slight on `:tcp` (BACKGROUND.md,
  WebAssembly), so a program shouldn't see which it's using.

Next session starts here, with Stevan.

#### A second target: x86-64 (done, Oct 2026)

Taken out of order, at Stevan's request, as BACKGROUND.md ("Other
targets") planned it (D145, D146). First the code generator was put
behind a target interface (`target.ts`), checked by the generated
assembly of all 150 programs coming out the same byte for byte; then
`x86_64.ts` and the runtime's x86-64 half (`rt_asm_x86_64.S`, `rt_ctx_t`
and `start` per architecture), and `--target` in the driver, `t/run.sh`
and the Makefile. Every golden test passes on both, natively on x86-64,
with the same expected output. A third target (RISC-V, say) is now a
target file and its runtime assembly. Untested so far: macOS x86-64
(Rosetta 2), which `make golden TARGETS=x86_64` would run on the M2.

The adversarial review that followed (differential tests of some 400
programs on both targets, the ABI shape by shape, the interface's
contract) found no difference in the x86-64 code. It found some older
bugs, fixed with tests: a name ending in a backslash, written into an
assembly comment, made the preprocessor swallow the next instruction
(160); `min` and `max` compared two integers as doubles (161); a lambda's
stack fault named its position twice. **Open, for Stevan:** printing,
copying (a message, or a fork's values) and `eq?` recurse in C down a
list's cars, so a value nested some 50,000 deep in its cars crashes the
whole program instead of faulting its process, at a depth that differs
between targets. The collector already avoids this with a work stack
(D107). Options: the same for the three of them, or a check of the C
stack against the process's limit that faults with `:stack`.

### 11. Port the examples

See the table below. The window manager and text editor are the real
tests of whether the language is pleasant to use. Stevan is rewriting
those three (`text-editor`, `window-manager`, `better-window-manager`)
himself. Checked for them in Oct 2026: nothing is missing now that
`@ARGV` exists (D144), but two things may come up: the runtime always
prints the root's value at exit, so `(ok ())` follows the editor's
"Goodbye!"; and strings are bytes, so typing non-ASCII text puts the
cursor in the wrong column until there are UTF-8 helpers (DESIGN.md's
level 2 strings).

### 12. Self-host

Port the compiler to slight (it's written slight-shaped for this), then
the three-stage bootstrap from DESIGN.md.

## The ts-slight examples

Not every example has to be ported, and an example can change as much as
it needs to: if one calls for a change to the compiler or runtime, change
the example instead, or leave it out (D84).

From a survey of `reference/ts-slight/examples/`. Almost all of them need
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
| `simple-db-server`, `game-of-life-actors` | `(recv)` used as an expression mid-function becomes receive functions; `simple-db-server`'s `db-client` does `(recv)` inside a lambda, so it needs restructuring |
| `active-objects` | as above, plus drop `gensym` |
| `key-catcher`, `divisions`, `tail-chase-game` | ported in step 10b (D123) |
| `window-manager`, `better-window-manager` | as above, plus `connect :keypress` and the screen size (step 10b) |
| `meta-circular` | `join`/`yield`/`apply`; no `recv`, so likely as-is |
| `more-oop` | local `defun`s lifted to top level; it uses `slight/eval` to look up methods, which has to go |
| `text-editor` | local `defun`s lifted; drop the `slight/parse`/`slight/expand` feature; files (`lib/fs.slight`'s `slurp` gives lines) |
| `repl` | no `eval`, so it becomes a line-editor demo (or evaluates a tiny calculator language written in slight) |
| `eval-string`, `hot-code-reload` | out: they exist to show `eval` and hot reload |
