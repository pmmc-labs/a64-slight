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
is never freed yet; step 8 shrinks what's kept to the exit record (D88).

**D97. The actor examples.** *(Default, under D84.)* `ping-pong`,
`ring-benchmark` and `million-forks` are ported: a `(recv)` in the middle
of a function became a receive function, the `(yield ...)` around loops is
gone (preemption does that), and with no `join` or timing yet they print
as they go and use fixed sizes.
