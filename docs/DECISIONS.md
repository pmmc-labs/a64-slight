# Decision log

What was decided, why, and what was turned down, roughly in the order it
came up. **User** means Stevan decided it; **default** means I (Claude)
proposed it and it wasn't explicitly discussed. Defaults are fair to revisit.
Open questions are listed at the end of [`DESIGN.md`](DESIGN.md).

## Scope

**D1. Resurrect the async calling convention, natively.** *(User)*
VM3's docs described an assembly-level actor system. The AArch64 spike
(in git at `3fd71e0`) showed it works as real machine code: 232 ns per
round trip and ~16.8 KB per idle process on an M2 Max. See
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
short-circuit), like ts-slight's `and?`/`or?`. (Revisited in D142: they're
back, made into `cond` before compiling.)

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
the caller's stack and return a Result. (Narrowed by D111: `sleep` can't
fail, and returns `()`.)

**D29. Event sources are devices that send messages.** *(Default.)*
`:keypress` keeps ts-slight's key shape. readline isn't needed: the REPL is
slight code over key events. *(User asked whether readline could be used.)*

**D30. C libraries come in as drivers exposed as processes.** *(Default.)*
Erlang's lesson: direct calls can crash or stall the whole runtime.
(Refined in D151: a pure function bounded by its input may be a builtin.)

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
if it gets slow. (Revisited in D150: it's compiled once, and kept.)

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

## Step 3

**D52. Symbols print without the colon.** *(User.)* As in ts-slight:
`(pprint :ping)` prints `ping`. `:ping` and `'ping` are the same symbol.

**D53. `sym?` is true for `#true` and `#false`.** *(Default.)* It follows
from D13: booleans are reserved symbols. (In ts-slight booleans were their
own type, so `sym?` said no.) `bool?` is true only for those two.

**D54. `eq?` compares words until there are lists.** *(Default.)* Every
value so far is a single word, so word equality is structural equality.
Step 4 adds the structural comparison for lists (and step 5 for strings
and floats).

**D55. Symbol ids follow first mention.** *(Default.)* `#false` is 0 and
`#true` is 1 (rt.h), then each symbol gets the next id the first time the
compiler sees it. The names go into `slight_symbol_names` in id order.

## Step 4

**D56. `car`/`cdr` of a non-cons and `cons` onto a non-list fault.**
*(Default, as in ts-slight.)* So every list is proper, and printing and
`eq?` never meet a dotted pair.

**D57. The heap pointer and limit live in the process struct.** *(Default,
agreed in passing.)* Two loads and a store per allocation, but C builtins
that allocate (strings, step 5) bump the same pointer with nothing to
sync. Rejected for now: keeping them in callee-saved registers (faster,
but every C call that allocates would have to sync them). Revisit if
profiling says so.

**D58. One 64 MB heap chunk per process until GC.** *(Default; replaced
by D89 in step 7.)* As PLAN
step 4 said. It's mapped lazily, so unused space costs nothing, but step 7
will want something smaller per process, or step 9's chunk chain, before a
million processes can each have one.

**D59. `c[ad]r` up to four letters.** *(Default.)* Common Lisp's 28 plus
`car`/`cdr`; ts-slight had seven of them, and the examples use `cadr`,
`cddr`, `caddr`, `cadddr` and `cddddr`. A five-letter name like `caddddr`
is an ordinary name a program can define.

**D60. Quoted lists are static data.** *(Default; DESIGN had it.)* Cells
side by side in a constant section (`__DATA,__const` on macOS,
`.data.rel.ro` on ELF, since they hold addresses the loader fixes up),
shared by every process and never collected.

**D61. `eq?` calls the runtime only when it must.** *(Default.)* When
either side is a literal immediate (an integer, a symbol, `()`), comparing
the words decides it, so symbol dispatch like `(eq? dir :up)` stays a
single compare. Otherwise the same word is equal, and when the words
differ it calls `rt_equal`, which compares lists structurally.

## Step 5a: strings

**D62. String builtins keep ts-slight's edge cases.** *(Default.)*
ts-slight's were JavaScript's: `substring` clamps and swaps its indexes,
`index-of` finds `""` at 0, `str-split` on `""` gives `()`, `format-num`
pads like `padStart`, `concat` and `str-join` render non-strings as
`pprint` does. Indexes and lengths count bytes (DESIGN, level 1). Where
JavaScript would give `NaN`, a fault instead: `byte-at` out of range,
`bytes->string` given something other than 0–255.

**D63. C builtins are passed the call's site.** *(Default.)* In the
register after the arguments, so a fault raised in C reports the slight
source position, as the compiler's own checks do. Rejected: inline type
checks before every call (more code), or C faults without positions.

**D64. `pprint` doesn't escape inside strings.** *(Default, as ts-slight.)*
`(pprint "a\"b")` prints `"a"b"`.

**D65. `tty/write` takes its arguments as they are.** *(Default.)*
ts-slight also accepted one list argument and wrote its elements; here a
list is written as `pprint` shows it, like any other value.

**D66. `string->int` gives `#false` for anything that isn't an integer.**
*(Default.)* Like `string->symbol` (D14), rather than a fault or a Result.

**D67. The running process is a C global, `rt_current`.** *(Default.)* C
builtins need it to allocate. Single core, so a global is enough; the
scheduler (step 7) will set it at every switch.

## Step 5b: floats

**D68. `div` and `%` truncate toward zero.** *(User.)* Like C, AArch64's
`sdiv`, and ts-slight's `%` (JavaScript's). `(% -7 2)` is -1. Dividing by
zero faults; `sdiv` would quietly give 0. Rejected: floor division
(Python's), handier for wrapping around.

**D69. Floats print as `3.0`, otherwise as the shortest text that reads
back as the same double.** *(User.)* ts-slight printed `3.0` as `3`
because JavaScript has one number type; slight has two.

**D70. `(== 1 1.0)` is `#true`, `(eq? 1 1.0)` is `#false`.** *(User.)*
The comparisons compare numbers; `eq?` compares values, and an integer and
a float are different values.

**D71. `/` by zero faults too.** *(Default.)* D68 said so for `div` and
`%`; `/` follows, rather than returning `inf` as JavaScript did. Floats
can still reach `inf` and `nan` other ways (overflow, `(sqrt -1)`), and
print as `inf`, `-inf` and `nan`.

**D72. The math builtins.** *(Default.)* ts-slight's set, less `rand` and
`hex`: `sqrt pow sin cos tan exp` always return floats (as `/` does, so
`(pow 2 10)` is `1024.0`); `abs`, `min` and `max` keep their argument's
type (`min`/`max` return the argument itself, the first on a tie);
`ceil floor round trunc` return integers and fault when the result
doesn't fit; `round` is JavaScript's (halves up), as ts-slight's was. `PI`
is a constant, like `\n`.

**D73. Arithmetic on non-numbers says "not a number".** *(Default.)*
`+ - *` and the comparisons accept floats now, so their fault changed from
"not an integer"; `div` and `%` still say "not an integer".

**D74. Integer fast path, everything else in C.** *(Default.)* Two
integers stay inline: one `orr`/`tst` tests both tag bits. Anything else
branches to an out-of-line stub that calls `rt_add`, `rt_compare` and so
on, which promote to float or fault. `/` always calls C.

**D75. The cross-compile drops `--sysroot`.** *(Default.)* With it, lld
can't link `-lm` on Debian/Ubuntu, whose cross sysroot's `libm.a` is a
linker script with absolute paths; without it, clang finds the cross
toolchain by itself. macOS is unaffected.

**D76. Golden tests don't depend on libm's last bit.** *(Default, after a
macOS failure.)* macOS's `tan(1.0)` is `1.557407724654902`, glibc's
`1.5574077246549023`; the second is the correctly rounded one (checked to
60 digits), the first one ULP off, which libm's accuracy allows. IEEE 754
requires `sqrt` to be correctly rounded, so its results are compared
exactly; `sin cos tan exp pow` are compared to 12 places, apart from cases
that are exact everywhere (`(sin 0)`, `(pow 2 10)`).

## Step 6

**D77. `apply` is a builtin.** *(User.)* `(apply f xs)`, at most 8
elements. It's a short assembly routine (`rt_apply`) that spreads the list
into `x0`–`x7` and jumps to `f`, so a call to `apply` in tail position is
still a tail call. `meta-circular` defines its own `apply` and will need
another name.

**D78. The prelude's confusing names were changed.** *(User allowed it;
the changes are mine.)* `filter` keeps what matches and `remove` drops it
(ts-slight's `filter` dropped and `grep` kept; `grep` is gone). `(range
start end)` is half-open with no step (ts-slight's took a step and always
ended with `end`, which gave `game-of-life-actors` a grid one wider than
asked). `take` and `skip` stop at the end of the list rather than fault.
`starts-with?`/`ends-with?` got their `?`. `concat-list` is gone:
`str-join` does it. Kept: `fold/l`/`fold/r` (init, f, list), `assoc`
(adds, as in Clojure) and `lookup` (`:not-found`), `nth` and `find`
(`()` when there's nothing), `dotimes`, `pad-start`/`pad-end` (s, n,
fill). Every loop in the prelude is a tail call.

**D79. A lambda copies its captured values into its frame on entry.**
*(Default.)* Then they're ordinary locals in the body, and the code for
using one doesn't depend on where it came from. Rejected for now: reading
them from the closure at each use (less copying; more kinds of variable
in the code generator). Lambdas that capture nothing, and `defun`s used as
values, are static closures, never allocated.

**D80. The prelude has its own namespace.** *(Default.)* A program can
define a function with a prelude name; its own code uses its definition,
and the prelude keeps calling its own (labels `pf_...` vs `fn_...`). So
`(defun reverse ...)` can't break the prelude's `map`. Rejected: making
prelude names reserved (programs like golden test 053 define `range` and
`sum`), and letting a program's definition replace the prelude's
everywhere (a different `reverse` would break `map`).

**D81. Builtins can be values, through small wrapper functions.**
*(Default.)* `(fold/l 0 + xs)` and `(map car xs)` work: the first use of a
fixed-arity builtin as a value compiles a function that calls it, plus a
static closure. Builtins that take a varying number of arguments (`list`,
`concat`, `tty/write`, `format-num`) can't be values.

**D82. The test library is opt-in.** *(Default.)* `lib/test.slight`
isn't part of the prelude (its names, `ok` and `is`, are too common). A
golden test asks for it with a first line `; with: lib/test.slight`.
`ok` takes a boolean, as `cond` does (ts-slight's took anything but
`#false` and `()`), and tests are numbered from 1, as TAP expects.

