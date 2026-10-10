# Background

How this project came about, and what the earlier attempts taught. None of
this is needed to build the next step, but it explains why the design
looks the way it does.

## The lineage

| Project | When | What it was |
|---|---|---|
| **AVM** (stevan/AVM) | Aug 19–22, 2024 (8 commits) | An actor VM in Perl. It works: a tick loop, a message bus, spawn/send/recv, yield/stop, reaping, emulated multi-core. Its best trick was that a process's stack survives a yield, and a RECV on an empty mailbox resumes at the RECV itself. So every RECV is an await point with locals intact, which is how its recursive multiplier waits mid-handler. |
| **VM3** (stevan/VM3) | Aug 26 – Sep 11, 2024 | The second attempt, in Perl: docs first (an async calling convention, an assembly language for actors), then an assembler, CALL/RETURN frames, locals and int widths. It got no actor opcodes. About a week went into a TimerWheel that nothing used ("I will stop hyper focusing on this for now"), and the project stalled. |
| **ts-slight** (pmmc-labs/ts-slight) | Jul 6 – Sep 24, 2026 (190 commits) | slight in TypeScript: a Lisp with fork/send/recv/join/yield, a fuel-driven scheduler, and 27 examples. **Its user surface is what a64-slight kept.** |
| **ts-cpi** (pmmc-labs/ts-cpi) | Sep 25, 2026 – (71 commits by Oct 3) | ts-slight's successor: a "control plane interpreter" with errors as values, checkpoints, exit notifications with reasons, reply addresses answered at most once, a deterministic tick scheduler and a virtual clock. Much bigger (roles, grants, TUI, HTTP). a64-slight takes the virtual clock, the reader, and some ideas; not the CPI. |

**Removed from the repository in Oct 2026.** Until then `reference/` held
read-only copies of the parts of ts-slight and ts-cpi this project
started from (copied on 2026-10-03 from `pmmc-labs/ts-slight` at
`c0842ff` and `pmmc-labs/ts-cpi` at `a756ab9`), and `spike/` held the
AArch64 spike and its calling-conventions doc. Everything taken from
them now lives in the tree: ts-cpi's reader and its tests
(`compiler/src/reader.ts`, `compiler/tests/reader.test.ts`), ts-slight's
prelude names and TAP library (`lib/`), fifteen of its examples
(`examples/`), its key events (`runtime/tty.c`), and the spike's
`rt_switch`, register convention, header check and cross-compiling
(`runtime/`, `t/headers.c`, the Makefile). The last commit with both
directories is `3fd71e0`: `git show 3fd71e0:spike/aarch64/README.md`,
or `git checkout 3fd71e0 -- spike reference` to have them back for a
while. Their originals are still in those repositories.

## The calling convention (VM3, revised Oct 2026)

The spike's `CALLING_CONVENTIONS.md` (in git at `3fd71e0`) was the
October 2026 revision of VM3's doc.
It fixed the holes in the 2024 design:

- **A stray message could be taken as a reply.** In AVM's multiplier test, an
  extra message made `4 × 10` print **1003**. The fix was a skip with a
  cursor, as in Erlang, plus replies matched by ref.
- **A callee couldn't know how to reply**, so the request had to carry a
  reply address.
- **Message frames and call frames were unified.**
- **`.spawn` ran in the parent.**
- **Signals had no sender.**

It also found bugs in the Perl code: VM3's `LOAD_ARG` indexed by frame
depth, and AVM's `*_MSG3` branches were duplicated as `*_MSG2`.

a64-slight then dropped most of that machinery. The `recv` rule (D8 in
`DECISIONS.md`) means there is no AWAIT, no REQUEST/REPLY and no selective
receive. What carries over is the native layer the spike proved.

## Why native

The question was whether the convention could be real machine code rather
than a VM. The answer: with a native stack per process, **the async
calling convention collapses into the platform's sync one.** An AWAIT is a
function call that returns later, and everything the actor needs is in
callee-saved registers or on its stack, which AAPCS64 already preserves
across a call. No function colouring.

