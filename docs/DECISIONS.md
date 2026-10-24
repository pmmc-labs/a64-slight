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
proposal). (Since step 13 a program writes send-then-wait in one function,
and the compiler makes it two: D167. The rule holds for what it
generates.)

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
(Lean 4, Koka). Kept open by keeping the heap acyclic; not now. (Since
step 12b a collection can also start with frames on the stack, read as
they are, with no stack maps: D166.)

**D25. `send` deep-copies; `recv` adopts the chunk.** *(Default.)*

**D26. Static data lives in the binary.** *(Default.)* Literals, quoted
constants and top-level function references are shared, never collected,
and sent without copying.

**D27. Strings: level 1 now (immutable bytes, byte indexing, ASCII case),
level 2 UTF-8 helpers when the editor needs them, full Unicode out.**
*(User agreed.)* No char type. Rejected: mutable C-style byte buffers
(break immutability). D219 counts characters everywhere, and maps case
in full.

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
`bytes->string` given something other than 0–255. D219 renamed these,
and took JavaScript's `slice` in place of `substring`.

**D63. C builtins are passed the call's site.** *(Default.)* In the
register after the arguments, so a fault raised in C reports the slight
source position, as the compiler's own checks do. Rejected: inline type
checks before every call (more code), or C faults without positions.

**D64. `pprint` doesn't escape inside strings.** *(Default, as ts-slight.)*
`(pprint "a\"b")` prints `"a"b"`. Replaced by D219: it escapes them.

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
fill). Every loop in the prelude is a tail call. D219 made the string
functions builtins, `str/starts-with?` and `str/pad-start` among them.

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
what stops them. (Changed in step 12b: D166.)

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

**D111. `after` and `sleep` return `()`.** *(User.)* (`after` gives its
timer's number since D232.) Neither can fail, so
a Result would always be `:ok`. DESIGN's "blocking calls return a Result"
now covers the ones that can fail: `slurp` gives `(:ok string)` or
`(:error reason)`, and `spew` `(:ok ())` or `(:error reason)` (10c).
Rejected: `(:ok ())` from `sleep`, for uniformity. (`slurp` and `spew`
became slight functions in 10c, and `slurp` gives lines: D129.)

**D112. A timer whose process has ended is dropped when it's due.**
*(User.)* (Reopened by D232: dropped sooner, at no cost.) An `after` aimed at a process that has ended, or the wake-up of
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
  mixes timers with sockets may not. (A lookup's own timer is waited
  out differently: D165.)
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
D219 renamed the byte builtins after all, to `bytes/`, and maps case
beyond ASCII; display width and normalization are still to come.

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
and it can miss per-interface and VPN settings. (Built in step 12d: D165.)

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
(The details: D164.)

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

**D164. The details of doc blocks and `:source`.** *(Default; D156 as
agreed.)*
- **Where one begins:** at the start of a line, between top-level forms:
  not inside a list, and not after a `'` still waiting for what it
  quotes. Anywhere else `=doc` is a name, as `==` always is.
- **Its lines:** the first is `=doc` alone, and a line `=cut` ends the
  block, each with spaces allowed after it; a line like `=cutting` is
  part of the block. The end of the file ends one too, as in POD.
- **Other kinds:** D156 keeps any other `=word` for later kinds, so for
  now one is an error, a stray `=cut` among them: a file can't come to
  depend on what an unknown kind does, and a later kind can't change the
  meaning of a file that already has one. The compiler says so (`unknown
  doc block '=example'`); the data reader, where the text stops being
  valid (D162), at the first letter that isn't `doc`'s.
- **In the readers:** the compiler's reader skips a block in its
  tokenizer, which counts parentheses for it. The data reader treats one
  as space, as it does a comment, so `sexp/parse` and `:sexp` skip it;
  with `:source` it's a message of its own.
- **`(:doc f text line)`:** `text` is the lines between `=doc` and
  `=cut`, newlines and all, and `line` the line of `=doc`. A form's
  `line` is that of its first byte, a quote's if it has one. A device counts
  lines through every way of reading, so after a line read with `:lines`
  the next form's line is still the file's.

**D165. How the runtime looks up host names.** *(Default; D153 as
agreed.)* `runtime/dns.c`, about 350 lines.
- **The order**, as glibc and Go have it: a dotted IPv4 address is
  itself; then the hosts file, its IPv4 lines only (D139), names compared
  without case, as DNS compares them; then `localhost` is 127.0.0.1 even
  when the hosts file doesn't say so (RFC 6761, as Go does); then the name
  servers in `resolv.conf`, at most three, or 127.0.0.1 when it names
  none. Both files are read at every lookup, so a change to them counts
  at once, and there's nothing to keep up to date.
- **`resolv.conf`:** `nameserver`, `search` (or `domain`), and the
  options `ndots:n`, `timeout:n`, `attempts:n` and `use-vc`, with glibc's
  meanings and defaults (1, 5 seconds, 2, off). A name with fewer dots
  than `ndots` is tried in each search domain first and then as it is;
  any other, as it is first; one that ends in a dot, only as itself. Each
  name is asked of each server in turn, `attempts` times round. A server
  that says the name doesn't exist, or that it has no IPv4 address, moves
  the lookup on to the next name; one that fails (SERVFAIL, REFUSED),
  can't be reached, or doesn't answer in `timeout`, to the next server.
  The rest of the file is ignored: `rotate`, `sortlist`, `edns0`, and
  IPv6 servers.
- **The query** asks for one A record, recursion desired, under a random
  ID; an answer with another ID is ignored. It goes over UDP, then over
  TCP to the same server if the answer was cut short, or only over TCP
  with `use-vc`. The address is the first A record in the answer, after
  any CNAMEs the server followed.
- **Waiting:** the query's socket is waited for in `select()`, as the
  connection's own will be, and its timeout is a timer the device sets
  itself and unsets when the answer comes, so a lookup keeps the program
  running, and the virtual clock covers it in tests. The connection is a
  device from the start: what's written to it while its host is looked up
  is kept, and `disconnect` gives up the lookup.
- **On the virtual clock**, before the clock moves to a lookup's timer,
  the runtime waits for real, up to 200 ms, for a socket to do something.
  That timer is the runtime's, not the program's, so a program that only
  connects hasn't mixed timers with sockets (D139), and it mustn't beat
  an answer on its way. It did on macOS, where loopback delivers a
  moment after a send, too late for a `select()` that doesn't wait:
  `t/186` failed there, every query timing out before its server saw it.
  Linux delivers on loopback within the send, so there it shows only
  with a server outside the program that answers late, which was tried
  by hand (a 30 ms delay: `:etimedout` before, the answer after). A
  lookup that really times out costs a test 200 ms of real time.
- **Errors:** `:enotfound` when every name tried doesn't exist, has no
  IPv4 address, or can't be one (an empty label, or a label over 63
  bytes); `:etimedout` when no server would answer for a name, which
  might exist, and then the names after it aren't tried (glibc's "no
  servers could be reached").
- **The port** in `"host:port"` is a number now; `getaddrinfo` also took
  a service's name (`"host:http"`), which is `:enotfound` now.
- **For tests:** `SLIGHT_HOSTS` and `SLIGHT_RESOLV_CONF` name other files
  to read, and a `nameserver` line may give a port (`127.0.0.1:5353`), an
  extension no libc reads, so a test can serve its own names on a port the
  system picked. A golden test's line `; env: NAME=value ...` sets them
  (`t/186`). The name server there is slight, so it's asked over TCP
  (`use-vc`), since slight has no UDP; UDP is tested against a port
  nothing listens on, and was tried by hand against a real resolver and
  against one that never answers (two queries, two seconds, then
  `:etimedout`).
- **Turned down:** `getaddrinfo` on a worker thread (D153); IPv6 (AAAA
  records), which waits for IPv6 sockets (D139); keeping answers (a
  cache needs TTLs, and the local resolver usually keeps them anyway);
  reading what macOS's `scutil --dns` shows, per interface (D153's
  caveat), since `/etc/resolv.conf` has the default servers, which is
  enough for now.

**D166. Collecting at a function's entry, with the frames as roots.**
*(User, Oct 2026, for step 12b: collecting at any function's entry,
over collecting at tail calls between state functions, at tail calls
from the bottom of the stack, or anywhere, C included; slots zeroed for
now; the trigger through preemption; and the stress mode a documented
variable.)*
- **Where:** at a function's entry, in `rt_preempt`, as well as when a
  receive function waits (D105). Every loop is a tail call, so every long
  computation passes through function entries: the root and a process
  that never waits collect too, and the 64 MB limit (D106) is on what's
  live, not on what's been allocated. Collecting at tail calls between
  state functions would have helped only actors, which reach `recv`
  soon anyway; at tail calls from the bottom of the stack, not the root,
  whose own frame is under any loop it calls; and anywhere, C included,
  needs handles and a rooting API in C, which D24 turned down.
- **The roots are the frames,** read as they are. The code generator made
  that cheap: nothing lives in a register across a call; a slot only ever
  holds a value; C never calls compiled code (`apply` jumps, and `fork`
  starts a new process), so no C frame comes between compiled ones, and C
  code still never sees anything move (D24); and frames are linked by
  `x29`/`rbp`, laid out the same on both targets. At its entry a function
  has stored its parameters and captured values and nothing else, so
  `rt_preempt` is given those; each frame below holds its slots between
  the linkage of the one above and its own; the first frame's saved frame
  pointer is 0, from the zeroed context a process starts with.
- **Stale slots:** a slot not yet written holds whatever was on the stack
  before, an old return address or a pointer to what an earlier
  collection freed, and the collector would follow it. So a function that
  calls compiled code other than in tail position (a call, a call through
  a closure, or `apply`) zeroes its slots past its parameters at entry:
  an `stp xzr, xzr` for two slots on AArch64, a `mov` for each on
  x86-64. A function that only tail-calls, as most loops do, is never
  under a collection, and pays nothing. Not now: a table of the live
  slots at each call site, which costs nothing at run time and keeps less
  alive, but takes more of the compiler; it's the next step if the
  zeroing shows in measurements. Measured on x86-64, at four placements
  of the code (which alone moved times by up to 15%): `fib 37`, which
  zeroes three slots a call, took 0.237–0.252 s against 0.211–0.275 s
  before, about 3% more on the mean; a loop of tail calls, the same code
  as before, the same times.
- **When:** at the end of every slice of 1,000 calls (D86), and sooner:
  when `rt_heap_grow` finds the heap in use past the point of collecting
  (D106), it cuts the slice short, so the next call's entry goes to
  `rt_preempt`, and keeps the rest of the slice (`slice_rest`) for after.
  So collecting never changes when a process is preempted, and output
  that depends on scheduling comes out the same (`t/190`). Turned down: a
  check of its own at every entry.
- **What a frame keeps:** everything in its slots, until it returns, so a
  `let` in the root's body lives till the program ends, and a value left
  in a slot can outlive its use until the slot is used again.
- **`SLIGHT_GC_STRESS=n`** collects at every nth call's entry (n up to
  1,000, a slice: past that, at every slice's end), and at every `recv`,
  with the slices unchanged. With 1, every golden test gave the same
  output on both targets, but for those that then take too long (much
  live data, or 200,000 frames deep); with 101 so did those, but for five
  under qemu, and with 1,000 three of those; the other two fill 64 MB
  with live data, which takes too long under qemu even so, and they did
  on x86-64.
  `t/189` sets it itself: with it, a function that didn't zero its slots
  finds a freed pointer in them at once, on both targets.
- **The cost to the parked targets:** compiling to C, or to WebAssembly,
  suited the design because the collector never scanned a stack
  (BACKGROUND.md). Now it reads the frames, so either would keep its
  slots where the collector can read them (a shadow stack: frames in
  memory of the runtime's own, which is what slots are already), or
  collect only at `recv` there, as before.

**D167. `recv` anywhere a tail call could go, by splitting functions
there.** *(User, Oct 2026, step 13: splitting at `recv`, over `defactor`,
over waiting in the middle of a function with the stack kept, and over
converting functions that wait to continuation-passing style; and every
recommendation that came with it.)*
- **Where:** a form of a body in tail position (a function's, the top
  level's, a `fork`'s or `connect`'s, a `cond` or `recv` clause's, a `do`'s
  or `yield`'s in tail position), or a `let`'s value there; inside a call
  there, or inside a `do` with no `let`s, flattened first in the order
  things are evaluated, so what comes before the `recv` (but names,
  constants, quoted data and lambdas, which come out the same whenever
  they're evaluated) is taken into `let`s first; and a `cond`'s test in
  tail position, whose clauses from there go into a `cond` of their own.
  Not in a lambda, which can't wait; nor in a `cond`, `yield` or `do` with
  `let`s that isn't in tail position, each an error that says to take the
  message first. (Copying what follows into each branch would do it.)
- **What it gives:** the value of the clause that matched. `(recv)` takes
  any message, and gives it. A message no clause matches is a dead letter
  and the next is taken, as before.
- **How:** a pass, `compiler/src/split.ts`, that `compileProgram` runs
  before the `recv` rule's classification. A `recv` and the rest of its
  body become a receive function of their own, which the body tail-calls
  with the locals the rest still uses. A `recv` with more than one clause
  and something after it, or whose clause has a `recv` of its own, gets a
  second function for what's after, which each clause tail-calls with
  its value, around its body's last form, rather than a copy in each.
  The locals come across under other names, with the `recv`'s site in
  them, so neither a pattern nor a `let` in a clause's body can hide
  what the rest needs from before, and are bound again under their own
  names where they're used. More than 8
  locals go across as one list, made with `cons` and taken apart with
  `car` and `cdr`, so then none of them may be named one of those (an
  error). `@ARGV`, which can't be a parameter, is renamed. Generated names
  have spaces in them, so no program can write one, and say where the
  `recv` is (`ask (recv at f.slight:3:12)`, `ask (after recv at ...)`);
  faults and dead letters say `recv at ...`, as before.
- **Unchanged:** the code generator, the `recv` rule's classification,
  and the runtime see only receive functions, so a waiting process holds
  no stack (D8, D9), and `million-forks` still waits with a million. A
  function with a `recv` in it waits, so, like one that tail-calls a
  receive function, it can only be tail-called; lambdas still can't wait.
  A program that doesn't use the new places compiles as it did: every
  golden test and example gave the same assembly, byte for byte.
- **`defactor` dropped:** it was one shape of this.
- **Turned down:** waiting in the middle of a function with the stack
  kept, possible since D166, but a waiting process's stack would then be
  a cost the code doesn't show (about 32,000 of them at most, on Linux),
  and with no selective receive, a helper's `recv` would take messages
  meant for its caller; continuation-passing style for functions that
  wait, a large change that allocates at every call to one, after which a
  lambda still couldn't wait. A wait that gives a value back to a caller
  stays a process and `join` (`ask`, `(join (connect :tcp ...))`), which
  works anywhere. To look at again after HTTP (step 14), if its code needs
  helpers that wait inside one process.

## After step 13: patterns, tuples, replay

Three of the changes Claude listed (Oct 2026) when Stevan asked what
would make slight more useful, taken up at once: "Lets do 1, 2, and 4
now." Stevan chose the features; the details below are Claude's
defaults, not discussed, and fair to revisit.

**D168. `match`, and nested patterns for `recv` too.** *(User asked for
it; the details are defaults.)* `(match topic (pattern body...) ...)`
evaluates the topic once, and the first clause whose pattern fits gives
the value, its names bound in its body only. The patterns, `match`'s and
`recv`'s alike: `_`; a name; a literal compared with `eq?` (a number, a
string, `#true`, `#false`, `()`, anything quoted); `(p ...)`, a list of
exactly that many; `(p ... & rest)`, at least that many, `rest` matching
the list of what's left. So D85's "patterns don't nest, and the head
keyword is the only literal" no longer holds; every pattern that was
allowed means what it did.
- **How:** one lowering (`compiler/src/pattern.ts`) makes a pattern into
  tests (`cons?`, `nil?`, `eq?` on car and cdr paths into the value) and
  bindings (a name and its path). The expander makes `match` into `cond`
  with them, as `case` is (D142), so `split.ts`, the `recv` rule and the
  code generator need nothing new for it, and a `recv` can go in a
  clause's body. The code generator compiles `recv`'s clauses with the
  same lowering, so the two can't drift apart. A test that can only be a
  boolean branches without making one (in any `cond`, which helps `and`
  too), and a path's steps skip the cons check the tests already made,
  so a `recv` costs about what it did; the ring benchmark can't tell the
  difference.
- **Hidden names:** the tests and paths call the builtins by names with
  spaces in them, which no program can write and no local can hide, so a
  pattern can bind `car`, and a local named `cons?` changes nothing.
- **No match:** a `match` that matches nothing faults, `:no-match` with
  the topic as the value, as `cond` faults `:no-clause`: a missed case is
  a bug. Rejected: giving `()`, as `case` does, which hides it.
- **`&` for the rest**, as Clojure's destructuring and Common Lisp's
  `&rest`; slight has no dotted pairs to write `(a . rest)` with. `&` is
  only special inside a list pattern.
- **Left out for now:** guards (`(pattern :when test body...)`), which
  would let a clause fall through on a value as well as a shape; matching
  a name's current value (Erlang's bound variables); `match` in `lambda`
  parameters and `let`. Each is small if it's wanted.

**D169. Tuples.** *(User asked for them; the details are defaults.)* An
immutable value of a fixed number of elements, any of them one load
away, where a list takes a step per element. What it's for: records of
a few fields, rows, and the nodes that persistent maps and vectors are
made of (a hash trie, a balanced tree), which on lists alone cost a walk
at every level.
- **Representation:** a box (D23): the header (element count, type 4),
  then the elements, a word each. Copying a message, the collector,
  printing and `eq?` already walked closures' captured values; they now
  ask the header which boxes hold values and where they start
  (`rt_box_holds_values`, `rt_box_first_value`), so a tuple is just
  another such box. Printing and `eq?` keep their work stacks (D147),
  now of cursors that are a list or a tuple and an index, so a tuple
  nested 100,000 deep is fine (`t/197`).
- **Builtins, in C** (`runtime/tuples.c`): `tuple`, `tuple?`,
  `tuple/len`, `tuple/get`, `tuple/set`, `tuple/make`, `tuple/slice`,
  `list->tuple`, `tuple->list`. `tuple/set` copies the whole tuple; that's
  what makes it immutable, and is cheap at the sizes a tuple is for. An
  index out of range faults (`:out-of-range`); anything but a tuple faults
  `:not-a-tuple`, a new kind. `tuple/slice` clamps, as `substring` does,
  but doesn't swap: `start` not before `end` is empty, as JavaScript's
  `slice`.
- **Printing:** `#(1 2)`, after Scheme's vectors. `eq?` is structural,
  and a tuple is never `eq?` to a list with the same elements.
  `json/print` writes a tuple as an array; `json/parse` still makes
  lists.
- **Left out for now:** a reader syntax (`#(1 2)` as a literal, which
  would make constant tuples static data, as quoted lists are), and so
  `sexp/print` of a tuple faults `:not-sexp`, like a pid's; tuple
  patterns in `match` (which would follow the syntax); growing a tuple
  in place, which nothing immutable can do. Rejected: the name "vector",
  which in Clojure and Scheme suggests something that grows or changes.

**D170. Recording and replaying a run; a seeded schedule.** *(User asked
for both; the details are defaults.)* A run is decided by what it takes
from the world, since the scheduler counts calls, not time (D86, D91).
So:
- **One place for the world:** `runtime/replay.c`, behind `runtime/io.h`.
  Every system call that brings something in, and the clock, goes
  through it: about twenty sites in `process.c`, `dns.c` and `tty.c`.
  `SLIGHT_RECORD=file` makes each call and writes what it gave (result,
  `errno`, bytes); `SLIGHT_REPLAY=file` makes none, taking the record's
  answers instead, so the run goes as the recorded one did: same output,
  same faults, with no files, sockets or terminal. Writes aren't made in
  a replay (but stdout and stderr). The record starts with the settings
  that change a run (the clock, the seed) and `@ARGV`, which a replay
  takes over from its own.
- **Recorded at the system calls, not at the messages:** recording the
  messages devices send would miss when they're cut (D158), when a
  socket can take more, and what the clock said, all of which change the
  run. The calls are where the runtime meets the world, and there are
  few of them.
- **What's not recorded:** writes' bytes (they're the run's own output),
  and stdout and stderr, which a replay writes again. A replay that asks
  for something the record doesn't have next stops, with exit status 2,
  rather than guess: it's another program, or a source of difference left
  out.
- **The record is flushed after every entry,** so a crash leaves all of
  it. That costs a write per entry while recording, which is for
  debugging; off, the calls cost a test of a flag.
- **`SLIGHT_SCHED=n`:** a generator (splitmix64) seeded with `n` picks the
  next ready process from anywhere in the run queue, and each slice's
  length (1 to 1,000 calls), so a program can be tried under orders that
  first in, first out never shows, each the same every time. Picking walks
  the queue, which is fine at the sizes it's tried on. Every promise the
  language makes holds in any order (`t/199`).
- **Tests:** `t/replay.sh` (`make replay`, part of `make test`), since a
  check of a replay takes two runs: programs with keys, files, the real
  clock, sockets and a lookup are recorded and replayed with none of them,
  and must print the same; a record replayed by the wrong program must
  stop; twenty seeds must give more than one order, each the same twice.
  The real clock is used there, but nothing depends on how long anything
  takes, since a replay is compared with its own recording.
- **Rejected:** recording at the level of messages (above); a record
  that's portable between machines (it's for debugging where it was
  made); exploring schedules systematically (PCT, or a model checker),
  for which seeds are the cheap first step.

**D171. `:system`, a device that tells about the processes and the
runtime.** *(Stevan agreed to the device, to anyone connecting, to
counting reductions, to the table of function names and to tests of
facts; the message shapes are defaults.)* For a `top` written in slight,
health checks in a supervisor, and finding where a stuck program waits.
- **A device, not builtins:** `(connect :system expr)`, whose owner asks
  with `(:processes)`, `(:process pid)` and `(:runtime)`. A list of
  every process wouldn't fit a heap at a million processes; a device
  sends one `(:process s info)` at a time, the next when its owner waits
  again (D158), and then `(:end s)`. With one core nothing runs while
  the device answers, so it copies what it tells of every process at
  once, outside any heap, and every message describes the same moment
  for free. And it's a capability, which `(connect s expr)` can hand on.
  A `(:processes)` asked before the last one's `(:end s)` is answered
  after it; `(:process pid)` and `(:runtime)` are answered at once.
- **Anyone can connect,** as with `:keypress` and files. Only the root
  connecting, and handing the device to code it trusts, goes with
  Misty's grants, if devices are ever restricted.
- **`info` is a list of `(key value)`,** which `lookup` reads, so keys
  can be added without breaking anyone; a tuple by position would break
  at the first. A process's: `:pid`, `:parent`, `:state` (`:running`,
  `:ready`, `:recv`, `:joining`, `:sleeping`), `:joining`, `:in`,
  `:mail`, `:heap` (in use), `:reductions`, `:stack` (held or not) and
  `:devices`. The runtime's: `:processes`, `:pids`, `:ready`, `:timers`,
  `:devices`, `:heap`, `:stacks`, `:clock` (milliseconds since the
  program started, on either clock) and `:reductions`.
- **Reductions are counted,** since a `top` can't show who's busy
  without them: the calls a slice had, less what's left of it, added up
  when the next slice starts, so nothing is added to a call. The count
  is exact, and the same under `SLIGHT_GC_STRESS` and `SLIGHT_SCHED`.
- **Where a process is:** the compiler emits every function's entry and
  name (`slight_functions`, a few bytes a function), and the runtime
  finds the function with the last entry at or before an address. A
  process holding a stack is where it last stopped: the function it was
  entering when preempted (`rt_preempt` is passed the entry), or the one
  that called `join`, `sleep`, `yield` or `send` to a device (their
  return address); one without a stack is at its code: the receive
  function it waits in, or where it starts. A receive function made from
  a `recv` inside a function is named for both (D167), so `:in` says
  which `recv`: `"server (recv at prog.slight:7:12)"`.
- **The deadlock message says where the root waits:** `deadlock: the
  root process is waiting in server (recv at prog.slight:7:12) for a
  message, and nothing else can run`, from the same table.
- **Tests check facts,** not the bytes of a heap or the count of
  reductions, which change with the prelude: states, names, mailboxes,
  devices, exits, and comparisons (`t/200`–`t/202`). `examples/top.slight`
  isn't a golden test for the same reason.
- **Left for later:** a feed of events (`(:watch)`, each process's start
  and end), which a supervisor of everything would want and `monitor`
  covers for one process; acting on processes (`kill` already works on
  any pid, so the device only looks); the heap's limit before the next
  collection.

**D172. HTTP before TLS.** *(User.)* D149 put 12c (C libraries, then
TLS) before HTTP (14). The user chose the other order: HTTP/1.1 on plain
`:tcp` first, its details worked out before any code, and HTTPS when 12c's
`:tls` device exists, with the same library. Reason: HTTP's open questions
are about its API, and HTTP/1.1 over `:tcp` doesn't need TLS. The library
uses only what a `:tls` device would also send (lines, chunks, counts,
`:eof`, `:write`), so TLS slots in beneath it (D152). Rejected: TLS first,
as D149 had it, which would hold HTTP back for questions it doesn't need
answered. The proposal is in PLAN.md, step 14.