**D83. Ported examples live in `examples/` and are golden tests.**
*(Default.)* Each that has a `.expected` runs in `make test`. The header
comment of each says what changed from ts-slight's version.

**D84. Examples bend to the language.** *(User.)* "You don't need to make
all examples pass. Especially if it calls for changes to the interpreter,
it's okay to change examples." An example that needs something slight
doesn't have gets rewritten, or stays unported.


## Step 7

**D85. `recv` patterns.** *(User.)* A clause is `(pattern body...)`. A
pattern is a name (matches anything and binds the whole message; `_` binds
nothing), a `:keyword` (matches that symbol), or `(:keyword names...)`
(matches a list of exactly that length headed by the keyword, and binds
the rest by position; `_` skips a position). Clauses are tried in order
against the first message only. A message no clause matches goes to the
dead-letter log, and the function takes the next one. Rejected: nested
patterns, literals other than the head keyword, a rest binding, and
selective receive (each is more matching machinery, and the body's `cond`
does the rest).

**D86. A FIFO run queue.** *(User.)* Deterministic on one core, and the
simplest thing that is. Rejected: the spike's ticks.

**D87. Error reasons.** *(User; built in step 8.)* A fault ends its
process with `(:error (kind value site))`, `(raise r)` with `(:error r)`,
and `(kill pid)` with `(:error :killed)`. Until step 8, a fault still ends
the whole program.

**D88. Exit records are kept forever, and mailboxes are unbounded.**
*(User.)* Both are fine at the scale slight runs at, and both policies
can come later without changing programs. Rejected for now: dropping a
record once the parent has seen it, and bounded mailboxes (which, since
`send` can't block, would drop the message or fault the sender).

**D89. The heap became a chunk chain in step 7, not step 9.** *(Default.)*
Received messages join the receiver's heap as chunks of their own, so the
heap had to be a chain anyway; and D58's one 64 MB mapping per process
would be 64 TB of address space at a million processes. The first chunk,
4 KB, comes with the first allocation; each later one doubles, up to 1 MB;
a process's chunks, messages included, are capped at 64 MB in all, past
which the allocation faults. Compiled code checks the limit inline and
calls `rt_heap_grow` out of line. Replaces D58.

**D90. Static data is found by address.** *(Default.)* The compiler
brackets the program's read-only data and constants with
`slight_rodata_start`/`_end` and `slight_const_start`/`_end`, and the
copier shares anything between them instead of copying it. So literals,
quoted lists and static closures cross processes for free. Step 9's
collector will use the same check.

**D91. Preemption switches only when another process is ready.**
*(Default.)* A lone process that runs out of reductions just gets a new
quota, instead of a round trip through the scheduler every 1000
reductions. `yield` always goes to the back of the queue.

**D92. A message to an ended process disappears.** *(Default, as
Erlang.)* It isn't a dead letter: that log is for a `recv` that didn't
understand a message, and sends that race with an exit are normal.
Sending to something that isn't a pid faults.

**D93. The program ends when nothing can run.** *(Default.)* Not when the
root ends: ping-pong's root forks two processes and returns at once, and
they should still run. The root's value is printed last. If the run
queue empties before the root has ended, that's a deadlock: a line on
stderr and exit 1. Processes still waiting in `recv` once the root has
ended are dropped silently. (Timers and devices, step 10, will count as
"something can run".)

**D94. Pids.** *(Default.)* A pid is the process's index in the process
table, from 1 (the root), and prints as `#<pid N>`. The root's `^$$` is
`()`. Inside `(fork expr)`, `$$` is the child, since `expr` runs there;
to pass the parent along, bind it first (`(let me $$)`), as ts-slight's
`ring-benchmark` does.

**D95. A fork carries at most 8 locals.** *(Default.)* They become the
hidden entry function's parameters, and so registers (D47). More is a
compile error; put them in a list.

**D96. Process memory.** *(Default.)* Stacks are 8 MB, mapped lazily with
a guard page below, and pooled: a process takes one when it runs and gives
it back when it waits in `recv` or ends. The stack check (D48) keeps 64 KB
of headroom for a frame and a C call. A process struct (about 350 bytes)
is never freed yet; step 8 shrinks what's kept to the exit record (D88;
done in D102).

**D97. The actor examples.** *(Default, under D84.)* `ping-pong`,
`ring-benchmark` and `million-forks` are ported: a `(recv)` in the middle
of a function became a receive function, the `(yield ...)` around loops is
gone (preemption does that), and with no `join` or timing yet they print
as they go and use fixed sizes.

## Step 8

**D98. Fault kinds are keywords, named in `rt.h`.** *(User.)* A fault's
reason is `(kind value site)`: `kind` one of `:not-an-int`, `:overflow`,
`:not-a-bool`, `:no-clause`, `:stack`, `:not-a-cons`, `:not-a-list`,
`:heap`, `:not-a-string`, `:not-a-symbol`, `:out-of-range`,
`:not-a-number`, `:div-by-zero`, `:not-a-function`, `:arity`,
`:not-a-pid` or `:join-self`; `value` the offending value, or `()` for
the faults that have none (overflow, no clause, stack, heap, division by
zero); `site` a string, `"car at file:line:col"`. The runtime can't make
symbols (ids are fixed at compile time, D55), so the ones it needs (`ok`,
`error`, `exit`, `killed` and the kinds) get the ids after `#true` in
every program, and `rt.h` numbers them. `values.test.ts` checks the two
lists agree, reading the kinds from `rt.h`'s comments.

**D99. How the root's result shows.** *(User.)* `(:ok v)` prints `v`
when the program ends, and exits 0. A fault prints `fault: ...` as it
always has; `raise` or `kill` prints `error: <reason>`; either way the
program exits 1. Errors print when they happen, the value at the end
(D93), and other processes still run to the end.

**D100. Faults are logged in every process; `raise` and `kill` only in the
root.** *(User agreed to logging errors from other processes; Claude
narrowed it to faults, and the user confirmed that after step 8.)* A fault
is always a bug, so it's logged as `fault in #<pid N>: ...` even if
someone will `join` the process. `raise` and `kill` are the program's own
choices, and whoever joins or monitors sees them. Rejected: logging every
`(:error ...)` (a supervisor killing its workers would fill the log), and
logging nothing (a crash that nobody joins would be invisible).

**D101. The details of `join`, `monitor` and `kill`.** *(User.)*
`(join $$)` faults (`:join-self`), and so does giving any of them
something that isn't a pid (`:not-a-pid`). `kill` and `monitor` return
`()`. `(kill $$)` ends the caller, and killing a process that has ended
does nothing. Monitoring a process that has ended sends the message at
once; monitoring twice sends two. Joiners wake, and monitors hear, in the
order they asked. A root stuck in `join` is a deadlock too, and the
message says which pid it's waiting for.

