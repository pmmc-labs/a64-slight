# Decision log

What was decided, why, and what was turned down, roughly in the order it
came up. **User** means Stevan decided it; **default** means I (Claude)
proposed it and it wasn't explicitly discussed. Defaults are fair to revisit.
Open questions are listed at the end of [`DESIGN.md`](DESIGN.md).

## Scope

**D1. Resurrect the async calling convention, natively.** *(User)*
VM3's docs described an assembly-level actor system. The AArch64 spike
(`spike/aarch64/`) showed it works as real machine code: 232 ns per round
trip and ~16.8 KB per idle process on an M2 Max. See
[`BACKGROUND.md`](BACKGROUND.md).

**D2. Build a stripped-down slight on it, keeping ts-slight's user surface.**
*(User)* "I was the most happy with the user surface of ts-slight." The
internals of ts-slight and ts-cpi need not be preserved. Anything Node-world
(TUI library, HTTP) is dropped.

**D3. A small runtime; the OTP-like layers are written in slight.** *(User)*
Supervisors, gen-server patterns, pools, the REPL and line editing are
slight code. This is how Erlang does OTP.

**D4. Scale: hundreds to thousands of processes; 1M possible, not optimized.**
*(User)* "more (hundred|thousand)s of processes working together to run an
Operating System." So Erlang's answers are not automatically right.

**D5. Aggressively simple; practical over pure.** *(User)* Any slight feature
that adds complexity can be cut or changed.

**D6. AOT compilation only.** *(User)* No `eval`, no hot reload, no runtime
code loading. Rejected: a BEAM-style bytecode VM with a compiler in the
runtime (my earlier recommendation, made because ts-slight's REPL, eval and
hot-reload examples needed it). With AOT, there's no compiler in the
runtime, no global name table to patch, and no interpreter.

**D7. No deep non-tail recursion.** *(User)* The deep recursion in ts-slight
(`take`, `range`, `fold-konts`) was only there to compare against the
fold/l and fold/r work. A fixed stack plus a fault on overflow is fine.

## The core rule