Natively, three styles are possible, each with a different cost per
process:

- **Stackful** (BEAM, Go, the Transputer): a stack per process; can wait
  anywhere.
- **Stackless** (Zig's async, Rust, C++20): the compiler turns suspendable
  functions into state machines.
- **Run-to-completion** (Pony, Active Messages, the J-Machine): no stack
  per actor, but no waiting mid-handler.

a64-slight is a hybrid. The `recv` rule makes every `recv` a
run-to-completion boundary (no stack), while preemption, `yield`, `join`
and blocking syscalls use the stackful mechanism (keep the stack). That is
"option A" (D9).

## The AArch64 spike (in git at `3fd71e0`)

Hand-written AArch64 actors on a small C runtime, implementing the
calling-conventions doc: the multiplier (with the forged reply), dying
adders, a wrong-tag fault, preemption, deadlock detection. Its README (in
git at `3fd71e0`) has
the details. Results on an M2 Max, natively:

- **232 ns per REQUEST/AWAIT round trip** (deterministic tick mode; every
  message a `calloc` + `free`; not profiled).
- **16,851 bytes per idle process**: one 16 KiB stack page plus a 416-byte
  `proc_t`. The deepest suspension in any scenario used ~130 bytes of
  stack. **The page is the cost**, which is why a64-slight gives stacks
  back at `recv` (D9): a waiting process should cost about 100 bytes plus
  its heap.
- **1.9 µs per spawn**, mostly `mmap` + `mprotect` and the first page
  fault. A stack pool removes most of that.

Lessons that carry over:

- The 25-instruction `rt_switch` (x19–x30, sp, d8–d15) and the register
  convention: `x28` = current process, `x19`–`x27` preserved across every
  op including suspension, `x18` untouched (macOS).
- **AArch64 doesn't trap on division by zero** (`sdiv` returns 0), so the
  compiler must check.
- **macOS header collisions.** `<stdlib.h>` pulls in `<sys/wait.h>`, whose
  `P_PID` broke an unprefixed macro. Everything in shared headers is
  prefixed `RT_`/`rt_`, and `t/headers.c` checks `rt.h` against ~40 system
  headers.
- **Symbol naming and addressing differ** between Mach-O and ELF: see
  `RT_ASM()` in `rt.h` and `LOADADDR` in `actor.h`.
- GNU `as` rejected one of the macros; standardize on clang's integrated
  assembler.
- Guard-page hits (`SIGSEGV`) currently kill the whole program. Turning
  them into a process fault needs an alternate signal stack. Not done yet.

## Looking back, and ahead (after step 9)

Answers to questions Stevan asked once steps 0–9 were done. The estimates
are in sessions of the size the plan's steps have taken.

### What survived of VM3's calling convention

Very little of the convention itself. Function calls use AAPCS64, the
platform's C convention (D47): arguments in `x0`–`x7`, the result in `x0`.
Compiled functions are ordinary C-callable functions, and builtins and
runtime calls are a plain `bl`. On top of that: `x28` is always the
current process, every entry checks the stack and the reduction count, and
tail calls are branches.

The asynchronous half is gone, removed by the `recv` rule (D8):
`REQUEST`/`AWAIT`/`REPLY`/`REPLY_TO`, reply addresses, refs and `PENDING`;
the mailbox cursor and `MSG_SKIP`; MESSAGE and SIGNAL frames and signal
handlers; tags owned by modules, with arities; `@state`; the operand
stack; and the ticks with their delivery phases. That's about a third of
the document, and all of what made it a calling convention.

What survived:

- **The key idea, changed.** The sketch's `YIELD` was a commit point that
  faulted unless the PROCESS frame was the only frame, and a suspended
  `RECV` simply ran again on waking. The `recv` rule is that check moved
  to compile time, and running again became calling the receive function
  again with its arguments. That's why a waiting process holds no stack,
  and why the collector needed nothing from the compiler.
- **The sketch's run-to-completion style** became the only style.
- **The process semantics.** `%EXIT (pid, reason)` became `monitor`'s
  `(:exit pid result)`, `WATCH` became `monitor`, `WAIT` became `join`;
  stopping with a reason, the dead-letter queue, deadlock detection,
  per-pair FIFO order, and determinism (from a FIFO run queue rather than
  ticks) all carried over.
- **The spike's native layer:** the 25-instruction `rt_switch`
  (unchanged), `x28` for the process, `x18` left alone, and the Mach-O and
  ELF macros.

The spike predicted this: with a native stack per process, the
asynchronous convention collapses into the platform's synchronous one.

### Other targets: x86-64 and RISC-V (revised Oct 2026, after 10e)

**x86-64 was built in Oct 2026** (D145, D146; PLAN.md, after 10e), as
planned below: first the interface (`compiler/src/target.ts`), checked by
the assembly of all 150 programs coming out the same byte for byte, then
`x86_64.ts` (about 250 lines) to the register map below, which passed
every golden test natively on its first full run. What the plan didn't
foresee: a native Linux link uses GNU ld, which wants `.note.GNU-stack`
or makes the stack executable (both assembler headers now carry it);
`rt_apply`'s spread takes every caller-saved register, so it checks the
list in one pass and spreads it in a second; and the dozen calls that
relied on the accumulator being the first argument all go through one
`placeArgs`, which orders the moves. The plan as it was written follows;
RISC-V's part still stands, with the interface done. The runtime's files
are now `asm_aarch64.h` and `rt_asm_aarch64.S` (were `asm.h` and
`rt_asm.S`).

What's AArch64-specific: `codegen.ts` emits AArch64 text directly (about
170 instruction lines and 38 mnemonics, over some 25 functions, in 1,382
lines), `rt_asm.S` (126 lines: `rt_switch`, `rt_trampoline`,
`rt_apply`), `asm.h` (57), the saved registers (`rt_ctx_t` and
`RT_CTX_*` in `rt.h`), and three lines of `start` in `process.c`. That's
all. The C runtime (2,346 lines, with the collector, devices and
sockets) compiles for x86-64 unchanged and without warnings, and the
reader, the expander, `classify.ts`, the code generator's structure and
every golden test carry over. The tests are the real asset: a backend is
done when the same outputs come out, and they should, unchanged. Frames
are the same size on both (16 bytes of linkage), reductions count
function entries, allocations are the same sizes, and no output shows an
address.

