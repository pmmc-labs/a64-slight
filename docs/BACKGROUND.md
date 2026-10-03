# Background

How this project came about, and what the earlier attempts taught. None of
this is needed to build the next step, but it explains why the design
looks the way it does.

## The lineage

| Project | When | What it was |
|---|---|---|
| **AVM** (stevan/AVM) | Aug 19–22, 2024 (8 commits) | An actor VM in Perl. It works: a tick loop, a message bus, spawn/send/recv, yield/stop, reaping, emulated multi-core. Its best trick was that a process's stack survives a yield, and a RECV on an empty mailbox resumes at the RECV itself. So every RECV is an await point with locals intact, which is how its recursive multiplier waits mid-handler. |
| **VM3** (stevan/VM3) | Aug 26 – Sep 11, 2024 | The second attempt, in Perl: docs first (an async calling convention, an assembly language for actors), then an assembler, CALL/RETURN frames, locals and int widths. It got no actor opcodes. About a week went into a TimerWheel that nothing used ("I will stop hyper focusing on this for now"), and the project stalled. |
| **ts-slight** (pmmc-labs/ts-slight) | Jul 6 – Sep 24, 2026 (190 commits) | slight in TypeScript: a Lisp with fork/send/recv/join/yield, a fuel-driven scheduler, and the examples in `reference/ts-slight/examples/`. **Its user surface is what a64-slight keeps.** |
| **ts-cpi** (pmmc-labs/ts-cpi) | Sep 25, 2026 – (71 commits by Oct 3) | ts-slight's successor: a "control plane interpreter" with errors as values, checkpoints, exit notifications with reasons, reply addresses answered at most once, a deterministic tick scheduler and a virtual clock. Much bigger (roles, grants, TUI, HTTP). a64-slight takes the virtual clock, the reader, and some ideas; not the CPI. |

## The calling convention (VM3, revised Oct 2026)

`spike/CALLING_CONVENTIONS.md` is the October 2026 revision of VM3's doc.
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

## The AArch64 spike (`spike/aarch64/`)

Hand-written AArch64 actors on a small C runtime, implementing the
calling-conventions doc: the multiplier (with the forged reply), dying
adders, a wrong-tag fault, preemption, deadlock detection. Its README has
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

### Other targets: x86-64 and RISC-V

What's AArch64-specific: `codegen.ts` emits AArch64 text directly (about
140 emitting lines and 35 mnemonics, in 1,318 lines), `rt_asm.S` (126
lines: `rt_switch`, `rt_trampoline`, `rt_apply`), `asm.h` (57), and the
saved-register layout (`rt_ctx_t`, and `start` in `process.c`). The rest
carries over: the reader, `classify.ts`, the code generator's structure
(the value in an accumulator, temporaries in frame slots, the tagging),
about 1,270 lines of C runtime including the collector, and every golden
test. The tests are the real asset: a backend is done when the same
outputs come out.

- **First, for either target (about 1 session):** separate the code
  generator's structure from its instruction text, behind a small target
  interface: slot loads and stores, tag tests, compare-and-branch, calls,
  tail calls, allocation, prologue and epilogue. Worth doing before the
  self-hosting port anyway.
- **x86-64 (2–3 sessions after that).** System V passes only six
  arguments in registers, against our eight, so slight-to-slight calls
  would use our own registers and only calls into C the System V ones.
  Two-operand instructions; pin `r15` (say) for the process. Overflow is
  easier (`jo`). `idiv` traps on zero, where AArch64's `sdiv` returns 0,
  but the compiler checks first anyway. Cloud sessions run on x86-64
  Linux, so tests would run natively, without qemu.
- **RISC-V, RV64 (about 2 sessions).** The closest cousin: 32 registers,
  load/store, arguments in `a0`–`a7` (exactly eight, as D47 has), and
  `s11` could be the process register. No condition flags, so an
  overflow check costs about three more instructions per operation; no
  `csel` in the base ISA (branches, or the Zicond extension); 12-bit
  immediates, so longer constant sequences. Tested with `qemu-riscv64`.
- **A 32-bit target** (Cortex-M, RV32) is a bigger change: 31-bit
  integers, 4-byte words and 8-byte cells change the value layout and the
  runtime's offsets, not just the instruction text.

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

## Prior art

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