**D173. HTTP's first design: the handler, the head, the request and
connections.** *(User, asked with Claude's recommendation for each.)*
- **The handler runs in a process of its own.** The connection's process
  forks it and joins it, so a fault in the handler is a 500 reply, not a
  connection that ends with nothing. (The alternative, in the connection's
  process, saved a process per request.)
- **The head is read in bounded chunks,** `:chunks` rather than `:lines`,
  with the library splitting lines itself and refusing a head over 64 KB
  with 431. The device cuts a line at 64 MB (D160), which is too late to
  refuse a long header.
- **A request is a keyed list,** `(:method :get)`, `(:path ...)`,
  `(:query ...)`, `(:headers ...)`, `(:body ...)`, read with `lookup`, as
  `:system`'s info is (D171). A response is positional, `(status headers
  body)`.
- **Connections close after each response** (`Connection: close`) on both
  sides. Keep-alive comes later, with no runtime change.

The rest of the proposal in PLAN.md's step 14 is taken as written, with
one change: there is no default timeout (D234 gave it 30 seconds, and
D236 took them out again). A timer stays pending until it's
due and keeps the program running (as `ask-within` does), so a
30-second default would hold every program's exit for up to 30 seconds
after its last request. A timeout is asked for with `http/request-within`.

**D174. HTTP as built (step 14).** *(Default, under D173: the details the
proposal left to the code.)*
- **Limits:** a head over 64 KB is a 431. A body over 8 MB is a 413, judged
  by Content-Length before it's read, or as chunks add up. A client's body
  has no limit of its own, beyond the 64 MB of a count or a line and the
  process's heap.
- **Errors:** a server answers 400 to a start line that isn't HTTP, a
  malformed head or chunk line, or a body that ends early. A client gives
  `(:error reason)`, with `:closed` for a connection that closed before its
  reply.
- **Methods:** a keyword for GET, HEAD, POST, PUT and DELETE, a string for
  any other. A client writes the name in capitals.
- **Headers the library writes:** Content-Length on every request, `0` for
  none; Content-Length and Connection: close on every response. No Date
  (slight has no calendar time). A request with `Expect: 100-continue` gets
  `100 Continue` as soon as its head is in, which curl waits for before a
  big upload.
- **Chunked bodies** are read the same way on both sides, and trailers are
  read and dropped. Their bytes are taken with `:chunks`, not counts; only a
  Content-Length body uses a count.
- **Processes:** `http/serve` returns the listener's pid, and its owner says
  `(:listening port)` to the caller. Each accepted connection is a process
  that runs the handler in a process of its own (D173). A client request is
  one connection's process, which the caller joins; `request-within` adds a
  box that kills it when the time is up, as `ask-within` does.
- **Lines** keep their `\r` from the device, and the library drops one.
- **Not checked:** a server that answers a request it hasn't fully read may
  have the connection reset before the client reads the reply. Tests 207's
  431 case sends nothing after its long line, and its 413 sends only the
  head, so neither resets. A lingering close needs a timer that can be
  cancelled, or a way to half-close.
- **Checked outside the tests:** our client against Python's `http.server`
  (a file, and a 404), Python's `http.client` against our server (GET,
  POST, and a chunked POST), and curl against it (GET, a 5 KB POST with
  Expect, and a chunked upload). Python and curl are not dependencies of
  the tests.

## After step 14: what the boids example asked for

**D175. `sort`, `sort-by` and `zip` in the prelude.** *(User asked for
them, after `examples/boids.slight` wanted to put messages in order;
the details are defaults.)*
- **`(sort less? xs)`**: the function first and the list last, as `map`
  and `filter` (D78). `less?` is any function of two values that gives a
  boolean, so `(sort < xs)` and `(sort > xs)` need nothing more. Stable,
  since a sort by one key after another relies on it. A merge sort from
  the bottom up (runs of one, merged in pairs till one is left), so
  every loop is a tail call and a list of any length is fine, at
  n log n calls of `less?`. Rejected: a sort with no function that
  compares any two values (there's no order across types, nor of
  strings yet), and quicksort (not stable, and n² on a sorted list).
- **`(sort-by key xs)`**: the elements in the order of `(key x)`, a
  number, compared with `<`. It pairs each element with its key first,
  so `key` is called once for each, not at every comparison.
- **`(zip xs ys)`**: `(x y)` pairs, as long as the shorter list (as `take`
  stops at the end, D78), so `(zip keys values)` is an association list
  that `lookup` reads, and `(zip (range 0 n) xs)` numbers a list.
  Rejected: tuples for the pairs (patterns take lists, and association
  lists are lists), and faulting when the lengths differ.
- **Left out:** comparing strings (a builtin, to propose when it's
  wanted), and `zip` of more than two lists.

**D176. Tuple patterns, `#(p ...)`, and only as patterns.** *(User: "Tuple
patterns are a good idea"; they chose this syntax from three, Claude's
recommendation.)* D169 left tuple patterns out until there was a reader
syntax for tuples. `#(p ...)` in a pattern, `match`'s or `recv`'s,
matches a tuple of exactly that many elements, each matching its
pattern; it nests in list patterns and they in it. It's written as
`pprint` prints a tuple, so a pattern looks like the value it takes
apart, as a list pattern does.
- **Only a pattern:** as an expression, or quoted, `#(...)` is a compile
  error; `(tuple ...)` still makes a tuple, as `(list ...)` makes a list
  that `(p ...)` takes apart. What `#(...)` would mean as an expression
  (a tuple made of its elements' values, as Clojure's vectors; or a
  constant, as Scheme's, which needs static tuples, D169) is left open.
- **How:** the reader reads `#(` as a list headed by a name no program
  can write (`TUPLE`, with a space in it, as pattern.ts's hidden names
  are, D168), so no list pattern can be taken for it. The lowering tests
  `(tuple? v)`, then that `(tuple/len v)` is `eq?` to the count, then
  each element at `(tuple/get v i)`; a path is car and cdr steps and
  tuple indexes in any mix. The expander's walk, which leaves patterns
  alone, rejects one it comes to elsewhere, and `staticList` rejects a
  quoted one. No `&` in a tuple pattern: a tuple doesn't grow, and the
  rest of one would be a `tuple/slice`, which can wait till it's wanted.
- **As data:** `sexp/parse` (and `:sexp` and `:source` on a device)
  reads `#(...)` as a tuple, so slight's own source, with tuple patterns
  in it, still reads as data; and `sexp/print` writes a tuple as
  `#(...)` where it faulted before (`t/181`, `t/216`).
- **Rejected:** `(tuple p ...)` as the pattern, with no new syntax (just
  pattern.ts), because the pattern would look like the call that makes a
  tuple rather than like the tuple, and `(tuple a b)` would stop being a
  pattern for a list of three; and `#(...)` as a constructor too, for
  now, since it would be a second way to make a tuple and would settle
  the open question above.

**D177. A message is copied in one walk, and sent in one malloc.**
*(User chose these two from Claude's list, after the boids example's
profile.)* With 250 birds each telling every other where it is, 62,500
sends a tick, copying was a third of the time and malloc a sixth. A copy
was two walks over the value (one only adding up its bytes, so that one
chunk could hold it) and two mallocs (the chunk, and the message's record
in the mailbox).
- **One walk:** the value is copied into a scratch buffer kept from one
  copy to the next, and then moved by memcpy into a chunk of just the
  size it came to, so the receiver's heap holds what it did before. While
  the copy is made, scratch can grow and move, so the copy refers to
  itself by offsets (with their tags), and notes where each such word is;
  the move adds the chunk's address to each. Fork's values and results
  go the same way. Each value is sorted out once (kept as it is, a cell,
  a box with nothing in it to copy, or a box of values), and a float's
  box is copied as two words.
- **One malloc:** the message's record is at the front of its chunk.
  Taking the message adopts the chunk and frees nothing; the record is
  dead space in the taker's heap until it collects (32 bytes a message).
- **Measured** in instructions, under callgrind: boids with 250 birds,
  17.7 per cent fewer; 200,000 small messages, 22.9; the ring benchmark,
  12.8. In wall time on the cloud machine, boids and the messages are
  25 to 40 per cent faster, and the ring the same (it's mostly processes
  and stacks).
- **Rejected:** copying into a chain of chunks as the walk goes (no size
  walk, but a small message would get a chunk bigger than it needs, and
  the waste would count against the receiver's heap); remembering the
  size of the last value sent, for broadcasts (it helps only when the
  same value goes to many, and needs clearing when the sender collects);
  and copying straight into the receiver's heap, as the BEAM does (the
  collector would have to take queued messages as roots).

**D178. A tuple pattern compiles inline.** *(User chose it from Claude's
list, after the boids example's profile.)* A tuple pattern of n elements
was two calls into C to test (`tuple?`, then `tuple/len`) and one for
each element (`tuple/get`), where a list pattern's tests branch inline
and its parts are loads (D168). Now its test is one: a tuple's header
word is exactly n shifted by `RT_BOX_SIZE_SHIFT`, or'd with
`RT_BOX_TUPLE`, so the lowering makes `(TUPLE_OF v n)`, which the code
generator compiles as a tag check, a load and one compare (the target's
`branchUnlessBox`); and an element is `(TUPLE_REF v i)`, one load
(`tupleRef`), on a tuple the test has passed, as `c[ad]r` on conses the
tests have passed (`ON_CONSES`). Both are names no program can write. On
AArch64, an element past the 31st is past `ldur`'s reach and is loaded
with its offset in a register. Should `TUPLE_OF` ever be compiled as a
value rather than a test, it's the builtins' `tuple?` and `tuple/len`.
`values.ts` gains `TAG_BOXED`, `tupleHeader` and `tupleElement`, checked
against `rt.h`. Boids with 250 birds: 5.9 per cent fewer instructions.
Rejected: inlining `tuple/get` everywhere, which would need its index
and bounds checks inline too, for calls where the index isn't known.

**D179. What the boids example settled, besides D175–D178.**
- **Birds talk to birds.** *(User: "The first version was a nicer example
  for slight than this round version.")* Each bird tells every other
  where it is, collects the tick itself, and keeps what comes early, by
  hand. A version with a round-up process for each bird and the display
  (a join object: the waiting written once, the race at tick 0 gone,
  four parameters a bird) was tried and taken back; it also cost 1.2 to
  1.6 times as much, each state copied twice.
- **Libraries held off.** *(User.)* A grid (rows of a tuple of tuples,
  which `game-of-life-actors` and boids each write) and `lib/random`
  (Park and Miller's generator, which stays inside 63 bits with no bit
  operations) wait till more programs want them.
- **Floats stay floats.** *(User, on Claude's recommendation.)* Fixed-point
  boids would be faster (no float boxes in messages, integer arithmetic
  inline) but would trade the steering's clarity for it, and hide a cost
  that belongs to every program using floats; a numerical vector type
  would make storing floats cheap but reading one costs a box, and it
  pays only with numpy-style operations on whole arrays. If float speed
  is wanted, it's the representation's to fix (PLAN.md, "Later"), with
  boids as the benchmark.
- **The flock stays all-to-all.** *(User: "keep the birds as actors and
  skip spatial stuff".)* n² messages a tick; BACKGROUND.md has why
  locality is hard for actors, and the way through that keeps them.

## Step 15: agents

**D180. Agents before TLS, starting with a local model.** *(User agreed,
Oct 2026, to Claude's suggestion.)* D172 put 12c (C libraries, then TLS)
next; but nothing in step 15's design needs TLS: a local model (ollama,
or a Jev-compatible server) is plain HTTP. So step 15 starts now, with
what any agent needs, in order: HTTP bodies streamed to the caller as
they come, where `lib/http.slight` now reads the whole body first (the
shape to settle first); `:exec`, a device for running a tool's command;
and a way to read the environment, for a model's address or a key (a
builtin, to propose). With them, the turn and the tool as messages
(PLAN.md, 15). TLS (12c) comes when the first hosted model is wanted,
with a reader of server-sent events, which hosted APIs stream in; it
needs randomness, which must go through `replay.c` for a run to replay.
Rejected: TLS first, as planned, since it holds agents back for a model
they can start without.

**D181. A streamed HTTP body comes to its caller as messages, as a
device's do.** *(User chose it from three, Claude's recommendation.)*
`(http/stream method url headers body how)` gives the stream's pid at
once, and the process that called it is sent `(:head s status headers)`,
then the body as it comes, `(:chunk s bytes)` when `how` is `:chunks` or
`(:line s text)` when it's `:lines`, then `(:eof s)`; if anything fails,
`(:error s reason)` comes in its place, with the reasons `http/request`
gives. Chunked encoding is undone on the way, so the pieces are the body's
own. `(kill s)` stops a stream and closes its connection; a timeout is the
caller's own `after`. So an agent takes a model's output in its own
`recv`, among its other messages, as it would read a file; ollama's JSON
lines are `:lines` and `json/parse` on each.
- **To settle while building:** a line's `\r`, which the head's lines
  drop (D174); that a last line with no newline still comes; and that a
  reply with no body (HEAD, 1xx, 204, 304) is `:head` and then `:eof`.
- **Rejected:** flow control, the stream sending its next piece only when
  asked, as a device cuts its next message only when its owner waits
  (D158): a round trip a piece, and a send wherever a piece is taken, to
  guard against a fast server, which a model isn't (it can come later, as
  a `how`, if a big download wants it); and a fold run in the
  connection's process, which would show the caller nothing till the end.

**D182. `http/stream` as built.** *(Default, under D181: what it left to
the code.)*
- **Lines** are cut at `\n`, and one `\r` at a line's end is dropped,
  as the head's lines' is (D174), the last line's too, as Go's
  `bufio.ScanLines` does. An empty line comes as `""`. A last line with
  no newline still comes, once the body has ended as its framing says;
  one cut off by an error doesn't, since it may not be whole.
- **No body:** HEAD, 1xx, 204 and 304 are `:head` and then `:eof`, and
  so is a body of no bytes.
- **A line over 64 KB** (65,536 bytes before its `\n`) ends the stream
  with `(:error s :line-too-big)`, after the lines before it, so a reply
  with no newlines can't grow a buffer without end. Each line is
  measured, not the buffer, so where the reads fall doesn't change the
  outcome.
- **Chunks** are what the device gives (up to 64 KB), less the chunked
  encoding's framing: a read holding two of the encoding's chunks gives
  two messages, and one that ends in the middle of a chunk gives what it
  has. Never empty; no sizes promised.
- **Every piece goes as soon as it's in:** bytes that came with the head,
  or with the end of a chunk's size line, are sent before the next read
  is waited for, not with it.
- **How it ends:** `http/stream` forks a watcher that monitors the
  stream. A device that fails ends its owner at once (D136), so the
  stream can't say `(:error s reason)` for a refused connection or a
  reset; the watcher says how every stream ended: `(:eof s)` when it
  returned, `(:error s reason)` when it ended with an error (the device's,
  or one the stream raised itself for a reply that isn't HTTP), and
  nothing when it was killed. So one place sends the last message.
  Rejected: the stream sending its own `(:eof s)` and HTTP errors, with
  the watcher only for the device's (two places that end a stream).
- **A bad URL** still gives a pid: a process that raises the reason, which
  the watcher reports. So every failure comes the one way.
- **A kill** says nothing, since the caller asked for it (or someone did;
  the watcher can't tell who). What the stream sent before is still in
  the caller's mailbox, and a stream that had already ended may have its
  last message on the way.
- **A caller that ends first** has its stream killed, as a device closes
  when its owner ends; otherwise a stream nobody reads would keep the
  program running (open sockets do) till the server finished. The watcher
  monitors the caller for this; that monitor goes when the watcher does
  (D183).
- **A `how`** other than `:lines` or `:chunks` faults (`:no-match`) in the
  caller, at the call, rather than in the stream later.
- **One set of readers:** the head, the framing and the body's readers are
  the client's and the server's too. Each hands every piece of the body to
  `http/take`, which keeps it for a whole body or sends it on for a
  stream. So a Content-Length body is now read in chunks, counted down,
  on every side, rather than as one count (D174), which would have held a
  stream's body back to the end; a whole one's chunks are joined at the
  end, as a chunked body's were.
- **Checked outside the tests:** against Python's `http.server` writing
  chunked JSON lines 0.4 s apart, on both targets: each line came when it
  was written, not at the end.
- **Tests** (217–221): the raw server sends a reply in pieces, a
  millisecond apart, or holds the connection open after them, to show
  that what came, came before the end. Their output doesn't depend on
  where the reads fall (lines; chunks joined), since macOS's loopback can
  deliver a piece after the virtual clock has moved past the next. Run
  under `SLIGHT_SCHED` seeds and `SLIGHT_GC_STRESS` too.

**D183. A monitor goes when either process ends.** *(User chose it from
three, Claude's recommendation, after asking whether a supervisor could
handle `http/stream`'s monitors.)* A monitor stayed on the watched
process's list till that process ended, even after its watcher had, and
`monitor` walked the list to add to its end. `ask`'s box monitors the
structure it asks and ends with the answer, so every ask left a monitor
on a structure that may live as long as the program, and the next ask
walked them all: 80,000 asks to one cell took 71 seconds on the cloud
machine, and 0.07 with a box that didn't monitor. (`http/stream`'s
watcher left one on its caller the same way, a stream at a time.) Now a
monitor is on two lists, the watched process's, in the order they asked
(D101), with its last kept so adding is one step, and the watcher's; a
process that ends takes its monitors off both, telling those that
watched it, as before. 80,000 asks take 0.10 seconds, and a million 1.2.
A program sees no change: a notice to a process that had ended was
dropped anyway.
- **A process that monitors itself** isn't told of its own end. The
  notice went with its mail anyway; but if it was killed while it waited
  in `recv`, delivering the notice put it back on the run queue as it was
  freed, which valgrind showed as reads and writes of freed memory. Found
  while doing this.
- **Cost:** a monitor is 48 bytes where it was 16, but lives only while
  both processes do; a process struct gains two pointers.
- **Tests:** 222, watchers that end from the front, the middle and the
  end of a list, one with two monitors on it and one with monitors on two
  processes, then a monitor added after them, and a process that
  monitors itself, killed; 223, 100,000 asks to one cell, which the old
  runtime is killed over, even on x86-64 natively. Under
  `SLIGHT_GC_STRESS`, which collects at every `recv`, 223 needs an n of
  about 1,000 to finish in time (22 seconds under qemu). Valgrind is clean
  on every golden test that monitors, asks, or uses HTTP.
- **Rejected:** a supervisor process for all of `http/stream`'s monitors.
  slight has no process names, so every caller would be handed its pid,
  or the runtime would need a registry; it would be one point of failure
  for every stream; and it would fix the streams and not `ask`. A
  `demonitor` builtin, as Erlang has: a new builtin, and code that forgets
  to call it leaks. A supervisor in OTP's sense, written in slight (D3),
  waits till agents need one (step 15).

**D184. A chat agent: a conversation as a process, on ollama's
`/api/chat`.** *(User agreed to Claude's proposal, before `:exec`, "to
get the agent working first just for a simple chat".)* Step 15's first
point to settle (PLAN.md): what a turn and an answer look like as
messages. `lib/agent.slight`, `(@include :agent)`, and
`examples/chat.slight`.
- **The model:** ollama's `/api/chat`: the conversation, as messages with
  roles, sent whole with each turn, and the answer as chunked JSON lines
  with a piece in each; so `http/stream` with `:lines`, `json/parse` on
  each, and the request one `json/print`. Rejected for now: the
  OpenAI-style `/v1/chat/completions`, which ollama, llama.cpp and LM
  Studio serve too, but which streams server-sent events, left for hosted
  models with TLS (D180); and `/api/generate`, whose `context` carries
  the conversation where the program can't see it.
- **The messages:** `(agent/new url model system)` gives the agent's pid;
  `(agent/say a text)` sends it `(:say text from)`, and `from` hears
  `(:piece a text)` as the model streams, then `(:answer a text)`, the
  whole answer, or `(:error a reason)` in their place. A caller that
  doesn't want the pieces waits for the `:answer`.
- **The conversation is the agent's state.** A question and its answer
  join it once the answer is whole, so a turn that fails leaves it as it
  was, and asking again is clean.
- **One turn at a time:** a `:say` that comes while an answer is on its
  way waits in the agent's state, oldest first. Rejected: answering
  `(:busy a)`, which every caller would have to handle.
- **A turn is a process of its own,** the stream's caller. It sends the
  pieces, and ends with the answer or raises why there's none; the agent
  monitors it, and hears how it went in one `(:exit ...)`. So the stream's
  messages never reach the agent, and a turn stopped early leaves none
  behind in its mailbox (after a kill, a stream's last message can still
  be on its way, D182); and a fault reading a reply ends just the turn.
  The turn monitors the agent, so killing the agent ends the turn, and
  with it the stream (D182). Rejected: the agent as the stream's caller,
  which would have to drop an old stream's messages in every state.
- **Errors:** the stream's reasons; `(:http status message)` for a reply
  that isn't a 200, with ollama's `{"error": ...}` or else the body's
  first line; `(:model message)` for an error line in a 200. A line that's
  neither a piece nor an error is skipped.
- **The chat** reads keys with `:keypress` and edits a line in slight
  (D29), since reading the terminal as a file would hold up the whole
  runtime (D130). The keyboard's process is the agent's caller, so pieces
  and keys come to one `recv`; keys typed during an answer wait for it.
  The model and the URL come from `@ARGV` till the environment can be
  read. Like `top`, it isn't a golden test: it needs a model.
- **Checked:** golden test 224, against a fake ollama in the program (the
  history in order, pieces before the answer, two questions at once, an
  error in the middle of an answer, a 404, a refused connection, and a
  kill in the middle of an answer that closes the connection), also under
  `SLIGHT_SCHED` seeds and `SLIGHT_GC_STRESS`. The chat by hand, on both
  targets, against a Python stand-in for `/api/chat` that checks what it's
  sent, with keys piped and in a pseudo-terminal (raw mode on while it
  runs and off after, Backspace, keys typed during an answer). Not yet
  against a real ollama: this environment can't fetch a model.
- **Left for later:** tools (and `:exec`), stopping an answer without
  ending the agent, saving a conversation, agents talking to each other,
  the model's options (temperature, the context's length), and the
  figures on the last line (tokens, time).

**D185. `:exec`, a device for running a command.** *(User agreed to
Claude's proposal, every point as recommended.)* For tools (step 15):
`(connect :exec (list "ls" "-l") expr)` runs a command, a list of
strings with no shell unless one is asked for (`(list "sh" "-c" cmd)`),
on a device the new process owns.
- **Its messages:** `(:open p)`; its output as a connection's (lines at
  first, `(:read how)` as for any reader); `(:eof p)`; and once the
  command has ended too, `(:exited p status)`, after which the device
  closes. Not PLAN.md's `(:exit p status)`: a pattern doesn't compare a
  name with one already bound, and `recv` looks only at the first
  message, so a process that owns a command and monitors anything (an
  agent's turn monitors the agent, D184) couldn't tell it from
  `monitor`'s `(:exit pid result)`. Rejected: `(:exited p status)` in
  place of `(:eof p)` (a clause fewer, but the only reader without
  `:eof`). The status comes after both ends, so a command that leaves
  something running with its output (`cmd &`) isn't done till that ends
  too, as with Python's `subprocess.run`.
- **Status:** the exit code, or `(:signal name)`, `name` being one of
  15 (`:hup :int :quit :ill :trap :abrt :bus :fpe :kill :usr1 :segv
  :usr2 :pipe :alrm :term`: the same on macOS and Linux, where some of
  their numbers aren't), or the number of any other. Rejected: one
  integer, 128 + n, as a shell's `$?` (`exit 137` would look like
  SIGKILL, and n differs between the systems).
- **stderr goes with stdout,** one pipe for both: a model sees errors
  where they happened, in the order written, and that order is the
  command's own, so tests are exact. `sh -c "cmd 2>/dev/null"` gives
  stdout alone. Rejected: `(:err p s)` messages (a second pipe and
  buffer, a `:read` that would have to say which, and an order between
  the two that depends on timing); passed through to the program's
  stderr (a model wouldn't see it, and it would write over a terminal UI
  such as the chat).
- **stdin is a pipe.** `(:write x ...)`, from anyone, rendered as
  `tty/write` does and kept till the pipe takes it, as a socket's; and
  `(:eof)` (the word a device says when its input ends, sent the other
  way) closes it once that's written, while the output still comes. A
  command that reads its input waits for it till then, so a tool with no
  input sends `(:eof)` in its `(:open p)` clause. Input the command
  doesn't take (it has ended, or closed its stdin, as `head -1` does)
  goes nowhere, and so does what's written after `(:eof)`: not `:epipe`,
  which would end the owner, who still wants the output and the status.
  Rejected: input closed from the start unless asked for (another source
  name, or an argument).
- **Ending it:** the command runs in a process group of its own.
  `disconnect`, or the owner ending, kills the group with SIGKILL (so all
  of `sh -c "a | b"` goes) and the command itself (under qemu, whose
  `posix_spawn` returns before the child has run, it may not be in its
  group yet: found by golden test 227 hanging there), then waits for it,
  which a killed process can't hold up for long; nothing comes after.
  The program's end, however it ends (a deadlock, Ctrl-C in raw mode,
  SIGINT, SIGTERM or SIGHUP), kills every group still running: `atexit`,
  and handlers for the three signals (not one that's ignored, as `nohup`
  leaves SIGHUP), which then die of the signal as before. Rejected:
  SIGTERM, then SIGKILL after a grace period (a timer of the runtime's
  own for each command, which on the virtual clock would have to wait
  for real). A `(:signal name)` to the device can come if a tool wants
  to stop a command gently.
- **Noticing its end:** SIGCHLD's handler, on a signal stack of its own
  (a process's may be nearly full), writes a byte to a pipe that
  `select()` waits on while any command runs; then each is asked with
  `waitpid` and `WNOHANG`. Rejected: polling with `WNOHANG` when the
  output ends, since on Linux the pipe closes a moment before the
  command can be waited for, so it would need a timer to poll again.
- **Starting it:** `posix_spawn`, the same on both systems, with the
  pipes as file actions, its own group, and SIGPIPE back to its default
  (the runtime ignores it, D136, and an ignored signal stays ignored
  across exec, so `yes | head` would misbehave in a command). It inherits
  the program's environment and working directory (`env K=V cmd`, and
  `sh -c "cd d && cmd"`, change them). The runtime finds the command in
  `PATH` itself, as `execvp` would, since under qemu-user `posix_spawnp`
  reports a command that isn't there as a child that exits 127, not as
  `ENOENT`, and the targets must agree. One that can't be started ends
  the owner before it runs: `(:error (:enoent "ls"))`; `:eacces` for one
  that can't be run (a directory, a file without its x bit); `:io-error`
  for an argument with a NUL in it, as for a path (D130). A command that
  isn't a list of strings, at least one, faults in the caller
  (`:not-a-cons` or `:not-a-string`), as a path that isn't a string does.
- **Replay:** starting a command (the search, the spawn and its pipes) is
  one call through `replay.c`, its output's reads are reads, and waiting
  for it and killing it are calls too; so a replay runs, kills and writes
  nothing. SIGCHLD's pipe is made through it too, and what `select()`
  said of it is in the record with the rest.
- **The virtual clock:** a command's pipes are waited for in `select()`
  as sockets are, on the same list, and it counts as something that can
  still happen while there's something to wait for: output its owner
  waits for, input to write, or its end, once its output has ended. A
  command whose input is closed can only be working on its own, so on
  the virtual clock, while its owner waits for its output or its end,
  nothing else moves, keys or time: the runtime waits for it, for real.
  One whose input is open may be waiting for the program, so it counts
  as a socket does (D139). So a tool's timeout around a command that ends
  doesn't fire first in a test just because `select()` looked before the
  command had written anything (golden test 230 fails without this).
  Rejected: always as a socket, which would make any test with a timer
  around a command race.
- **Limits:** none for now beyond the caller's own timeout (`after`,
  then `disconnect`) and `:too-big` for a message of 64 MB or more. No
  sandbox: a command runs as the program's user. Its total output isn't
  capped: a tool keeps what fits a model's context, and disconnects past
  it. `ulimit` in `sh -c` limits CPU and memory with nothing new. Each
  command takes two descriptors of the 1,024 `select()` can wait on.
- **Tests** (225–230), on the virtual clock, with commands that behave
  the same on macOS and Linux (`sh -c`, `printf`, `cat`, `kill`): lines,
  stderr, statuses and signals, and a monitor beside a command; input
  all at once, a line at a time, from another process, closed by a
  timer, to a command that doesn't read it, and a megabyte through `cat`
  and back; chunks, a count, JSON and `:too-big`; commands that can't be
  started, dead letters and faults; a command stopped when its owner
  raises, disconnects or is killed (another command's `kill -0` shows
  it's gone), and one handed over; the clock. `t/replay.sh` records and
  replays commands, one stopped early, and checks that the replay runs
  none. Also run under `SLIGHT_SCHED` seeds and `SLIGHT_GC_STRESS`, and
  under valgrind, with the file, socket and HTTP tests (the socket code
  changed too). By hand: SIGINT, SIGTERM and SIGHUP to the runtime, a
  deadlock and a `disconnect` each kill a command and a grandchild in its
  group.
- **Left for later:** a `run` that gives `(:ok (status lines))`, with the
  tools; a command's own environment and working directory; a signal to
  a command without closing it; stderr apart from stdout.

**D186. `%ENV`: the environment, as the top level's second parameter.**
*(User chose it from Claude's two, A of A and B, and named it `%ENV`
"to match the perlish `@ARGV`"; the details are as proposed.)* For a
model's address or a key (D180). `%ENV` is the program's environment as
an association list, `(("HOME" "/home/s") ("OLLAMA_HOST" "...") ...)`,
read with `lookup`, which gives `:not-found` for a variable that isn't
set; so nothing new for that, and a default is `(if (eq? h :not-found)
... h)`.
- **Why a parameter, as `@ARGV` is (D144):** the environment is the
  program's input, given to the root. Only the top level sees it; a
  `defun` that names it is a compile error saying to pass it on, and a
  lambda or a `fork` at the top level captures it as any local. So a key
  reaches only the code it's handed to: a tool, or a library such as
  `lib/agent.slight`, can't read `ANTHROPIC_API_KEY` unless its caller
  passes it. (A command can still run `env`: `:exec` gives every command
  the whole environment, D185, and that stays till devices are
  restricted, if they ever are.) Rejected: `(env "NAME")`, a builtin
  readable anywhere (simpler to build, and a record would hold only what
  was read, but any process or library could read any key: the shape
  D144 turned down for `@ARGV`); a device, `(connect :env expr)` (a
  process and messages for a constant).
- **What's in it:** every variable, as a `(name value)` list of two
  strings, in the environment's own order; an entry is cut at its first
  `=` (a value may have `=` in it), and one with no `=` is left out; a
  name that's there twice is found first where `getenv` finds it.
  Nothing for a missing variable but `lookup`'s `:not-found`; a
  `lookup-or` with a default can come to the prelude if wanted.
- **How:** `slight_main` takes `@ARGV` and `%ENV`, in that order
  (`TOP_PARAMS`, `values.ts`), and the runtime's `main` builds both in
  the root's heap. `split.ts` carries both across a `recv` at the top
  level, under names no program can write, as it carried `@ARGV`. The
  top level's frame has a slot more, so a few unit tests' offsets moved
  by 8 bytes.
- **Replay:** the record keeps the environment in its header, after
  `@ARGV`, and a replay's `%ENV` is the record's. So a record holds every
  variable, keys included: records are made only when asked for, to debug
  where they were made, and the docs say to keep one as the environment
  itself. The record's format changed, so its first line is now `slight
  record 2`, and an older record is refused as not a record.
- **The chat** takes its URL from its second argument, then from
  `OLLAMA_HOST`, as ollama reads it (no scheme means http, and no port
  means 11434), then `http://localhost:11434`. Checked by hand on both
  targets against a stand-in for `/api/chat`.
- **Tests:** golden test 231 (`; env:` sets three variables: one set,
  one empty, one with `=` in its value; one unset; every entry a pair of
  strings; `%ENV` in a lambda, a `fork`, and across a `recv` at the top
  level); unit tests for the compile errors, the second slot, and the
  carrying; `t/replay/world.slight` reads a variable set when recorded
  and unset when replayed.

**D187. Tools: a function of the model's arguments, each call in a
process of its own.** *(User agreed to Claude's proposal, every point as
recommended.)* Step 15's "tools as messages", on ollama's tool calling:
the request carries `tools`, each a name, a description and a JSON
Schema for its parameters; a reply's message may carry `tool_calls`,
each a name and an object of arguments, a whole call in one chunk when
streamed, and no ids; and the results go back as `{"role": "tool",
"content": ..., "tool_name": ...}` after the assistant's message with
the calls.
- **A tool is a function**, `(lambda (args) ...)`, and each call runs in
  a process of its own: one that faults, raises or is killed ends alone,
  and its value, rendered as `concat` renders it, is the result. A tool
  with state is a function that `ask`s a process, and one that runs a
  command does it with `(join (connect :exec ...))`, since `join` works
  in a lambda. Rejected: PLAN.md's first sketch, a tool as an actor that
  takes `(:call args to)` and answers with `reply`, which made every
  tool, stateless or not, a long-lived process with a loop, and a tool
  that died in the middle of a call something each call had to guard
  against; the function covers it with less.
- **`(tool/new name description params f)`,** `params` a list of `(name
  type description)`, `type` `:string`, `:number`, `:integer` or
  `:boolean`, each required; the library builds the JSON Schema, at
  once, so a bad type faults there. Rejected: the schema written as
  `json/print` data (four nested `:object`s for one parameter); a schema
  read from a doc block (a parser for it); a tool that describes itself
  when asked (a round trip, and the schema written all the same).
  Optional parameters, enums, arrays and objects can come when a tool
  wants one.
- **What a tool is given:** the arguments as `json/parse` makes them,
  less the `:object`: a list of `(name value)` for `lookup`. Nothing
  checks them against the parameters: a tool that finds one missing or
  wrong raises, and the model, told why, can try again.
- **Results:** `"error: reason"` for a call that raised, faulted or was
  killed, and `"error: there's no tool called x"` for a name the agent
  doesn't know; neither ends the turn. A result over 16 KB is cut there,
  with `[cut: n more bytes]` after it, so a command that prints a
  megabyte doesn't fill a model's small context; one constant, where
  every tool would otherwise cap its own.
- **The caller hears** `(:calling a name args)` for each call before any
  runs, then `(:called a name result)` for each, in the calls' order,
  once all have finished; `(:piece a text)` and `(:answer a text)` as
  before, the answer being the last reply's text.
- **A turn is rounds:** the reply is streamed as before, its calls
  collected from any of its lines; with calls, they all run at once,
  each in a process the turn monitors, and the assistant's message with
  the calls and a tool message for each go back to the model for
  another round; without, it's the answer. All at once, since a model
  that asks for several in one message treats them as independent, and
  processes make it cheap. If the agent ends while calls run, the turn
  kills them and ends. A turn is still all or nothing (D184): the
  question, each round's messages and the answer join the conversation
  once the answer is whole.
- **At most 10 rounds a turn:** one whose tenth reply still calls tools
  fails with `(:rounds 10)`, without running them, since a small model
  can call tools in a loop for ever. No time limit for a call: a tool
  that might hang sets its own `after`, as a command's owner does
  (D185).
- **The tools are the agent's,** fixed when it starts: `(agent/new url
  model system tools)`, `()` for none, which sends no `tools` at all.
  `agent/new` took three arguments before; the chat and test 224 pass
  `()`.
- **Tests:** 232, against a fake ollama in the program: the schema the
  model sees, a call after a piece of text and the history after it, two
  calls at once, a tool that raises, one that isn't there, a result cut
  short, a model that keeps calling (stopped at its tenth round, the
  history left as it was), an agent with no tools, and an agent killed
  while a tool runs, which kills the tool. Also under `SLIGHT_SCHED`
  seeds and `SLIGHT_GC_STRESS`, and valgrind.
- **`exec/run`,** the helper D185 left for later, in an opt-in
  `lib/exec.slight`: `(exec/run cmd)` runs a command with its input
  closed and gives `(:ok (status output))`, the output whole, or
  `(:error reason)`. Golden test 233.
- **The chat** has two tools: `read_file` (`slurp`), and `run`, a shell
  command, which asks the keyboard's process first, with `ask`: the
  person sees the command and answers `y` or `n` (`n` tells the model
  "the user didn't let it run"), and another command asked about
  meanwhile waits its turn. The keyboard's process makes the agent, so
  the tools can name it (`$$` in the `connect`). Calls and results are
  shown as they happen. The `run` tool has no time limit (2 minutes since
  D234), unlike what
  the proposal suggested a tool might set: a timer can't be cancelled
  yet, so one set for every command would keep the chat running after
  Ctrl-D till it was due; a command that doesn't end is stopped with
  Ctrl-C, which ends the chat and kills the command's group (D185).
  Checked by hand on both targets, in a pseudo-terminal, against a
  stand-in for `/api/chat` that calls the tools: `y`, `n`, and a file
  read, with the whole conversation sent each round.

**D188. `lib/tools.slight`: tools to read, find and change files, and
run commands.** *(User chose these, the first of four batches Claude
proposed; the details are defaults.)* The chat's two tools (D187), made
general, and four more, so the chat is a small coding assistant.
- **A library of constructors,** opt-in: each function makes a tool,
  and a program gives its agent those it wants. Those that change
  something take `approve`, a function of what the tool is about to do,
  as text for a person to read, giving `#true` to go ahead: the chat
  makes it ask the keyboard's process, a script can say yes to all, and
  a test records what it was asked. A refusal is a result ("the user
  didn't let it ..."), not an error, so the model knows to stop.
- **`read_file`** reads in chunks, so the bytes come as they are;
  **`list_files`** is `ls -1Ap -- path`, and says "the directory is
  empty" rather than nothing, which a model reads as no answer;
  **`search`** is `grep -rnIF --exclude-dir=.git -e text -- path`: `-F`,
  since a model's text often has a bracket or a dot in it, and a
  pattern would surprise it; status 1 is "no lines contain it". The
  commands take their arguments directly, with no shell, and `--`, so
  a path can't be read as an option.
- **`write_file`** writes the whole file; `approve` sees the content,
  each line after `+ `.
- **`edit_file(path, old, new)`**, as Claude Code's: `old` must be in the
  file once, exactly (a second, even overlapping the first, would leave
  which to change to chance), or it raises, before asking; the file is
  read in chunks and written whole, so the rest stays byte for byte, its
  last newline included. `approve` sees `old`'s lines after `- ` and
  `new`'s after `+ `.
- **`run`** is the chat's, moved here: `sh -c`, run to its end
  (`exec/run`), with its exit status.
- **An argument** that's missing or not a string raises, and the model
  is told which.
- **`tool/name` and `tool/call`** in `lib/agent.slight`: a tool's name,
  and a call made here as the model's would be, for tests and for tools
  built on others.
- **The chat** has all six, with one `approve`; the question shows what
  the tool showed it, and a call's arguments are shown as their first
  line, cut at 60 bytes, so a file's content doesn't fill the screen.
- **Tests:** 234 calls each tool as the agent does, in a process of its
  own, with approvers that say yes and no: writing, reading, listing an
  empty directory and a full one, finding (a bracket matched as itself),
  editing and its errors, refusals that leave everything as it was,
  running, and arguments that are missing or not strings. By hand, on
  both targets: the chat in a pseudo-terminal, against a stand-in that
  calls each tool, with `y` and `n`.
- **Next** (the user's order): `fetch`, through curl, which asks before
  every call, since a URL can carry anything out; then `calc` and notes,
  in slight; then `ask_agent`. Left for later: reading part of a file,
  and keeping the tools to one directory.

**D189. `fetch`: a URL through curl, a page made plain text in slight.**
*(User: "fetch should ask for each call"; the rest are defaults.)* The
second batch of D188's tools. HTTPS waits for TLS (12c), so curl, through
`:exec`, is how a local agent reaches the web for now (the user's call).
- **`fetch(url)`** takes `http://` and `https://` only, since curl reads
  `file://` and much else; runs `curl -sSL --max-time 20 --max-filesize
  5000000 -w ... -- url`; and asks `approve` with `fetch url` before
  every call, since a URL can carry anything out (`?data=...`).
- **What came back:** curl's `-w` writes the status and the content type
  after the body, behind a line no page has (`@@slight-fetch@@`), which
  is split off. HTML (a type with `html` in it) is made plain text; text,
  JSON, XML and JavaScript come as they are, and so does a body with no
  type that's valid UTF-8; anything else is `not text: type, n bytes`. A
  status that isn't 2xx goes before the text, `HTTP 404`, since an error
  page can read like an answer. A failure (curl's own status, not the
  server's) raises curl's message, without the trailer or anything
  fetched before it.
- **HTML to text in slight,** `tools/html->text`, for a model's small
  context: the HTML is split at each `<` (in C, once), so each piece is
  a tag and the text after it, and the pieces are read in one pass;
  comments go first, split at `<!--`, since one can hold a `<`; scripts,
  styles, SVG and `noscript` go with what's in them, till their closing
  tag; a `<` that doesn't start a tag (`a < b`) stays. A block's tag
  starts a line (an item's with `- `, a cell with ` | `, and the ends of
  items and rows don't, so a list is on lines one after another);
  entities are decoded, named and numbered, a numbered one that isn't a
  character left as it was; space is squeezed per line, with at most one
  blank line in a row. About 0.7 seconds for 1.7 MB of HTML on the cloud
  machine. Rejected: a converter outside slight (`lynx -dump`, `pandoc`),
  which not every machine has; a full HTML parser, for a model that only
  needs the words and their order.
- **Tests:** 235: the conversion, on HTML with each case, empty HTML, and
  text with no tags; then the tool, through the real curl, from a server
  in the program (`http/serve`): a page, JSON, plain text, an image, a
  404, a redirect, a refused connection (curl's message), a URL that
  isn't http, and one the approver refuses. Also under seeds,
  `SLIGHT_GC_STRESS` and valgrind. By hand: the chat, in a
  pseudo-terminal, fetching over HTTPS with `y`, and refused with `n`.
- **Left for later:** a page's links (a model can't follow what it
  can't see: `[text](url)` would show them, at a cost in context); a
  charset other than UTF-8; POST and headers.

**D190. `calc`: arithmetic parsed and worked out in slight.** *(User
asked for it, the third of D188's batches; the details are defaults.)*
A small model gets arithmetic wrong, and a tool is the usual cure.
`lib/calc.slight`, `(calc/eval s)`, since arithmetic is of use beyond
agents, and `(tools/calc)` around it.
- **Infix, as people and models write it,** not s-expressions, which a
  model writes less reliably: numbers (`.5`, `1.5e3`), `+ - * / %`, `^`
  and `**`, parentheses, and `×`, `÷` and `−`. `^` first and from the
  right, then a sign (`-2^2` is -4, as in maths and Python), then `* /
  %`, then `+ -`. Recursive descent, each step giving its value and the
  tokens left; the expression is short, so nothing is done for speed.
- **Whole numbers stay whole** while a float's estimate of the answer is
  under 4×10^18, well inside 62 bits, so `2^61` and `10^18` are exact;
  past that the answer is a float, rather than slight's `:overflow`,
  which would end the call with a fault logged to the terminal. A whole
  power is worked out by squaring; a division that comes out whole is
  whole (`6/2` is 3), and anything else is a float. `%` is slight's (the
  sign of its left side), and on floats C's `fmod`. Rounding a float too
  big for an integer, or one that isn't a number, gives it back as it
  is, since `floor` and the rest would fault.
- **Functions:** `sqrt abs floor ceil round sin cos tan exp`, `min` and
  `max` of one or more, and `pi` and `e`, any case. No `log`: slight has
  no logarithm, and adding one is a builtin to propose, not a library's
  to fake.
- **Errors** raise what's wrong and the byte where, for a model to mend
  its expression: an empty one, one that ends too soon, a `(` not
  closed, a token where something else should be, division by zero, a
  name that isn't one (with the list of those that are), an argument
  count, a character that isn't arithmetic, a point with no digits, and
  a number too big. A comma says numbers are written without them,
  since `1,000` is what a model writes for a thousand.
- **Tests:** 236: each rule, the functions and constants, the errors,
  and the tool through `tool/call`; no libm function but `sqrt` is asked
  for anything but an exact answer (D76). By hand: the chat, with a
  stand-in that calls `calc`, on both targets.

**D191. Notes, kept from one conversation to the next.** *(User asked
for them, the third of D188's batches with `calc`; the details are
defaults.)* `remember(key, text)` and `recall(key)`, so an agent can
keep what it learns: "a tool with state asks a process" (D187), in
practice.
- **A store is a process,** `(notes/open path)`, holding the notes as
  `(key text)` pairs, newest first, and answering `ask`s (`lib/ds`):
  `notes/get`, `notes/keys` and `notes/put`. One process, so two calls
  at once can't lose a note.
- **On disk, an s-expression:** the store reads the file when it starts
  and writes all of it back after each change (`sexp/print`), so a
  person can read and edit it, and the program can end at any time
  without losing a note. A file that isn't there, or is empty, is no
  notes; one that isn't a list of string pairs ends the store, rather
  than be written over and lost. A write that fails is an error for the
  call, and the store goes on as it was, the note not kept.
- **No asking:** `remember` writes only the notes file, which the
  program chose, so it doesn't ask `approve`, as `write_file` does.
- **`recall` of a key with no note gives the keys there are** (or "there
  are no notes yet"), so a model that guessed a key wrong can see the
  right one without a tool to list them.
- **The chat** keeps its notes in `~/.slight-chat-notes` (from
  `%ENV`'s `HOME`, or the current directory without one), and, when
  there are some, its system prompt names their keys, since a model
  doesn't think to recall what it can't know it has.
- **Tests:** 237: no notes, remembering, replacing, recalling a key that
  isn't there, the file written, a second store on the same file, a
  file that isn't notes, and one that can't be written. By hand: two
  chats, the first remembering, the second, on the other target, told of
  the key and recalling it.

**D192. Agents asking agents: `agent/ask` and `tools/ask-agent`.** *(User
asked for it, the last of D188's batches; the details are defaults.)*
PLAN.md's "agents talk to each other in messages", in its simplest form:
one agent as another's tool.
- **`(agent/ask a text)`** gives `(:ok answer)` or `(:error reason)`, as
  `http/request` gives a Result where `http/stream` gives messages. A
  process of its own asks and waits, so the pieces and the calls it's
  told of go nowhere else, and since it's `join` underneath it works in a
  lambda, so in a tool. It monitors `a`, so an agent that has ended, or
  ends while it answers, is `(:error (:ended a result))`, not a wait
  for ever.
- **`(tools/ask-agent name description a)`** makes a tool of an agent,
  with one parameter, `question`, described as needing to be in full,
  since the agent asked knows nothing of the conversation it's asked
  from. Its name and description are the program's, since a program
  may have several (an expert, a reviewer). It doesn't ask `approve`:
  the agent asked has its own tools, which ask about what they do. The
  agent asked keeps its own conversation, from one question to the next.
- **Not guarded against:** an agent given itself as a tool waits for
  itself, one turn at a time (D184). The docs say not to; a check would
  need a tool to know whose it is.
- **The chat:** with `CHAT_EXPERT` set to a model, an agent on it ("Answer
  briefly and exactly.", no tools) is the chat's `ask_expert`. A variable,
  not a third argument, so it needn't come with the URL.
- **Tests:** 238, against a fake ollama: an answer, nothing else reaching
  the caller, a model that isn't there, an agent that has ended, an agent
  as a tool and one whose model isn't there, and the asked agent's
  conversation going on. Also under seeds, `SLIGHT_GC_STRESS` and
  valgrind. By hand: the chat with `CHAT_EXPERT`, on both targets, the
  expert sent its own system prompt and no tools.

**D193. A fetched page's links, and Escape to stop a command.** *(User:
"keep the links in fetch, and do the escape to stop a running command";
the details are defaults.)*
- **Links** are `[text](url)`, as Markdown has them, which models read
  well: D189 left them out, and a model can't follow what it can't see.
  The URL is resolved against the page's own, which curl's `-w` now
  gives too, `%{url_effective}`, after any redirects (the status, the
  URL and the type a line each, since a type can have a space in it):
  `//host/x` takes the page's scheme, `/x` its host, `?x` its path, and
  `x` its directory; a `../` is left in, for the server to take out. A
  link to a place on the same page (`#top`), or by a scheme a fetch
  can't follow (`mailto:`, `javascript:`), or with no words (an image's)
  is its text alone. A link's text is put on one line, since a heading
  or a paragraph inside one (a card) would break it, with the space or
  line breaks around it kept outside. `href` is found after a space, so
  `data-href` isn't it, in any case, its value quoted either way or not,
  its entities decoded. A new `<a>` ends an open one, as browsers do, and
  the end of the page ends the last. `tools/html->text` takes the base
  as a second argument, `""` leaving a URL as it's written. The cost is
  context: a page of links is longer, and the 16 KB a model is given of
  a result (D187) holds less of its text. Rejected: numbered links with
  a list at the end (`[1]`), which a model has to look up; links only
  when asked for, a second tool or a parameter, more for a model to
  choose between.
- **A command ends with the process that ran it.** `exec/run`'s process,
  the one `connect` starts, monitors its caller (`^$$`) and ends when it
  does, which closes the device and so kills the command's group (D185).
  Before, a command whose caller was killed ran to its end, read by a
  process no one would join. So killing a tool's call stops its command
  or its fetch, whoever kills it.
- **Escape, in the chat,** kills the calls the person has said `y` to in
  this answer: the approve function now sends the asking call's pid with
  what it's about to do, and the keyboard keeps those it let go ahead.
  Killing one that has ended does nothing, so the list isn't trimmed as
  calls end, only emptied by Escape and at the answer's end. The model is
  told the call's result, `error: killed`, and answers on. Escape at a
  y/n question is `n`. Escape with nothing running does nothing, and
  isn't kept as a key typed during the answer. Rejected: Escape stopping
  the whole answer, which would mean a way to stop a turn without ending
  its agent; calls that weren't asked about (reading, searching, `calc`,
  an agent asked) aren't stoppable, since they end on their own, or (an
  agent asked) are stopped through their own tools.
- **Tests:** 235, links: as written with no base, and resolved against
  one (each kind of URL, the cases above, a card, an `<a>` left open);
  then through curl, from the server in the program, a page reached by
  a redirect, its relative links resolved against where it ended. 233,
  a command (`exec sleep 900`, its pid in a file) whose caller is
  killed, seen running by a second command and gone by a third; without
  the monitor, the test hangs till its time limit. Also under seeds,
  `SLIGHT_GC_STRESS` and valgrind. By hand: the chat in a
  pseudo-terminal on both targets, against a stand-in for `/api/chat`:
  Escape at the prompt (nothing), Escape while an approved `sleep` runs
  (killed, the model told), two calls at once with the second answered
  by Escape (`n`) and the first then stopped by Escape, and a quick
  command after (unaffected).

## Benchmarks

**D194. Benchmarks: slight against C, Go, Erlang, Node, Perl and
Python.** *(User, Oct 2026: on Claude's proposal, with Perl and Python
added, "it will be interesting to see"; Go and Erlang on every core too,
"to see what it gains"; everything committed, results included; phase 1
first.)* `bench/`, `docs/BENCHMARKS.md`.
- **Three questions:** how good the compiled code is (calls, integers,
  allocating, floats); what the actor runtime's operations cost (a
  message, a switch, a process started, a process waiting); and, later,
  slight against itself over time, on programs (boids, `html->text`,
  JSON, HTTP) that would mostly compare libraries across languages.
- **Against:** C, the floor; Go, compiled, with goroutines; Erlang, the
  same model as slight; Node, the everyday reference; Perl and Python.
  Chez Scheme, a native Lisp, in phase 2. Not Rust (about C), Java (its
  warming up makes it hard to time fairly), Elixir (Erlang's VM), or
  more Lisps than Chez.
- **Phase 1's programs:** `startup`; `fib`, `binary-trees` and `n-body`
  in all seven; `ping-pong`, `thread-ring`, `spawn`, `idle-memory` and
  `parallel-fib` in slight, Go and Erlang. Phase 2: Chez, more of the
  Gabriel and Benchmarks Game programs, Savina's fan-in, and profiling
  slight's worst ratios.
- **Fair, and checked:** each program is written as its language usually
  is, doing the same work, and prints only numbers (slight's last line is
  its root's value), which the harness checks against what it works out
  itself, or, for `n-body`, against C. slight's bend to slight (D84):
  nothing was added to the language for them, and the ones that need
  what it hasn't got (arrays that change, hash tables, bignums, regular
  expressions) are left out. `binary-trees` runs at 18, not its usual 21,
  for every language, since a bigger tree doesn't fit in one slight
  process's 64 MB.
- **Two programs changed** when their first numbers looked wrong, so as
  to measure what they meant to: Erlang's `spawn` was quadratic (its
  mailbox, holding up to a million messages on its heap, walked at every
  collection: 36 s), so it keeps them off its heap, as Erlang's
  documentation says for a long queue (1.3 s); and Go's `spawn` and
  `idle-memory` sent on unbuffered channels, which kept every goroutine
  alive and waiting, so their channels hold every answer, as a mailbox
  would (15 s to 0.8, and 9.4 to 7.0). Erlang's `idle-memory` keeps its
  queue on its heap, where it's faster (4 s, not 7).
- **Measured** by `wait4`, from `bench/rusage.c`, a small process that
  starts each program: on Linux a process forked from Python starts with
  Python's 15 MB counted in its peak. Built once, checked once, then 5
  timed runs taking turns (fewer, never under 3, for a slow one); the
  median and the fastest, CPU time, peak memory. Go and Erlang run on one
  core (`GOMAXPROCS=1`, `+S 1:1`) and with their defaults (`-mc`).
  Erlang's process limit is raised only for `spawn` and `idle-memory`,
  and only as far as they need: a limit of 4 million costs 47 MB from
  the start.
- **Results on the cloud machine** (x86-64; BENCHMARKS.md has them):
  slight is the fastest at sending (106 ns a message), starting a process
  (250 ns) and keeping one waiting (431 bytes, a sixth of Go's or
  Erlang's); between Go and Erlang on calls and the ring; and slowest of
  the compiled ones at allocating (`binary-trees`, 4.2 times Erlang) and
  with floats (`n-body`, 52 times C, 1.3 Erlang). On 4 cores Go gains
  3.7 times on `parallel-fib`, Erlang 2.7.
- **Found, for later** (not changed here): `GC_MIN` lets each busy
  process keep up to 256 KB of garbage, so `thread-ring`'s 503 take 266
  MB; and one structure can't be much over 25 MB.
- **On the M2 Max** (Stevan's run, the same day; the reference): slight
  is the fastest of the three at every actor benchmark on one core, the
  ring included (79 ns a message, 85 a hop, 135 a process started, 554
  bytes a process waiting); a little behind Node and Erlang on `fib`
  (1.2 and 1.1 times), 2.5 times Go; 6 times Erlang on `binary-trees`
  and 1.8 on `n-body`. On 12 cores, Go gains 8.7 times on
  `parallel-fib`. Erlang on one scheduler
  (`+S 1:1`) is on a core only 37 to 60 per cent of the time there,
  unexplained, so the Mac's Erlang numbers are its `-mc` ones for now;
  and slight's peak memory is higher than on Linux (356 MB on
  `binary-trees`, against 98), probably macOS's allocator keeping freed
  chunks.

**D195. Tuples built in place.** *(User, Oct 2026: the first of the fixes
phase 2's profiles found, D194, in the order Claude proposed.)*
- **Before:** `(tuple a b ...)` was a variadic C builtin. The compiler
  made a list of the elements and called `rt_tuple`, which counted the
  list and copied it into the tuple: so a `binary-trees` node allocated
  64 bytes, half of them garbage at once, and called into C, and each of
  `n-body`'s bodies made a list of seven to throw away.
- **Now** it's compiled as `list` is: the elements wait in frame slots,
  then one inline allocation takes the header and the elements (a
  target's `tupleCells`), rounded up to 16 bytes as `tuples.c` rounds
  them. `list->tuple` still goes through `rt_tuple`, which is right for a
  list that's already made.
- **Measured** on the cloud machine: `binary-trees 14`, 1,590 million
  instructions to 862 million (46 per cent fewer), and `binary-trees 18`
  from 3.8 s to 1.95; `n-body 20000`, 642 million to 549 (14 per cent).
- **Tests:** 239: each size from none to three, and seventeen (an even
  count of words and an odd one round differently); the elements worked
  out left to right; tuples in tuples; one whose later elements collect
  the heap while the earlier ones wait in the frame; one sent and
  matched. Also under `SLIGHT_GC_STRESS` and seeds.
- **Not changed:** a two-element tuple is still 32 bytes, against a
  cons's 16, since every heap object is a multiple of 16 bytes and the
  collector and copying count on it; so `binary-trees` at depth 19 still
  doesn't fit in a process's 64 MB.

**D196. Collecting before the heap's cap.** *(User, Oct 2026: "just do
the cap fix for now", of Claude's two changes to when a heap is
collected; growing to three times what's live, not two, was turned down
for now, for the memory it costs.)*
- **Before:** the next collection came when the heap in use reached
  twice what survived the last (D106). A process with more than 32 MB
  live would only collect past the 64 MB cap, so it failed there with
  `:heap`, even with most of its heap garbage. A collection can only run
  at a function's entry (or a `recv`), where every live value is in a
  frame, so it can't simply happen when an allocation finds the cap.
- **Now:** twice what's live, but never within 8 MB of the cap
  (`GC_ROOM`): room for a new chunk (up to 1 MB) and for what's allocated
  before the next function's entry. And never at or below what's live:
  with more than 56 MB live, the next is halfway from there to the cap.
  Otherwise `rt_preempt`, which collects whenever the heap in use has
  reached the threshold, would collect at every slice's end, copying
  the whole heap each time, which is what the first version of this did
  to tests 059 and 130 (each filling the heap with live data).
- **Turned down:** halfway to the cap whenever that's sooner than twice
  what's live. It's never at or below what's live, but it collects
  sooner than needed whenever more than 21 MB is live: 6.2 per cent more
  instructions on `binary-trees 18`, whose stretch tree is 33 MB.
  This rule costs 0.8 per cent there.
- **Costs:** a heap that's all live data collects a few times more
  before it fails (test 059 takes 3.9 s under qemu, not 1.3). A single C
  builtin that allocates more than the 8 MB at once near the cap (a
  `str-join` of big strings) can still fail where a collection would
  have made room.
- **Tests:** 240: 38 MB kept, then 48 MB of garbage, which failed before
  and passes now, on both targets.

**D197. Better code for calls, arithmetic and tests.** *(User, Oct 2026:
the third of phase 2's fixes, D194, in the order Claude proposed.)* Each
value used to wait in a frame slot, and each integer operation checked
both operands, a constant's too: fib's recursive path was 61
instructions on AArch64. Now, in `codegen.ts`, with new shapes from each
target:
- **A simple operand waits nowhere.** A local (in its slot, which
  nothing changes), or a literal integer, `()`, `#true`, `#false` or
  quoted symbol (a word), is read straight into the register it's wanted
  in, when it's wanted: a binary operation's left or right operand, a
  call's argument (a compiled function's or a C builtin's). The last
  argument that does need working out stays in the accumulator, and the
  call's register moves take it from there. A left operand that's
  simple is read after the right is worked out; it has no effects, so
  the order can't be seen. `div` and `%`, which check each operand as it
  comes, read only a right one late, so a left that isn't an integer
  still faults before the right is worked out.
- **An integer literal is an immediate** for `+`, `-` and the
  comparisons, so only the other operand is checked: `adds x0, x0, #2`,
  turned round for a negative one, or a word in a register past twelve
  bits (32 on x86-64). Out of line it's the runtime's right operand, as
  before. `*` keeps its two-register path, which untags one side.
- **A comparison as a test branches**: a compare and a branch to the
  next clause, with no `#true` or `#false` made and tested; a literal on
  the left is turned round (`(< 2 n)` is `(> n 2)`). Out of line,
  `rt_compare` gives a boolean (or faults), which is branched on.
- **A cond whose last test is `#true`**, as an `if`'s is, has no jump to
  the no-clause fault, and its last body runs on into the end.
- **Measured** (instructions, on the cloud machine): fib 30, 131 million
  to 79 (40 per cent fewer), and fib 38's time 0.44 s to 0.36 (medians
  of ten, interleaved; the stores and loads taken out were cheap ones);
  `binary-trees 14`, 8.5 per cent fewer; `n-body`, 3.3, its time being in
  float arithmetic in C; `ping-pong`, 1.3, its time being in the
  runtime. fib's path on AArch64 is 39 instructions.
- **Turned down: the reduction count in a register.** It would save two
  of those 39 on AArch64 and none on x86-64, where it's one subtraction
  from memory already; but the runtime reads and writes the count in
  four places (refilling it, `rt_heap_grow` cutting a slice short for a
  collection, which C builtins reach too, `SLIGHT_GC_STRESS`'s counting,
  and `:system`), which would all have to go through each process's
  saved registers.
- **Tests:** 241: each comparison as a test agreeing with it as a value,
  on integers, floats, mixed and NaN, with literals on either side, past
  an immediate's reach on each target, and near the edges of 63 bits;
  `+` and `-` with literals on integers and floats. The unit tests check
  the new shapes, and that a simple operand takes no slot. Since a
  simple operand is read late and the last argument waits in a register,
  every golden test ran under `SLIGHT_GC_STRESS` too (x86-64): all give
  their output, at 7 or at 100, but the three that fill a heap with tens
  of MB of live data (059, 130, 240), which can't finish under stress,
  each collection copying all of it.

**D198. Float arithmetic in the compiled code.** *(User, Oct 2026: the
fourth of phase 2's fixes, D194, in the order Claude proposed.)*
- **Before:** `+`, `-` and `*` on anything but two integers called
  `rt_add`, `rt_sub` or `rt_mul`, and `/` always called `rt_divide`: a
  call into C, a conversion, and `rt_new_float` through `rt_alloc`.
  `n-body` spent 27 per cent of its time in those calls and 14 in
  `rt_new_float`.
- **Now** the integers' path is as it was, and the out-of-line path
  first tries floats: two float boxes (each told by its tag, then its
  whole header word, as a tuple pattern's test does, D178), or a float
  box when the right operand is an integer literal (made a double as C's
  `(double)` makes it); then the result's box allocated inline, the IEEE
  operation done in `d0` or `xmm0`, and the double stored. Anything else
  (an integer operand that isn't a literal, a string) calls C as before.
  `/` has no integer path, so its float path is inline: two floats, or a
  float and a literal integer that isn't 0, the divisor checked for
  either zero first; anything else, and a zero, is `rt_divide`'s, which
  faults on the zero as it did.
- **To the bit:** the same IEEE operation on the same doubles, with no
  multiply-add fused. Boids flies the same flight, byte for byte.
- `*` got an integer-literal path too (the literal in a register, and
  only the other operand checked), which is what lets a float times a
  literal take the float path.
- **Measured** on the cloud machine: `n-body 20000`, 531 million
  instructions to 378 million (29 per cent fewer), and `n-body 1000000`
  2.4 s to 1.25; boids (60 birds, 30 frames), 4.4 per cent fewer, its
  time being mostly in messages.
- **Not done:** comparisons on floats still ask `rt_compare` (with a NaN
  each target's branches differ: AArch64's condition codes, x86's parity
  flag); `sqrt` and the other math builtins are still C; and floats are
  still boxed, each result a 16-byte allocation. Unboxed floats (PLAN.md's
  "Faster floats") are still a proposal of their own.
- **Tests:** 242: the four operations on floats, signs, zeros,
  infinities and NaN, with literals, and long runs that collect, the
  expected output from Python's IEEE arithmetic; and what still goes to
  C, which must agree: integer operands that aren't literals, an integer
  divided by one, zero divisors and a string, with their faults. Also
  under `SLIGHT_GC_STRESS`, with boids. Unit tests for the shapes.

**D199. A message with nothing to copy leaves nothing behind.** *(User,
Oct 2026: point 5 of the generational collector's note, which can come
first, on Claude's recommendation.)* A message is a chunk holding its
record and the copy of its value (D177), and taking it linked the chunk
into the taker's heap, where the record was dead space till the next
collection. For a message with nothing to copy (an integer, a keyword,
static data) the chunk is just the record, 32 bytes, so a process taking
many such messages filled its heap with them, up to the 256 KB a heap
reaches before it's ever collected (D106): `thread-ring`'s 503 processes,
266 MB. Now `recv` frees such a chunk as it takes the message, having
read its value and device first. `thread-ring` on the cloud machine: 3.3
s and 266 MB to 0.74 s and 2.6 MB, the memory having been most of the
time too. A message with something to copy is as before. Test 243: a
thousand integers leave an empty heap, where before they left 32 KB;
shown so as not to depend on when a heap is collected, and so also under
`SLIGHT_GC_STRESS`. On the M2 Max: 0.88 s and 210 MB to 0.54 s and 2.0
MB, 54 ns a hop, where Go takes 106 and Erlang 229; nothing else moved.

**D200. A generational collector.** *(User, Oct 2026: the design note's
five points, each as Claude recommended; the fifth came first, as
D199.)* Every collection copied everything live, so what lived long was
copied again at each one, about a byte copied for each allocated:
`binary-trees` spent 63 per cent of its instructions collecting, `n-body`
11. Now a process's heap has two generations (DESIGN.md, "Process heaps
and GC"):

- **The young one** is a nursery, one chunk, allocated in as the heap
  was (`heap_ptr` and `heap_limit`, so compiled code doesn't change): 4
  KB at first, twice that at each collection that finds it overflowed,
  up to 1 MB. Once it's full, overflow chunks, listed after it, till the
  next function's entry, which `rt_heap_grow` brings at once by cutting
  the slice short, as for D196.
- **A minor collection**, at a function's entry or a `recv` once the
  nursery has overflowed, copies what's young and reachable into the old
  generation, all of it (promotion en masse, point 1), following only
  pointers into the young generation (a range check), so it costs what
  survives; then the nursery starts again.
- **A major collection**, once the old generation is twice what was live
  after the last (D106, with D196's room), is the collection there was:
  everything live, in both, copied into fresh chunks.
- **No barrier**: nothing old ever points at anything young, since an
  object only points at objects made before it (nothing changes once
  made; compiled code and C builtins fill an object before any
  collection can come; adopted chunks point only into themselves). So
  nothing in the compiler or the C builtins changed.
- **The old generation** is the chunk list as it was: what's promoted
  goes into its first chunk, messages, a fork's values and joined
  results are adopted behind it, and strings of 64 KB or more (point 3)
  are allocated straight into it, a chunk each. The 64 MB cap counts
  both generations, and so does `:system`'s `:heap`.
- **A nursery that grows** (point 2), not a fixed one, which every
  process that allocates at all would hold, and which on Linux would be
  a mapping of its own at 256 KB.
- **Fields:** a first version added five to `rt_proc_t`, and a waiting
  process cost 36 bytes more (`idle-memory`, a million: 433 MB to 469).
  Now the overflow chunks hang off the nursery, one count of bytes
  covers both generations, the chunk promoted into is kept first in the
  old list so only how far it's filled is stored, and `keypress` sits in
  the padding after `state`: 352 bytes, as before.
- **Testing** (point 4): `SLIGHT_GC_VERIFY=1` walks everything reachable
  before each minor collection, and aborts at an old object pointing at
  a young one. Promotion broken on purpose (a cell's car left
  unforwarded) stopped there with its message; with only
  `SLIGHT_POISON`, a segfault. The golden tests pass on both targets
  under VERIFY, and under `SLIGHT_GC_STRESS=7` (a minor collection at
  every seventh entry and every `recv`, every eighth of them a major
  one) but for timeouts, 10 on x86-64 and 14 under qemu, which pass at
  100, except 059, 130 and 240, which fill the heap with live data and
  never finish under stress, as before. No test sets VERIFY: a walk of
  the heap at each minor collection takes test 244 under
  `SLIGHT_GC_STRESS=100` from 4 seconds to 96. Test 244: a kept list
  built across minor collections, young cells over old ones, a big
  string, a list bigger than the nursery ever is made in one call,
  messages and a joined result under young cells, all checked after the
  rest has been collected round them; small, since under stress every
  eighth collection copies all that's live.
- **Measured** on the cloud machine: instructions, `binary-trees 14`
  789 million to 450 (collecting 3.2 times less, 63 per cent to 35),
  `binary-trees 16` 3,712 to 2,661, `n-body 20000` 378 to 328
  (collecting from 11 per cent to under 2). Times, noisy there:
  `binary-trees 18` 2.2 s to 1.7, `n-body` 1.41 to 1.16, the actor
  benchmarks the same. Peak memory the same, but `n-body`'s 2.9 MB is
  4.7, its 1 MB nursery. Less than the note's guess of one and a half to
  two times for `binary-trees`: its trees of depth 14 and more are
  bigger than the nursery, so each is promoted as it's made and left to
  major collections, which copy the long-lived tree each time. On the
  M2 Max: `binary-trees` 1.35 s to 1.07, `n-body` 1.45 to 1.35, the rest
  the same, `idle-memory`'s 556 MB included.

Turned down, for now: aging (point 1; it wouldn't help those trees,
which outlive a nursery anyway); a write barrier and a remembered set
(nothing needs them); mark-sweep or mark-compact for the old generation;
and a pool of nurseries that waiting processes give back.

**D201. Phase 3: Chez Scheme and five more programs.** *(User, Oct 2026:
"lets add Chez and the other programs now": D194's phase 2 programs,
put after phase 2's fixes.)*

- **Chez Scheme**, a native Lisp, the nearest thing to slight's compiled
  code: compiled ahead of time (`compile-program`, in
  `bench/chez-compile.ss`) and run with `--program`; at its default
  optimize level, which is safe, as slight is; with Scheme's ordinary
  arithmetic, as Scheme is usually written, not `fl+` or `fx+`. Found as
  `chez` (Homebrew), `chezscheme` or `scheme` (Debian), a `scheme` only
  if its `--version` is a bare version, as Chez's is (MIT Scheme's
  isn't). In every sequential benchmark; none of the actor ones, since
  it has no processes of that kind.
- **The programs:** `tak 10` (Gabriel's, of 30, 20 and 10: 101 million
  calls), `nqueens 13` (Gabriel's, as in the R7RS benchmarks), `deriv
  2000000` (Gabriel's) and `spectral-norm 1000` (the Benchmarks Game's,
  on one core), in all eight languages; `fan-in 10000000` (100 senders,
  one receiver) in slight, Go and Erlang. Sized so that slight takes
  about a second on the cloud machine, and Python and Perl no more than
  about 20.
- **Each language's own data**, D194's rule: `nqueens`'s lists are
  arrays in C, Go, Node, Perl and Python, copied where the list version
  makes a new list; `deriv`'s s-expressions are each language's own (in
  C, cells from an arena emptied after each derivative, a symbol a cell);
  `spectral-norm`'s vectors are lists in slight and Erlang, made again
  each step, and arrays changed in place elsewhere. `deriv` prints its
  derivatives' lengths, summed, so that no compiler can skip one.
- **Checked:** `tak`'s and `deriv`'s results are worked out in the
  harness, `nqueens`'s are OEIS A000170's, `spectral-norm`'s are C's
  (at 100, the Benchmarks Game's 1.274219991). Python's sums are loops:
  from 3.12 its `sum` of floats compensates its rounding, and would
  differ from the others in the last bits.
- **`fan-in`'s choices:** Go's channel holds 1,024: unbuffered, every
  message is a switch between goroutines, 4.7 times slower, and from 100
  up the size makes no difference. Erlang's receiver keeps its queue off
  its heap, as `spawn`'s does: 4.9 s to 4.5 on one core, 4.7 to 1.0 on
  every core.
- **Found:** on one core nothing slows `fan-in`'s senders, so in slight
  and in Erlang nearly every message waits in the receiver's mailbox at
  once: 640 MB at ten million (Erlang 637), where Go's channel blocks
  its senders (2 MB). slight's time there is mostly the system's (3.0 s
  of 3.7 on the cloud machine), for that memory; at a million, 0.09 s.
  A mailbox isn't counted in its process's 64 MB. Slowing a sender
  whose receiver's queue is long, or counting a mailbox in the cap,
  would be changes to the runtime, for later if they're wanted.
- The harness reads a version from stderr when stdout has none, as
  Chez's `--version` prints it there.

**D202. A heap's cap is 1 GB, or what `SLIGHT_HEAP_MAX` says.** (Or a
quarter of a container's memory limit, since D233.) *(User,
Oct 2026: the second of three options, as Claude recommended; the
others were keeping 64 MB, and no cap, as in Erlang and Go, where one
runaway process takes the program down when `malloc` fails.)* The 64 MB
came from D58's one mapping per process, which D89's chunks replaced;
D106 kept the figure. A cap costs nothing till it's used now, since
chunks come as they're needed, but 64 MB held a structure of 56 MB at
most: `binary-trees` ran at 18, not its usual 21, and no process could
hold a big file's data.

- **1 GB by default.** `SLIGHT_HEAP_MAX` sets it per run: a number of
  bytes, or with `K`, `M` or `G` after it, and at least `1M`, a
  nursery's most; anything else stops the program at its start with
  exit 2.
- **The room kept below the cap** for its last collection (D196) is an
  eighth of it: 8 MB at 64 MB, as before, 128 MB at 1 GB.
- **What it costs:** a process that does hold hundreds of MB makes
  everything wait through each of its major collections, since they
  copy everything live, on the one core; and while one runs, the
  process's memory can be twice what's live. With 64 MB, neither could
  be large.
- **A device's limit stays 64 MB** (D160), its own constant now: a line,
  a count or a value read from a device of 64 MB or more is `:too-big`
  however big the heap, so an endless line still can't take a server's
  memory.
- **Replay:** the cap is one of the settings a record keeps, as a
  different one could fault where the recorded run didn't; the record's
  format is 3.
- **Tests:** `t/run.sh` sets `64M`, the cap the tests were written for,
  so those that fill a heap or read 64 MB stay as they were, and quick.
  Test 245: at `2M`, a process keeping 256 KB ends and one keeping 3.2
  MB faults; 246: `512K` is refused.
- **A hole from D200, closed:** the cap was checked only where the heap
  grows through `rt_heap_grow` (the nursery overflowing, a big string),
  never as a collection promotes; normally a minor collection comes only
  after an overflow, which checks, but under `SLIGHT_GC_STRESS` the
  nursery need never overflow, and test 245's process kept 3.2 MB
  under a 2 MB cap. Now a heap past its cap after a collection gets no
  room in its nursery, so its next allocation goes to `rt_heap_grow` and
  faults there, with its own site, as before.
- **Measured** on the cloud machine: `binary-trees 21`, its usual size,
  now runs (16.4 s, 676 MB at its peak; at `64M` it faults, as before).
  Its major collections take 0.5 to 1 ms for each MB live: 264 ms with
  267 MB live, 108 to 161 with 200, and 4.8 s of the 16.4 in all. So a
  process with a few hundred MB live can hold everything up for a
  quarter of a second; at 64 MB the most was about 50 ms. The benchmark
  stays at 18 (Stevan): its runs then compare with every one before,
  and a full run doesn't take ten minutes more.
- Not changed: a mailbox still isn't counted in its process's cap
  (D201's `fan-in`).

**D203. The prelude's list functions as loops, and its `not` inline.**
*(User, Oct 2026: option A of three, as Claude recommended, with B to be
looked into later.)* Phase 3's profiles (D201) put about 80 per cent of
`deriv`'s instructions in the prelude's `map`, `fold/l`, the closures
they call, `length` and `not`, and `not` alone at 18 per cent of
`nqueens`'.

- **Loops:** `reverse`, `length`, `append`, `sum`, `product`, `map`,
  `filter`, `remove` and `fold/r` are each a tail-recursive loop of
  their own, with a helper (`reverse-onto`, `map-loop` and so on), not
  `fold/l` with a lambda: the same results, `f` called in the same
  order, and every loop still a tail call, so long lists are as before.
  `fold/l` stays as it was.
- **`not`:** a call to the prelude's `not` compiles inline: as a value,
  the `cond` the prelude says it is; as a test, its argument's test with
  the branches the other way round, so `(not (== a b))` is a compare and
  a branch (D197). A program's own `not` (D80), or a local called `not`,
  is called as before, and `not` as a value is still the prelude's
  function. Anything but a boolean faults `:not-a-bool`, as before; the
  cond test it names is now the argument, at its own place, not a line
  of the prelude.
- **Measured** on the cloud machine, in instructions: `deriv 200000` 924
  million to 592 (36 per cent fewer), `nqueens 11` 383 to 247 (35),
  `n-body 20000` 328 to 310; `spectral-norm` the same. Tests 247 and
  248. On the M2 Max: `deriv` 0.575 s to 0.380, `nqueens` 0.841 to
  0.703.
- **Turned down:** folds as C builtins, which would have to call
  compiled code, which C never does (the collector's roots are the
  frames, D166). **B, for later:** `fold/l` or `fold/r` given a literal
  lambda compiled to a loop specialised to its call site, the lambda's
  body inlined and its free variables passed as arguments (lambda
  lifting), as ts-slight's fold continuations were the fast path all
  its list functions went through; the prelude could then be written
  over folds again, and a program's own folds would gain too, for a
  pass of its own (free variables, generated functions named for
  `:system`, D80, and the eight argument registers). **C:** B, and the
  list functions inlined at each call site, so what `map` is given is
  called directly or inlined: a `deriv` specialised by hand that way
  took 574 million instructions, 38 per cent fewer, against 28 for the
  loops alone (both before `not` was inline); not worth the pass for
  now.

**D204. A value is held to 64 MB while it's read, and nothing a program
gives is copied onto a stack.** *(User, Oct 2026: two findings of a
review of the runtime, fixed as found.)*

- D160 held a value to 64 MB only once it had ended: the cap was what
  building it took. Until it ended, the reader went on reading, holding
  all of it, so an endless value (a string opened and never closed, from
  a socket) grew the device's buffer without limit, as an endless line
  did before D160. Now a reader with 64 MB and no end to the value, the
  datum or the doc block ends its owner with `:too-big`, as it does for a
  line. Test 249.
- `connect :tcp` copied the host of its "host:port" onto the C stack,
  and a device that failed copied its path there for the reason, each as
  long as the program made it (a port may have any number of zeros in
  front). A process's stack is 8 MB, with one guard page under it, and C
  runs with 64 KB of it. Now the host goes in a buffer of 256 bytes, a
  host longer than that (than any name can be, 253) isn't found, and a
  device keeps its path as a string from the start, which the reason
  uses as it is. Test 250.
- **For later:** building the runtime with `-fstack-clash-protection`
  too, once it's checked that Xcode's clang takes it for both targets.

**D205. What the model says is shown as it is, and other hardening
from the review.** *(User, Oct 2026: the first two groups of the
review's list, as Claude recommended, the rest to be talked over.)*

- **`tools/printable`.** A terminal takes control characters as
  commands, so a model, or a page or file it has read, could make what
  the person is asked to approve differ from what's done: a command
  with part of it hidden, say. Each tool that asks `approve` now gives
  it the text made printable: control characters but newline and tab,
  DEL, the C1 controls, Unicode's bidirectional embeddings, overrides
  and isolates, and stray bytes, written as escapes. The chat writes
  everything from the model through it too. In slight, in
  `lib/tools.slight`; not a builtin. Test 251.
- **A fetch doesn't follow redirects.** Approval covered only the first
  URL, and curl's `-L` went wherever a page sent it, the machine's own
  services included. Now a redirect is told to the model, and fetching
  where it goes is asked about in turn; and `--proto =http,https` holds
  curl to those. Test 235.
- **The parsers' open lists count.** Building a JSON value or a datum
  needs a frame for each list or object still open in it, which came
  from `malloc`, outside any limit: 64 MB of `[` took a gigabyte. Now
  the frames are allocated as the value is, from the arena a device
  builds in (D160) or the heap, so they count against the same limit.
  Test 252.
- **A record is its owner's alone.** `SLIGHT_RECORD`'s file holds all
  a run read and its environment: it's made 0600 (even over a file that
  was there), not through a symbolic link; and a set-user-ID or
  set-group-ID program ignores `SLIGHT_RECORD`, `SLIGHT_REPLAY`,
  `SLIGHT_HOSTS` and `SLIGHT_RESOLV_CONF` (`rt_io_getenv`). `t/replay.sh`
  checks the first two.
- **Keys are decoded in a loop.** Each ESC before a key (Alt) was a
  level of recursion, 48 bytes each, so a few thousand in one read,
  decoded on a process's stack near its limit, ran off it. Not a golden
  test: keys come that way only on the real clock; `t/live.py` checks
  it (D206).
- **A lookup's query ids are random**, each its own, from
  `getentropy`, recorded like any seed. They came from xorshift on the
  clock and the pid, so one id told the next.
- **`format-num` makes its string whole first**, so a width past what
  the heap holds faults `:heap` at once, instead of building that much
  outside the heap a byte at a time. Test 253.

**D206. Live checks, for what a golden test can't see.** *(User, Oct
2026: "add new tests that the golden tests do not cover".)* `t/live.py`
(Python 3, as the models and the benchmarks are), run by `make live` and
`make test` for each target, with its programs in `t/live/`:

- **Keys on a stack all but full,** on the real clock, where a busy
  process reads stdin itself (D122): the one place D205's recursion
  could run off a stack. The process connects to `:keypress` only at
  the bottom, so the keys can't be read before it gets there.
- **Peak memory,** from `wait4`, under 400 MB for an endless JSON value
  (D204), thirty million arrays deep (D205), and `format-num` to 4 GB
  (D205), where the code before held 514 MB, 1.1 GB and 2 GB on its way
  to the same outcomes.
- **The chat, in a terminal** (a pty), against a model of its own,
  which streams a terminal escape, asks to run a command with one in it
  and fetches a redirect: the escapes are shown escaped, the command is
  refused and not run, and the redirect is told, not followed. The
  chat had no test before.

Each check waits for what it expects to see, never for a time, so
nothing depends on how long anything takes, as in `t/replay.sh`, which
is on the real clock too; a deadline only says when to give up. Against
the code before D204, eight of the ten fail; the other two (the chat
ends, a refused command isn't run) are there so that a failure of the
rest can't be the chat not working at all. Rejected: these as golden
tests, which can't measure a run, put keys in at a moment, or run a
second program; and pexpect, which isn't in Python's standard library.

**D207. An example: a simulated day, branched.** *(User, Oct 2026:
"Let's write an example", after a use case of Stevan's: airliners'
routes simulated from historical data, branched by an agent at any
moment with a change, each branch recorded till it rejoins the run it
came from, the branches a history like git's.)* `examples/airline.slight`
and `examples/airline-day.sexp`: five aircraft, twenty legs through a
hub, connections, weather, and the record of what flew, read as
s-expressions from a device. The base flies every leg as the record
says; four what-ifs (a delay, the hub closed, an aircraft out of service,
and quicker turns on top of the closure) each run again with their
changes and those of the branches they're on. Golden, from
`t/models/airline.py`.

- **Exact rejoining.** Each minute, between ticks, when nothing is in
  flight, the desk makes a digest of all that decides the rest of the
  day: where each aircraft is, its next leg, and how long till it's ready
  or due; and the passengers waiting to change planes, and how long till
  they can. The totals (fuel, lateness, connections missed, seats
  cancelled) aren't in it, since they don't change what happens next.
  A branch whose digest equals a run's at a minute, after every change
  between them has stopped making a difference to what comes in, flies
  the rest of the day as that run does: it stops there, and its totals
  are that run's moved by what it had changed. It's compared with each
  run it's on, oldest first, so a branch can rejoin the base without
  rejoining its parent: quicker turns after the closure rejoin the base
  at 17:05, three hours before the closure alone does.
- **Whole numbers.** Minutes, kilograms and passengers, so that the same
  is the same exactly, and no libm function can make two platforms
  differ (D76).
- **Canonical order without sorting strings,** which slight can't: every
  list in a digest is in the day's order, and the aircraft's reports are
  looked up by tail, so the same flight comes out under any
  `SLIGHT_SCHED`.
- **What it showed:** a branch is its parent till its change, minute for
  minute, which the program checks; rejoining works for a delay absorbed
  by slack, a closure recovered from by evening, and a mitigation; one
  that never rejoins runs to the end. Run again from the start of the
  day, the branches took 3,031 minutes of simulating, 2,011 of them their
  own: forking from a copy of the parent at the branch would save the
  rest. The whole program takes about 60 ms.
- **Rejected:** a digest of the runtime's own (every waiting process's
  function, arguments and mailbox), since pids and incidental state would
  keep runs that are the same apart, and what decides the future is the
  model's to say; rejoining within a tolerance, which is a judgment, not
  a guarantee; and airports as processes, which this model doesn't need,
  the desk keeping the passengers' books.

**D208. `clock/us`.** *(Stevan, Oct 2026: "let's make sure we capture
timing information", for the airline at the size of a small airline;
`clock/us` agreed, after options.)* `(clock/us)` is the microseconds
since the program started, an integer, on the clock the timers go by:
`now()` in `process.c`, the monotonic clock read through `replay.c`, so
a recorded run replays with the same readings; or, on the virtual clock,
the virtual one, which moves only as the timers do, so a golden test can
check it exactly (`t/255`). Before it, the only clock a program could
read was `:system`'s `(:clock ms)`: a device's answer, so only where a
`recv` can go, and in milliseconds. Rejected: nanoseconds, which claim
more than macOS's monotonic clock gives, where a part of a run worth
timing takes tens of microseconds or more; milliseconds as a float, the
timers' unit, but a float where everything else about time is an
integer; CPU time, which on one core, for work that never waits, is
the time the clock gives, and which `bench/rusage.c` measures from
outside; and the time of day, which nothing has needed (`lib/http.slight`
still sends no Date).

**D209. `hash`.** *(Stevan, Oct 2026: "hash", after options, for the
airline's digests at the size of a small airline: three hundred
aircraft make a minute's digest some 20 KB with integer ids, 23 MB a
day, copied into every branch that compares with it, and a history of
dozens of branches would pass the heap's cap.)* `(hash v)` is a
non-negative integer for any value, the same for values that are `eq?`,
and the same on every run, target and machine: the value's words, as
`eq?` walks it, through splitmix64's mixer (DESIGN.md says the words).
A symbol and a function are hashed by name and a pid by its number,
never by an address, which differs from run to run and moves with the
collector. Pinned in `t/254`, from `t/models/hash.py`.

- **What it costs:** equal hashes don't prove equal values. At 62 bits,
  a false match in a thousand branches compared at a thousand minutes
  each is about one chance in 10^12; it's the bargain git makes for its
  objects' names, and LANGUAGE.md says it.
- **Not seeded,** so that a hash kept in a file still means the same
  value; so it's no defense against keys picked to collide, and a table
  keyed by them shouldn't use it.
- **Rejected:** keeping exact digests, which fit for a handful of
  branches and not for the history the airline is for; and keeping only
  what changes each minute, with the aircraft that differ from the run
  compared with counted, which is exact and small but more machinery in
  every simulation that wants it; a seeded hash, which a file couldn't
  keep; and a cryptographic one, slower, for an adversary there isn't.

**D210. The airline at the size of a small airline.** *(Stevan, Oct
2026: "300 airplanes, 100 airports, and around 1500 daily flights ...
I just want to see how slight might perform", with timing, no Python
model, and builtins added rather than worked around: `clock/us` (D208)
and `hash` (D209); string sorting later; where the what-ifs live left to
Claude.)* `examples/airline-gen.slight` makes such a day: 300 aircraft,
each of a hub's, out to spokes and back, five legs; 100 airports, four
of them hubs; 1,499 legs and 2,287 connections for seed 1, and ten
what-ifs naming its flights. Its random numbers are hashes of (seed what
which), so nothing is threaded from one to the next. A made-up day has
no record, so `airline --record` prints the base's, to append. The
example itself, rewritten for the size, prints what it printed for the
five aircraft (`t/models/airline.py` still makes the expected output):

- **Numbers, not names:** aircraft, legs and connections are numbered
  in the file's order, and each leg's connections in and out are tuples
  by number, so a minute's work is what happens in it.
- **A digest that doesn't depend on order:** each aircraft hashes its
  number and state, each waiting connection its number and how long
  till boarding, and the parts are added modulo 2^61 - 1, so nothing is
  sorted. The first version sorted the 300 answers and the waiting
  passengers every minute, for a canonical order: 28 per cent of the
  instructions. Two minutes that differ add up alike only by chance, as
  with `hash`.
- **The what-ifs in the data file,** `(:branch name parent change)`,
  so a generated day names its own flights; the small day's moved there
  too, and the model reads them from it.
- **`--time`** prints, to stdout (slight can't write to stderr), how
  long the day took to read, and each run: its milliseconds, its
  microseconds a minute, and its messages a second; and how much of the
  branches' time was the minutes before their changes. No golden test
  has it; timings vary.
- **Measured** on the cloud machine, x86-64, three runs: reading the
  day 54 ms; the base 311 to 346 ms, about 300 µs a minute, so a
  microsecond for an aircraft's minute, its tick and its answer, about
  1.9 million messages a second; the ten branches 2.2 to 2.5 s, 1.15 to
  1.35 of it the minutes before their changes; the whole run 2.6 to 2.9
  s, at most 84 MB. The same output on AArch64 under qemu, and under
  `SLIGHT_SCHED` seeds 1 to 5.
- **What it found,** biggest first. A fork copies whole each local its
  expression names, and `simulate` named the day in each aircraft's: the
  day 300 times a run, 6.6 s and 247 MB to 4.3 s and 82 MB once each
  aircraft was given only its part; LANGUAGE.md now says so. The sort,
  above: 4.3 s to 2.9. Then memory: a message with something to copy is
  dead space in its taker's old generation, and so is what a minor
  collection promoted and was dropped after, till a major collection,
  which a heap under 256 KB never has (D106, `GC_MIN`); so each aircraft
  can keep up to that much. Bare integers for ticks (freed as taken,
  D199) make the 84 MB 61; and a program of the same shape and nothing
  else, 300 processes ticked 1,080 times, holds 56 MB with ticks that
  are lists and 20 with integers. So most of it is the collector's
  policy, which is left for Stevan; the example keeps `(:tick t)`, as a
  tick would be written. And reading the day: association lists searched by
  name, a string compare at each step, about a quarter of the
  instructions of the day read and the base flown, which an ordering of
  strings or a map would cut.
- **Checked** by `t/live.py` (`make live`), on both targets: the
  generator's day, its 1,499 legs all landing in the base's record, the
  same record in a seeded order.
- **Rejected:** the generator as a golden test (300 KB of output, which
  the live check runs instead); timings in a golden test, on the
  virtual clock, where work takes no time; and the day in `bench/`,
  whose programs are each written in several languages.

**D211. The airline's runs as JSON, for charts.** *(Stevan, Oct 2026:
"Can we read the data generated by the airline run to build some
visualizations showing the affects of the branches against the main?")*
`airline --json file` also writes, with `json/print` and `spew`, the day
and every run, on one line: the legs; the airports' places, which
`airline-gen` now writes as `(:airport code x y hub?)` records; and for
each run its parent, change, where it was its own and the run it
rejoined, its totals at every minute, and each leg's off and on. A
branch that rejoined is given its whole day, as its totals already were:
its own legs till it stopped, one in the air then landing when the
run it rejoined lands it, the two being the same then, and that run's
after. Checked from outside on both days: every run's passenger-minutes
late and seats cancelled are the sums over its legs, every departure
and landing before a branch's change is its parent's, and every one
after it rejoined is the rejoined run's. The report is unchanged, so the
golden test is too.

**D212. The airline's checkpoints and history.** *(Stevan, Oct 2026: "So,
can we replay the entire run, with a new set of branches? Or would we
need to re-run the full simulation", and yes to keeping runs in a file
and starting branches from checkpoints, in the example, as Claude
proposed.)* Replay (D170) doesn't do it: it runs the program again,
doing all its work, with what it read the first time, the branches
included. So `examples/airline.slight` keeps what it works out:

- **Checkpoints.** Every half hour (`--every n`, 0 for none) a run's desk
  asks each aircraft for its state, its status and the legs it has
  left, and keeps that with the passengers waiting: the day before that
  minute, but for the totals and the legs flown so far, which the run's
  whole day already has. Between minutes every aircraft waits in `recv`,
  so its state is its arguments, all of it data. A branch starts from
  the last checkpoint at or before its change that holds for its parent:
  one the parent kept while it ran; one its own parent held for before
  it began, when the two were the same; or, once the parent had rejoined
  a run, one of that run's from after, the two being the same then. The
  aircraft are made again from it with the branch's changes, which can't
  have made a difference yet. A cancelled leg is now `(:cancelled
  minute)`, so the legs as they stood at a minute can be had from the
  whole day.
- **A history.** `--keep file` writes every run as an s-expression, a
  line each, its checkpoints too; `--from file` starts from those runs
  and runs only the branches not in it (a branch kept under the same
  name with another change is left out, and says so). It's refused for
  a day it wasn't kept for: it holds a hash of what decides a run but
  its changes, the model's constants and version and the day's aircraft,
  legs, connections, wind and closures, not its branches or record, so
  branches can be added to the day's file a round at a time.
- **Checked:** the golden test, with a fifth what-if, a delay on top of
  one that has rejoined the base, which starts from the base's
  checkpoint, against `t/models/airline.py`, which runs every branch from
  the start and works out only where the program begins it; the big day
  the same with checkpoints as without, on both targets and under
  `SLIGHT_SCHED`; and `t/live.py`, what-ifs of its own covering each way
  a checkpoint is found, some kept and the rest run from the history,
  the same as all of them from the start with none.
- **Measured** on the cloud machine, three runs each, the big day:
  without checkpoints 3.3 to 3.6 s, the branches 2.7 to 3.2 of it, half
  of that the minutes before their changes; with them 2.1 to 2.2 s, the
  branches 1.4 to 1.6, 10 to 14 ms of that before their changes, and 92
  MB at most where it was 86. The history is 2.1 MB. Three more
  branches from it: 0.6 s in all, of which 150 to 220 ms reading the day
  and the history.
- **Rejected:** checkpoints by the runtime, a waiting process written
  out whole (PLAN.md's processes as data), which a simulation that knows
  its own state doesn't need, and which would keep pids and mailboxes
  that mean nothing to another run; `fork(2)` at a checkpoint, a branch
  to a core, which is the next step if it's wanted, as a proposal; a
  checkpoint every minute, 30 times the memory for at most 29 minutes;
  keeping a rejoined run's checkpoints past its rejoining, which are its
  target's; and replaying a record with its branches edited, which is
  only running it again.

**D213. The airline recovering from a disruption, with services.**
*(Stevan, Oct 2026: when a disruption happens, "run the simulation with
many branches, find the best ones, check them against crews, maint
schedules ... then rank the choices", the checking and ranking by
outside models or web services, "This keeps the PII out of slight";
then "Yes, lets get started" on Claude's first step.)* Slight stays an
operational simulator; crews, maintenance and passengers stay with the
systems that know them.

- **Two changes and a rule in the model.** `(:cancel flight)`, at the
  flight's departure; `(:swap a b minute)`, the two aircraft taking each
  other's legs ahead at that minute, the desk asking each for its legs
  and giving them to the other before the minute's tick. And a leg is
  cancelled at its departure if its aircraft isn't where it leaves from,
  or is withdrawn, so cancelling a leg out cancels the leg back. Both
  changes make a difference only at their minute. Each leg flown keeps
  the aircraft that flew it, `(off on aircraft)`, and the model is at
  version 2, so older histories aren't used.
- **The digest now holds every aircraft's next leg,** in the air too,
  and withdrawn. Before swaps an aircraft's legs ahead followed from
  which it was and the leg it was on; a swap broke that, and a swap with
  both aircraft in the air rejoined the base a minute later, wrongly.
  The next leg says what's ahead: the rest of the legs it was planned
  with, which a swap moves together. The report didn't change.
- **`--recover file`:** the disruption a branch of the base; remedies,
  each a branch of the disruption from when it's known, asked for in
  the file or made: holds of 15, 30 and 45 minutes for each flight
  leaving later that passengers connect to, a cancellation of each such
  flight, and a swap for each two aircraft at the first minute they're
  both on the ground at one airport. Then rounds: the candidates, doing
  nothing among them, that no other left is better than in all of
  passenger-minutes late, connections missed and seats cancelled go to
  the crew and maintenance services, till the budget (`(:check n)`) has
  been sent; those that pass go to the ranking service. Rounds, not one
  front: in the test's disruption, holding SL102 15 minutes, the best
  that passes, is bettered by swaps that fail, and a single front
  never sent it.
- **What a service is sent** is every leg's flight, aircraft, times and
  fate, as JSON; nothing of anyone. It answers with reasons, about
  flights and aircraft, or with a cost and what makes it up.
- **Stand-ins,** in slight, in `examples/airline-services.slight`, as
  HTTP handlers knowing the plan: crews, each aircraft's first two legs
  and its rest, at the same airport, with 30 minutes between, and on
  duty at most 470 minutes from 45 before to 15 after; maintenance,
  each aircraft ending the day where its plan does; a cost of refunds,
  missed connections, compensation and minutes late. `--recover` starts
  them itself, on ports the system picks, unless `(:services crew
  maintenance rank)` gives URLs.
- **Checked:** `t/256`, the whole of it on the small day over HTTP, from
  `t/models/airline.py --recover`, which runs every candidate from the
  start of the day and has the stand-ins' rules too; the same under
  `SLIGHT_SCHED` seeds 1 to 6, `SLIGHT_GC_STRESS=7` and
  `SLIGHT_GC_VERIFY`; the new changes against the model on branches of
  their own; and the report and the record unchanged. On the real clock
  the whole recovery takes 0.15 s.
- **Not yet:** the remedies made are every one of their kind, which
  suits a small day; a big one would make tens of thousands, so they
  need choosing, near the disruption or sampled, and running on more
  than one core. HTTPS waits for TLS (12c), so a service is plain HTTP
  or reached through curl.
- **Rejected:** crews, maintenance and passengers in the simulation
  (Stevan's call: they stay with their systems); one front of the
  measures, for the reason above; and a cost of the simulator's own to
  rank by, which is the ranking service's to say.

**D214. The airline's what-ifs and remedies at random, and a runner.**
*(Stevan, Oct 2026: "Let's start with generating seeded random
branches, but make this a parameter so we can experiment with the size.
Put together a runner for this in bash or Python that allows me to run
different options. Include timing info, etc. in the output.")* The first
step of the larger idea: a base, a seeded batch of branches, and later
an agent consulted when one strays far enough.

- **`--random n --seed s`:** n what-ifs in place of the day's own. A
  delay of a flight, 15 to 120 minutes, three in five; a closure of an
  airport, the one some leg leaves from, so busier ones come up more,
  for 30 minutes to 3 hours from between 07:00 and 19:45, one in four;
  otherwise an aircraft out of service from between 08:00 and 17:45.
  One in four branches from an earlier one, the rest from the base. The
  numbers are hashes, as airline-gen's (D209): the ith branch's are of
  `(seed what i)`, so it depends on nothing but s and i, and is named
  `rS-I`. A batch of 200 with a seed takes in the batch of 100, so
  rounds kept in a history (`--keep`, `--from`, D212) run only what's
  new.
- **Of two changes of one flight or aircraft,** a branch's and an
  ancestor's, the one that says more holds: the longer delay, the
  earlier time out of service. The program took the first in its list
  and the model the last, which nothing had shown till random branches
  delayed a flight a second time (`t/257`, r7-18 on r7-9). A delay says
  how long a flight can't leave, so the longer is what both say; which
  came first is an accident of the order.
- **A run's checkpoints are kept only while a branch still to run
  branches from it.** Nothing else in a run can start from them; a
  branch of it in a later round, from a history, starts from its
  parent's from before it began, and comes to the same, running more
  minutes. On the big day a thousand branches peak at 1.4 GB with it
  and 2.1 GB without, in 162 s and 172.
- **`--remedies n`** with `--recover`: the remedies asked for, and n of
  those made, picked by `--seed`: those whose hashes of `(seed :remedy
  change)` come first, in the order made. On the big day the program
  makes 7,629 for a delay at 11:35 (holds, cancellations, and above all
  swaps, of every two aircraft that meet), in about 0.55 s; each takes
  about 120 ms to run. With `--time` it says how long the base, the
  making, the running, the checking and the ranking took.
- **The stand-in crews come from the plan:** each aircraft's legs in
  order, a crew taking as many as fit in 450 minutes on duty as
  planned, then the next crew. "The first two legs and the rest" was
  written for the small day's four legs an aircraft; on the big day it
  put 109 of the 600 crews over the 470-minute limit before anything
  happened, so every remedy failed. Planned to the limit itself, the
  wind and the closures still put four over in the base; 20 minutes to
  spare puts none. The small day's crews are as they were, and `t/256`
  didn't change.
- **`t/models/hash.py` hashed a list holding 8 wrongly:** it marked a
  list's end on its stack with `END`, which is 8, and `x is END` is true
  of every small 8 in Python. The runtime was right (seven of the first
  eight random branches agreed with the model, and the eighth's roll
  hashed an 8). The end is now a marker of its own; `t/254`'s values
  have no 8, so its expected output stands.
- **`bench/airline.py`:** builds the program and the generator for this
  machine, makes and keeps the day (`gen:SEED`, `small`, or a file),
  sweeps lists of `--random`, `--seed`, `--every` and `--remedies` (and
  `--rounds`, each round from the history the last kept), runs each
  `--repeat` times, and prints a row a run: wall and CPU seconds and
  the peak resident set, measured by `bench/rusage.c`; what the program
  says of its own times; each branch's median, 95th percentile and
  longest; the minutes simulated, and how many rejoined; and for
  `--recover`, the remedies made and run, how long checking and ranking
  took, how many passed, and the best. `--json` keeps all of it.
- **Checked:** `t/257` (`--random 40 --seed 7`, on the small day) and
  `t/258` (`--recover ... --remedies 10 --seed 2`), from
  `t/models/airline.py`, which makes the what-ifs and picks the remedies
  by its own hashes; the program and the model agree on six other
  seeds and sizes up to 120 branches, and on four other samples; both
  tests under `SLIGHT_GC_STRESS=7`, `SLIGHT_GC_VERIFY` and
  `SLIGHT_SCHED`; the report, and `t/256`, unchanged.
- **Not yet, found by the runner:** on the big day, recovering from a
  60-minute delay that puts SL1002's crew over its duty, nothing passes
  however many remedies run (10 to 2,000): the budget of a dozen goes
  on the first front, doing nothing and the remedies that come to the
  same, swaps far away that rejoin it at once, and all fail on that
  crew. Remedies near the disruption (its aircraft, its crew's legs, its
  airports), and a candidate that comes to the same as another checked
  once, are the next step, each to settle first. And recovery keeps
  every remedy's run whole and makes every candidate's operated day
  before checking any: 2,000 remedies peak at 2.3 GB, and all 7,629 run
  out of the 1 GB heap; a remedy is never branched from, so its minutes
  could go once it's measured.
- **Rejected:** a generator whose state goes from one number to the
  next, which would make a branch depend on those before it, and a
  bigger batch not take in a smaller; the runner in bash, since parsing
  the output, measuring the peak and writing JSON are Python's, and
  `bench/run.py` is too.

**D215. The airline's remedies near the disruption, a day checked once,
and remedies kept lean.** *(Stevan, Oct 2026: "I agree with you
suggestions", on D214's three: remedies near the disruption, a candidate
that comes to the same day as another checked once, and a remedy's run
let go once it's measured.)*

- **Near:** what the disruption touched is what the simulation sees,
  since crews stay with their services: the legs it flew otherwise than
  the base (another minute, another aircraft, or not at all), and the
  aircraft planned for them or flying them. Holds are made for the
  flights passengers change to from a touched leg; cancellations for a
  touched aircraft's legs ahead, which in the plan are its crews' too;
  swaps for a touched aircraft with each other it meets on the ground,
  which is where its airports come in. Remedies far from it can't mend
  what it broke, so they aren't made at all. On the big day a 45-minute
  delay of SL1002 makes 63 remedies, where it made 7,629, in 13 ms
  where it took 0.55 s, and all 63 run in 7 s; 15 pass, and the best is
  a swap at the hub after SL1002 lands. The small day's t/256 makes 10
  where it made 39, and the best is the same, holding SL102 15 minutes.
- **A day checked once:** a remedy is kept as the legs it flew otherwise
  than the disruption did. Candidates with the same differences come to
  the same day, and are checked and ranked once, as the first of them,
  the others counted beside it; the budget and the last line count days.
  Two days are the same when their differences hash alike, as two
  minutes are when their digests do (D209); the model compares them
  exactly. On the small day only doing nothing's day ever has others
  (five aircraft make few remedies, and they differ), as in `t/259`,
  where the hub closed 08:00 to 10:00 makes 21 remedies, two of which
  come to doing nothing's day; on the big day a hub closed for an hour
  and a half makes 905, which come to 885 days.
- **Lean:** a remedy is kept as (name label measures differences
  minutes rejoined?), and its run goes once it's measured, so the runs
  don't grow with the remedies tried; an operated day is made only for
  what's sent to a service.
- **A run makes checkpoints only when a later branch will start from
  them,** where it made them and the root dropped them (D214). A run's
  process ends with its result, and its exit record keeps that for good
  (D88), so this is what shrinks the record: the hub closure's 905
  remedies peak at 251 MB where they peaked at 745, and a thousand
  what-ifs at 828 MB where they peaked at 1.4 GB, in 149 s where they
  took 162.
- **The runner's default disruption is now airline-gen's own first
  what-if, SL1002 45 minutes late.** At 60 nothing can mend it under the
  stand-ins' rules: SL1002 ends its crew's day, so landing an hour late
  puts the crew at 482 minutes whatever's done elsewhere; cancelling it
  leaves N100 away from its check; and no remedy of the three kinds
  gives the leg another crew. That's an answer, not a fault: every
  remedy's reasons say so.
- **Checked:** `t/256` and `t/258` (now 4 of the 9 made) again, and
  `t/259`, from `t/models/airline.py`, which makes the remedies near the
  disruption and the days by its own rules; the program and the model
  agree on eight disruptions of the small day and two samples; the
  three under `SLIGHT_GC_STRESS=7`, `SLIGHT_GC_VERIFY` and
  `SLIGHT_SCHED`; the report and the random what-ifs unchanged.
- **Not yet:** exit records (D88) keep every value a process ends
  with, so each remedy or branch still leaves about 0.15 MB for good on
  the big day; left as it is for now (D216). A remedy
  the crews' own service could suggest (a reserve crew) is that
  service's to make; quick turns at a touched airport could be made too.
- **Rejected:** making every remedy and trying the near ones first,
  since the far ones can't help and cost a run each; comparing days
  pairwise with `eq?`, which is n² for thousands.

**D216. Exit records stay as they are, for now.** *(Stevan, Oct 2026:
"Let's leave it for now, and make a note of this case, and we can
revisit later.")* D215 found that a record keeps the value its process
ended with for good (D88), not only that its pid's entry stays: a
process ending with a list of 10,000 integers, joined and dropped,
leaves 165 KB a time, and each of the airline's runs, a process ending
with its whole result, leaves about 0.15 MB on the big day. D88 was
decided for many small results, and stands. The case is kept in
`docs/PLAN.md` ("Exit records that keep big values"), for when it's
taken up, with the ways out weighed then:

- a record's value let go once its parent has joined it, a later `join`
  of that pid then an error: the retention policy D88 put off, and
  Claude's recommendation, since it serves every program that joins
  many big results, and monitors, told as a process ends, don't change;
- the airline's runs sending their results and ending with `()`, which
  changes only the example, but makes `run-branch` wait in `recv`, and
  so it and its callers state functions;
- nothing, since at thousands of runs it costs a few hundred megabytes.

**D217. The airline's recovery as JSON, for charts.** *(Claude, Oct 2026,
for Stevan's "lets do another visualization using the new code".)*
`--json file` with `--recover` writes what the recovery came to, as
D211's does for exploring: the disruption, when it's known and what it
does against the base; every candidate, doing nothing among them, with
its change, (late missed cancelled) against the base, the day it comes
to (named for the first candidate that does), how many legs it flew
otherwise than the disruption, its minutes and whether it rejoined; the
days checked, with their round and the services' reasons; and those
that passed, ranked, with what the ranking service said. So `shortlist`
keeps a log of its rounds, and `tell-ranked` gives its scores beside
its line. The printed report doesn't change (`t/256`, `t/258`, `t/259`).
The dashboard *What-Ifs and Remedies* is drawn from it, and from
`bench/airline.py`'s `--json`: no golden test reads either file, as
none reads D211's.

**D218. The airline's what-ifs in shares, a core each.** *(Stevan, Oct
2026: "Yes let's do that", to running the what-ifs as copies of the
program, each with a share, in place of a runtime on many cores.)* The
runtime stays on one core (BACKGROUND.md, "Multiple cores", is still
its own step); the cores are used by the operating system, one copy of
the program each.

- **`--shard k/n`:** the kth of n shares of the what-ifs. A tree, a
  branch of a run that isn't among them (the base, or one kept) and
  every branch from it, goes whole to one share, the jth tree to share j
  mod n, so each share has every run its branches start from, and every
  what-if comes out as it does in one run. Each share runs the base
  itself (0.36 s on the big day). Dealing the trees out by their size,
  or by how much of the day lies after each change, was tried in a
  model of the measured times: at most a tenth faster at 32 shares, and
  nothing at 8, since a few big trees decide the slowest share; so
  every nth tree it is.
- **`bench/airline.py --shards 1,2,4`** runs that many copies at once,
  each with its share, and gives the set's wall time, the slowest
  share's, the CPU and memory of them all and the speed-up; and checks
  the shares' reports together against one copy's, what-if for what-if.
- **Measured** on the cloud machine's 4 cores, 1,000 what-ifs on the big
  day: 149.7 s in one copy, 74.7 s in two (2.00 times as fast), 38.0 s
  in four (3.94), and 40.2 s in eight, more copies than cores; every
  set the same as one copy. Each copy holds its own base, so the memory
  in all grows (838 MB, 734, 923) as each copy's falls (838, 371, 248).
  Four runs of the program side by side take the time of one, so
  nothing but the cores is shared: on this machine, whose cache is 260
  MB, at least.
- **Modelled** beyond 4 from each what-if's measured time, the day's
  reading, the base, and the root's own 3 ms a what-if, in every share:
  7.1 times as fast on 8 cores, 13 on 16, 22 on 32, 39 on 64 and 57 on
  128, the slowest share setting the pace. The model said 77.1 s for
  two shares and 39.4 s for four, where they took 74.7 and 38.0.
- **Measured on the M2 Max** (Stevan's run, 12 cores, 8 of them
  performance cores and 4 efficiency): 63.2 s in one copy, 34.0 in two
  (1.86 times as fast), 19.3 in four (3.27), 11.0 in eight (5.77) and
  9.9 in twelve (6.37), every set the same as one copy. The model's
  split holds; what it leaves out is that the copies slow each other as
  the cores fill: their CPU together grows 1.07 times with two, 1.19
  with four, 1.27 with eight and 1.58 with twelve, and the model's
  speed-up divided by that is 1.85, 3.25, 5.58 and 6.36, as measured.
  Eight copies on the performance cores take 1.27 times as long each
  as one alone; on an efficiency core a share takes about 2.2 times as
  long. Dealing many small shares to whichever core is free, from one
  base kept beforehand, would gain about a tenth (a model: 7.1 times
  with 96 shares), since macOS already moves copies off the efficiency
  cores as the others finish; so it isn't done.
- **Memory on the M2 Max:** a copy peaks at about 820 MB and 0.95 MB a
  run it holds, where on Linux it's 46 MB and 0.77 MB a run, so twelve
  copies peak at 8 GB in all. The stacks aren't it (pooled, a page or
  two of each touched). Likely, though not yet checked, macOS's
  allocator: the heaps of each run's 301 processes are freed when it
  ends, and freed memory there stays counted as resident until the
  system wants it. `/usr/bin/time -l` gives the peak memory footprint
  beside the peak resident set, which would tell.
- **Checked:** `t/260` (`--random 40 --seed 7 --shard 2/3`), from
  `t/models/airline.py`, which shares the what-ifs by its own reading of
  the rule; the program and the model agree on other shares; and a live
  check that three shares, run apart, report every what-if as one run
  does, and simulate as many minutes.
- **Not yet:** the recovery in shares, which needs a step after them,
  since the rounds and the services want every remedy's measures at
  once; and why the copies slow each other on the M2 Max, and what its
  memory really is.
- **Rejected for now:** forking the runtime itself at a branch (fork(2)),
  which shares the base without running it again but needs the runtime
  to know about it; and the runtime on many cores, whose own step is
  BACKGROUND.md's.

**D219. Strings: one set of builtins, counting characters, after
JavaScript's.** *(Stevan, Oct 2026: "Let's start with cleaning up all the
string handling ... Let's aim for clean UTF handling similar to JS, let's
be forward thinking here"; and, to the proposal, "We can get rid of
ord/chr, and yes, lets fix pprint too. One small change, instead of
`str/bytes-` lets do `bytes/` instead and have `bytes->str` to convert it
to the utf8 string.")* It replaces the byte builtins of D27 and D62, the
`utf8/` ones of D148, the prelude's string functions (D78), and D64.

- **The unit is the character of D148:** a well-formed UTF-8 sequence,
  or a stray byte on its own, whose code point is U+FFFD. So strings stay
  bytes, nothing faults on bad bytes, and splitting into characters
  loses nothing. `(str/len "😀")` is 1, where JavaScript's length is 2:
  its UTF-16 units are a fact of its history, and would put half an
  emoji in slight's hands for nothing.
- **Names.** The `str/` builtins count characters and are JavaScript's
  methods in kebab case, `toUpperCase` and `toLowerCase` shortened:
  `str/len`, `str/at`, `str/code-at`, `str/slice`, `str/index-of`,
  `str/last-index-of`, `str/includes?`, `str/starts-with?`,
  `str/ends-with?`, `str/split`, `str/join`, `str/repeat`,
  `str/pad-start`, `str/pad-end`, `str/trim`, `str/trim-start`,
  `str/trim-end`, `str/upper`, `str/lower`, `str/replace`,
  `str/replace-all`, `str/well-formed?`, `str/to-well-formed`, with
  `str/chars`, `str/codes`, `str/compare` and `str/<` beside them. The
  `bytes/` builtins count bytes, in one step, for protocols and for
  parsers whose delimiters are ASCII: `bytes/len`, `bytes/at`,
  `bytes/slice`, `bytes/index-of`; they take an ordinary string (a type
  for bytes that aren't text is a proposal of its own). Conversions are
  `a->b`: `str->int` and `int->str` (in a radix, 2 to 36), `str->float`,
  `str->sym`, `sym->str`, `str->bytes`, `bytes->str` and `codes->str`.
  `str` is what `concat` was; `~` stays, strings only, and takes any
  number now. `format-num` is `(str/pad-start (str n) w fill)`; `ord`
  and `chr`, `(str/code-at s 0)` and `(codes->str (list n))`. A clean
  break: no old name is kept, and every program moved (129 files,
  Stevan's sketches in `author/` among them, where two counts that a
  narrow window makes negative are kept at 0 now that `str/repeat`
  faults on them; by a script that rewrote names, not text, so strings,
  comments and doc blocks were left alone; `str-len`, `substring` and
  `index-of` became their `bytes/` names, which kept what each program
  did, the parsers in `lib/` counting bytes as they should).
- **Edge cases are JavaScript's,** where it has them: `str/at` and
  `str/code-at` count from the end when an index is negative;
  `str/slice` (and `bytes/slice`) count from the end, clamp, and are
  empty unless start comes first (`substring` swapped them, which no
  program wanted); `index-of` takes where to start, clamped; `str/split`
  takes a limit, and `(str/split "" ",")` is `("")`, where `str-split`
  gave `()` (one caller, `tools/marked`, changed for it); an empty
  separator splits into characters; `str/replace-all` with an empty
  pattern goes around every character; padding counts characters.
  Where JavaScript gives `undefined` or `NaN`, a fault, as D62 had it:
  an index past the end, a negative count or limit, a code point with no
  UTF-8. `str/compare` is code point order, which for UTF-8 is byte
  order, where JavaScript's `<` compares UTF-16 units and puts U+FFFF
  after an emoji.
- **A match is whole characters,** in every search, split and replace:
  it starts where a character does, and now ends where one does too, so
  a stray byte 195 isn't found in `"é"`, though it's é's first byte. A
  byte search does that by itself unless the pattern starts with a
  continuation byte or ends with a stray byte; only then does it check.
- **Case and white space are Unicode's,** without a language:
  `str/upper` and `str/lower` map in full (SpecialCasing's unconditional
  mappings over UnicodeData's: `"straße"` is `"STRASSE"`, `"ﬁ"` is
  `"FI"`), with Final_Sigma, the one conditional mapping that needs no
  language; `str/trim` takes White_Space. `runtime/gen-unicode.py`
  writes the tables into `runtime/unicode.c` from the Unicode Character
  Database, pinned to 15.1, the version Python 3.13 has, so the tests'
  model agrees (unicode.org is blocked from the cloud sessions; ICU's
  copy of the files, `release-74-1`, is the same). They agree with
  Python's `str.upper` and `str.lower` on every code point. They're
  about 64 KB of each binary; packing the one-to-one mappings would
  take that to about 30, when it matters. Normalization, graphemes and
  display width (`str/normalize`, `str/graphemes`, `str/width`) are
  utf8proc's to give, vendored as D151 has it, when they're wanted.
- **ASCII strings are marked.** A string whose bytes are all below 0x80
  has `RT_BOX_ASCII` (0x80) in its header, the box's type narrowed to
  the low seven bits; there a character is a byte, so `str/len`,
  `str/at` and `str/slice` take one step. It's set where strings are
  made: `rt_new_string`, the readers and decoders, `rt_string_mark`
  after a builtin fills a blank string, and the compiler on literals.
  One made without it is only slower. Rejected: a character count in
  each string (a word each, and indexing non-ASCII text still walks),
  and an index of offsets (memory, for text that's mostly walked once).
- **`pprint` escapes a string** as the reader takes it back, on one
  line, as `sexp/print` did: `"` and `\`, and the control characters,
  C0, DEL and C1 (`\n`, `\t`, `\r`, `\e`, or `\u{hex}`); the C1 ones
  are new, since a terminal acts on U+009B as it does on ESC [. Other
  characters, and stray bytes, as they are. The two share `rt_quote`.
  Two tests that escaped by hand now let `pprint` do it.
- **A builtin with optional arguments can be a value,** taking the ones
  it needs: `(map str->int words)`. Before, only a fixed arity could.
- **Checked:** `t/models/strings.py`, an evaluator for the part of
  slight the string tests use, with every builtin written again in
  Python over characters, gives their expected output. It agrees with
  Node's own string methods on 33,000 random ASCII cases, and the
  runtime agrees with it on 2,000 random calls over strings of every
  kind of character and stray byte. Golden tests 261 to 265 are new,
  and 061, 164 and 166 (`ord` and `chr`'s, now code points') were
  written again; the tests whose output changed only by the escaping
  were checked to read back as before; the string tests pass
  `SLIGHT_GC_STRESS` and `SLIGHT_GC_VERIFY`.
- **Rejected:** UTF-16 units, as JavaScript counts (above); graphemes as
  the unit (tables, and slow, for what a terminal's columns want more
  than a program's indexes; later, as `str/graphemes`); strings that are
  always well-formed (every string checked as it's made, and a bad
  file's bytes lost); a character type; `str/bytes-` names, the
  proposal's, for the user's `bytes/`; keeping the old names as aliases;
  keeping `ord` and `chr`.

**D220. `join/race`, `join/any` and `join/all`: waiting for the first of
several processes.** *(Stevan, Oct 2026: "Let's stick to the JavaScript
meanings. And I agree with your recommendations. Let's build it", to
Claude's proposal, after he asked whether `join/all` and `join/race`
should come with `join/any`.)* `join` waits for one pid. Waiting for
whichever of several ends first meant `monitor` and `recv`, and so a
state function, which the caller and everything above it then had to be
too: why the airline ran its what-ifs one at a time, and why
`http/await` forked a box to watch for the reply and the timer.

- **JavaScript's meanings.** `(join/race pids)` gives `(pid result)` for
  the first of `pids` to end, well or not (`Promise.race`); `(join/any
  pids)` gives `(:ok v)` for the first to end well, or `(:error
  reasons)`, in the order of `pids`, once all have failed
  (`Promise.any`); `(join/all pids)` gives `(:ok values)`, in the order
  of `pids`, or the first `(:error r)` as soon as there is one, without
  waiting for the rest (`Promise.all`). `allSettled` is `(map join
  pids)`, so it has no name. Claude's first name for the race was
  `join-any`, which JavaScript's `any` would have made mean something
  else.
- **One of them in the runtime.** `join/race` is a builtin in C; the
  other two are a few lines of slight in the prelude, on it. Like
  `join`, it keeps the caller's stack while it waits, so the caller
  needn't be a state function, and the `recv` rule doesn't change.
- **How it waits.** A process blocked in `join` is on its pid's list of
  joiners, and on only one; a race is on several. So it's a watch on
  each pid, as a monitor is (D183), that wakes its process instead of
  sending it a message: the first to end wakes it, and it takes the
  rest off when it runs; a pid that ends in between finds it woken
  already, and only goes. Its watches go with it if it's killed, as a
  monitor's do. `join` keeps its own way, which allocates nothing, since
  the benchmarks join a great deal; a watch is a malloc a pid.
- **Pids that have ended already:** the first of them in the list, as
  `Promise.race` settles with promises settled before it's called, and
  at once. An exit record doesn't keep when its process ended, and
  keeping it would cost every process.
- **Losers go on running,** as in JavaScript. A process may be another's
  to join too, and `(map kill pids)` is one call. Rejected: killing them,
  as structured concurrency does, which is tidier but would surprise a
  caller who joins them later.
- **Faults,** all found before it waits, so a bad pid faults whichever
  ends first: an empty list (`:not-a-pid`, the value `()`), since
  JavaScript's race of nothing waits forever, and here that's a hang;
  something that isn't a list (`:not-a-list`), or in it that isn't a
  pid (`:not-a-pid`); the caller's own pid (`:join-self`). `join/any` of
  `()` is `(:error ())`, and `join/all` of `()` is `(:ok ())`, as in
  JavaScript.
- **Telling of it:** `:system` shows a racer as `:joining`, its
  `:joining` field the list of the pids it races, in order; and a root
  racing processes that never end is a deadlock, `... waiting in f for
  the first of #<pid 2>, #<pid 3> to end, ...`.
- **`http/await`** races the request's process against one that sleeps,
  in place of a box that monitored it and took the timer's message.
- **What it doesn't do:** make CPU work faster. The runtime runs on one
  core, so the airline's what-ifs in processes of their own would take
  as long, and want `--shard` (D218) as before. It's for waits that
  overlap (a timeout, services asked at once, the first good answer),
  and programs written so get the cores if the runtime ever has them.
- **Checked:** golden tests 266 (the race: the first to end, an error
  winning, pids already ended, two ending at once, a killed target and
  a killed racer, racers waking in order, a pool of two, a timeout, and
  `:system`), 267 (`join/any` and `join/all`, with the times they took
  on the virtual clock) and 268 (the faults, and the deadlock), worked
  out by hand; 211's timeout goes through the new `http/await`. 267
  gives the same under five seeded orders, a replay check now. Not
  tested: a reply over a socket that comes before its timer, on the
  virtual clock. The clock moves to the timer when nothing is ready to
  read, and macOS's loopback delivers a moment after the send, so there
  the timer wins (it did, when 211 had such a case: Stevan's run on the
  M2 Max). The runtime waits for loopback before the clock moves only
  for a lookup's own timer (D165); doing it for every timer would change
  the virtual clock, a proposal of its own. `within` in 266 is the same
  race between processes, with no socket. A program that races thousands of times, killing targets
  and racers along the way, runs clean under AddressSanitizer and under
  eight seeded orders.

**D221. Numbers: `math/`, the rest of JavaScript's `Math`, and the bit
operations.** *(Stevan, Oct 2026: "Lets put everything under math/. Make
pow exact on 2 ints. Make bit/shl wrap. round toFixed. E and INF can be
constants, and nan? is enough. And I agree, lets do random numbers
after, and numerical objects only if needed", to Claude's proposal, and
"lets build this".)*

- **`math/`.** The math builtins move under it, as JavaScript keeps them
  in `Math`: `math/abs`, `min`, `max`, `ceil`, `floor`, `round`,
  `trunc`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `exp` and `PI`. The
  arithmetic (`+ - * / div %`) and the comparisons stay as they are. A
  clean break, as D219's was: every program moved, 124 names, by a
  script over tokens that leaves a name alone in a form that binds it as
  a local (`agent.slight`'s `round`, `calc.slight`'s `exp`), and t/241's
  top-level `min` and `max`, which bind to the end of the file, put back
  by hand.
- **The rest of `Math`:** `math/asin`, `acos`, `atan`, `atan2`, `sinh`,
  `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `log`, `log2`, `log10`,
  `log1p`, `expm1`, `cbrt` and `hypot`, libm's, always floats, `nan` or
  `inf` out of their domains. glibc's `cbrt` of 27 is a bit over 3, so,
  as D76 has it, the tests check them to 12 significant places, against
  V8's values, which come from its own libm and not the system's.
- **`math/pow` of two integers is exact**, as `*` is: past 63 bits it
  overflows, and a negative exponent is out of range, since the result
  wouldn't be an integer (rather than a float for some exponents and an
  integer for others). A float on either side gives a float, as before.
  `(math/pow 2 10)` is 1024, where it was 1024.0.
- **`math/mod`,** the floored remainder, its sign the divisor's, which
  both game-of-life examples faked with `(% (+ n size) size)`. Integers
  only, as `%` is.
- **`math/nan?`,** as `Number.isNaN`: anything but a float that's NaN is
  `#false`. Rejected: `finite?` and `infinite?` beside it (Stevan: "nan?
  is enough"), and a `NAN` constant.
- **Constants:** `math/E` and `math/INF` beside `math/PI`.
- **`int->float`,** and **`float->str x [digits]`**: as `str` shows a
  float, or with digits, 0 to 100, `toFixed`: the nearest decimal with
  that many digits to the float's exact value, a tie going away from
  zero, where `printf`'s goes to even, so it's rounded from the exact
  expansion (`%.1100f`, which every double's ends within). From 1e21 up,
  and `nan` and `inf`, as `str` shows them, as `toFixed` does. The
  airline makes its tenths of a millisecond by hand, and keeps them, since
  it cuts where `float->str` rounds.
- **Bits:** `bit/and`, `bit/or`, `bit/xor`, `bit/not`, `bit/shl` and
  `bit/shr`, compiled inline on both targets, and `bit/count` in C, on
  63-bit two's-complement integers. An integer is `n << 1`, so and, or
  and xor of the tagged words are the tagged results; not is a xor with
  -2; a shift left of the word wraps by itself, losing what passes the
  top (Stevan: wrap, rather than fault as `*` does, which hashing and
  packing bytes want); a shift right is of the word, its bit 0 cleared,
  and fills with the sign. A count of 63 or more gives 0, or -1, as Go
  has it, where JavaScript takes a count mod 32; a negative one faults
  (`:out-of-range`). Rejected: `>>>`, a logical shift right, which on a
  63-bit word means little; `(bit/and (bit/shr x n) mask)` does it.
- **Not taken up:** big integers, rationals, complex numbers, 32-bit
  floats, unsigned 64-bit integers. Random numbers are next, as a
  proposal of their own: they're about where state lives, seeding,
  replay and processes, not arithmetic. Numerical vectors and matrices
  wait for a program that needs them, and floats that need no box would
  come first (D179).
- **Checked:** golden tests 269 (`math/pow`'s edges, from Python's
  integers), 270 (the libm functions against V8, `math/mod`, `math/nan?`,
  the constants, `int->float`), 271 (`float->str`, from Python's
  `Decimal`, `t/models/float-to-str.py`, which also checks every case
  against V8's own `toFixed`) and 272 (the bits, from
  `t/models/bits.py`, the operands as literals and as values, which the
  compiler takes two ways); t/070, t/074 and t/080 changed only where
  `math/pow` is now exact or a fault names `math/round`.

**D222. Random numbers: a generator per process.** *(Stevan, Oct 2026:
"Lets go with your picks, I agree", to Claude's proposal, which D221
left for one of its own.)*

- **Where the state lives: in each process, in the runtime,** as
  Erlang's `rand` keeps it in the process dictionary: two words in the
  process's record. A draw is a builtin with no generator to pass, and
  nothing else sees it, so it's no more shared state than `$$` is.
  Rejected: a generator as a value, passed along and handed back with
  each draw, as Haskell's `StdGen` (pure, but every function that draws
  grows a parameter and a result, and a process's loop carries it); a
  generator as a process, asked for each number (a message each way per
  draw, and the order processes ask in decides who gets which, so a
  seed wouldn't give the same run under another order); and draws made
  from a counter, a hash of a seed and an index, as a builtin. That last
  is how the airline makes its what-ifs and remedies (D214), each the
  same whatever the others are, and it stays a pattern in slight
  (`(% (hash (list seed what i)) n)`) rather than a builtin.
- **The generator is SplitMix64, Java's `SplittableRandom`** (Steele,
  Lea and Flood): a state that steps by an odd gamma, each step mixed.
  It's small, fast, passes BigCrush, and splits, which a fork wants;
  and Java gives the same numbers from the same seed, which the tests
  are checked against. `(rand/int n)` is Java's `nextLong(bound)`, a
  power of two masked and anything else drawn again in the top part of
  the range that would favor some numbers; `(rand/float)` is
  `nextDouble`, 53 bits. A bound under 1 faults (`:out-of-range`).
- **A fork splits the parent's generator,** as `SplittableRandom.split`
  does: the child's state and gamma are the parent's next two draws. So
  what a process draws depends on the seed and on what was drawn and
  forked before it, along its line of forks, never on the order
  processes run in: under one seed a program draws the same numbers
  whatever `SLIGHT_SCHED` says. Rejected: every process seeded from one
  generator for the run (the numbers would follow the order processes
  run in), and every process seeded alike (forked workers would all draw
  the same).
- **Seeding:** `SLIGHT_SEED=n` seeds the root, digits only (anything
  else stops the run, exit 2, as `SLIGHT_SCHED` does); without it, the
  system's entropy (`getentropy`), so runs differ. `(rand/seed n)`
  starts the calling process's generator again from `n`, for a program
  that takes its seed as an argument (boids). The seed goes through
  `replay.c` (`rt_io_random_seed`) either way, so every record now holds
  one more entry, before the program's first, and a replay draws what
  the recorded run drew without being given the seed. `t/run.sh` sets
  `SLIGHT_SEED=1`, so the golden tests are the same every run.
- **The prelude** has `rand/pick`, an element, `()` for none, as `nth`
  gives, and `rand/shuffle`, which sorts the elements by a `rand/float`
  each: a list can't be shuffled in place, and sorting by random keys
  is hard to get wrong where swapping as elements come is easy to bias.
  Two keys alike, which would keep their order, come about once in
  2^54 / n^2 shuffles.
- **Not cryptographic,** and said so: what SplitMix64 draws next can be
  worked out from what it drew. Keys and tokens want the system's
  entropy each time, which TLS will want anyway (12c); a lookup's query
  ids already take theirs from it (`rt_io_seed`). Left for then.
- **Boids** is the first program to use it: its Park–Miller generator,
  written in slight, is gone, and it draws its flock with `rand/float`
  after `(rand/seed seed)`, its seed still an argument. Its expected
  output was made again by its model, `t/models/boids.py`, which draws
  from `t/models/rand.py`, a SplittableRandom in Python, checked against
  Java's on 9,000 draws and splits from three seeds.
- **Checked:** golden test 273, whose expected output is Java 21's
  `SplittableRandom` (`t/models/Rand273.java`): draws from
  `SLIGHT_SEED`, `rand/seed` again giving the same, a negative seed,
  bounds of 1, a power of two and 2^62 - 1, floats, forks and a fork's
  fork, `rand/pick`, `rand/shuffle`, and the faults; and
  `t/replay/rand.slight` in `t/replay.sh`: processes that interleave
  draw the same under five `SLIGHT_SCHED` orders from one seed, an
  unseeded run replays as it was recorded, and two unseeded runs
  differ.

**D223. A persistent map, a hash trie in slight.** *(Stevan, Oct 2026:
"Yes, let's build it", to Claude's proposal, the second of what the
airline asked for, after strings, joins and numbers.)*

- **What:** `lib/map.slight`, opt-in (`(@include :map)`): a hash array
  mapped trie, Bagwell's, as Clojure's maps are, made of tuples. A map
  is a value; a change copies only the path to what it changed. The
  airline looked things up by name in association lists, a few of them
  quadratic (`share-of`, naming a candidate's day); a map makes each
  lookup a few steps. Rejected: a balanced tree, which needs an order on
  keys that slight's values don't have, where every value has a `hash`
  and `eq?`; a table in C (D157's other option), a type of its own in
  the runtime, to be copied, collected, printed and compared; and the
  map in the prelude, which every program would compile.
- **Two builtins:** `tuple/insert` and `tuple/remove`, a copy one longer
  or one shorter, which a node needs as keys come and go. In slight they
  were two slices, two lists, an `append` and `list->tuple` on every
  write at every level.
- **The trie:** a map is `#(size root)`; a node `#(bitmap slot ...)`,
  a slot for each bit set; a slot a leaf, `(k v h)`, or a node a level
  down. 5 bits of the hash a level, the lowest first, so a node has up
  to 32 slots and its bitmap fits in an integer (64 wouldn't, in 63
  bits). `hash` gives 62 bits, so past 13 levels keys with the same hash
  share a bucket, `#(leaf ...)`, in the order they came. A leaf keeps
  its key's hash, so splitting it needn't hash the key again, and a test
  can give keys hashes of its choosing (`map/put-hashed` and the like),
  collisions included, which real keys can't be found for.
- **The same entries make the same map:** a slot is a leaf only when
  one key alone is under it, so deleting folds a node left with one
  leaf back into it, and a map's shape depends only on its keys (but for
  a bucket's order, which needs two keys with the same 62-bit hash). So
  `eq?` and `hash` work on maps as on any value.
- **The names,** under `map/`: `new`, `get` (`:not-found` for a missing
  key, as `lookup`), `has?`, `put`, `delete`, `update`, `size`, `fold`,
  and `map->list` and `list->map`. Claude proposed `map/empty` and
  `map/remove`; it became `(map/new)`, since a library can define only
  functions, and `map/delete`, and `map/update` takes a default, as
  `lib/ds.slight`'s `dict/` has them. `map/` beside the prelude's `map`,
  as `str/` beside `str`. Not added: `map/keys` and `map/values`, which
  `map->list` covers.
- **What a map is:** a tuple, not a type: `tuple?` says `#true` of one,
  and `pprint` shows the trie. Sharing ends at a process's edge: a send,
  a fork's captured values and an exit record copy a map whole, as they
  copy everything (D25); so a map serves a process's own data, and
  copying that keeps sharing would be a proposal of its own.
- **Not done here:** `bit/count`, `tuple/get`, `tuple/len` and `tuple?`
  are calls into C, several a level; compiling them inline waits for a
  profile that asks. 100,000 puts, a fold and 200,000 deletes take 0.2 s
  on the cloud machine. `hash` isn't seeded, so keys picked to collide
  pile into one bucket.
- **The airline,** its first user, in a commit of its own: its runs,
  kept as an association list and looked up by name about fifteen
  times, are a map and the list of names, newest first, for the order
  the history, the JSON and `--keep` write them in; `--shard`'s
  `share-of` finds each branch's parent and tree's number in maps, where
  it walked lists; and `days-of` counts the candidates that come to each
  day in a map, where it sorted them by hash and folded neighbours. Its
  output is the same, byte for byte, golden tests 256 to 260 and the
  live checks unchanged, and the JSON and kept histories compared with
  the program before. It's no faster at 1,000 what-ifs on the big day
  (140 s either way): the simulating is the cost there, and the lists
  would have told only at many times that.
- **Checked:** golden test 274 (`tuple/insert` and `tuple/remove`, by
  hand), and 275: the API and the trie's layout by hand, collisions and
  a path 13 levels deep through the hashed functions, and a seeded run
  of 3,000 puts and deletes checked against an association list at
  every step, its entries, their order and their hash from
  `t/models/map.py`, a Python dict with slight's `hash` and generator
  in Python, the order worked out from the hashes.

**D224. Rust, with tokio, as the actor programs' floor.** *(Stevan, Oct
2026: "Yes, let's write it up", to Claude's answer to "would it be
useful to add a Rust benchmark with tokio? Or Scala with Akka? Or Swift
actors?")* `bench/Cargo.toml`, `bench/rt.rs`, `bench/NAME/NAME.rs`.

- **Why:** D194 left Rust out as being about C. It is, for the
  sequential programs, which C already floors; but the actor programs
  had no floor, only peers (Erlang, the same model) and Go. tokio is
  about as fast as tasks get, and its tasks are stackless, a waiting
  one only its future's state: the nearest thing to a slight process
  waiting in `recv` with nothing on its stack. So `spawn` and
  `idle-memory` against it show what slight's design gets close to.
- **Where:** `ping-pong`, `thread-ring`, `fan-in`, `spawn`,
  `idle-memory` and `parallel-fib`, and `startup`, which `idle-memory`'s
  bytes a process needs. None of the sequential ones.
- **A floor, not a peer:** tokio doesn't preempt (a task runs till it
  waits), has no heap per task, and moves a message, where slight copies
  it. Erlang stays the comparison with the same model.
- **One core and every core,** as Go and Erlang: `rust`, tokio's
  current-thread runtime, every task on the thread that runs `main`;
  and `rust-mc`, its multi-thread one, a worker per core, as
  `#[tokio::main]` makes it. `bench/rt.rs` picks one, by `BENCH_MC`,
  which the harness sets for `rust-mc`. A multi-thread runtime with one
  worker isn't one core: `main`'s future runs on a thread of its own,
  so every message to it crosses threads.
- **A mailbox is an unbounded channel** (`tokio::sync::mpsc`), as
  slight's and Erlang's are: a send never waits. So `fan-in`'s senders,
  as slight's and Erlang's (D201), are never slowed, and its messages
  pile up in the receiver's; Go's channel, which holds 1,024, is the
  odd one out. `idle-memory`'s waiting tasks each hold a channel of
  their own, as a process holds its mailbox, not a `oneshot`, which
  would leave out what a mailbox costs.
- **Built** by cargo, one package for every program (`bench/Cargo.toml`,
  each a `[[bin]]` by its directory, and `rt.rs` its library), with
  `--locked`, so tokio is the version `bench/Cargo.lock` says; the first
  build fetches and compiles tokio, which that build's time includes.
  The harness gives tokio's version with rustc's.
- **Rejected:** Scala with Akka, on the JVM, whose warming up D194 left
  Java out for; Akka has been under the Business Source License since
  2022 (Apache Pekko is its open fork); and its model, actors with
  mailboxes, is what Erlang already covers, more faithfully (preemption,
  a heap per process). Swift's actors, which aren't processes with
  mailboxes: an actor is an object whose async methods run one at a
  time on a shared pool, with nothing like `recv`, so `ping-pong` would
  be method calls, and `thread-ring` and `fan-in` something else again
  (D194's "doing the same work"); and its pool has a thread per core,
  with no ordinary way to one.
- **Found** (BENCHMARKS.md, "Rust: a floor for a message, not for a
  process"): on the M2 Max, tokio on one core is faster at a message
  (`ping-pong` 0.83 of slight's time, `fan-in` 0.69), and slight at a
  process (`thread-ring` 1.3 times, `spawn` 2.8, `idle-memory` 2.2,
  where a waiting task with its channel takes 1,105 bytes and a slight
  process 575); on the cloud machine slight is ahead at the messages
  too (`ping-pong` 1.8 times). `parallel-fib` is LLVM's code (0.54).
  `rust-mc` runs `main`'s future outside its workers, as
  `#[tokio::main]` does, so `ping-pong`'s every round trip wakes a
  thread: 12 s on the M2, 78 on the cloud machine. Kept as it is, as
  Rust is usually written.

**D225. `rpc`: stupidrpc, finished, in s-expressions.** *(Stevan, Oct
2026: asked whether stupidrpc, Matt S Trout's protocol, would do to connect
two slight programs, and whether people could use it too, "as kind of a
REPL", to share the tools agents use; then, to Claude's proposal:
s-expressions, not JSON; "always require IDs, we can always hide this
with library code"; no `cast`; no flow control for now; "barewords
where we can, but strings are fine too"; a bind address (D226), Unix
sockets later; and "rpc" as the name.)* `lib/rpc.slight`, opt-in,
`(@include :rpc)`.

- **Where it comes from:** stupidrpc, by Matt S Trout (mst, of Perl's
  community; github.com/mst-ripples/stupidrpc), a line protocol after
  IRC's, meant for machines and people alike: a
  JavaScript `Nexus` speaking `CALL`, `NEXT`, `DONE`, `FAIL` and `STOP`
  as JSON arrays, a line each, and notes for more (`HELO`/`EHLO`,
  `cast`, `barf`, subscriptions, interception, calls announced). Not
  followed as it was, but finished: a sketch in slight, talking JSON to
  the `Nexus` and to itself, showed the model fits processes (and found
  `STOP` lost at both ends of the JavaScript), and the rest was settled
  from there. Mojo-StupidRPC, an earlier implementation, in Perl, of
  the notes, and Hydling, which uses it, were looked at after (D230).
- **The messages,** a datum each, read with `(:read :sexp)` and written
  with `sexp/print` (D155): `(call id name arg ...)`, `(next id v)`,
  `(done id v)`, `(fail id reason)`, `(stop id)`, `(helo rpc 1)` and
  `(barf reason)`. S-expressions rather than JSON because slight reads
  them already, keeps symbols and tuples through them, and a person can
  type them; the JavaScript side, the reason for JSON, is gone. What a
  person types and what a program sends are the same text, so there's
  no second syntax, as Redis has its inline commands beside RESP.
- **Ids always:** every `call` has one, chosen by the caller, any datum.
  The verb says which way a message goes (`call` and `stop` to the
  callee, the rest back), so an id is unique only among the calls one
  end has in flight, and the two ends' ids never clash; that rule is
  also what the `Nexus`'s `STOP` lacked. The library chooses them,
  counting from 1 on each connection, and a program never sees them.
  Rejected: a call without an id as a person's shorthand, answered in
  order (stupidrpc's "async-or-barf"), which the library can do for a
  program and a person can do without.
- **Dropped from the notes:** `cast`, a call with no answer, which isn't
  needed; subscriptions (`listen`, `notify`), which are calls that
  stream, ended with `stop`; interception (`trap`, `wrap`), which is a
  call the other end makes to you; `nope`, which is a `fail` with
  `(:no-such-call name)`; and calls announced (`callable`), which
  `help`, a call every connection answers, replaces: asking beats being
  told, and a person can type it.
- **Bare words:** a name, and an argument a `:string` parameter takes,
  can be a bare word, its name the string: a symbol, or `(:symbol
  "name")` for one the program doesn't mention (D155), which prints back
  as the bare word, so a relay passes it on. A call's name goes out bare
  if it reads back as that word. Other arguments go as they are, since
  an `:any` parameter may want a symbol.
- **Handlers:** `(rpc/handler name description params f)`, the shape of
  `tool/new` (D187), but `f` takes the arguments in order, as parameters
  of its own, and each parameter is typed (`:string`, `:integer`,
  `:number`, `:boolean`, `:any`), so `help` can say what a call takes and
  the connection can check it: a wrong count is `(:arity name n)`, a
  wrong type `(:bad-arg name param type value)`. Each call runs in a
  process of its own, which the connection monitors: its answer is
  `done`, a fault or a `raise` is `fail`, and `stop` is `kill`. Its
  answer and its values are printed in its own process, so one that
  isn't data faults the call, not the connection. A handler reaches the
  connection as `^$$`, which `rpc/next` sends to, and which it calls its
  caller back on: so no context is passed in.
- **Calling:** `rpc/call` waits in `join` for a process that makes the
  call and waits for its answer, so it works anywhere; `rpc/stream`
  gives that process's pid at once, as `http/stream` does (D181), and
  the caller hears `(:next s v)`, then `(:done s v)` or `(:fail s
  reason)`. The connection monitors each waiting process, and a call
  whose process ends before its answer is stopped: so `(kill s)` stops a
  stream, and a caller that ends stops its call, as a command ends with
  the process that ran it (D193).
- **Connections:** `rpc/serve` (a port or `"host:port"`, giving the
  listener's pid and telling the caller `(:listening port)`, as
  `http/serve` does), `rpc/connect`, which gives the connection once
  it's open, so nothing is sent before it can be, and `rpc/exec`, the
  same over a command, for ssh, as stupidrpc's own `outpost` reached
  other machines. Either end can call the other. The connection's end,
  `rpc/close` or the other end hanging up, fails the calls from here
  with `:disconnected` and stops those from there.
- **Rejected:** remote pids, Erlang's distribution, where a send can go
  to another machine: it brings monitors across machines, names, and
  what a pid means after a reconnect; another program is reached only
  by calls, which give a result, values as they come, cancelling, and a
  failure when the connection ends. Calls whose arguments are calls,
  which would evaluate what a person types: an interpreter by another
  name (D6). Flow control (credits, `(more id n)`): left out for now,
  as stupidrpc had none; a reader holds back the sender through TCP
  (D158), but a stream's values pile up in its caller's mailbox. JSON,
  for the `Nexus`, and a second syntax for people.
- **Not yet:** to whom a remote call's `approve` goes, which offering an
  agent's tools (D188) over `rpc` needs first: never to the caller just
  because it called. Who the other end is (on a Unix socket, D235; a `helo` carries only the
  protocol and its version), and any authentication: listen at
  `127.0.0.1` (D226) and come in over ssh. A stray `)` from a person
  ends the connection, as bad text ends a device's owner (`:bad-sexp`).
  A connection that ends with an error leaves the calls from the other
  end running to their ends (D230 stops them). Unix sockets (D231).
- **Checked:** golden tests 277 (two ends in one program: answers,
  checks, bare words, failures, `help`, a stream, a call back to the
  caller, a stream stopped and a caller that ends, the processes for
  both killed, and the connection's end), 278 (the messages as a person
  types them, from a raw client: ids of any datum, bare words, `barf`,
  an id in use and used again after `stop`, the server calling the
  client back, comments and a message over lines, a `helo` for another
  version), and 279 (`rpc/exec`, with `cat` as the other end: a call
  made here comes back as a call from there, its answer as the answer,
  the two calls numbered 1 kept apart), each worked out by hand; and by hand, two
  programs, one reaching the other through `rpc/exec` and `nc`, and a
  person at `nc`, approving a command the program asked about.

**D226. A listener at an address.** *(Stevan, Oct 2026, with D225: "add
the bind address (we can do unix sockets later)".)* `(connect
:tcp/listen "host:port" expr)` listens at that address alone, and
`(connect :tcp/listen port expr)` stays on every interface. Without it,
a program offering `rpc` calls that change things (D225) would offer
them to the network.

- **The host** is an address, a name in the hosts file, or `localhost`,
  as a connection's would be found, but never a name the name servers
  are asked about (`rt_lookup_local`, the first steps of
  `rt_lookup_begin`): what's listened at is this machine's, and a
  lookup would hold up the listener for nothing. Any other is
  `:enotfound`, as a `"host:port"` that doesn't parse is, the same
  splitting as a connection's; an address that isn't this machine's is
  `:eaddrnotavail`, from `bind()`.
- **Errors** name the `"host:port"` given, where a listener on a port
  names the port (D139).
- **Rejected:** a third argument, or a keyword (`:tcp/listen-at`), when
  a string where a port goes says it; and Unix sockets now, which need
  a device of their own and a path's permissions, for later (D231).
- **Checked:** golden test 276 (listening at `127.0.0.1`, `localhost`, a
  name the hosts file has and `0.0.0.0`, each connected to; an address
  that isn't this machine's, names not in the hosts file, which aren't
  looked up, one there for IPv6 alone, no port, a port too big, and an
  address in use, the error naming it as given), and 151, where a string
  is now an address; and by hand, a listener at `127.0.0.1` refusing a
  connection to the machine's own address.

**D227. Running in a container: static binaries on Linux, and SIGTERM
as PID 1.** *(Stevan, Oct 2026: "Let's start", to the first of Claude's
steps for running slight in containers and on edge machines, now that
programs can call each other, D225: build statically on Linux, and end
on SIGTERM.)*

- **Static on Linux:** `slightc` links a native build with `-static`,
  as it already linked cross builds. A program built for x86-64 on Linux
  was linked against glibc, so in an empty image (`FROM scratch`) it
  couldn't start: `exec /serve: no such file or directory`, the loader
  missing. Static, it runs on any Linux of its kind, the image no more
  than the binary (about 2 MB). The runtime calls nothing that a static
  glibc can't do: it looks host names up itself (D165), reading the
  hosts file and `resolv.conf` that a container is given, so two
  containers find each other by name without `getaddrinfo`. Not on
  macOS, which has no static binaries.
- **SIGTERM as PID 1:** the kernel ignores a signal sent to PID 1 that
  it hasn't a handler for, and a container's first process is PID 1. The
  runtime set its handlers for SIGINT, SIGTERM and SIGHUP only when it
  started a command (D185), and they died of the signal again, so a
  program in a container ignored `docker stop`, which waited its 10
  seconds and sent SIGKILL. Now the handlers are set at the start, with
  the stack of their own they run on; each kills the commands, gives the
  terminal back (which raw mode had kept, D117 being Ctrl-C's alone),
  and takes the signal again, unblocked, so the program dies of it as
  before; and if that comes back, as it does for PID 1, it exits with
  128 and the signal, 143 for SIGTERM, as a shell would say it died.
  `docker stop` now takes a fifth of a second, and says 143.
- **Rejected:** SIGTERM as a message to the root, for a program to stop
  gracefully (finish its calls, say): a way in for the outside world,
  to be proposed when a program needs it; `--init` (tini) in every
  container, which works but is one more thing to remember; and
  `-static-pie`, which the toolchains handle less evenly.
- **Checked:** t/live.py's signals, on both targets (AArch64 under
  qemu): a program waiting dies of SIGTERM, as it did; and as PID 1, in
  a PID namespace of its own (`unshare`, as root, on Linux; skipped
  without one), SIGTERM ends it with 143. Against the runtime before,
  the second fails, the program still running. By hand, in Docker: a
  program serving `rpc` from an image `FROM scratch`, called from the
  host with `nc`, stopped by `docker stop` in 0.22 s with 143; and two
  such containers on a network, one calling the other by its name,
  `server:7317`, through Docker's name server, and a name that isn't
  there giving `:enotfound`.

**D228. `rpc` for fleets: a hub calling its devices, connections kept
up, and `:system` asked from outside.** *(Stevan, Oct 2026: "your
proposals sound good", to Claude's three, for programs on machines that
come and go, a device behind a firewall among them.)*

- **A hub calls its devices with nothing new:** inside a handler, `^$$`
  is the connection the call came on, which `rpc/call` takes. So a
  device connects out, and calls the hub's `register`, whose handler
  hands `^$$` to a registry, a process of the program's own, which
  `monitor`s each connection and lets go of one that hangs up; and the
  hub calls the device over the connection the device made, with no
  port open on it. Written up in LANGUAGE.md, not made a library: a
  registry is a few lines, and what it keeps (a name, tags, what the
  device offers) is the program's to say. Rejected: `rpc/serve` telling
  its caller of each connection, `(:connected conn)`, which a program
  that doesn't want it would get anyway, as dead letters; it needed a
  second `rpc/serve`, where a handler already has its connection.
- **`(rpc/keep where handlers on-open)`:** a keeper, a process that
  `rpc/call`, `rpc/stream` and `rpc/close` take as they take a
  connection. It dials, and when the connection drops, or can't be
  made, dials again: after 100 ms, then twice as long each time, up to
  10 s, and from 100 ms again once one opens. Each new connection runs
  `(on-open conn)` in a process of its own, so a device can say who it
  is again after the hub comes back. While it's down, a call fails at
  once with `:disconnected`. Rejected: calls waiting for the connection
  to come back, which needs a bound, a timeout and an order for them,
  and hides from a caller that the other end is gone; and the delays as
  arguments, till a program needs other ones.
- **A call sent through a keeper** goes to the connection it had, which
  may have ended a moment before (its `(:exit ...)` not yet taken), so
  the call would go nowhere and its caller wait for ever, watching only
  the keeper. So the keeper tells the caller's waiting process the
  connection it sent it on, `(:rpc/via c)`, and that process monitors
  `c` too: had `c` ended, it hears so at once, and the call fails with
  `:disconnected`.
- **`(rpc/system)`:** a handler, `system`, that answers with what the
  `:system` device says (D171): `((runtime info) (processes (info
  ...)))`, asked of the device at once, so both tell of one moment, and
  a pid in them as its printed form, `"#<pid 3>"`, since a pid can't go
  as data. Not one every connection has, as `help` is: it shows a
  program's insides (its functions' names, its heaps), so a program
  offers it if it wants to be looked into, at `nc` say.
- **Not yet:** a timer can't be taken back, so a keeper closed while it
  waits to dial again leaves that wait's timer, which keeps the program
  running till it's due, 10 s at the most. (D232: no longer, since the
  keeper has ended.) A timer whose process has
  ended could stop counting as something that can still happen, which
  would be a change to the runtime, for a proposal of its own.
- **Checked:** golden tests 280 (a hub, a registry and three devices in
  one program: the hub reads each, a device that hangs up is let go and
  the registry told, and a call to it fails), 281 (`system`'s keys, as
  many processes as the runtime says, the root first, and the listener
  and both ends of the connection waiting where they should) and 282 (a
  call to a keeper before anything listens fails at once; once a server
  listens, the keeper's next dial opens, and on-open says hello; the
  server hanging up brings a new connection and a new hello; a call to
  a keeper that's closed fails), each worked out by hand; all six `rpc`
  tests under `SLIGHT_GC_STRESS` 1 and 7, `SLIGHT_GC_VERIFY` and seeded
  orders. By hand, on the real clock, two programs: a device kept up,
  and a hub that it registers with, killed and started again; the
  device registered again by itself, and the hub went on reading it;
  and `(call 1 system)` at `nc` to the hub. And a keeper closed while
  waiting to dial again: the program ended 1.4 s later, when that wait
  was due.

**D229. Running in a container: output that goes out when the program
waits, and orphans waited for as PID 1.** *(Stevan, Oct 2026: "Yes", to
Claude's two fixes for what checking the runtime against a container
turned up, both seen to fail first.)*

- **Output when the program waits:** `pprint` writes through stdio, and
  nothing flushed it but `tty/write`, a fault's or a dead letter's
  message, and the program's end. A container's stdout is a pipe, so
  stdio held the output till 4 KB of it had come, and a program that
  SIGTERM ended lost what it held: one that printed a line and waited
  gave the pipe nothing at all. Now the runtime flushes stdout whenever
  nothing can run and it's about to wait (`idle`): a log has the
  program's output as soon as it waits, and a program stopped while it
  waits, as a server mostly is, has lost none of it. It costs a write
  per wait that has output to write, not one per line. What a program
  printed while busy and was stopped before waiting is still lost: a
  handler can't flush stdio safely. Rejected: stdout flushed by line
  (`_IOLBF`), or by `pprint`, which costs a system call a line in a
  program that prints a lot.
- **Orphans, as PID 1:** what a command starts and leaves running (`sh
  -c "worker &"`) is PID 1's once the command has ended, and when it
  ends stays a zombie till PID 1 waits for it. The runtime waited only
  for its commands, so as PID 1 a `sleep` left that way stayed a zombie
  for the program's life. Now, as PID 1 (`getpid()` at the start), the
  runtime waits on SIGCHLD's pipe from its first command on, not only
  while one runs, and after waiting for its commands, waits for every
  other child that has ended: each is looked at first with `waitid`'s
  `WNOWAIT`, which leaves it as it is, and a command's own is left to
  the wait that's recorded for a replay. Not as any other process, whose
  orphans go to its init. Rejected: `docker run --init` (tini), which
  works but must be remembered, and which D227 set out to need no more;
  and `waitpid(-1)` alone, which could take a command's status from the
  wait a replay depends on.
- **Checked:** t/live.py's signals, on both targets: its program now
  says it's waiting with `pprint`, so seeing that line before SIGTERM
  needs the flush; and as PID 1, a command that leaves a `sleep` behind,
  the program's children looked at in `/proc` from outside till no
  zombie is left. With the runtime as it was, all three fail; with only
  the orphans' wait taken out, the last fails, a zombie left. By hand:
  the same two, a log line through a pipe and `ps` as PID 1.

**D230. `rpc` beside mst's own: a call stopped however its connection
ends, and `barf`.** *(Stevan, Oct 2026: "Let's do 1 and 2 here", to
Claude's review of two more of Matt S Trout's, Mojo-StupidRPC and
Hydling, beside `lib/rpc.slight`: the fix below, and this record.)*

- **What they are:** Mojo-StupidRPC (github.com/mst-ripples/Mojo-StupidRPC,
  Sep 2021, about 540 lines of Perl on Mojolicious) is the one
  implementation of stupidrpc's early notes (`sketches/early`, which
  Hydling keeps as its spec): JSON arrays, a line each over TCP or a
  Unix socket, or a WebSocket frame each; `call`, `cast`, `listen` and
  `wrap` with an id, `unlisten` and `unwrap` by name, answered with
  `done`, `fail` and `data`, and `notify` keyed by a channel's name. A
  handler is given the request, and answers through it (`$req->done`).
  A `listen` is acknowledged with a `done`, which may carry the current
  value, and then notifies; a `wrap` registers for an event, and the
  server calls the client back for each, whose `done`, `done stop` or
  `done next args` lets it through, vetoes it, or changes it. No
  cancelling a call, no `helo`, no `barf`. Hydling
  (github.com/mst-ripples/Hydling, Sep 2021) uses it: an IRC bot with a
  control port on a Unix socket, and `dev/rawctl`, a command-line client
  that reads JSONY (bare words allowed), numbers the calls itself, and
  prints each answer.
- **What they confirm of D225 and D228:** ids always, hidden by a
  library: Mojo requires them, and `rawctl` keeps them for a person.
  Interception as a call the other end makes to you: that is how Mojo's
  `wrap` works. Subscriptions as calls that stream: the `Nexus` made the
  same move, and Mojo's, keyed by name, are where two of its bugs are.
  People typing the messages: the notes' bare words, `rawctl`'s JSONY.
  And D228's hub: stupidrpc's `outpost` plugins (`send-identity`,
  `save-identity`, `route-to`) are the same design, a device connecting
  out and saying who it is, and the hub keeping each connection's
  identity and routing calls by it.
- **Its bugs,** each run against it, its own test passing: `cast` goes
  out without its verb; an id reused while in flight gets two `done`s
  that can't be told apart, and one equal to an id of its own going out
  dies, since it looks in the wrong table; an `unlisten` that crosses a
  `notify` dies, and two `listen`s to one channel leave one that can't
  be stopped; an unknown verb is a bare `die`; and a handler that dies
  leaves its caller waiting for ever. `lib/rpc.slight` gets each right
  (golden tests 277 to 279).
- **And one it shared:** a connection that ends with an error never
  hangs up: text that isn't an s-expression (a person's stray `)`), or
  a reset (`:econnreset`, `:epipe`), from a peer killed with data
  unread, or a write to one that has gone. Nothing then stopped the
  calls from the other end: a stream used as a subscription ran for
  ever, and a program kept up with `rpc/keep` (D228) would gather them,
  a set for each connection that dropped. D225 had it under "not yet",
  as calls running to their ends. Now each call from the other end has
  a watcher, a process the connection forks beside it, that monitors
  both and kills the call if the connection ends first (`rpc/watch`):
  a process for each call, and the loop, and how it reads, holding back
  a sender through TCP (D158), as they were. Rejected: a process that
  owns the socket and passes what it reads to the loop, so the loop
  hears the error, which takes what it reads as fast as it comes and
  loses that; links, a runtime change for this alone; and bad text told
  to the owner as a message, which changes every device (D159) and
  leaves a reader to find where the next datum starts.
- **`barf`:** in the notes, a `barf` is fatal, and a close follows.
  `lib/rpc.slight` closes only after barfing at a `helo` for another
  protocol or version; a message it can't take, or an id in use, is
  barfed at and the connection goes on, so a person at `nc` who
  mistypes doesn't lose the connection and every call on it.
- **Checked:** golden test 283, worked out by hand: a raw client starts
  two calls that never end on a server, then sends a stray `)`, and both
  calls' processes are killed; and a raw server calls a client, is
  called by it, and sends one, and the client's call fails with
  `:bad-sexp` and the process running the server's call is killed. It
  deadlocks without the watcher. All seven `rpc` tests under
  `SLIGHT_GC_STRESS` 1 and 7, `SLIGHT_GC_VERIFY` and five seeded orders.
  By hand, on the real clock: a stream running when a Python client
  resets the connection (`SO_LINGER` 0), which before ran to its end and
  now stops; and a stream of a million values, then a stray `)`, which
  before ran every value.

**D231. Unix sockets: `:unix` and `:unix/listen`, a path's permissions
saying who may connect.** *(Stevan, Oct 2026: "do 3, taking into account
the container work", after D230's review found both of mst's own uses of
stupidrpc on Unix sockets; then, to Claude's questions, devices of their
own, a socket left behind replaced, and the umask.)*

- **The devices:** `(connect :unix path expr)` and `(connect
  :unix/listen path expr)`, read and written as TCP's are; a listener
  says `(:open l path)` and `(:accept l conn)`, and errors name the path,
  an accepted connection's too, since a Unix peer has no address.
  Rejected: `:tcp` taking a path, which would have `:tcp` mean something
  that isn't TCP; `rpc` alone reaching sockets through `rpc/exec`, with
  `socat` or `nc -U`, which leaves a program no way to serve on one; and
  Linux's abstract names, which have no permissions and aren't on macOS,
  so an empty path is `:enoent`.
- **The file:** listening makes the socket as a file at the path, and
  `bind` won't make it over anything there. A program killed with
  SIGKILL, or one that crashed, leaves its socket behind, and in a
  container restarted on the same volume the path would be taken for
  ever. So a listener replaces a socket that refuses a connection,
  asked without waiting, so that on Linux a live one whose queue is full
  counts as alive (macOS refuses for that too, so there a socket so busy
  that a second copy of its program starts can be taken for dead); and
  anything else there is `:eaddrinuse`, a live listener or a file that
  isn't a socket. It removes the path when it closes, and at the
  program's end however it ends: `atexit`, and the handler for SIGINT,
  SIGTERM and SIGHUP (D227), from a table of the listeners' paths by
  descriptor, as it kills commands from one. Rejected: removing whatever
  is there first, as Hydling does, which takes a live program's socket
  from it unseen; and leaving it to the program, which has no builtin to
  remove a file, so a crash and a restart would never recover.
- **Who may connect:** connecting takes write permission on the socket,
  which the umask sets, as for every program: with the usual 022 it's
  0755, and only its owner, and root, can connect. So the path's
  permissions are the access control D225 left open, on one machine;
  in a container, `--user` with the host user's ids makes the socket
  theirs. Rejected: always 0600, which shuts a group out; and a mode
  argument, for a need not yet seen. Who the other end is (`SO_PEERCRED`,
  `getpeereid`), for a program to decide by: later (D235).
- **`rpc`:** `rpc/serve`, `rpc/connect` and `rpc/keep` take a path
  wherever `"host:port"` goes, any string with a `/` in it, so one here
  is `"./ctl.sock"`, and `(:listening at)` gives the path. Over ssh:
  `rpc/exec` with `nc -U`, or `socat - UNIX-CONNECT:path`, as
  stupidrpc's `outpost` reaches its sockets.
- **Replay:** listening at a path is one recorded call
  (`rt_io_listen_unix`, the look for a socket left behind within it),
  so a replay makes no socket, and removes nothing, since a path is
  noted for removing only when a socket was made.
- **Checked:** golden tests 284 (a listener's path, a connection echoed,
  `:eaddrinuse` at a live listener and at a file, `:enoent` for nothing
  there and for an empty path, `:enametoolong`, and a killed listener's
  path gone) and 285 (`rpc` at a path: a call, `help`, a keeper and its
  on-open, the path gone with the server, and a name without a `/` taken
  as `"host:port"`), worked out by hand; both under `SLIGHT_GC_STRESS` 1
  and 7, `SLIGHT_GC_VERIFY` and three seeded orders. t/replay.sh's net,
  over a Unix socket too: the socket gone with its listener, and the
  replay leaving a file put at the path alone. t/live.py's sockets: a
  listener answers at its path; a second copy gets `:eaddrinuse`, and
  the first still answers; SIGKILL leaves the socket, the next to listen
  there replaces it, and SIGTERM removes it. By hand, in Docker: a
  program serving `rpc` from an image `FROM scratch`, its socket in a
  directory shared with the host, called from the host; `docker stop`
  in 0.23 s, with 143, and the socket gone; `docker kill`, the socket
  left and refusing, and the next start replacing it; with the usual
  umask, `nobody` refused (`:eacces`), and with `--user 1000:1000` the
  socket that user's, which it could call.

**D232. Timers that can be cancelled, and a timer whose process has
ended no longer holding up the program's end.** *(Stevan, Oct 2026: "we
also need to work out how timers can be cancelled, and do that one";
then, to Claude's options, both, as recommended. PLAN.md had the cases:
D112, D157, D173, D187, 14's timeout that a reply cancels, a `recv` with
a timeout, and D228's keeper.)*

- **`timer/cancel`:** `after` gives its timer's number, an integer, 1
  for the program's first and one more for each after it (`sleep` takes
  none), where it gave `()` (D111). `(timer/cancel t)` takes the timer
  back if it hasn't fired: its message never comes, it no longer keeps
  the program running, and it gives `#true`; or `#false`, if it had
  fired (its message may be in the mailbox), had been taken back, or `t`
  is no timer's number. `#false` is what tells a caller that a timeout
  it no longer wants is on its way. Any process can take a timer back,
  as any can kill any process. For a process that goes on: a timeout a
  reply cancels, and later a `recv` with a timeout.
- **The index:** the pending timers are a binary heap by when they're
  due; `timer/cancel` finds one by its number in a table beside it,
  open addressing, the number multiplied by 2^64 over the golden ratio
  for its slot, updated as the heap moves a timer, and a slot emptied by
  moving back the ones after it that belong before it, so none is ever
  marked deleted. Taking a timer back is a lookup and the heap's own
  removal, O(log n). Rejected: a search of the heap, which the
  runtime's own timers use (`unset_alarm`), simpler but O(n) a cancel;
  numbers that reuse slots, with a generation, which aren't 1 up and
  can be mistaken for an old timer's; and a value of a kind of its own,
  `#<timer 3>`, which can't be mistaken for a number but takes a box
  type, printing, `eq?`, `hash`, copying and `sexp/print` refusing it.
- **A timer whose process has ended,** a killed sleeper's or an `after`
  aimed at a process that has gone (or a device that has closed), is
  dropped when nothing can run and it's the next due, so the program
  doesn't wait for it, nor the virtual clock move to it. D112 kept such
  a timer till it was due, turning down dropping a process's timers as
  it ends, which takes a list of them for each process, or a search of
  the heap. This takes neither: only the top of the heap is looked at,
  and one further down is dropped when it gets there, or when it's
  due, as before; nothing a process can see changes, but when the
  program ends. It frees, with no change to them: `ask-within` (D157),
  whose timer is its box's, which ends with the answer;
  `http/request-within` (D173), which kills the sleeper it raced; and a
  keeper closed while it waits to dial again (D228). HTTP's default
  timeout (D173) and the `run` tool's limit (D187), turned down for
  holding up a program's end, are now possible, for when they're
  wanted.
- **Checked:** golden tests 286 (numbers 1 up, a timer taken back that
  never comes, taken back again or after it fired or by a number no
  timer has, by another process, a fault for one that isn't a number,
  and a root waiting for what only a timer taken back would have sent,
  deadlocked), 287 (2,000 timers, a third taken back, last first, the
  rest coming in the order they're due and then were set) and 288 (4,040
  set and taken back so that their numbers meet in the table, each
  found once, from `t/models/timer-ids.py`, a model of the table, in
  which one that left a gap where a timer was taken back loses six; the
  runtime made to do that gives the six, 4,034); 135, where `after` now
  gives 1, and 137, where a killed sleeper's timer and one sent to it are
  dropped. t/live.py's timers: on the real clock, a program whose only
  pending timers are an hour long and come to nothing (a killed
  sleeper's, one sent to a process that has ended, one taken back,
  `ask-within`'s once answered) ends at once; without the drop it was
  still running five seconds later.

**D233. In a container, a heap's cap is a quarter of the memory
limit.** *(Stevan, Oct 2026: "Let's do 2", the list of what D231 and
D232 made possible, its first item PLAN.md's "Containers, what's left";
then, to Claude's question, half the limit, which a measurement turned
to a quarter, also Claude's recommendation.)*

- **What it was:** each process's heap may grow to 1 GB (D202), and in
  a container limited to less, a process that runs away takes the
  program over the limit, and the kernel kills it whole: in Docker with
  `--memory 256m`, exit 137, nothing said.
- **Now:** with no `SLIGHT_HEAP_MAX`, the runtime reads the limit at its
  start, on Linux, from `/proc/self/cgroup`: v2's `memory.max` or v1's
  `memory.limit_in_bytes`, for the program's cgroup and each above it,
  the least of them. In a container with a view of its own the path is
  `/`; in one without (v1, as on the cloud machine), the host's path
  isn't there to read, and the walk up comes to the container's own at
  the top. v2's `max` and v1's figure for none are no limit. The cap is
  a quarter of the limit if that's less than 1 GB, and at least 1 MB.
  It's one of the settings a record keeps (D202), so a replay has it,
  wherever it runs. `:system`'s runtime info has it now, `(:heap-max
  bytes)`.
- **Why a quarter, not half:** a process that keeps all it makes holds
  about twice its cap at the collection where it faults, the old chunks
  and the new: measured, 134 MB at a 64 MB cap, 269 at 128, 537 at 256.
  At half the limit, such a process reaches the limit itself: in a
  cgroup of 256 MB it faulted and the program went on, but the kernel
  had reclaimed at the limit 1,132 times to get there. At a quarter it
  peaks at half the limit, and the other half is the rest of the
  program's. Rejected: half, as first chosen, for that; a third; and
  only saying in the docs to set `SLIGHT_HEAP_MAX`.
- **Not yet:** a cap on all the heaps together. Each process's is
  capped, but many processes can still take a program over its limit;
  a total would fault the process that asks, whatever holds the memory,
  which is a proposal of its own. Mailboxes and stacks aren't counted
  either, as before.
- **Checked:** t/live.py's memory limit, on both targets (AArch64 under
  qemu): in a cgroup of 256 MB, made under the program's own, the cap is
  64 MB, and a process that grows without end faults, `(error heap)`,
  and the program goes on; skipped without root or a memory controller
  to write to. Test 281's keys now have `heap-max`. By hand, in a cgroup
  of 256 MB: the most it used was 134 MB, the limit never reached; with
  the old 1 GB, killed. In Docker, from an image `FROM scratch`:
  `--memory 256m` gives a cap of 64 MB, and the program goes on; with no
  limit, 1 GB; and with `SLIGHT_HEAP_MAX=1G`, the old default, the
  kernel kills it, 137.

**D234. Default timeouts: 30 seconds for an HTTP request, and 2 minutes
for the `run` tool.** *(Stevan, Oct 2026: "Let's do 2"; then, to
Claude's questions, both as recommended. D173 and D187 had turned these
down only because a timer held up a program's end till it was due,
which D232 ended.)* (D236 took the HTTP half out again, since on the
virtual clock it overtook replies crossing macOS's loopback.)

- **HTTP:** `http/request` and `http/get` are `http/request-within` with
  30 seconds, `(http/timeout)`, for the whole reply, and give `(:error
  :timeout)` after it. The timer is a sleeper's, killed when the reply
  has come, so it doesn't hold up the program's end (D232).
  `http/stream` has none: a model can stream for minutes, and its
  caller can kill it. Rejected: a limit on a stream's head, or between
  its pieces, which a caller can do itself; and no default, as before,
  which leaves a caller of a server that never answers waiting for ever.
- **A risk, not yet seen:** on the virtual clock, a pending timer makes
  the clock jump when no socket is ready at that instant (D110), so a
  reply on loopback that isn't readable at once would be overtaken by
  the timeout, in a golden test. Linux delivers loopback TCP at once,
  and every HTTP test passes with the 30-second timer pending; macOS
  delayed a UDP answer that way (D165), and isn't checked here. If it
  does the same with TCP, the virtual clock should wait a moment, for
  real, for a connection waiting for its reply, as it does for a
  lookup.
- **`run`:** `exec/run-within ms cmd` is `exec/run` with a limit: a
  command still running at `ms` milliseconds is killed, its group with
  it, as closing its device does (D185), and its status is `(:timeout
  ms)`, its output what it printed till then. The timer is the command's
  process's, which ends with the command, so it doesn't hold up the end
  either. `tools/run` uses 120 seconds, as Claude Code's own does, and
  `tools/run-within ms approve` another; the model gets what it printed
  and `[stopped after 120 s, still running]`, and the tool's description
  tells it the limit. On the virtual clock, time stands still while a
  command runs (D185), so there a command has no limit, and a test can't
  see one reached. Rejected: 10 minutes; and no limit, as before, where
  only Escape or Ctrl-C stops a command that never ends.
- **Checked:** golden tests 289 (on the virtual clock, `http/get` and
  `http/request` to a server that never answers give up 30,000 ms after
  asking, and a reply after 29,000 ms is in time) and 290
  (`exec/run-within` and the run tool on a command that ends, and the
  limit in the tool's description), worked out by hand; every HTTP,
  agent and tool test as before, with the 30-second timer pending. 289
  waits for ever without the default. t/live.py's run limit, on the real
  clock: `exec/run-within` and `tools/run-within` with 1 second, on a
  command that prints, then sleeps 10 minutes, each killed with what it
  printed; no `sleep` left after.

**D235. Who's at the other end of a Unix socket: `socket/peer`, and
`rpc/peer`.** *(Stevan, Oct 2026: "Let's do 2"; then, to Claude's
question, a builtin and `rpc/peer`, as recommended. D231 left it for
later, and D225 had "who the other end is" open; the first to use it
will be where a remote call's `approve` goes, step 15.)*

- **`(socket/peer c)`:** for a Unix connection, either end of it,
  `((:uid u) (:gid g) (:pid p))`, the other process's as the kernel had
  them at its `connect` (or, from the connecting end, at the listener's
  `listen`): Linux's `SO_PEERCRED`; macOS's `getpeereid`, and
  `LOCAL_PEERPID` for the pid, 0 where it can't be had (on macOS, once
  the other end has closed, D236). The other end
  can't say otherwise, as it could say anything of itself in a `helo`,
  so a program can decide by it whom it answers, beside what the path's
  permissions already let in (D231). For any other pid, a TCP
  connection, a command or a process, `()`; anything else faults, as
  for any pid. `:uid` and `:gid` are new runtime symbols, a group of
  their own after the commands' words, so nothing before them moves;
  `:pid` is `:system`'s.
- **A builtin, not a message:** the devices are otherwise asked by
  messages, `:system` too, answered to their owner; but a peer doesn't
  change, and `rpc`'s loop, which owns the socket, would have to keep
  track of who asked. Rejected: `(send c (list :peer))` answered with
  `(:peer c info)`, for that; and putting it in a Unix connection's
  `(:accept l c)` or `(:open c)`, which would change their shape, and
  every pattern that takes them.
- **`(rpc/peer)`:** from a handler, its caller's, asked of the
  connection, which answers with `socket/peer` of its socket: so `()`
  over TCP or a command. Only from the call's own process, as
  `rpc/next`, since it asks its parent.
- **Replay:** `rt_io_peer` is recorded, so a replay gives back the
  uid, gid and pid of the run it replays, though it runs as another
  process.
- **Checked:** golden test 291: a Unix connection made and accepted in
  one program, each end's peer this program's own, as `id -u`, `id -g`
  and the shell's `$PPID` say; `()` for a process, a fault for 42; an
  `rpc` handler's caller over a Unix socket, the same, and over TCP,
  `()`; on both targets, `SO_PEERCRED` going through qemu. t/replay.sh's
  net asks its Unix connection's peer, and the replay prints the
  recorded pid. By hand: a server as root, its socket's umask 000,
  called by root and by `nobody`, each seen as itself, 0 and 65534.

**D236. HTTP's default timeout taken out again, and `socket/peer`'s pid
on macOS.** *(Stevan, Oct 2026: the first `make test` on the Mac after
D234 and D235, twelve tests failing; then, after a first fix left six,
to Claude's question, "withdraw HTTP's default", as recommended over two
ways of keeping it.)*

- **What failed:** on the Mac, every HTTP test that gets a reply, from
  204 to 212, the airline's recoveries (256, 258, 259) and 289, gave
  `(:error :timeout)`; and 211's connection to a port nobody listens on
  timed out where it was refused. With D234's default, a 30-second timer
  is pending while a request waits, and the virtual clock moves to the
  next timer when nothing can run and no socket is ready at the instant
  it looks (D110). macOS's loopback delivers a moment after a send, and
  refuses a connection a moment after the connect, so the clock found
  nothing and went to the timeout. Linux's delivers at once, so the
  cloud machine never saw it; a reply from another program, a Python
  server, lost the same way on Linux, every time.
- **A first fix, taken out:** the runtime noted a write to a socket or a
  command, or a connection started, and forgot it when anything came
  from the sockets; before moving the clock with that noted, it waited
  for the sockets, for real, up to 200 ms, as it does before a lookup's
  timer (D165). The Mac then failed six (206, 209, 212, 256, 258, 259),
  each with replies on their way at once: the first to come cleared the
  note while the others were still crossing, and the clock went to their
  timeouts. Six requests at once to the Python server timed out every
  one, on Linux. One note for the program can't say what's still on its
  way.
- **Now:** `http/request` and `http/get` wait as long as the reply
  takes, as before D234, and the runtime is as it was. A timeout is
  asked for with `http/request-within`. The weakness is the virtual
  clock's, and stays: a test mustn't have a timer pending while it waits
  for a reply on loopback, unless the timer is meant to win, as 211's
  `/silent` is. No test but 289 did, which is taken out, with 292, the
  first fix's. D234's other half, the run tool's 2 minutes, stays:
  commands stop the virtual clock while they run (D185). Rejected: the
  virtual clock counting the bytes in flight between the program's own
  connections, each paired with its other end by port, about 80 lines
  of runtime for one default; and waiting 20 ms of quiet after every
  send, which costs the byte-at-a-time tests seconds and still loses on
  a loaded machine.
- **`socket/peer` on macOS:** 291 said `(accepted #false)`: the
  accepted end asked its peer once the connecting end had closed, and
  macOS's `LOCAL_PEERPID` answers only while the other end is connected,
  so the pid was 0 (`getpeereid`'s uid and gid are kept). The test now
  has each end ask while the other is still connected, and LANGUAGE.md
  says the pid is 0 on macOS once the other end has gone. `rpc/peer`
  asks during a call, while its caller is connected. With the first fix
  in, 291 passed on the Mac. Rejected for now: asking at accept and
  keeping the answer, which would make the pid the same everywhere, for
  a device field and a call per connection.
- **Checked:** the golden tests, the replay and the live checks on both
  targets here, the runtime's idle as before D234, and `lib/http.slight`
  as before but its comments.

**D237. Messages with something in them, and every core reported
apart.** *(Stevan, Oct 2026: "Yes, let's go", to Claude's recommendations
after the 23 Oct run on the M2 Max: what to add to the benchmarks,
whether Rust should run the sequential ones, and what the every-core
runs are for.)*

- **`payload 1000000`:** ping-pong with each message carrying the same
  list of 100 integers, which goes there and back a million times; it
  prints the round trips and the list's sum. The actor programs' messages
  were a token or a tagged pair: `thread-ring`'s and `fan-in`'s a bare
  integer, nothing to copy (D199), `ping-pong`'s three words. A message
  that carries data is what slight's model costs most, and none showed
  it: slight and Erlang copy it into the receiver's heap, Go passes a
  slice and tokio moves a `Vec`, neither copying. In slight, Go, Erlang
  and Rust, beside `ping-pong`, which it differs from only in the list.
- **Found** on the cloud machine, x86-64: slight 1.1 s, against 0.17 for
  `ping-pong` at the same million; Go on one core 0.4 s and tokio 0.3
  (Erlang isn't installed there). Under callgrind, 84 per cent of the
  instructions are `copy_values_after`, about 73 a cell: checking that
  each cell isn't static data (both address ranges, their four bounds
  loaded each time), scratch's bounds, and noting each pointer for the
  move into the chunk, which goes over them again; and in the code
  clang makes, scratch's and the notes' sizes, which are globals, are
  loaded and stored back at each cell rather than kept in registers. A
  copy with less of this is for later, its own decision.
- **One core is the comparison:** slight runs on one, so the summary's
  tables now have Go, Erlang and Rust on one core. Their defaults, every
  core, still run, and are a table of their own at the end: each one's
  time over the same language's on one core (what every core gains or
  costs) and over slight's. `bench/run.py --summary` wrote the 23 Oct
  run's again this way. The every-core runs stay because `parallel-fib`
  means nothing without them (on one core it's `fib` 64 times); because
  they show what more cores cost at messages (tokio's multi-thread
  runtime 34 times slight's `ping-pong`, Go 9 times its `fan-in`),
  evidence for slight's one core; and because they're what Go, Erlang
  and Rust give their users unless told otherwise.
- **Erlang on one scheduler on the Mac** (D194: on a core 37 to 60 per
  cent of the time it takes, unexplained) is to be looked into there,
  since the one-core comparison needs it: the new table shows it as
  every core making `fib` and `tak` take 0.55 of their one-core time,
  which a sequential program can't gain. Till then the text in
  BENCHMARKS.md, and the page that reads the 23 Oct run, quote Erlang at
  its every-core numbers.
- **Next, in order:** latency under load, the worst and 99th-percentile
  round trip while another process works without waiting (what counting
  reductions buys, which `fib` shows only the cost of) or collects a
  big heap (what one core costs: a major collection holds everything
  up, 0.5 to 1 ms a MB live, D202), which needs the harness to take a
  measurement from a program's output; then a map, `lib/map.slight`
  against each language's own (Erlang's, persistent too, the like for
  like). Later, D194's third question: strings, JSON and HTTP, which
  mostly compare libraries.
- **Rejected:** Rust in the sequential programs, D194's reason still
  holding: its code is LLVM's, as C's is, and seven more programs would
  land beside C's. And dropping the every-core runs, for the reasons
  above.

**D238. Latency: how long a message waits while other processes work.**
*(Stevan, Oct 2026: "Yes, go with your recommendations", to Claude's
design for D237's next step: pings paced by yielding, the pinger inside
the program, 160 MB live at full size, and tokio's busy task written
plainly.)*

- **`latency s`:** a pinger pings a responder, ping k due k ms after it
  starts; till then the pinger yields and looks at the clock again, so
  it sees a ping is due as soon as it next gets a turn. A ping's latency
  is from when it was due to when its answer came, so a stall counts
  against every ping it held up, as wrk2 counts them. Three phases:
  idle; busy, beside a process working out `fib(30+s)` without ever
  waiting, what counting reductions buys (`fib` shows only its cost);
  and heap, beside one holding 100s blocks of 10,000 integers and
  replacing the oldest 1,000s times, yielding after each, its pinger
  started once the blocks are made: what a collection costs everyone
  else. In slight, Go, Erlang and Rust; full size 10 (`fib 40`, 160 MB
  of lists in slight), quick 1.
- **The harness:** a program's lines that start with `#` are
  measurements, `# phase p50 p99 max n` in microseconds, taken out
  before its output is checked (fib's result and the heap's checksum)
  and kept with each run. The summary gives `latency` tables of its own,
  each phase's figures the median over the runs: one core, then every
  core.
- **Rejected:** pacing the pings by timers: Go's on Linux, tokio's and
  Erlang's are good to a millisecond, so the idle floor would be their
  timers' (Go's 0.6 ms on the cloud machine), not a message's wait. And,
  for now, a client outside the program over a socket, which would also
  show how often a runtime looks at its sockets while busy (every 10 ms
  in slight, D122), for a client in C and phases kept in step with it.
- **tokio:** its busy task never waits, written plainly; tokio doesn't
  preempt, so on one thread every ping waits for `fib` to end
  (`spawn_blocking` would give it a thread of its own, which isn't one
  core). On every core, `yield_now` leaves the pinger on its own worker,
  which the busy task then holds: 130 ms at the median, where a pinger
  that spins without yielding waits 8 µs (and starves what's deferred on
  its worker, the heap phase taking 28 s). So `rust-mc` would show how
  tokio defers a yield, not what every core gains, and is left out
  (`SKIP` in `bench/run.py`).
- **Found** on the cloud machine, x86-64, at full size: slight idle under
  a microsecond at the median; busy 5 to 7 µs at the median, its tails
  0.2 to 12 ms from run to run on a shared machine; heap 12 ms at the
  median, 375 at the 99th percentile and 400 at most, 488 MB at its peak:
  a major collection of a 160 MB heap holds every process up, about 2.5
  ms a MB. Go on one core: busy 9.9 ms at the median, 20 at the 99th
  (it preempts every 10 ms), heap 13 µs and 0.9 ms. tokio on one thread:
  busy 125 ms at the median and 249 at most, all of `fib`; heap 8 and
  22 µs. Go on every core: 4 µs at the median, busy and heap.
- **A slip on the way:** slight's heap phase first kept its first blocks
  alive, in the frame of a function that called `churn` outside tail
  position (a frame's slots are all roots, D166), so twice the live data
  was copied at each major collection: 241 ms at the median, 967 MB.
  `churn` is a tail call now, and joins the pinger itself.

**D239. The collector's walk, faster.** *(Stevan, Oct 2026: "Let's do A
for now, and wait for C until we are sure we need it, or we find other
reasons to add additional threads", from Claude's options for the pauses
`latency` showed, D238.)*

- **The options:** A, a faster copy; B, collecting when nothing else can
  run; C, big major collections on a second thread, probably what keeps
  Erlang's pauses from showing (D238), the collector as it is and the
  process collected not runnable while its heap is copied; D, major
  collections in pieces on one core, which immutability allows with no
  barrier (the copy made while the process goes on using the originals,
  which can't change, then the roots and what was made meanwhile
  switched over), with forwarding in a side table, and the most to get
  right; E, an old generation that isn't moved (D200 turned it down);
  F, advice alone. A is done here. C waits till it's needed, or till
  another reason for a thread comes, since it would be the runtime's
  first (D153). B, D and E aren't taken up.
- **Where the time went:** `latency`'s heap phase alone (a list of
  blocks, the oldest replaced again and again), under callgrind on the
  cloud machine: 81 per cent of the instructions collecting, 52 a cell
  copied. Each copied cell went on the work stack and off again; each
  pointer was checked against both ranges of static data, its four
  bounds loaded each time; and the walk's to-space pointer, the bytes
  copied and the work stack lived in `gc_t`, which the walk hands to the
  function that gets a new chunk, so the compiler loaded and stored them
  around every word copied, a word copied being, as far as it knew, one
  of them.
- **Now:** what changes at every object is a struct of its own that only
  always-inlined helpers point at, so it lives in registers; the walk is
  compiled twice, for a minor collection and a major one; what moves is
  worked out once (in a minor one the nursery and the chunk it
  overflowed into, as two ranges, and a walk along the chunks if there
  are more; in a major one, all but static data, tested in full only
  between the lowest and the highest of its bounds); along a list, each
  cdr still to move is copied as the walk comes to it and the walk goes
  on from the copy, so a list's spine never goes on the stack, only cars
  that are lists or boxes of values; and a box of 64 bytes or less is
  copied a word at a time. The same objects are copied as before,
  forwarded the same way, sharing kept.
- **Measured** on the cloud machine, x86-64. Instructions: the heap phase
  alone at size 2, collecting 4,575 million to 1,839 (21 a cell), the
  program 48 per cent fewer; `binary-trees 14` collecting 163 million to
  125, the program 8 per cent fewer. Times, noisy there: the heap phase
  alone at size 4, 1.28 s to 0.95; `binary-trees 18` 2.05 s to 1.89 (the
  medians of five); `latency`'s heap phase, five runs each, the worst
  pause 195–206 ms to 131–153 (medians 197 and 133), the 99th
  percentile 164–186 ms to 122–135, the phase 3.1 s to 2.2; test 162
  under `SLIGHT_GC_STRESS=100`, 55 s to 43. On the M2 Max (Stevan's run,
  `2026-10-23-1516`): `binary-trees 18` 1.03 s to 0.79, 1.30 times
  faster, now ahead of C (1.26 times) and level with Erlang on one
  scheduler; `latency`'s heap phase at the median, the 99th percentile
  and the worst 0.23, 57 and 61 ms to 0.12, 44 and 47, about 0.3 ms a MB
  live.
- **Tested:** `make test` on both targets, 282 golden tests each.
  `SLIGHT_GC_VERIFY=1 make golden`: all of x86-64's; under qemu, 059,
  130 and 240, which fill a 64 MB heap, killed at 60 s, 130 and 240
  passing given 15 minutes and 059 not finishing in that (it passes on
  x86-64; the old collector wasn't run so, which would take as long).
  `SLIGHT_GC_STRESS=7 make golden`: timeouts only, 14 under qemu and 8 on
  x86-64 (D200 had 14 and 10), 293 among them before it was made smaller,
  as it now is, passing at 7; the rest pass at 100 but 059, 130 and 240,
  as before (162 under qemu given 15 minutes). Golden test 293: lists of
  lists and of boxes, a list run onto an old one and onto a quoted one, a
  tail shared by 1,000 lists and a list held 1,000 times (80 MB each,
  unshared, over the tests' 64 MB), a list of 2,000 lists filling the
  walk's stack, and major collections brought on by big strings.
- **Not done:** the message copy (D237), with the same kind of
  bookkeeping, about 73 instructions a cell: next, if wanted.

**D240. The message copy, faster.** *(Stevan, Oct 2026: "Yes, lets apply
this to the message copy as well", after D239.)*

- **Where the time went:** `payload` under callgrind on the cloud
  machine, x86-64: the copy's walk 68 instructions a cell, malloc and the
  memcpy into the chunk aside. Scratch's fill and size, the words noted
  and their count were globals, and the work stack an `rt_work_t` in a
  function of its own, so the compiler loaded and stored them around the
  words it wrote; each value was sorted into one of four kinds and then
  dispatched on; noting a word checked the room for notes each time; and
  the move into the chunk asked of each word noted whether it was among
  the values at the front of scratch or in the copy.
- **Now:** the walk is the collector's (D239), with nothing to forward.
  What changes at every word (scratch, its fill, the words noted, the
  stack) is a struct that only always-inlined helpers point at, so it
  lives in registers. A box is copied when the walk comes to it, and a
  cell along with the rest of its list, each cdr as the walk comes to
  it; cars that are lists, and boxes of values, go on the stack, with
  where their copies are, for after. Static data is tested in full only
  between the lowest and the highest of its bounds, and a box of 64
  bytes or less is copied a word at a time. The notes grow with scratch,
  room for one a word, so noting one checks nothing (each is a different
  word of scratch). Every word noted is made where it will be while it's
  still in scratch, the values at the front included, so that's one loop
  with no test in it, and then the copy is moved. And values with no
  pointer among them (integers, keywords, pids), as fork's values and a
  process's result often are, and many messages, aren't walked at all.
- **Kept:** D177's one walk, into scratch, by offsets, then a chunk of
  just its size; sharing still isn't kept. The stack now grows, as the
  collector's does, with how many cars that are lists wait on it, not
  with how deep they nest: a list of 2,000 lists puts 2,000 pairs on it,
  32 KB.
- **Measured** on the cloud machine, x86-64. Instructions: `payload
  20000`, 334 million to 223 (33 per cent fewer), the walk 41 a cell;
  `spawn 20000` 9 per cent fewer, `thread-ring 200000` 8, `fan-in
  200000` 7, `idle-memory 20000` 3; `ping-pong 20000` 0.6 per cent more,
  its messages being two and three cells, so that what a copy costs
  whatever its size (the struct set up, more registers saved) outweighs
  what the walk saves. Times, noisy there, medians of five: `payload
  300000` 0.48 s to 0.34; `ping-pong 2000000` 0.56 to 0.49, within the
  noise. On the M2 Max (Stevan's run, `2026-10-23-1701`, the other
  languages within 6 per cent of the run before): `payload 1000000` 0.84
  s to 0.56, 1.51 times faster, 2.8 times Go's where it was 4.3, about
  2.0 ns a cell copied where it was 3.3; `ping-pong 2000000` 0.348 to
  0.301, 1.16 times faster, level with tokio; `spawn`, `fan-in` and
  `thread-ring` 8, 9 and 4 per cent faster.
- **Tested:** `make test` on both targets, 283 golden tests each.
  `SLIGHT_GC_VERIFY=1 make golden`: all of x86-64's; under qemu, 059 and
  240 killed at 60 s, 240 passing given 15 minutes (059 doesn't finish
  in that, D239). `SLIGHT_GC_STRESS=7 make golden`: timeouts only, 14
  under qemu and 8 on x86-64, as with D239, each passing at 100 but 059,
  130 and 240, as before.
  Golden test 294: values taken into a fork, kept as its result, given
  back by `join` and `join/race`, and sent there and back, alone and all
  in one message: lists of lists and of boxes (strings, floats, tuples,
  closures), a list 20,000 long, one nested 1,000 deep in its cars, a
  list of 2,000 lists, tuples, strings and closures each side of 64
  bytes, quoted lists whole and as a list's tail, one list three times
  in another, and values with no pointer in them.
- **Not tried:** two walks again, one adding up the bytes and one copying
  straight into a chunk of that size, with nothing to note or move.
  D177 chose one walk when a walk was dear (the boids example's sends
  spent half their time adding up the bytes); now that it's cheaper, two
  might beat one for big messages, the first walk about ten instructions
  a cell, the second about twenty. It would undo D177, so it's for
  Stevan to say.

**D241. A simulation benchmark: `bank`.** *(Stevan, Oct 2026: "Yes, add
to bench/", after Claude measured an M/M/c queue in slight, SimPy and C,
asking where slight fits as a simulation platform.)*

- **What:** SimPy's bank example, the one its documentation starts with:
  3 tellers and n customers, a process for each customer, and the bank a
  process (in SimPy a `Resource`) that each asks for a teller and gives
  it back to. slight's runs on the virtual clock, which the harness sets
  for it alone (`BENCH_ENV`). Against it, SimPy, the usual
  process-oriented simulator, and C as an event list, not a process for
  each customer: the floor under any simulation engine. Python is left
  out if SimPy can't be imported (`pip install simpy`).
- **The same answer in every language:** the gaps between arrivals and
  the times served are whole milliseconds, uniform from 1 to 1,999 and
  from 1 to 5,399 (the tellers 90 per cent busy), from Park and Miller's
  generator, the same in each. With whole milliseconds and the queue
  first come, first served, each customer's wait follows from the times
  alone, whatever order the events at one moment run in (Kiefer and
  Wolfowitz's recursion: each customer takes the teller free soonest),
  so every version prints the same three numbers, the customers, their
  waits in all and how many waited, and the harness works them out.
- **Rejected:** exponential times from each language's own generator, as
  the measurement before it had (each agreed with Erlang C, slight's over
  40 seeds within one standard error, so its forked random streams show
  no bias), since the versions would then agree only in distribution,
  and `log`'s last bits differ between libms; and Python's own event
  list on `heapq`, since SimPy is what people use.
- **Measured** on the cloud machine, x86-64, a million customers, before
  D242: C 0.037 s, slight 1.50 s, SimPy 10.9 (7.3 times slower than
  slight). slight's 42 MB peak is the exit records, 40 MB a million
  processes that have ended (D88): 13.6 MB at 250,000 customers, 83.7 at
  2 million. Under callgrind, about 6,800 instructions a customer, 28 per
  cent in malloc and free (each message, process and timer is one), and
  a quarter of the time in the kernel, which D242 took out.

**D242. The virtual clock doesn't ask `select()` when there's nothing to
look at.** *(Stevan, Oct 2026: "lets do #1", from Claude's list after
`bank`.)*

- **Why:** on the virtual clock, each time nothing can run the runtime
  looks for keys, sockets and commands with `select()`, a timeout of 0,
  before it moves the clock (D139). With none of them open that's a
  system call that can see nothing. A simulation moves the clock at
  every event: `bank` made two calls a customer, two million a run, a
  quarter of its time.
- **Now:** `wait_for` returns at once when it has no descriptor to look
  at and no time to wait. It still calls `select()` to wait (on the real
  clock, to the next timer, or forever), and to look whenever a key is
  wanted or a socket or command is open. A recording and its replay make
  the same calls, since a replay's devices have the descriptors the
  recording got.
- **Measured** on the cloud machine, x86-64: `bank 1000000` 1.50 s to
  0.96, the median of five, its system time 0.42 s to 0.03; SimPy is now
  11.3 times slower than slight, C 27 times faster. Nothing on the real
  clock changes, nor anything with a device open.
- **Tested:** `make test` on both targets, 284 golden tests each.
  Golden test 295: `bench/bank`'s model, 2,000 customers, alone and
  then with a listener open, which strace shows still calling
  `select()` at each step (3,998 calls, where both runs made 7,996
  before), the waits and the time the last teller came free from
  `t/models/bank.py`, by the recursion.

## Tasks and actors

**D243. Two kinds of process: actors, which `recv`, and tasks, which
compute a value.** *(Stevan, Oct 2026: "there are two forms of
concurrency here ... perhaps we should spawn actors instead of fork
them", then "go with your recommendations on the points to settle", on
Claude's proposal in PLAN.md, "Tasks and actors".)* Agreed; built in
four steps (below), the rest of exit records still to come. A census of every `fork` and `connect` in `lib/` and the
examples (`docs/processes-2026-10-23.md`) found 49 actors, 14 tasks and
33 that were both: 9 waiting for one reply, 18 reading a device to its
end for a value, and 6 gathering several messages. Where the two meet,
the costs found in Oct 2026's review come from: a process, an exit
record and a held stack per `ask`; every value kept in case of a
`join`; a helper's `recv` taking its caller's messages.

- **`(spawn expr)` starts an actor.** The `recv` rule is an actor's
  (D8, D167). An actor can be sent to, monitored and killed, not joined
  (`:not-a-task`). When it ends its monitors are told `(:exit pid
  result)`, and its value isn't kept. `connect` starts an actor, as it
  started a process.
- **`(fork expr)` starts a task.** A task can't `recv`: the compiler
  refuses a wait in a `fork`'s expression or the top level, and the
  runtime faults one as a backstop. It can't be sent to
  (`:not-an-actor`). It can be joined, monitored and killed. The root is
  a task.
- **`(address)`, `(read src)`, `(read/any srcs)`:** a task makes
  addresses to be sent to, and waits, keeping its stack, for the next
  message from one, from a device it owns, or, with `read/any`, the first
  of several, a pid's being its end. `send` and `after` take an address
  where they take a pid; `disconnect` closes one. `join/race` becomes
  `read/any` over pids. `ask` is a prelude function on them, with no box.
- **`(open how ...)`** opens a device the calling task owns, with no
  process, giving `(:ok d)` or `(:error reason)`; `(read d)` gives its
  messages, cut at the `read` (D158), and its failure as `(:error d
  reason)`, where an actor that owns a device still ends (D128).

The points the proposal left open, each as Claude recommended:

1. **Names:** `spawn`, `fork`, `address`, `read`, `read/any`, `open`.
2. **An address is an immediate** under the pid tag: the task's number
   and the address's, printed `#<address 3.2>`. Nothing to copy or
   collect, `eq?` and `hash` a word's, and no way to make one from data,
   as with a pid. Rejected: a tuple `#(pid n)`, which can be printed,
   read back and faked.
3. **An actor can't `ask`:** it makes no address, so it can't `read`. It
   sends, and takes the answer as a message, or forks a task to wait. So
   no handler waits on another, and D8's "no call-cycle deadlocks
   between handlers" holds again for actors.
4. **An actor may block in `join` and `sleep`,** as now, keeping its
   stack while it does (`top`'s consumer sleeps).
5. **An actor's end, told late:** `monitor` of an actor that has ended,
   or `read/any` of its pid after its end, gives `:ended` as the result,
   `(:exit pid :ended)`, since its value isn't kept. A change to D101 for
   actors; a task's is as before.
6. **`:keypress` stays as it is** (D131): the terminal programs whose
   last state the root joins stay actors, and tell it through a monitor.
7. **Tasks used as `try`** (3 of the 14) stay so: `join` still gives
   `(:error reason)`. D17's "no `catch`" isn't reopened here.
8. **`ask` raises** `(:ended pid result)` if the actor ends first, as
   D157's does; `ask-within` raises `(:timeout pid)`. `http/request`,
   `agent/ask` and the rest keep their Results.
9. **The rest of exit records** (how long a task's value is kept, and
   when a pid's entry goes) is still the open item in PLAN.md, "Exit
   records".

To be built in four steps, each with passing tests: the kinds and their
rules; addresses, `read` and `read/any`, and `ask`; `open`; then exit
records. It replaces, as each lands, parts of D8 and D167 (the `recv`
rule is an actor's), D19 (`join` is a task's), D88 and D216 (an actor's
value isn't kept), D101 (point 5), D124 and D128 (a task opens a
device, and is told its failure), D157 (`ask` without a box) and D220
(`join/race` over `read/any`). Rejected on the way: extending
`split.ts` to split at every call that can wait (continuations on the
heap), which hides where a wait can go but not what it takes from the
mailbox; receivers as values, `recv/with`, which makes actors' loops
pleasanter and leaves the boxes; and a reply slot alone, which `address`
generalizes.

**Step 1, built** *(Claude, Oct 2026, on Stevan's "yes, start step 1")*:
`spawn`, the two kinds, and the rules that hold without the rest.

- **The order changed:** an actor can't be joined, and keeps no value,
  moved from step 1 to after addresses and `open` (PLAN.md's step 4),
  since till then the hybrids (`ask`'s box, `slurp`, `http/request`,
  `rpc/call`) can only be actors that are joined. So for now an actor's
  value is kept, and `join` takes one; `:not-a-task` is in `rt.h`, for
  then.
- **What a task can't do:** wait (the compiler refuses a state function
  called in a `fork`'s body or the top level, `classify.ts`'s
  classification as it was, and `rt_recv` faults as a backstop); be sent
  to, or timed with `after` (`:not-an-actor`, in the sender: a message
  that can't be taken is a bug in whoever sent it); and monitor, since
  the notice would be a message it can't take, which D243 didn't say and
  follows from it. A message to a task that has ended disappears, as
  before (D92).
- **In the runtime:** a flag in `rt_proc_t` (`keypress` and it share the
  word `keypress` had), `rt_spawn`, `connect`'s processes actors, and
  `:system`'s `(:kind k)`. Two fault kinds and the three words of
  `:kind` move every runtime symbol after the faults up by two, which
  only the tables see.
- **The deadlock message follows the root's join:** the root, a task,
  now waits in `join` where it waited in `recv`, so `... waiting in the
  top level for #<pid 2> to end` alone would hide where the program is
  stuck. It goes on, `, which is waiting in wait-forever for a message`,
  through each join, and stops after eight with `, and so on`, since
  tasks that join each other make a cycle.
- **Programs:** `fork` became `spawn` for every process whose body waits
  (a script asked the compiler, about 150 places, the hybrids among
  them). 66 programs whose top level waited (53 golden tests, three
  examples, the replay and live checks' three programs, seven
  benchmarks) have it moved into `(defun top-level ...)`, which the root
  spawns and joins; a benchmark takes its value back out with `cadr`, so
  the harness's check is unchanged. Two tests, and the live check of
  timers, set timers on a sleeper or the root, now an actor that never
  takes them; one keeps its
  root and moves only its round trip into an actor, so the hash of the
  root's pid it checks stays the model's.
- **Expected output:** a golden test's was changed only as the move
  explains, and checked so: its old expected output, with every pid one
  more (the top level's actor is pid 2) and the root's value as `(ok
  v)`, against what it prints, which left only deadlock messages (the
  longer one above) and sites in moved code, each checked to be the same
  expression. New: 296 (tasks and actors: a task joined, an actor sent
  to, a task sent to, timed and monitoring, a message to an ended task,
  and `:kind`) and 297 (a cycle of joins, and so on), worked out by
  hand.

**Step 2, built** *(Claude, Oct 2026, on Stevan's "start step 2")*:
addresses, `read`, `read/any`, and `ask` without a box.

- **An address** is an immediate under the pid tag with its top bit
  set: the task's index in 32 bits, and its number among the task's
  addresses in 28 above them, printed `#<address 1.0>`. It's copied,
  compared and hashed as a word, can't be made from data, and `pid?`
  says `#true` of it; every other process operation faults
  `:not-a-pid` on it, through `entry()`. A task keeps the numbers of
  its open addresses in a small table, a pointer in `rt_proc_t`.
- **Delivery:** a message to an address goes into its task's mailbox,
  its record's device field holding the address (a task has no device
  to read on yet), and wakes the task if it's reading that address; to
  an address that's closed, or whose task has ended, it disappears, as
  to an ended process (D92). A timer can be set for one, and is dropped
  as soon as nothing can come of it (D232).
- **`read` and `read/any`** take the first of their sources, in their
  order, with something: a message waiting for an address, or a pid
  that has ended, its result as `join` gives it. Else the task waits in
  a new state, `:reading` to `:system`, keeping its stack, woken by a
  message to one of its sources or by a watch on each pid among them,
  as `join/race`'s (D220), and looks again. Reading a closed address,
  another task's, or `()`, faults; reading one's own pid is
  `:join-self`. `join/race` stays in the runtime: actors still race
  (they may block in `join`, D243's point 4), and read only in a task.
- **`ask`, `ask-within` and `reply` moved to the prelude** from
  `lib/ds.slight`, `ask` making an address, sending, and reading the
  address and the actor together, so it gives the answer or raises
  `(:ended pid result)`; `ask-within` sets a timer for the address, and
  takes it back. No process and no exit record per question.
- **Actors that asked:** two did. `active-objects`'s object actor works
  out an answer that asks other objects' actors, so it forks a task for
  each answer, which replies; and 199's top-level actor only needed to
  be an actor for its timers, so the root does the rest again and its
  expected output is back to what it was before step 1.
- **Libraries:** `rpc/peer` asks the connection at an address, from the
  handler's own task. `rpc/connect`, `rpc/exec` and `agent/ask` fork a
  task that reads an address, and join it, rather than reading in the
  caller: their callers include actors (a test's actor that has just
  heard `(:listening port)`, a hub), which may join a task but can't
  read. They start a process each, as their box did, so no test's
  output changed.
- **Left for step 4,** which forces them: `rpc/call` and `rpc/stream`,
  whose waiting process the connection monitors and a keeper tells
  which connection it used (D228, D230), and the coordinators (the
  tournaments' games, ping-pong's pinger, the airline's desk and
  stand-ins, an agent's turn), still actors that are joined.
- **Tests:** 298 (an address printed, a message to oneself, an actor's
  answer, a task's, a timer's, `read/any` in its sources' order with a
  pid's end, a timer taken back, `disconnect` dropping what waits and
  what comes, reading a closed address and another's, `ask`, an actor
  that has ended, `ask-within` timing out, and the faults: an actor
  making an address, `read/any` of `()`, `read` of oneself) and 299
  (`:system` showing a reader and its source, and the deadlock message
  for `read/any`), worked out by hand.

**Step 3, built** *(Claude, Oct 2026, on Stevan's "start step 3")*:
`open`, a device a task owns.

- **`(open source arg)` is a builtin,** not a special form as
  `connect` is: its sources are runtime symbols (`:fs/read`,
  `:fs/write`, `:fs/append`, `:tcp`, `:unix`, `:exec`, `:system`, a
  group of their own), which the runtime tells apart, so `open` can be
  a local's name (`lib/tools.slight` has one) and a value. Listeners
  and `:keypress` aren't among them: what waits for whatever comes is
  an actor, and connects.
- **Opening** is `connect`'s, factored out of each `rt_connect_*`
  (`fs_open`, `exec_start`, and the socket openers as they were), with
  the task as the owner. `open` then reads the device's first message:
  `(:open d)` gives `(:ok d)`; a failure, `(:error reason)`. One that
  can't start at all (no such file or command) gives its reason with no
  device made.
- **Reading:** a device's message to a task is marked with the device,
  as an address's is with the address, so `read` takes it by its
  source; the mark is always there for a task, where for an actor it's
  set only when the next message is to be cut (reading on after a
  message that doesn't need it does nothing). The device whose message
  a task took last is read on at its next `read`, as at an actor's next
  `recv` (D158). A device's messages are read before it's taken as
  ended, so `(:eof f)` comes though the file has closed.
- **Failure:** D128 ends a device's owner; a task asked for its device,
  so it's told instead: `(:error d reason)` where the device's next
  message would be, and the device closes. `spew` and `tools/save`
  read their file after closing it, so a write that failed raises, as
  it ended the writer before.
- **A device closed or handed on** while its task reads it wakes the
  task, which finds it ended.
- **Libraries:** `slurp`, `spew`, `exec/run`, `exec/run-within`,
  `tools/read`, `tools/save`, the airline's `read-sexps` and
  `rpc/system` open and read in a task of their own, which raises what
  went wrong, so `join` gives what the actor did. `exec/run`'s task reads
  its command, an address for its timer, and its caller's end, which
  ends it and so the command (D193). Each starts a process and a
  device, as `connect` did, so no output changed but one test's that
  prints `lib/fs.slight`'s definitions (182).
- **Left for step 4: HTTP's client.** Its connection's process shares
  the head, framing and body readers with the server's (D182), all
  `recv`; reading them with `read` would be a second copy of each. In
  step 4 the connection sends its result to an address the caller
  reads, as `http/stream` sends its pieces, which needs neither.
- **Tests:** 300 (a file written and read, `(:read :chunks)` between
  reads, the end, a file that isn't there, a directory failing at its
  first read, a command written to and read, one that can't start,
  `:system`, a device closed while its reader waits, a refused
  connection, and the faults: an actor opening, a listener, a path
  that isn't a string), worked out by hand.

**Step 4, built** *(Claude, Oct 2026, on Stevan's "go with option 1":
strict D243, every program restructured, and starters for servers that
a task can call)*: an actor can't be joined, and keeps no value.

- **The rule:** `join` and `join/race` of an actor fault `:not-a-task`.
  A task reads an actor's end instead. Its value isn't copied into its
  exit record: it goes from the actor's own heap to those watching as
  it ends (its monitors, and each task reading it, as a message marked
  with its pid, as a device's is), and the record keeps only that it
  ended, so a later `read`, `monitor` or `:system` gets `:ended`.
- **A read's watch lasts:** `read` and `read/any` watch each live pid
  among their sources as they start to wait, and the watch stays after
  they return, till the task next reads without that pid. So an end
  that comes between two reads (two actors that end together, one read
  after the other: 143) waits in the mailbox for the next, and isn't
  lost; and a task that has stopped reading a pid isn't sent its end.
  So a task reads an actor from before it ends: the one it has just
  spawned, or just sent its last message to, with no call between that
  could let it run; otherwise the actor sends what it has to an address.
- **A `connect` that fails as it starts** (a file or command that isn't
  there, a port that's taken, an address that isn't one) used to end
  its actor at once,
  before `connect` returned, so a `read` of it would be `:ended`; now
  the actor starts, and ends with the reason when it first runs, so
  `(read (connect ...))` gets `(:error reason)`, as `join` did.
- **Libraries:** `http/listen` and `rpc/listen`, `http/serve` and
  `rpc/serve` for a task: they wait till the server listens, at an
  address, and give `(:ok (l port))`, or the listener's error.
  `http/request`, `http/request-within` and `rpc/call` wait in `join`
  for a task that reads the connection's, or the call's, end, so an
  actor (a handler, a hub's registry) can still call them, as
  `rpc/connect` and `agent/ask` already did (step 2): an actor may block
  in `join` (point 4). `rpc/stream` was already an actor that sends.
- **Programs:** a `(join (spawn ...))` or `(join (connect ...))` that
  a task reads at once became `read` (83 files, by a script). The rest were restructured, each in the way that
  kept its point: work moved from a top-level actor back to the root
  where it only needed to be an actor to hear `(:listening port)`
  (the HTTP and `rpc` tests, on `http/listen`, `rpc/listen`, and a
  test helper, `raw-up`); an actor that ends with a result sends it to
  an address (the tournaments' games, 199's collector, 151's and 250's
  listeners, 186's servers); an actor that answers a stop answers who
  stopped it (ping-pong's ponger, pub-sub's subscribers, so the broker
  ends with what they said); an actor that read another's end joins a
  task that reads it (277, 283); and tests that joined an actor to see
  it killed race a task (266) or read it while a task kills it (222).
- **Expected output:** changed only as these explain, each line
  checked: a value the root used to get through a wrapper actor's
  `(ok ...)` is the root's own; an actor read or monitored after its
  end is `ended` (125, 200, 229, 238's `agent/ask` of a killed agent);
  sites and pids that moved with their code; and the restructured
  programs' new lines (222's stops, ping-pong's and pub-sub's ends).
- **Tests:** 301 (an actor read from before its end, read and
  monitored after it, an end between two reads waiting for the second,
  and a watch dropped by a read without its pid), worked out by hand;
  and 296 and 268 gain joining an actor, and racing one.