- **First, for either target (about 1 session):** separate the code
  generator's structure from its instruction text, behind a target
  interface at the level of code shapes: the prologue and entry checks,
  slot loads and stores, compare to a boolean, branch unless a cons,
  allocation, arithmetic and division, calls, tail calls and calls
  through closures, and calling the runtime. (A virtual instruction set
  that each target prints would be more machinery.) Two
  assumptions in today's code don't hold on x86, and the interface has to
  absorb them: that the accumulator is also the first argument (`x0` is
  both, and about a dozen calls into the runtime rely on it; they become
  one "call the runtime with these operands"), and that a two-part test
  can leave one flag (`IS_CONS` and `bool?` use `ccmp`). The check is
  cheap and strict: the generated `.S` of every golden test and example
  mustn't change by a byte. Worth doing before the self-hosting port
  anyway.
- **x86-64 (about 2 sessions after that).** The runtime's half is about
  half a session: `rt_switch` (`rbx`, `rbp`, `r12`–`r15`, `rsp`),
  `start` putting the trampoline's address on the new stack (x86's `ret`
  takes it from there, not from a link register), `rt_apply`, `asm.h`,
  and a target option for `slightc`, `make` and `run.sh`. The code
  generator's second target is about 300 lines, debugged against the
  golden tests. A register map, to settle next to D47: `rax` the
  accumulator, the result, and the closure at entry; `rdi`, `rsi`,
  `rdx`, `rcx`, `r8`, `r9` the first six arguments (System V's, so calls
  into C line up, and no runtime function takes more than six) and
  `r10`, `r11` the last two; `r15` the process; `rbx` and `r12`–`r14`
  scratch, which is safe because compiled code is only entered from
  `rt_trampoline`, and `rt_switch` saves them. Intel syntax: clang
  assembles it with `rt.h`'s constants in memory operands, for ELF and
  Mach-O (checked). Easier than it looked: the entry checks need no
  scratch register (`cmp rsp, [r15 + RT_PROC_STACK_LIMIT]`); a closure
  is called through memory (`call qword ptr [rax + RT_CLOSURE_CODE]`);
  `*` is `sar`, `imul`, `jo`; `idiv`'s remainder is already the tagged
  remainder, and its trap on −2⁶³ ÷ −1 can't happen, since a tagged
  divisor is even; `LOADADDR` is one `lea [rip + sym]` on both formats.
  Harder: two-operand instructions, no `csel` or `ccmp`, all nine
  caller-saved registers taken, and a 16-byte stack alignment the
  hardware doesn't check, so a mistake crashes deep in libc. Stevan's M2
  Max could run the x86-64 build under Rosetta 2 (`cc -arch x86_64`),
  and does: the golden tests pass there (Oct 2026).