**D102. Exit records are table entries.** *(Default.)* When a process
ends, its struct (about 350 bytes) is freed, and its pid's entry in the
process table (32 bytes) keeps the result: the value, and a chunk holding
a copy of it if it needs memory. `(:ok v)`, `(:error r)` and
`(:exit pid result)` are built when they're asked for, so a process that
ends with an immediate (`()`, an integer, a symbol) costs just its entry:
32 MB for `million-forks`. Replaces D96's "never freed".

**D103. A killed process is freed wherever it is.** *(Default.)* Nothing
on a process's stack holds a resource: compiled code holds none, and a C
builtin can't be preempted or killed partway (except `join`, which holds
none either). So `kill` takes the process out of the run queue or a
joiner list, frees its stack, heap and mail, and leaves its exit record,
whatever state it's in. Rejected: delivering the kill when the target
next runs, as Erlang's exit signals do (more states, and no gain on one
core).

**D104. More actor examples.** *(Default, under D84.)* `pub-sub`,
`even-odd-actors` and `fixed-tournament` are ported; `ping-pong` uses
`join` again, as the original does. `join` gives `(:ok value)`, so
`fixed-tournament` takes the `cadr`. A function that waits in `recv`
can't be a value, so `fixed-tournament` forks its game by name.
`ping-pong-tournament` waits for `sleep` (step 10).

## Step 9

**D105. Collection happens only at `recv`.** *(User.)* When a receive
function asks for its next message, before it takes one, the stack is
empty (the `recv` rule), so the roots are just its arguments, in its
frame; `rt_recv` already had them, to restart the function after waiting,
so the compiler didn't change. Every actor loop passes through there.
Rejected for now: also collecting at tail calls between state functions
(more compiler work for loops that reach `recv` soon anyway), and at
tail calls from the bottom of the stack in plain loops (a runtime `sp`
check). So the root, unless it ends in a receive function, and a process
that never waits for a message, never collect, and the 64 MB limit is
what stops them.

