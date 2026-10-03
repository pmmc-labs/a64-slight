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