- **RISC-V, RV64 (about 2 sessions after the interface).** The closest
  cousin: 32 registers, load/store, arguments in `a0`–`a7` (exactly
  eight, as D47 has), and `s11` could be the process register. No
  condition flags, so an overflow check costs about three more
  instructions per operation; no `csel` in the base ISA (branches, or the
  Zicond extension); 12-bit immediates, so longer constant sequences.
  Tested with `qemu-riscv64`.
- **A 32-bit target** (Cortex-M, RV32) is a bigger change: 31-bit
  integers, 4-byte words and 8-byte cells change the value layout and the
  runtime's offsets, not just the instruction text.

**What native x86 would save the cloud sessions: little.** `make golden`
takes 3m07s there, and about 1.2 s of each test is clang compiling the
whole runtime at `-O2`; running under qemu takes about 20 ms a test, with
`million-forks` (3.2 s) the exception. Building the runtime once per run
would save most of the three minutes, and needs no new target. (Now that
`make golden` runs both targets on x86-64, it takes about twice as long,
so building the runtime once per target and run is worth more.) Done in
step 11 (D150): the runtime is compiled once and kept, and the tests run
in parallel, so `make test` takes 46 seconds.

### Compiling to C instead (discussed Oct 2026)

Stevan asked whether emitting C would be easier than a second assembly
backend, and what it would cost in speed. The design suits it unusually
well, for the reason it suits WebAssembly: the collector runs only at
`recv`, where the roots are the receive function's arguments (D105), so
it never scans a stack, and clang can keep values wherever it likes. Most
compilers that emit C need a shadow stack for their roots. (Since step
12b the collector also runs at a function's entry and reads the frames,
D166, so a C target would need one too, or would collect only at `recv`,
as before.)

Measured by hand-writing the C a backend would emit for `fib` (the same
tags, checks and slow paths into the runtime) and compiling it with
clang 18 at `-O2`:

- **Faster code.** `fib`'s recursive path is 33 instructions on AArch64
  (31 on x86-64), where today's generator emits 59, and about 9 loads and
  stores where it does 21. Clang keeps values in registers; today's
  generator spills every temporary to a frame slot, by design (no
  register allocation). Not timed: qemu's timings mean nothing, so that
  needs the M2.
- **Tail calls hold, with conditions.** `__attribute__((musttail))`
  (clang since 13, GCC since 15) guarantees them, at `-O0` too, but only
  between functions with the same signature, so every slight function
  would take the same eight parameters. On RISC-V, clang 18 crashes on a
  `musttail` call once arguments go on the stack, so the signature has
  to fit in eight registers: the arguments only, with the process taken
  from the runtime's `rt_current` and the closure passed beside it, in
  the process or a global the callee reads at entry. Unused arguments
  cost nothing: left undefined, they're whatever is in the register. On
  x86-64 the seventh and eighth go on the stack.