**D106. When to collect, and the limit.** *(User.)* Once the heap in use
(all its chunks, less what's free in the current one) reaches twice what
survived the last collection, and never below 256 KB, so a short-lived or
small process never collects. The limit stays 64 MB per process.

**D107. Copying with forwarding pointers and a work stack.** *(Default.)*
Each object is copied once and leaves a forwarding pointer behind (a moved
cell's car becomes a boxed null, a moved box's header 0: neither is
possible otherwise), so sharing is kept and a DAG can't blow up into a
tree. Cheney's algorithm scans to-space in order, but a cons cell has no
header to say what it is, so a stack of copied cells and closures whose
fields still need forwarding takes the scan's place; forwarding the cdr
before the car keeps it short along a list. To-space is fresh chunks of up
to 1 MB (or the old heap's size, if smaller), and allocation carries on in
the last one. Rejected: headers on cons cells (a cell would grow from 16
bytes to 32, with the alignment, everywhere, for the collector's sake),
and recursive copying (a deep structure would overflow the C stack).

**D108. The collector is tested by volume, and with poisoning.**
*(User agreed to testing without a heap-size builtin.)* Each GC test
allocates far more than 64 MB over a process's life, so it only passes if
garbage is collected: a store actor whose state is replaced on every
message, a DAG of 60 cells that is 2^30 as a tree, two players volleying
over 100 MB of messages each, and a handler that allocates 80 MB between
messages and faults. With `SLIGHT_POISON` set in the environment (as
`t/run.sh` sets it), the collector fills what it frees with garbage, so a
pointer it missed fails at once instead of reading memory that still looks
right; without it, a mutation that skipped a closure's captured values
passed. Rejected: a builtin to read the heap's size (more language), and
always poisoning (collection would cost the whole heap, not just what
survives).

## Step 10

**D109. Step 10 comes in three parts.** *(Default.)* 10a timers, 10b the
terminal, 10c files, each committed with its tests.

**D110. The virtual clock moves only when nothing can run.** *(User.)*
With `SLIGHT_CLOCK=virtual` in the environment (`t/run.sh` sets it), time
starts at 0, stands still while anything can run, and jumps straight to
the next timer when nothing can. Timer tests are exact, take no real time,
and don't depend on how much work the compiled code does. The cost: a
process that sleeps while others stay busy never wakes, so
`ping-pong-tournament` (players volley until a sleeping referee stops
them) spins forever under it, and is ported without a golden test (its
counts depend on the machine anyway). Rejected: also moving the clock 1 ms
per used-up quota of reductions (busy programs would see time pass, but
expected output would shift whenever codegen or the prelude changed).

**D111. `after` and `sleep` return `()`.** *(User.)* Neither can fail, so
a Result would always be `:ok`. DESIGN's "blocking calls return a Result"
now covers the ones that can fail: `slurp` gives `(:ok string)` or
`(:error reason)`, and `spew` `(:ok ())` or `(:error reason)` (10c).
Rejected: `(:ok ())` from `sleep`, for uniformity. (`slurp` and `spew`
became slight functions in 10c, and `slurp` gives lines: D129.)

**D112. A timer whose process has ended is dropped when it's due.**
*(User.)* An `after` aimed at a process that has ended, or the wake-up of
a sleeper that was killed, stays in the timer heap until it's due, and is
dropped then, as a message to an ended process is (D92). Pids aren't
reused, so a late timer can't reach the wrong process. Until then it
keeps the program running, which costs no real time on the virtual clock.
Rejected: cancelling a process's timers when it ends (the program would
end sooner, but each process would need a list of the timers aimed at
it, or the heap would be searched).

**D113. The runtime waits with `select()`, on macOS and Linux alike.**
*(User.)* One code path, and the only file descriptor to watch is stdin
(10b); until then it just times out at the next timer. Rejected: `kqueue`
on macOS and `poll` on Linux, as DESIGN had it (two code paths; and
macOS's `poll()` doesn't support terminals).

**D114. How timers behave.** *(Default.)* `(after ms pid msg)` and
`(sleep ms)` take an integer number of milliseconds (anything else faults
`:not-an-int`), and a negative one counts as 0. `after` copies the message
when it's set, as `send` does, so it outlives its sender. Timers are a
binary heap; those due at the same time fire in the order they were set.
A due timer fires at the scheduler's next decision, which is also when a
preempted process would give way: without that, a process running alone
keeps going past its quota (D91), and would hold up a timer for as long
as it ran. So `(sleep 0)` lets the processes already waiting to run go
first, as `yield` does. A pending timer is something that can still
happen: a root waiting for a message only a timer will send isn't
deadlocked, and the program doesn't end while a timer is pending.

**D115. `ping-pong-tournament` is ported, without a golden test.**
*(Default, under D84.)* Receive functions instead of a `(recv)`
mid-function; fixed sizes instead of `@ARGV` (revisited in D144: the
sizes come from `@ARGV` again, with defaults); no `time-it`; the player's
unused `max-delay` is gone. It runs on the real clock only (D110): ten
games of 100 ms each come to about 80,000 messages under qemu.

**D116. Golden tests feed keys with a `; stdin:` line.** *(User.)*
`:keypress` reads stdin whether or not it's a terminal (raw mode only
when it is, as ts-slight did), so a test can give it bytes. A line
`; stdin: bytes` in the test, written with printf `%b`'s escapes (`\033`
is ESC), is written to a file that `t/run.sh` gives the test as stdin;
without one, stdin is empty. Rejected: a `.input` file of raw bytes next to
the `.expected` (exact, but the bytes don't show in diffs or editors).

**D117. Ctrl-C ends the program, with exit status 130.** *(User.)* As
ts-slight did, and whether stdin is a terminal or not, so a test can check
it. The terminal is put back first, and programs never see the key. Raw
mode turns signals off, so without this `key-catcher` (no quit key)
couldn't be stopped. Rejected: delivering it as `("c" :ctrl)` like any
key (quitting would be every program's job), and leaving signals on in
raw mode (Ctrl-Z and Ctrl-\ would act too, and no test could check it).

**D118. When stdin ends, the keys just stop.** *(User.)* Connected
processes get no more keys, and stdin stops counting as something that
can still happen; if nothing else can, a root still waiting is
deadlocked, as usual. A terminal never ends (Ctrl-D is a key); only a
pipe or a file does. Rejected: sending each connected process `(:eof)`
(a new message that only piped input would ever produce).

**D119. Every process connected to `:keypress` gets every key.**
*(User.)* In the order they connected, as in ts-slight, where each
`connect` added a listener. Raw mode is on while any of them is alive.
Rejected: the newest connection taking the keyboard from the others (more
state, and the order processes connect in starts to matter).

**D120. Keys.** *(Default, after ts-slight.)* A key is `(key mods...)`.
`key` is a string for a printable key (one UTF-8 character), or a name:
`:ArrowUp` `:ArrowDown` `:ArrowRight` `:ArrowLeft` `:Home` `:End` `:Insert`
`:Delete` `:PageUp` `:PageDown` `:Enter` `:Escape` `:Backspace` `:Tab`
`:F1`–`:F12`, or `:Unidentified`. The modifiers come in the order `:ctrl
:alt :shift`. Ctrl and a letter is the letter with `:ctrl`; an upper-case
letter comes with `:shift`, as readline gave it; ESC before a key means
`:alt`, and so do xterm's modifier parameters (`ESC [ 1 ; 5 C` is
`(:ArrowRight :ctrl)`). `\r` and `\n` are both `:Enter` (ts-slight made a
piped `\n` `:Unidentified`). An escape sequence has to arrive in one read,
as terminals send them, so an ESC at the end of what has been read is the
Escape key. The runtime decodes keys in C (`runtime/tty.c`); the names are
runtime symbols, so every program has 30 more.

**D121. Raw mode is Node's.** *(Default.)* As libuv sets it: no echo, no
line buffering, no signals, but output is still processed, so `\n` still
starts a new line and `pprint` works as before. The terminal is put back
when the last connected process ends, and at exit (not if the program is
killed by a signal). `tty/screen/rows` and `tty/screen/cols` ask the
terminal each time, and give 24 and 80 when stdout isn't one.

**D122. When keys are read.** *(Default.)* When nothing can run, the
scheduler waits in `select()` on stdin and the next timer; while processes
are busy, it looks at stdin every 10 ms, at a scheduling decision or a
preemption, so a busy process can't hold keys up. On the virtual clock,
keys come only when nothing can run, one at a time, before the clock
moves: as if typed by someone who waits for the program to settle before
each key. (Proposed as "all the input at once, the first time nothing can
run"; changed while building, since then a test's keys would all be in
the mailbox before any was handled, and a Ctrl-C at the end would end the
program before any of them ran.) The program ends when nothing can run,
no timer is pending, and no process is connected to `:keypress` or stdin
has ended.

**D123. The device examples.** *(Default, under D84.)* `key-catcher`,
`divisions` and `tail-chase-game` are ported, with golden tests whose
expected output comes from small models in `t/models/`. A `(recv)`
mid-function became a receive function, and `case`/`if`/`when` became
`cond`. `key-catcher` compared a key with the integer `1`, so its colour
keys never worked; it compares strings now. The window managers wait for
step 11.

## Step 10c: files

**D124. Files are devices, opened with `connect`.** *(User.)* Stevan's
plan from ts-slight, where files were to use Node's events: open and close
become `connect` and `disconnect`, and the network will work the same way.
`(connect :fs/read path expr)` forks `expr` and opens `path` on a device:
a pid that the runtime serves instead of compiled code. Its messages to
the new process (its owner) carry it; anyone writes to it with `send`;
`disconnect`, or the owner ending, closes it. Erlang's ports have the
same shape. Rejected: `slurp` and `spew` as builtins, as planned (D111):
blocking calls that hold a whole file in memory, and nothing for a
stream or a socket.

**D125. A device's first message is `(:open f)`.** *(User.)* So the owner
learns the device's pid before anything else, at the cost of one clause.
Rejected: `connect` binding a name, `(connect :fs/read path f (reader
f))` (no extra message, but how many arguments `connect` takes would
depend on the source).

**D126. `:fs/read`, `:fs/write` and `:fs/append`.** *(User, "for
symmetry"; Claude had proposed `:fs` for reading.)* `:fs/write` creates
the file or empties it; `:fs/append` creates it or adds to it.

**D127. A reader sends lines, one at a time.** *(User.)* `(:line f s)`,
split on `\n` alone and without it (a `\r` stays), and a last line
without a newline still comes; then `(:eof f)`, and the device closes.
The next line is read when the owner takes the last (the message carries
its device, and taking it reads on), so at most one line is ever in the
mailbox: a reader that disconnects still gets the one already on its way
(golden test 145). Rejected: reading the whole file into the mailbox at
once (memory, for a big file), and raw chunks (lines are what programs
want; chunks can come if something needs bytes). (Changed by D158: the
next line is cut at the owner's next `recv`, so a reader that disconnects
gets nothing more; and D159 adds chunks.)

**D128. A failure ends the owner.** *(User.)* A file that can't be opened
ends its owner before it runs, with `(:error (name path))`; a read or a
write that fails ends it then. So a reader needs no error clause, and
`(join (connect ...))` is a Result. `name` is errno's, as one of 14
runtime symbols (`:enoent :eacces :eperm :eexist :eisdir :enotdir
:enametoolong :eloop :erofs :enospc :efbig :emfile :enfile :eio`), or
`:io-error` for anything else. A directory opens, and fails with
`:eisdir` when its owner takes `(:open f)` (which reads the first line),
before that clause runs. Not logged, as `raise` isn't (D100): an I/O
error isn't a bug. (Since D158, a directory fails at the owner's first
`recv` after `(:open f)`, so that clause runs.)

**D129. `slurp` and `spew` are slight, in `lib/fs.slight`.** *(User:
opt-in, since "not everything needs filesystem access".)* A few lines
each, on `connect` and `join`; a golden test asks for them with
`; with: lib/fs.slight`. `slurp` gives `(:ok lines)`, not D111's
`(:ok string)` *(Claude, following D127: a file comes as lines, and
ts-slight's text editor split `slurp`'s string into lines at once;
`str-join` gives the string back)*. `spew` writes each line and a
newline, and gives `(:ok ())`.

**D130. The details of devices.** *(Default.)*
- A write renders its arguments as `tty/write` does, and goes out at once
  (writing a regular file doesn't wait).
- Anything sent to a device but a writer's `(:write ...)` is a dead
  letter, logged with the `connect`'s site. Anything sent after it has
  closed goes nowhere, as to an ended process (D92). An `after` aimed at
  a device works as `send` does.
- `disconnect` ignores anything that isn't an open device (a process, a
  closed device); something that isn't a pid faults `:not-a-pid`.
- To `join`, `monitor` and `kill`, a device looks like a process that has
  ended with `(:ok ())`.
- A device's pid is the one after its owner's.
- A file is read when its owner takes a line, so a reader is never waited
  for in `select()`, and doesn't count as something that can still
  happen. A file that can keep a read waiting (a FIFO, a terminal) waits
  with the whole runtime; sockets will be waited for (D132).
- A path with a NUL in it is `:io-error`.
- Golden test 149 opens a file 25,000 times, which runs out of file
  descriptors unless an owner's ending closes its files.

**D131. `:keypress` stays as it is.** *(User.)* Keys aren't wrapped as a
file's messages are: the keyboard can't fail, takes nothing, and is shared
by every connected process (D119), so there's no device to name. "It is
okay to look different, if looking the same would look strange."

**D132. The network: TCP in the runtime, HTTP in slight.** *(User.)* For
a later step: `:tcp` and `:tcp/listen` devices, waited for in the
runtime's `select()`; an accepted connection is a new device, which
`(connect conn expr)` hands to a process of its own; HTTP is a slight
library on top. Rejected: HTTP in C, which `(connect :http url ...)` (as
Stevan first sketched it) would need, since `connect`'s sources are the
runtime's and a library can't add one: more C, and HTTPS would need a
TLS library either way.

## Step 10d: the network

**D133. Sockets are devices, as files are.** *(User; D124, D132.)*
`(connect :tcp "host:port" expr)` connects and `(connect :tcp/listen port
expr)` listens, each on a device the new process owns. A connection's
first message is `(:open c)`, once it has connected; then `(:line c s)`
and `(:eof c)`, and it takes `(:write x ...)`, as a file does. A
listener's are `(:open l port)` and `(:accept l conn)`.

**D134. Lines for now.** *(User: "just lines for now, and move to chunks
later, just like with :fs".)* A connection is read only while none of its
lines is in its owner's mailbox, so a sender faster than its reader is
held back by TCP itself. That isn't checked by a golden test: seeing it
would take timers and sockets together, which don't give the same order
every run (D139); files share the mechanism, and test 145 checks theirs.
An HTTP request body that doesn't end in a newline will need chunks.
(They came in D159; D158 keeps this backpressure.)

**D135. Writes are buffered, and closing flushes.** *(User: "ideally we
buffer the writes".)* A `(:write ...)` goes out at once if the socket
takes it; the rest waits till it can, and `send` never blocks. Like a
mailbox (D88), the buffer has no limit. `disconnect`, or the owner
ending, closes a connection only once what it has is written, since a
handler that writes its reply and ends at once is the common case; till
then the socket keeps the program running (golden test 153 writes 5 MB
and ends at once). Rejected: an error when the socket is full, with a
`:ready` message to say when to write again.

**D136. Errno's names, and `:enotfound`.** *(User: "standard error
names".)* `:econnrefused :econnreset :epipe :etimedout :eaddrinuse
:eaddrnotavail :ehostunreach :enetunreach` join the file system's
(D128), and `:enotfound` (Node's name; a failed host lookup has no errno)
is a host that can't be found, or an address without a colon. SIGPIPE is
ignored, so writing to a closed connection is `:epipe`, not the end of
the program.

**D137. A listener's first message carries its port.** *(User, once
Claude explained the question.)* A golden test can't reach any server
outside, so it runs a server and a client in one program, talking over
127.0.0.1; and since a fixed port can be taken, or linger after a test, it
listens on port 0, which lets the system pick. `(:open l port)` is how the
program learns the port.

**D138. `(connect dev expr)` hands a device to a new process.** *(User.)*
`connect` given anything but a `:keyword` takes it to be a device: the new
process owns it from then on, and hears `(:open f)` (or `(:open l port)`)
first. An accepted connection belongs to the listener's owner, and reads
nothing, till it's handed over (golden test 154). *(Default:)* anything
that isn't an open device faults `:not-a-device`, a new fault kind, and
something that isn't a pid `:not-a-pid`; any process can hand over a
device, not just its owner; and handing over one that's already being
read can leave a line with the old owner, so hand a device over before
reading from it. (No longer needed since D158: nothing is cut ahead.)

**D139. The details of sockets.** *(Default.)*
- IPv4 only. A listener listens on all interfaces, with `SO_REUSEADDR`
  (so a restarted server can have its port back at once) and a backlog of
  128. Connections have `TCP_NODELAY`: messages are small, and waiting to
  fill a packet only delays them.
- Connecting doesn't stall the runtime, but looking up the host does,
  briefly, as reading a FIFO does (D130).
- `(:open c)` always comes from the event loop, even when `connect()`
  has finished at once, so it comes in the same order either way.
- After `(:eof c)` a connection stays open, and can be written to.
- A connection's "where", in an error, is the address given, or the other
  end's "address:port" for an accepted one; a listener's is its port.
- Anything sent to a listener is a dead letter.
- On the virtual clock, sockets are looked at only when nothing can run:
  first, without waiting; then a key comes, or the clock moves; only when
  nothing else can happen does the runtime wait for real. So a test whose
  every step follows from a message comes out the same every run; one that
  mixes timers with sockets may not.
- `select()` can't watch a file descriptor of 1024 or more, so a socket
  that would get one is refused with `:emfile`.

**D140. HTTP next, as `lib/http.slight`.** *(Default plan, under D132.)*
A client and a server in slight, on `:tcp`, opt-in like `lib/fs.slight`.
Until connections can send chunks (D134), a request body that doesn't end
in a newline can't be read.

## Includes, and the expanded forms

**D141. `@include`.** *(User.)* `(@include "path/file.slight")` splices in
another file's forms where it stands, and `(@include :name)` a built-in
subsystem, `lib/name.slight` (which may include others). Every program
starts with an unwritten `(@include :prelude)`. Not a module system: a
simple way to pull in what a program needs, now that files and the
network are opt-in; modules can come with separate compilation, if it
does. *(Default:)*
- A path is relative to the file the `@include` is in (or absolute), and
  positions name an included file by that path (`t/data/include/b.slight`),
  or a built-in as `lib/name.slight`.
- Only at the top level, where a program's definitions are.
- A file is included once however often it's asked for, known by its real
  path; one that's already in isn't read again. A file that includes
  itself, directly or not, is an error that names the chain.
- The prelude keeps its own namespace (D80): its definitions stay apart,
  so a program can still define `range`. Other included files are part of
  the program, as before.
- `; with:` in golden tests is gone: a test says `(@include :test)`.
  `slightc` still takes several files, expanded in turn, sharing what's
  been included.

**D142. `if`, `when`, `case`, `and` and `or` are back, as `cond`.** *(User;
D12 was decided when they would have needed macros.)* The expander makes
them into `cond` before compiling, as ts-slight's expander made them into
`if`, so the compiler, the `recv` rule and the tail calls see only `cond`.
`(if test then else)`, and `()` without an else; `when`, `()` when the
test is `#false`; `case`, as ts-slight's: the topic is evaluated once and
compared with each clause's value by `eq?`, a `#true` clause is the
default, and without one, nothing matching gives `()`. Its topic is bound
to a name with spaces in it, which the reader can't produce, so no
runtime `gensym` is needed and no program can clash with it. `and` and
`or` stop at the operand that decides *(User: "short-circuit")*, and, as
in Scheme, give the last operand as it is, untested, if they get to it
*(User, after Claude first had every operand tested)*: `(and #true 5)` is
5, while a non-boolean before the last faults as any `cond` test does.
*(Default:)* `not` stays a prelude function; none of the five names can be
bound or defined. A non-boolean test faults as `cond test at ...`, pointing at the
test. Rejected: `unless`, and `case`'s `else` (`#true` does it).

**D143. The expander is a pass of its own, after the reader.** *(Default;
Stevan described this as the reader's job.)* `compiler/src/expand.ts`
takes what the reader read, splices in includes (through a loader the
driver gives it, so the pass itself does no I/O) and expands the five
forms; the reader stays text to s-expressions. It knows just enough of
the special forms to leave alone what isn't an expression: quoted data,
`defun`'s and `lambda`'s parameters, and `recv`'s patterns.

**D144. `@ARGV` is the top level's parameter.** *(User, after Claude
proposed `@ARGV` readable anywhere and then this.)* The program's
arguments after its name come as a list of strings, `()` with none. The
top-level forms are compiled as the body of `slight_main`, so `@ARGV` is
that function's one parameter: the runtime's `main` builds the list in
the root's heap and starts the root with it, as `fork` starts a process
with its copied values. Only the top level sees it; a `defun` that names
it is a compile error saying to pass it on, as a top-level `let` isn't
seen there either, and a lambda or a `fork` at the top level captures it
as any local. *(Default:)* the name is ts-slight's, and the `@` keeps it
from clashing with a program's names; it can't be bound or defined.
Strings, not numbers: ts-slight's examples used `(car @ARGV)` as a number,
but guessing what an argument is meant to be is the program's business
(`string->int`). Golden tests pass arguments with a line `; args: words`.
The ported examples that had `@ARGV` take their sizes from it again, with
the fixed sizes as defaults. Rejected: `@ARGV` readable anywhere, like
`$$` (a hidden global, and each read would have to build a copy in the
reading process's heap, since nothing may point from one heap into
another); and a `(defun main (args) ...)` the compiler looks for (a
second shape of program, and a name treated specially).

**D145. x86-64 is a second target, behind an interface of code shapes.**
*(User asked for the x86-64 backend; the shape is the plan in
BACKGROUND.md, "Other targets".)* `codegen.ts` keeps the structure (what
to emit, and where values live: the accumulator, a binary operation's
left operand, frame slots) and asks a target (`target.ts`) for the
instructions of each shape of code: the prologue and its checks, a call
with its operands, loads and stores, tests that branch, booleans,
arithmetic, allocation, and filling a cell or a closure. `placeArgs`
orders a call's moves into argument registers so none is overwritten
before it's read, breaking a cycle through a free register; on AArch64
the accumulator is the first argument register and on x86-64 it isn't,
and this is what absorbs that. `aarch64.ts` holds the AArch64
instructions the generator used to emit inline: the generated assembly
of every golden test, example and sketch (150 programs) came out the same
byte for byte. `x86_64.ts` is about 250 lines, in Intel syntax (whose
operand order matches AArch64's), with the register map settled next to
D47: `rax` the accumulator, the result and the closure at entry; `rcx`
the left operand; `rdx` what was just allocated; `rdi`, `rsi`, `rdx`,
`rcx`, `r8`, `r9` (System V's) then `r10`, `r11` the arguments; `r15` the
process; `rbx` and `r12`–`r14` unused. The runtime's half is
`rt_asm_x86_64.S` (`rt_switch` keeps `rbx`, `rbp`, `r12`–`r15` and `rsp`;
`start` puts `rt_trampoline`'s address on the new stack for `ret`;
`rt_apply` checks the whole list before spreading it, since the spread
takes every caller-saved register) and `rt_ctx_t` per architecture. The
language is the same on both, limits included (8 arguments, a frame's
4,095 bytes), and so is every golden test's expected output. *(Default:)*
Rejected: a virtual instruction set that each target prints (more
machinery than the shapes, for no gain with two targets); compiling to C
(BACKGROUND.md, "Compiling to C instead": it would replace the emitting
half rather than add a target, and is still possible before step 12);
AT&T syntax (its operand order is the reverse of AArch64's, so the two
targets would read backwards side by side).

**D146. Choosing a target, and testing both.** *(Default.)* `slightc
--target aarch64|x86_64`, AArch64 by default on every machine: it's the
project's target, and the M2 is where slight is used. It links natively
when the target is the machine's architecture; otherwise it
cross-compiles, with `cc -arch` on macOS (an x86-64 binary runs under
Rosetta 2) and for Linux with clang and lld elsewhere. `t/run.sh` takes
`TARGET`, and `make test` runs the golden tests for `TARGETS`: both on an
x86-64 machine (x86-64 natively, AArch64 under qemu), AArch64 on arm64
(`make golden TARGETS=x86_64` runs x86-64 under Rosetta). The runtime's
assembly is named per architecture: `asm_aarch64.h` and
`rt_asm_aarch64.S` (were `asm.h` and `rt_asm.S`), `asm_x86_64.h` and
`rt_asm_x86_64.S`. Both headers mark the stack non-executable on ELF
(`.note.GNU-stack`): a native Linux link uses GNU ld, which otherwise
warns and makes the stack executable (lld, used for the cross-compiles,
didn't need it). Both targets pass on the M2 (Stevan, Oct 2026),
x86-64 under Rosetta 2. Rejected: defaulting to the machine's
architecture (an x86-64 machine would quietly stop testing AArch64).

**D147. No walk over a value recurses in C.** *(User, choosing it over a
limit.)* Printing, copying (a message, a fork's values, a result through
`join` or a monitor) and `eq?` recursed down a list's cars, so a value
nested some 50,000 deep crashed the whole program with a segfault, not a
fault that ends its process, at a depth that depended on the size of a C
frame: about 52,000 levels for printing on x86-64 and 58,000 on AArch64,
and the other way round for copying. Deep values aren't common, but
they're easy to make (a left fold of `(list acc x)`, a degenerate tree,
a chain of closures each capturing the last) and, once HTTP lands, to be
sent from outside. Now each keeps a work stack, `rt_work_t`, as the
collector already did (D107), which it now shares: its first 32 words are
in the struct, on the C stack, so a shallow walk never calls `malloc`.
Each walk goes along a list's cdrs, deals with an element that's an atom
(or, copying, a string or a float) on the spot, and pushes the rest of the
list only to go down into a nested list or closure, so the stack grows
with nesting, not length, and a flat list never touches it. A value can
nest as deep as the heap allows, the same on every target. Sending a list
of 10 to 60 elements is 13–32% faster than before, since the old copier
recursed for every element, and printing and `eq?` are as fast as before.
(The first version pushed for every element, and was 20–33% slower at
sending; the review that followed caught it.)
Rejected: checking the C stack against the process's limit and faulting
with `:stack` (cheaper, but the limit would still depend on the target,
on the compiler, and on how deep the program's own stack was, and a fault
in the middle of printing or copying would have to clean up after
itself); a fixed limit on nesting, counted in levels, with a fault of its
own (the same on every target, but a rule of the language about what can
be sent, in a language where sending is everything; Erlang's runtime has
no such limit either).

**D148. UTF-8: a second set of string builtins, counting characters.**
*(User asked for enough to write a JSON parser and printer, with byte and
UTF-8 builtins side by side; the names `ord` and `chr`, calling the
`utf8/` builtins, are the user's too.)* Part of D27's level 2, without display
width. The byte builtins don't change. New, in C: `(utf8/len s)`,
`(utf8/substring s start end)` (clamped and swapped as `substring` is),
`(utf8/index-of s m)`, `(utf8/chars s)` (one-character strings, which
`str-join` puts back together), `(utf8/code s)` (the first character's
code point), `(utf8/char n)` and `(utf8/valid? s)`; and in the prelude,
`ord` and `chr`, which call `utf8/code` and `utf8/char`, as Perl's do.
Nothing else needed to change: output writes a string's bytes as they
are, string literals were already UTF-8, and the byte builtins that don't
count (`concat`, `~`, `str-join`, `eq?`, `starts-with?`, `ends-with?`,
and `str-split` on a separator that isn't empty) are right for UTF-8 as
they are, since a well-formed sequence can't match in the middle of a
character.

A byte that doesn't start a well-formed sequence (Unicode's table 3-7:
shortest form, no surrogates, nothing past U+10FFFF) is a character of
its own, as in Go: it counts 1, `utf8/chars` keeps it as it was, and its
code point is U+FFFD. So nothing faults on bad input, nothing is lost, and
a parser that must reject bad input asks `utf8/valid?`. `utf8/index-of`
finds only a match that starts a character, which matters only when `m`
starts with a stray byte. `utf8/code` faults with `:out-of-range` on `""`
(Perl's `ord` gives 0, a real character), and `utf8/char` on a surrogate
or past U+10FFFF, which have no UTF-8; the reader now refuses
`\u{D800}`–`\u{DFFF}` too, which it used to turn into U+FFFD without a
word.

JSON's numbers needed `(string->float s)`: `-?D+(.D+)?([eE][+-]?D+)?` as
the nearest float, or `#false` (also for a number too big for a double;
one too small for any comes out as 0, as the reader's literals do).
`strtod` rounds correctly on glibc and macOS alike, so it gives the same
float as the literal, which a float built from its digits in slight
couldn't promise, `pow` least of all (D76). An integer stays
`string->int`'s.

Checked against Go's `unicode/utf8` on 3,200 random strings mixing every
kind of bad byte, on both targets; of a dozen mutations of the decoder,
the two that went unnoticed change nothing it does (one reads the NUL
after the string, which is never a continuation byte). `t/168-json.slight` is a JSON parser and
printer in slight, on these builtins, checked against Python's `json`
module (`t/models/json-round-trip.py`). The cost to know about:
`utf8/chars` takes 32 bytes a character (a string and a cell), and the
root never collects, so a parse there runs out of heap at about a 2 MB
text. Sharing static strings for the ASCII characters would halve that;
left until it matters.
Rejected: renaming the byte builtins `bytes/...` to match (it churns every
program, for no gain); U+FFFD in place of a bad byte in `utf8/chars`
(loses the byte); faulting on bad bytes (harsh on a file you didn't
write); WHATWG's rule of one U+FFFD for a whole cut-short sequence (it
differs only on bad input, and needs more machinery); a `string->number`
giving an integer or a float (`string->int` already does the one);
display width, case mapping beyond ASCII and normalization (not needed
for JSON; `pad-start`, `pad-end` and `format-num` still count bytes).

**D149. The plan after 10e: porting stops, and HTTP's groundwork comes
first.** *(User, Oct 2026.)* Fifteen of ts-slight's examples are ported,
enough to have tried the language on; new examples are written for this
version, and the text editor and window managers leave the plan (their
display width stays, under 15). Next is tooling (11), so that every step
after it gets a quicker loop; then what HTTP needs underneath it (12):
chunks, collecting outside `recv` ("it will bite in HTTP"), and C
libraries, starting with a discussion of what belongs in C (TLS, hashing
and crypto for certain; JSON, database drivers and parts of HTTP to
decide), then TLS; then where `recv` can go (13), since HTTP will be the
biggest library yet in slight; then HTTP (14, was 10f). The other things
left "for now" wait until something needs them (15). Self-hosting moves
to the end (16, was 12), once the language has settled. WebAssembly,
compiling to C, RISC-V, 32-bit microcontrollers and multiple cores stay
parked, in three groups that later ideas can join: new compilation
targets, new platforms, and parallelism.

## Step 11: tooling

**D150. The runtime is compiled once and kept; the golden tests run in
parallel.** *(Default, in step 11, which Stevan put first, D149.)*
`make test` took 7.5 minutes, nearly all of it clang compiling the
runtime again for each of the 154 programs on each target (D38).
`slightc` now compiles it once into `build/runtime/`, in a directory
named for a hash of the compiler command, the flags and every file in
`runtime/`, and links each program against those objects. So an edit to
the runtime, or another compiler, gets a build of its own, and nothing
goes stale, which was D38's worry about an archive. Compiles that start
at once each build in a directory of their own and rename it into
place, the first rename winning; `slightc --runtime` builds it alone,
which `t/run.sh` does before starting the tests. Old builds stay until
`make clean` (about 300 KB each). `t/run.sh` runs `JOBS` tests at once
(one per CPU unless set), with `xargs -P`: each says `ok` or `FAIL` as
it finishes, and what each failure printed comes after, in order, with a
count; a test the watchdog kills now says so in its diff. The tests
needed nothing for it: each writes files of its own, and listens on port
0. `make test` now takes 46 seconds here, building the runtime included.
The same step made two recursions over a list into loops (D39), since a
long body ran Node out of stack at about 1,000 `let`s: `compileBody`,
over a body's forms, and the expander's `mapList`. Every program
compiles to the same assembly as before, byte for byte. A function is
still limited to about 500 locals, by its frame (4,095 bytes, the most
AArch64's `sub` takes in one instruction), and says so in a compile
error. Rejected: a prebuilt archive made by `make` (it can go stale, and
`slightc` alone, outside `make`, wouldn't use it); running the targets'
tests at the same time too (the file tests share names across targets,
and the CPUs are already busy).

## Step 12: what goes in C

**D151. A C library comes in one of two shapes, by what it does.** *(User
agreed, Oct 2026; refines D30.)* A pure function whose time is bounded by
its input (a hash, a parser given a whole text) may be a plain builtin,
called on the process's stack as `strings.c`'s are: it can't stall
anything as long as it never blocks. Anything with state or I/O (TLS, a
database connection) is a device, as files and sockets are: its state
lives in the runtime behind a pid, it works inside `select()` through a
non-blocking API, it ends with `disconnect` or its owner, and it runs on
the scheduler's stack. Either way, values cross by copying, and a slight
value never holds a C pointer (the collector moves values, and a
message's copy would carry the pointer to another process); C's own
memory is outside the 64 MB limit, so a call frees what it allocates; a
C error is a fault or a Result, but a crash in C ends the whole program.
A builtin has only the 64 KB that the check at function entry keeps
below the stack's limit (`STACK_HEADROOM`), so one that needs more must
run on the scheduler's stack. Not decided: a worker thread for a library
that can only block (SQLite), which would be the runtime's first thread.

**D152. C libraries are vendored as source, as Odin's are, and linked
statically.** *(User, Oct 2026, after a look at Odin's `vendor:`
collection; BACKGROUND.md.)* `lib/` holds the libraries written in
slight, our `core:`; `vendor/` will hold the C ones, each its upstream
source at a pinned version, with its licence and a `VENDOR` file saying
where and when it came from. Odin's rule: slight where that's practical,
C where it isn't (crypto isn't, in slight: no fixed-width integers, no
bit operations, no constant time). `slightc` compiles a vendored library
as it compiles the runtime (D150), once per target and version, kept
under `build/`, and links it only into a program that uses one of its
builtins or devices, which the compiler knows: no new syntax, no build
step, and the same code under qemu and Rosetta as natively. (Odin needs
build scripts, and a `#panic` when they haven't been run, because it
doesn't drive a C compiler; `slightc` does.) Static, so a binary is one
file (a Raspberry Pi takes it as it is), with the same library on every
machine and the same test output; the cost is a rebuild for a library's
security fix, which for TLS means bumping the vendored copy. TLS is to be
a `:tls` socket device, so encrypted bytes never reach slight and HTTP
treats `:tcp` and `:tls` alike; mbedTLS (Apache-2.0, TLS 1.3,
non-blocking, made for small machines) is the proposal, to confirm in
12c with where a client finds its CA certificates (macOS keeps them in
the Keychain). Rejected: system libraries (macOS has no OpenSSL to build
against, versions differ between machines, and testing under qemu would
need each target's copy); prebuilt binaries in the repository; loading
libraries with `dlopen`; a package manager.

**D153. The runtime looks up host names itself.** *(User, Oct 2026, after
Odin's `core:net`.)* `getaddrinfo` blocks, and every process with it
(D136). As Odin does, the runtime will read `/etc/hosts`, send its own
query over UDP to the servers in `/etc/resolv.conf`, and wait for the
answer in `select()` like any socket. That also takes away the main
reason for a worker thread. Caveat: macOS generates `/etc/resolv.conf`,
and it can miss per-interface and VPN settings.

**D154. Parsing in C: JSON and s-expressions, as builtins and as ways to
read a device.** *(User, Oct 2026: "most common parsing cases get done in
C instead of slight".)* A device that delivers bytes (a file, a socket,
later TLS) chooses how to cut them into messages: lines (now), chunks
(12a), JSON values, or s-expression forms. With the last two, the bytes
go from the file descriptor through a parser in C to finished values,
each top-level value a message, `(:json f v)` or `(:sexp f v)`, and JSON
has an "items" mode as well, a message for each element of a top-level
array, the usual big file. As with lines (D134), the device parses the
next value only once its last one has left the owner's mailbox, so an
owner that collects at each `recv` gets through any size of input in a
bounded heap. For text already in hand there are builtins: `json/parse`,
`json/print`, `sexp/parse` and `sexp/print`. One push parser for each
grammar serves both, written for slight in C (about 500 lines for JSON,
and 300 more for s-expressions, which share strings, numbers, UTF-8 and
building values straight into a message's chunk). It's tested against
JSONTestSuite (its `y_` files must parse and its `n_` files must not),
against Python's `json` as `t/168` is, and with inputs cut at random
places, escapes and UTF-8 sequences included. JSON in slight is as
`t/168` has it: `:null`, `#true` and `#false`, integers (floats past 63
bits, as in JavaScript), strings, lists for arrays, and `(:object (key
value) ...)` for objects, keys as strings, in order, duplicates kept.
`json/print` faults on what JSON can't hold: another symbol, a pid, a
closure, `nan`, `inf`. Rejected: simdjson (C++17, 13.7 MB of
single-header source, a whole padded document in memory and no feeding
it bytes as they come, and its speed would go on building slight values
anyway); yyjson for streaming (its incremental reader only resumes as
bytes arrive: the whole document still lands in one buffer and comes out
at the end; it stays the fallback for `json/parse` if ours is slow);
YAJL (a streaming parser, but unmaintained for some ten years); raw
parse events as messages (many more messages, and slight would rebuild
the values anyway). (How it was built: D162.)

**D155. S-expressions as data: symbols as D14 has them, and a printer
that reads back.** *(User.)* The data reader takes lists, integers and
floats, strings with the reader's escapes, symbols and keywords, `#true`
and `#false`, `'x` as `(quote x)`, and comments; no positions and no
`@include`. A symbol the program mentions reads as itself; one it
doesn't reads as `(:symbol "name")`. That keeps D14 (symbols are
compile-time ids), keeps the name, and fills no table, and a program can
only compare or match symbols it mentions anyway. `:a` and `a` read as
the same symbol (D52), not as `:a` in a quoted list reads to the
compiler. `sexp/print` is the printer that reads back: strings escaped
as the reader takes them, floats in the shortest form that reads back
the same, `(:symbol "name")` as `name`, and a fault for what isn't data
(a pid, a closure, `nan`, `inf`). `pprint` stays as it is, unescaped as
in ts-slight. So two slight programs can talk over TCP in printed forms.
Later, the self-hosted compiler could read its source with this reader,
keeping positions and giving every symbol as a record (D37). Rejected:
interning symbols at run time (it reopens D14, and the table only grows:
Erlang's atom table, filled by untrusted input, brings down the whole
node); an error on an unknown symbol (it would fail on well-formed
text). (How it was built: D163.)

**D156. Documentation inline, as Perl's POD: doc blocks, and a `:source`
device.** *(User, Oct 2026: "a specified and structured format that can
be ignored by the compiler, but read as a stream".)* A line that starts
with `=` and a letter, between top-level forms, begins a doc block, and
a line `=cut` ends it (or the end of the file does, as in POD); the
compiler skips it, so docs never change what a program means. Only
between top-level forms, so nothing inside a form is ever taken for one,
and only `=` and a letter, so a bare `==` isn't. `=doc` is the one kind
for now; any other `=word` is kept for later kinds, such as `=example`
blocks that could run as tests (as Python's doctests do). Inside, a
subset of Markdown that reads well as plain text: headings (`#`, `##`,
`###`), paragraphs, `*emphasis*` and `**strong**`, `` `code` ``, fenced
code blocks, lists (`-` and `1.`), and `[links](url)`; no HTML and no
tables. A doc block comes out as its raw text at first; parsing the
Markdown into s-expressions, for a renderer, can come later (md4c, which
is C, MIT, CommonMark-compliant and reports what it meets as it goes,
would be the one to vendor). `:source` is one more way to read a file
(D154): where `:sexp` skips doc blocks as the compiler does, `:source`
gives everything in file order, `(:form f form line)` and `(:doc f text
line)`, with the line each starts on; forms follow D155, and a form's
raw text instead is an option, for a pretty printer or a doc tool that
shows code. So a `slightdoc` written in slight could render `lib/`'s
docs, and the prelude could document itself. Rejected: Lisp docstrings
(a string first in a `defun` documents a function, not a file, and isn't
structured); `#| ... |#` block comments (a stream can't pick them out
without reading the code around them, where POD's markers are lines).

**D157. Data structures as processes: `lib/ds.slight`.** *(User asked for
the library; its conventions are defaults.)* DESIGN.md's first goal,
mutability from actors that keep their state in a loop's arguments, as
a library in slight, with no change to the language: a cell, a
dictionary, a queue and a channel, each a process, plus `ask` and
`reply` for structures of one's own. Writes are sends, and return at
once; reads ask. `ask` forks a box for the answer, which monitors the
structure, and `join`s it: `join` keeps the caller's stack, so `ask`
works anywhere, and each question has an address of its own, so a late
or stray answer can't be taken for it. `ask` gives the answer, and
raises `(:ended pid result)` if the structure ends first, or
`(:timeout pid)` from `ask-within`; as Stevan's sketch `ask?` did, it
raises rather than giving a Result, since a structure going away is a
bug, not an outcome to handle. A structure answers with `(reply to v)`,
which sends `(:reply v)`, so an answer can't be mistaken for the box's
`(:exit ...)` notice. Updates take a function, which runs in the
structure, so a read-modify-write is atomic. Rejected for now: a reply
box in the runtime (a fork per question is cheap enough until a program
says otherwise); a faster dictionary (a balanced tree in slight, or a
table in C behind a device, D151, when one is needed).

## Step 12a: ways to read a device

**D158. A device cuts its next message when its owner next waits.**
*(User chose this, "B", over a device that sends one message per
request.)* Until now, taking a device's message read on at once, so the
next line was cut before the owner's clause ran: an HTTP server that
learned from the blank line after the headers that the body was 512
bytes would find the body's start already cut as a line. Now a reader
keeps a way of reading (lines, at first), and the runtime cuts its next
message when the owner next calls `recv` after taking the last one. So
whatever the owner does while it handles a message, `(:open f)`
included, applies to the next one: changing the way of reading,
disconnecting, handing the device over. Line readers don't change, and
D134's backpressure stays: at most one of a device's messages is in its
owner's mailbox or being handled. This is Erlang's `{active, once}`,
re-armed by each `recv` instead of by a call. What it changes:
- A message another process sends while the owner is busy now comes
  before the device's next one, not after. Nothing promised that order.
- A reader that disconnects gets nothing more; D127's line "already on
  its way" no longer is (golden test 145).
- A directory opened for reading fails at the owner's first `recv` after
  `(:open f)`, so that clause now runs (D128; golden test 147).
- D138's warning, to hand a device over before reading from it, no
  longer applies, since nothing is cut ahead.

A process takes one message at each `recv`, so it has at most one device
to read on, and a device read on by a process that no longer owns it
(it was handed over) is left alone. Rejected: a device that sends
nothing till it's asked, one message per `(:read how)` (exact, but every
line loop, `lib/fs.slight` included, would need a send per line);
parsing HTTP in C, as Erlang's `{packet, http}` does (D132 put HTTP in
slight); chunks alone, with lines and lengths left to slight (it would
undo D154).

**D159. `(:read how)` changes how a reader cuts its bytes.** *(User: a
count is a one-off, and the names are as proposed.)* Any process may
send it to a reading file or connection, as with `(:write ...)`; to a
writer or a listener it's a dead letter, and so is a `how` it doesn't
know. `how` is one of:
- `:lines`, the default: `(:line f s)` (D127).
- `:chunks`: `(:chunk f s)`, what one `read` gives (or what's been read
  and not yet sent, if there is some), up to 64 KB and never empty.
- A count, an integer `n` from 0: one `(:chunk f s)` of the next `n`
  bytes, then back to the way of reading before. A one-off, since a
  count is nearly always a payload inside a line protocol (an HTTP body,
  a piece of chunked encoding, a Redis bulk string), and one message per
  payload beats two. If the input ends first, `s` is shorter, and
  `(:eof f)` follows.
- `:json`, `:json/items`, `:sexp` and `:source`, to come (D154, D156):
  `(:json f v)` for each top-level value, so that NDJSON and JSON texts
  one after another both work; with `:json/items`, `(:json f v)` for each
  element of a top-level array (any other top-level value comes as one
  message); `(:sexp f v)`; and `(:form f form line)` and `(:doc f text
  line)`. A form's raw text, D156's option, is left out till a pretty
  printer or a doc tool needs it.

*(Default:)* a count always gives exactly one chunk (empty at the very
end, and `(:read 0)` gives `""`, so a `Content-Length: 0` needs no
special case), and a reader checks its length. A count is for the next
message alone, whatever the way of reading, which applies after it
whichever was sent first; a second count before the first is used
replaces it. Nothing comes after `(:eof f)`. The same `(:chunk f s)`
serves chunks and counts.

**D160. Bad input, and a cap on what's read.** *(User: as proposed.)*
- `json/parse` and `sexp/parse` give `(:ok v)`, or `(:error (:bad-json
  at))` (`:bad-sexp` for s-expressions), `at` being the byte offset where
  the text went wrong. Not `#false`, as `string->int` gives, since
  `false` is JSON; and not a fault, since bad input is an outcome, not a
  bug.
- A stream can't recover from a syntax error, so on a device bad JSON
  ends the owner, as a failed read does (D128): `(:error (:bad-json
  where))`, `where` being the path or "host:port" (`:bad-sexp` the same).
- A line, a count or a value of 64 MB or more, too big for a process's
  heap, ends the owner with `(:error (:too-big where))`. Until now a line
  with no newline grew the device's buffer without limit, so one endless
  header line could use up a server's memory.

**D161. `json/print` writes compact JSON, on one line.** *(User.)*
`{"a":1,"b":[1,2]}`, so what it prints is NDJSON as it is. Indenting can
come when something wants it.

**D162. How JSON is read and printed.** *(Default; the rest of D154 and
D160 as agreed.)*
- **A validator, then a builder.** D154 had one push parser building
  values as the bytes came. What's built is simpler: a validator that
  takes text a piece at a time and keeps only its state (where it is, and
  the containers open), and a builder that makes the value of text the
  validator has passed, so it checks nothing. `json/parse` runs one, then
  the other; a device runs the validator on each read, and the builder
  once a value is whole, into chunks of its own that are copied into the
  message. It costs a second pass over each value and a copy, and saves
  keeping half-built strings, numbers and lists between reads. Both walk
  without recursing in C (D147), so nesting is limited only by memory.
- **Where bad text went wrong** is the length of its longest valid
  prefix: the first byte no valid text could have there, or the end of
  one that stops too soon (`"[1,]"` at 3, `"tru"` at 3, `"01"` at 1). It
  depends only on the text, not on how a parser is written.
- **JSONTestSuite's implementation-defined (`i_`) cases.** Numbers too
  big for a double are `inf` or `-inf`, too small `0.0`, as in JavaScript
  and Python; integers past 63 bits are floats. A `\u` escape of a lone
  surrogate is U+FFFD (D148). Bytes in a string that aren't UTF-8 pass
  through, since strings are bytes, and `json/print` passes them back.
  UTF-8's byte order mark and UTF-16 are turned down: neither is JSON's
  whitespace. `-0` is the integer 0.
- **`json/print`** escapes `"`, `\` and the control characters (`\b`,
  `\f`, `\n`, `\r`, `\t`, and `\u00xx` for the rest), and nothing else;
  numbers come out as slight prints them, so a float keeps its point or
  exponent and reads back as a float. What JSON can't hold faults with
  `:not-json`, a new fault kind, showing the piece that couldn't go.
- **On a device**, values need no space between them where they can't
  run together, so `1 2` is two values and so is `[1][2]`. With
  `:json/items`, the `[`, `,` and `]` around the elements are read
  between them; changing the way of reading inside the array forgets it.
  A value that would take 64 MB or more as slight values, however short
  its text, is `:too-big`.

**D163. How s-expressions are read and printed.** *(Default; D155 and
D160 as agreed.)*
- **As JSON is** (D162): a validator that takes text a piece at a time,
  and a builder for text it has passed (`runtime/sexp.c`), with the same
  rule for where bad text went wrong. A token's fate is known only at its
  end, so an error in one is at the first byte that rules it out (`#fo`
  at the `o`) or at the delimiter after it (`:12 `, `. `).
- **The text** is the compiler's reader's, without positions: the same
  delimiters, `#true` and `#false` and no other `#` word, no `.`, `` ` ``
  or `,`, the same escapes (`\"`, `\\`, `\n`, `\t`, `\r`, `\e`,
  `\u{hex}`), a string may run over lines, and a keyword can't be a
  number or start with `#` or `:`. But `:a` reads as the symbol `a`
  (D155), and numbers read as JSON's do: an integer past 63 bits is the
  nearest float, a float too big is `inf`, where the compiler gives up.
- **`sexp/print`** writes one line: strings escape `"`, `\`, `\n`, `\t`,
  `\r` and `\e`, and the other control characters and DEL as `\u{hex}`;
  anything else, bytes that aren't UTF-8 included, goes as it is. A float
  has digits on both sides of its point (`1.0e+21`), as the reader wants.
  A symbol is its name, without a colon (D52); `(:symbol "name")` is
  `name` when that reads back as a name, and otherwise stays the list
  `(symbol "name")`, which reads back as itself. `(quote x)` stays as it
  is, not `'x`. What isn't data faults with `:not-sexp`, a new fault kind
  ("not data" in the log).
- `quote` and `symbol` are runtime symbols, so every program mentions
  them. Finding a symbol by name, which the reader does for every name,
  goes through a table made the first time it's wanted (`string->symbol`
  uses it too).