**D8. `recv` only at the top of a receive function.** *(User: "I am actually
okay with doing the recv only at the top of the loop if it simplifies
things.")* This is the decision everything else leans on. The stack is
empty at every `recv`, so a waiting process is (function, args, mailbox)
and holds no stack, and collection is trivial. It removes:
selective receive, the mailbox cursor, AWAIT/REQUEST/REPLY, reply refs, the
stray-reply bug (AVM's `1003`), and call-cycle deadlocks between handlers.
Cost: send-then-wait is two functions, and request/reply is correlated by
the actor's own state (run-to-completion style). Rejected: selective
receive plus Erlang-style `call`/`reply` written in slight (my earlier
proposal).

**D9. Stacks only while running (option A).** *(User)* A process holds a
native stack only while running, paused, or blocked mid-computation; at
`recv` it gives the stack back to a pool. Memory scales with how many
processes are *running*. Rejected: B, no per-process stacks at all, where
handlers run to completion on the scheduler's stack under a step budget and
overruns are killed. One long computation could only be killed, never
paused: the wrong trade for an OS.

**D10. Unmatched messages go to a dead-letter log.** *(User)* Rejected:
faulting the receiver.

**D11. Lambdas are always plain; no local `defun`; lambdas can't refer to
themselves.** *(Default, follows from D8.)* Indirect calls would hide the
call graph that D8 needs. Also keeps the heap acyclic.

## Language

**D12. `cond` is the only conditional.** *(User)* `if`, `when` and `case`
are out. `and`/`or`/`not` are ordinary prelude functions (no
short-circuit), like ts-slight's `and?`/`or?`.

**D13. Booleans are the reserved symbols `#true` and `#false`.** *(User)*
Rejected: integers 0/1.

**D14. Symbols are compile-time ids. No runtime interning, no `gensym`.**
*(User)* `string->symbol` looks up the compile-time table and gives
`#false` for unknown names *(default)*.

**D15. Types: integer, float, symbol, list, string, function, pid.**
*(User, over several rounds.)* I first cut it to integers and symbols only
(no heap at all). The user pulled lambdas, closures, lists and strings back
in: "maybe that was a bit too aggressively simplified... the bare minimum
feature set for a useful language". Floats came in once they were shown to
be cheap (a boxed object, no new tag).

**D16. `do` is in.** *(User asked; it's trivial.)* Forms in order, the last
in tail position.

**D17. Errors are the Result pattern, `(:ok v)` / `(:error e)`.** *(User)*
No `catch`. `raise` ends the process with `(:error reason)`.

**D18. `/` always returns a float.** *(User)* As in ts-slight, where
`(/ 5 2)` was 2.5. Integer division is a separate name *(default: `div`,
open)*, with `%`. `ceil`/`floor`/`round`/`trunc` return integers
*(default)*, since their results are almost always coordinates or counts.

**D19. `join` is back, as a blocking wait.** *(User)* I had removed it
(replacing it with an exit message to the parent) while trying to make
every wait happen at `recv`. The user pointed out
`(join (connect :keyboard ...))` needs to *wait*. With D9, pausing
mid-computation is supported anyway, so `join` reuses it. From the
examples: it must work on any pid (`better-window-manager` joins
siblings), anywhere including lambdas (both tournaments do
`(map (lambda (pid) (join pid)) pids)`), and after the target ended. So a
finished process's Result goes into a per-pid exit record. Returns
`(:ok v)` / `(:error e)`.

**D20. `yield` stays, in ts-slight's form `(yield expr)`.** *(User asked.)*
It's the same pause as preemption, triggered by the program. Kept for
interleaving on purpose (`meta-circular`), letting a receiver run
(`hot-code-reload`'s `(yield ())`), and politeness.

**D21. Exit notifications are opt-in via `monitor`.** *(User: "opt-in is
good".)* No automatic message to the parent; with `join` back, it would
deliver the result twice.

**D22. `fork` keeps ts-slight's form `(fork expr)`.** *(Default, agreed in
passing.)* The compiler makes a hidden entry function; free variables are
deep-copied into the child.

## Values and memory

**D23. 64-bit tagged values, BEAM-like layout.** *(Default, after
discussion.)* I first proposed 32-bit values so a cons cell fits one 64-bit
word (31-bit integers, cells addressed by index). Once strings, closures and
floats were in, objects vary in size anyway, and 31-bit integers were a
real cost (milliseconds overflow in about 12 days). Cons cells are 16 bytes,
which is fine at this scale. Also considered: CDR-coding, VList/array-backed
lists. Both are possible later optimizations.

**D24. Per-process heaps; Cheney copying only where the stack is empty.**
*(Default.)* At `recv` and at base-of-stack tail calls the roots are just
arguments. C builtins never see objects move: no stack maps, no write
barrier, no rooting API. Considered: "Cheney on the MTA" (Baker, Chicken
Scheme). The copying algorithm and bump allocation apply; the CPS-in-C
trick doesn't, because we already have native tail calls and process
suspension. Also considered: Perceus-style reference counting with reuse
(Lean 4, Koka). Kept open by keeping the heap acyclic; not now.

**D25. `send` deep-copies; `recv` adopts the chunk.** *(Default.)*

**D26. Static data lives in the binary.** *(Default.)* Literals, quoted
constants and top-level function references are shared, never collected,
and sent without copying.

**D27. Strings: level 1 now (immutable bytes, byte indexing, ASCII case),
level 2 UTF-8 helpers when the editor needs them, full Unicode out.**
*(User agreed.)* No char type. Rejected: mutable C-style byte buffers
(break immutability).

## Runtime and I/O

**D28. Blocking syscalls block only their process.** *(Default; follows from
D9 and D19, not discussed separately.)* `sleep`, `slurp` and `spew` hold
the caller's stack and return a Result.

**D29. Event sources are devices that send messages.** *(Default.)*
`:keypress` keeps ts-slight's key shape. readline isn't needed: the REPL is
slight code over key events. *(User asked whether readline could be used.)*

**D30. C libraries come in as drivers exposed as processes.** *(Default.)*
Erlang's lesson: direct calls can crash or stall the whole runtime.

**D31. Platforms: macOS on Apple Silicon first, Linux AArch64 from the same
code.** *(User is on an M2 Max; Linux is how this container develops.)*

## Compiler

**D32. TypeScript on Node, for now.** *(User: "Node is acceptable.
Typescript is also acceptable.")* Considered: Perl (no static checks across
passes; Perl 5.40 isn't on macOS by default), C (slow to change tree
passes), slight from day one (needs a stage-0 compiler anyway), Racket with
nanopass (a new toolchain).

**D33. Write it "slight-shaped", then self-host.** *(Default, agreed with
D32.)* Pure functions, immutable s-expressions, recursion, association
lists, no classes, so the port is near line-by-line. Three-stage bootstrap.

**D34. Whole-program compilation to one `.S`, linked by clang.** *(Default.)*
D8 needs the full call graph.

**D35. Ghuloum's incremental approach.** *(Default.)* Start with integers,
keep a working compiler with passing tests at every step.

## Project

**D36. A new repo, `pmmc-labs/a64-slight`.** *(User asked for a new
pmmc-labs repo; the name is mine, after the `<lang>-slight` convention.)*
VM3 and AVM stay as they are; their useful bits are copied here.

## Step 0

**D37. Reader details.** *(Default.)* The reader is ts-cpi's, with these
changes:
- `#true`/`#false` read as symbols (D13).
- Integers are 63-bit, and a literal out of range is a compile error.
- Strings also accept `\r` and `\e` (ESC); ts-slight strings had no escapes
  at all, so its bare `\e` constants did that job, and they still read as
  symbols.
- Quasiquote, unquote and dotted pairs are errors.
- Symbols are records compared by name (slight can't make symbols at
  runtime, so the self-hosted compiler will hold source symbols as data
  too).
- Every atom carries a position, not just lists.

**D38. `slightc` compiles the runtime sources with every program.** *(Default.)*
Simplest possible build: no runtime build step, no stale archive. Revisit
if it gets slow.

**D39. "Slight-shaped" allows loops where slight would tail-recurse.**
*(Default.)* Node has no tail calls, and a recursive tokenizer would
overflow the JS stack on a large file. A `for (;;)` that only reassigns
locals ports to a tail-recursive function directly.

**D40. For now, the runtime prints the root process's result.** *(Default,
from PLAN step 0.)* It's how the golden tests observe programs before
`pprint` exists. Revisit when processes land (step 7), since the exit
status is meant to follow the root's Result.

## Step 1

**D41. `cond` tests must be booleans, and no matching clause faults.**
*(User.)* As ts-slight required. A test that is literally `#true` compiles
to no check.

**D42. `let` as the last form of a body gives its value.** *(Default.)* As
in ts-slight. A `let` anywhere but directly in a body is a compile error.
Binding `#true`, `#false` or a special form's name is a compile error;
locals may shadow builtins.

**D43. `pprint` arrives in step 1, and returns `()`.** *(Default.)* It was
planned for step 3, but golden tests need to print more than one value
per file. Its return value follows ts-slight.

**D44. Faults report to stderr and exit 1, for now.** *(Default.)* Each
fault site passes rt_fault a string saying what and where, like
`+ at t/x.slight:2:1`. Once there are processes and lists, a fault ends
only its process with `(:error ...)`; the reason's shape is still open.

**D45. Golden tests check stderr and the exit status too.** *(Default.)*
Stdout and stderr together, then `exit: N` when N isn't 0. rt_fault
flushes stdout first, so the order is stable.

**D46. Binary primitives only.** *(Default.)* `+ - * == != < <= > >=`
take exactly two arguments, as in ts-slight's notes. Both must be
integers (floats come in step 5); anything else faults.

## Step 2

**D47. Calling convention B: AAPCS64's argument registers.** *(User.)*
Arguments in `x0`–`x7` (so at most 8), the result in `x0`, and the closure
in `x9` for calls through a closure (step 6). Compiled functions and C
builtins are called the same way, and compiled functions are ordinary
C-callable functions, which helps when `fork` starts them (step 7).
Rejected: A, arguments in `x1`–`x7` with the closure in `x0` (the spike's
runtime-op convention; C builtins would need the arguments moved); C,
arguments on the stack (Ghuloum's original).

**D48. The stack check is a comparison at function entry.** *(Default.)*
`sp` against a limit in the process struct, three instructions, as in Go.
Rejected: catching guard-page `SIGSEGV`s on an alternate signal stack,
which is harder to get right and can't easily tell a stack overflow from
other faults. The same check will give each process's fixed-size stack a
clean overflow fault in step 7. The root process runs on an 8 MB `mmap`ed
stack with a guard page; the limit leaves 64 KB of headroom.

**D49. The reduction check is at function entry only.** *(Default.)* A
tail call jumps to the target's entry, so the one check covers calls and
tail calls, and every loop. The parameters are saved to the frame first,
because `rt_preempt` is a C call. The quota is 1,000.

**D50. A program with no top-level expressions has the value `()`.**
*(Default.)* Replaces step 0's "the program is empty" error, so a file of
only `defun`s compiles.

**D51. Golden tests time out.** *(Default.)* `t/run.sh` kills a test after
`TIMEOUT` seconds (60) with a watchdog in plain `sh`, since macOS has no
`timeout(1)`. A mutation that made a loop spin forever showed the need.