- **Static data carries over.** Quoted lists, strings and static
  closures become C initializers, tagged pointers included
  (`(V)&q1[2] + 1` becomes a relocation with an addend, on ELF and
  Mach-O).
- **Compiling is slower.** Clang takes about 4,000 lines of such C a
  second, at `-O1` or `-O2`. A program and the prelude would be 1,500 to
  2,000 lines (`fib` is 4,743 lines of assembly today), so about half a
  second more per program, and a few seconds for the self-hosted
  compiler.

**What it gains:**

- One backend for every 64-bit target clang has. x86-64 and RISC-V each
  need only `rt_switch` and the trampoline (about 30 lines of assembly)
  and the driver's settings: about half a session each.
- A simpler code generator to port when self-hosting (step 17): no
  frame slots to count, no immediates to encode (`loadWord`, `addImm`,
  `closureField`'s ranges), no Mach-O and ELF differences, and
  `rt_apply` in C.
- Readable output, and `#line` directives would let lldb and gdb step
  through the `.slight` source.
- A much cheaper WASI target: clang does the structured control flow,
  and with `-mtail-call` a `musttail` call becomes `return_call`
  (checked). Stack switching stays the hard part.

**What it costs:**

- The output leans on `musttail`, an extension, and on the generated C
  staying clear of undefined behaviour: overflow checks through
  `__builtin_add_overflow` and its siblings (which compile to `adds` and
  `b.vs`, or `add` and `jo`), and floats read with `memcpy` or
  `-fno-strict-aliasing`.
- Less control of frames. The stack check still compares the frame's
  address with the limit (D48), but clang decides how big frames are;
  the 64 KB of headroom covers it.
- A playground that compiles in the browser (below) would need clang in
  the page, or a direct `.wasm` backend after all.
- It rewrites the emitting half of `codegen.ts` (about 1,000 lines), D47
  gives way to the one C signature, and the project stops writing its
  own machine code: still native, but through clang.

**Cost, roughly:** 2.5 to 3 sessions to reach today's state on AArch64
(about 2 for the code generator, the rest for the runtime's side, the
driver, and the unit tests that read assembly text), then about half a
session each for x86-64 and RISC-V. That's about what x86-64 alone costs
as a second assembly backend, for every target and faster code. If it
happens, it should happen before self-hosting (step 17), so that the
compiler isn't ported to slight twice.

### Multiple cores

On a hosted OS, not without threads: the kernel owns the cores, so using
N of them takes at least N OS threads. They needn't be visible, though.
The usual design (the BEAM's schedulers, Go's Ms and Ps) is one OS thread
per core, each running the scheduler loop with its own run queue, taking
work from the others when idle; slight programs never see a thread. On
bare metal it's literally threadless: wake the other cores (PSCI
`CPU_ON` on ARM, SBI `hart_start` on RISC-V) and point each at a stack
and the scheduler loop.

The design suits it unusually well: no shared mutable data, a heap per
process, copied messages, and a collector that only ever touches its own
process's heap, so there's never a stop-the-world pause. What would need
synchronizing is all in the runtime, and small:

- mailboxes: an atomic multi-producer queue per process, plus a
  compare-and-swap on the process's state, so a message that arrives as
  the receiver decides to wait can't be lost;
- run queues: one per core, with stealing;
- the process table: it's grown with `realloc` today, which moves it; it
  would need a structure that doesn't move, and an atomic pid counter;
- locks on the join and monitor lists, the stack pool, and terminal
  output;
- acquire/release ordering for all of it: AArch64 reorders memory
  accesses (`ldar`/`stlr`, or the LSE atomics).

The costs: interleavings stop being deterministic, so tests need a
single-core mode (like the virtual clock); every send pays an atomic
operation; an idle core needs waking (a futex on Linux, `SEV` or an
inter-processor interrupt on bare metal). Not a bad idea, but a step of
its own once the language settles. The test is the one the VM3 sketch
listed: the same results on one core or N.

### Embedded boards

**AArch64 Linux boards work today, unchanged:** `slightc` makes static
`aarch64-linux` binaries, which is what qemu runs here. Copy one to a
Raspberry Pi 3, 4, 5 or Zero 2 W and run it.

What the runtime asks of the OS is small: `malloc`, `calloc`, `realloc`
and `free`; `mmap` and `mprotect` for stacks with guard pages; stdout and
stderr; `sysconf`, `getenv`, `exit`; and libm. Step 10 adds waiting on
timers, terminal raw mode, and file I/O.

**Bare metal is a natural fit, arguably more than an RTOS.** The runtime
already is a small scheduler: processes, message passing, memory per
process, and isolation by the language. Preemption counts reductions, so
scheduling needs no timer interrupt; interrupts are only for timers and
devices. An RTOS would add a second scheduler to work around. A
bare-metal AArch64 port needs:

- boot code: drop to EL1, zero `.bss`, enable the FPU (floats use the `d`
  registers), and turn on the MMU and caches with an identity map (with
  the MMU off, memory is Device memory: unaligned accesses fault, and
  exclusive loads and stores don't work);
- a UART driver for stdout and stderr (a PL011 on the Pi), and an
  allocator (picolibc or newlib, which also bring `printf` and libm, or
  our own);
- stacks from plain RAM, smaller than 8 MB (64 KB, say); the stack check
  at every entry (D48) already gives a clean fault, so guard pages are
  optional;
- for step 10, the generic timer and the GIC; interrupt handlers push
  into preallocated rings that the scheduler drains, since a handler
  can't `malloc`.

QEMU's `virt` machine has a PL011, a GIC and the generic timer, so it's
a good test bed before a real board. About 2–3 sessions to boot and pass
the non-device golden tests.

**Microcontrollers** (STM32, RP2040 and RP2350, ESP32, nRF) are 32-bit,
so they need the 32-bit target above, and budgets of a few hundred KB of
RAM rather than 64 MB heaps and 8 MB stacks. A bigger project.

**One caveat everywhere:** there's no memory protection between slight
processes; isolation comes from the language (no pointers, immutable
data, copied messages), as in the BEAM. A bug in the C runtime takes
everything down.

### WebAssembly, and the browser (discussed Oct 2026, parked)

Stevan asked whether slight could target WASM and run in a browser. It
could, and the design makes it easier than for most languages; the hard
part is processes that keep a stack while they aren't running. Parked
until the language settles. Browser support below is as of Oct 2026.

**What carries over:**

- The tagged 64-bit words, bump allocation, per-process heaps and the
  copying collector all fit WASM's linear memory, and the C runtime
  (`rt.c`, `strings.c`, `numbers.c`, most of `process.c`) compiles to
  wasm32 with clang.
- **No shadow stack.** WASM's own stack can't be scanned, so most
  garbage-collected languages keep their roots on a stack of their own in
  linear memory. We didn't need one: the collector ran only at `recv`,
  where the stack is empty and the roots are the receive function's
  arguments, handed to the runtime (D105). Since step 12b it also runs at
  a function's entry and reads the frames (D166), so a WASM target would
  keep its slots in linear memory, or collect only at `recv` there.
- Tail calls: `return_call` is standardized and in all three engines
  (Safari last, in 18.2). Leaning Technologies has written about rough
  edges in engines' tail calls; worth reading before relying on them.
- Frame slots become WASM locals; the accumulator style fits the operand
  stack. No condition flags, so overflow checks cost a few instructions
  more, as on RISC-V.
- A fault unwinds to the scheduler as a WASM exception (shipped
  everywhere). `apply` is `call_indirect`, one function type per arity
  0–8. libm's functions come from a wasm libm (or JavaScript's `Math`,
  whose last bits differ anyway, D76).

**The hard part: stacks.** Natively, `rt_switch` swaps `sp`; in WASM the
call stack belongs to the engine, and standard WASM can't switch it yet.
The `recv` rule means most switches don't need to: a process waiting in
`recv` has no stack, and the scheduler resumes it with an ordinary call
of its receive function. The cases that keep a stack are preemption in
the middle of a call, `yield`, `join` and `sleep` (so `slurp` and `spew`
too). The options:

1. **The stack-switching proposal** (continuations: `cont.new`,
   `resume`, `suspend`). Exactly `rt_switch`, so the port would mirror
   the native one. Phase 3; Wasmtime has an implementation, browsers
   don't yet. The long-term answer.
2. **JSPI** (JavaScript Promise Integration): Chrome 137+, Safari 27
   beta, Firefox 153 planned. Made for calling async JavaScript, but each
   process can run in its own suspendable ("promising") call into WASM.
   Every switch goes through JavaScript's promises, far dearer than
   `rt_switch`, but only the stack-keeping cases pay it. Plausible;
   needs a prototype.
3. **Asyncify** (Binaryen) rewrites the whole program so a stack can be
   unwound into linear memory and rewound. Works everywhere today, but
   costs size and speed in all code, and a switch costs in proportion to
   the stack's depth.
4. **Change the language** so no process keeps a stack: preempt only at
   tail calls at the bottom of the stack, and allow `join` and `sleep`
   only as tail calls. Undoes decisions made on purpose (`join` works
   anywhere, D101), and a deep non-tail recursion would never be
   interrupted. Not recommended.

Recommendation: make the stack strategy one small piece of the runtime
(start, suspend and resume a process); prototype it with JSPI, keep
Asyncify as a fallback, and move to stack switching when it ships.

**The scheduler turns inside out.** A browser's main thread can't block,
so the `select()` wait goes: the runtime runs a slice and returns, and
JavaScript calls it again on a key, when the next timer is due
(`setTimeout`), or when a fetch completes. The virtual clock is
unchanged.

**Devices in a browser:**

- **The terminal, yes:** xterm.js renders ANSI escapes and hands over
  keys as the bytes a terminal sends, so `tty.c`'s decoder works
  unchanged, and the terminal examples (and the editor and window
  managers) would run as they are, in a page.
- **Files, as a stand-in:** an in-memory filesystem, the Origin Private
  File System, or read-only `fetch`, behind the same device messages.
- **TCP, no:** browsers have no raw sockets, let alone listening ones.
  Their natural devices are `fetch` and WebSocket. **For HTTP (step
  14):** HTTP in slight on `:tcp` works natively and under WASI, but in a
  browser it would be a device over `fetch`; an HTTP API that doesn't
  care which sits underneath would let a browser port keep programs
  unchanged.

**Outside the browser, and a playground.** WASI (wasmtime, Node) has
stdin, stdout, files, a clock, and `poll_oneoff` for waiting like
`select()`: the natural first step, running the golden tests under
wasmtime instead of qemu (skipping sockets). The compiler is TypeScript,
so it already runs in a browser; what's missing is clang to link. If the
backend wrote `.wasm` directly, and the program imported its memory,
function table and builtins from a prebuilt `runtime.wasm`, no linker
would be needed, and a page could compile and run slight. Self-hosted,
the whole toolchain would live in the page.

**Rejected: WasmGC** (the browser's collector). It would cost the
per-process heaps and collecting at `recv`, integers would change (its
unboxed integers, i31, are 31-bit), and the C runtime wouldn't apply.

**Cost, roughly:** bigger than x86-64 or RISC-V (structured control
flow, the inverted waiting loop, stack switching). About 1 session for
the target interface (as above), 4–6 for a WASI target passing the
golden tests other than sockets, and 2–3 more for a browser host with
xterm.js.
Compiling to C (above) would take most of the code generation off the
WASI target.

Sources: [stack switching](https://github.com/WebAssembly/stack-switching),
[proposal phases](https://github.com/webassembly/proposals),
[JSPI (V8)](https://v8.dev/blog/jspi),
[JSPI support (OpenReplay)](https://blog.openreplay.com/jspi-javascript-wasm-bridge/),
[state of WebAssembly 2024–2025](https://platform.uno/blog/state-of-webassembly-2024-2025/),
[tail calls (Leaning Technologies)](https://labs.leaningtech.com/blog/extreme-webassembly-2-the-sad-state-of-webassembly-tail-calls).

## C libraries (looked at Oct 2026)

What led to D151–D155, for when it comes up again.

**What Odin does** (its repository, Oct 2026). Two collections: `core:`,
written in Odin and part of the language, and `vendor:`, bindings to
third-party C libraries shipped with the compiler and curated by its
team, each with its upstream licence. A vendored library arrives in one
of three ways, chosen per library and per OS: as source with a build
script (stb, miniaudio, cgltf: `build_stb.sh` makes `lib/*.a` on Linux,
`lib/darwin/*.a` for both architectures through `lipo` on macOS, and
`.o` files for WASM, and the binding `#panic`s until it has been run);
as prebuilt binaries in the repository (Windows `.lib`s for nearly
everything, box2d's macOS `.a`s); or as the system's library
(`system:curl` and `system:z` on macOS and Linux). A binding names its
file with `foreign import`, relative to the binding or `system:`, chosen
with `when ODIN_OS == ...`; bindings are written by hand, since Odin
doesn't read C headers. Much that might have been C is Odin: all of
`core:crypto` (AES, ChaCha20-Poly1305, SHA-2 and SHA-3, X25519, Ed25519,
ECDSA, RSA, ML-KEM, ...; its README says it hasn't had a third-party
review), `core:encoding/json`, and DNS (`core:net` reads `/etc/hosts`
and `/etc/resolv.conf` and asks the servers itself, so it never blocks
in `getaddrinfo`). TLS isn't there: the maintainers see a native one as
a long job, and talked of an interface for plugging one in. And
`vendor:libc-shim` is a small libc written in Odin, so that vendored C
compiles to WASM without Emscripten.

**What we took, and didn't.** The split (our `lib/` and `vendor/`),
vendoring a small pinned set, and DNS done by the runtime. Not crypto in
the language: Odin is about as fast as C and has fixed-width integers and
bit operations, and slight has neither. Not build scripts: `slightc`
drives clang, so it compiles a vendored library itself. For WASM
(parked), a `libc-shim` of our own would be how vendored C gets there.

**JSON parsers.** simdjson parses at gigabytes a second, but it's C++17,
its single-header form is 13.7 MB, it wants the whole document in memory
with padding after it, and it can't be fed bytes as they arrive
(`iterate_many` streams a sequence of documents, with a thread); its
"On Demand" mode, touching only the fields read, doesn't fit values that
are built whole and copied between processes. yyjson (C, MIT, 760 KB of
source) is fast and has a writer, but its incremental reader only
resumes as bytes arrive, with the document whole in one buffer at the
end. YAJL streams, but hasn't been maintained for about ten years. So
D154 writes its own push parsers. JSONTestSuite ("Parsing JSON is a
Minefield") is the conformance test: its `y_` files must parse, its
`n_` files must not, and its `i_` files may go either way.

## Prior art

- **Perl's POD**: documentation in the source, in blocks the compiler
  skips (`=head1` ... `=cut`), which a stream can pick out by lines. D156
  takes the markers, with Markdown inside.
- **Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction"**
  (2006): the build plan.
- **Nanopass** (Sarkar, Waddell, Dybvig; Keep): many small passes.
- **The BEAM**: per-process heaps, copying messages, the tagged value
  layout, reductions. **BeamAsm** is its JIT.
- **Cheney's algorithm**, and Baker's **"Cheney on the MTA"** (Chicken
  Scheme).
- **Perceus** (Koka) and Lean 4: reference counting with in-place reuse.
  Possible later, since the heap is acyclic.
- **Pony**: run-to-completion actors. **Go**: a stack per goroutine and a
  reserved register for `g`.
- **The Transputer**, **the J-Machine / Active Messages**, **L4/seL4 IPC**:
  hardware and kernels built around message passing.
- **CDR-coding** and **VLists** (Bagwell): denser list representations,
  possible later.
