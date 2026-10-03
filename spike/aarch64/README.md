# Native AArch64 spike

The calling conventions from [`CALLING_CONVENTIONS.md`](../CALLING_CONVENTIONS.md)
running as real machine code. The actors are hand-written AArch64 assembly,
each process has its own native stack, and every op (`SEND`, `REQUEST`,
`AWAIT`, `RECV`, ...) is an ordinary `bl` into a small runtime. The question
it answers: does the convention hold up below the VM?

## Running it

On Apple Silicon (or any AArch64 Linux):

```
make test        # rt.h vs. the system headers (t/headers.c), then the five
                 # scenarios below, diffed against t/*.expected
make bench       # REQUEST/AWAIT round-trip time and per-process memory
./spike bench 1000000 10000      # n round trips, k idle processes
RT_QUOTA=100 RT_STACK_KB=16 ./spike multiplier
```

On an x86-64 Linux box the Makefile cross-compiles with clang and runs under
`qemu-aarch64` (needs `clang`, `lld`, `gcc-aarch64-linux-gnu` for the sysroot,
and `qemu-user`). That's how this was developed; its timings under qemu
aren't worth anything.

| Scenario      | What it shows |
|---------------|---------------|
| `multiplier`  | The worked example from the doc: `4 * 10` across 20 processes, while `/main` forges a `^product` at the top multiplier. Prints 40; the forgery ends up in the dead-letter queue. |
| `dying-adder` | The adders stop without replying. Each `AWAIT` takes its failure branch, all the way up: "the multiplier died". |
| `wrong-tag`   | `SEND`ing `/multiplier`'s tag to an `/adder` faults the *sender*. |
| `preempt`     | A spinner in a tight loop (with `REDUCE` at its back-edge) can't starve a `REQUEST`/`AWAIT`: the 5 comes back first. |
| `deadlock`    | Two processes `AWAIT` each other; the scheduler notices nothing can run and says so. |

Two negative controls (not in `make test`, they need code changes or env vars):

- make `AWAIT` match on the tag alone (ignore the `ref`) and `multiplier`
  prints **1003**: AVM's original bug, reproduced natively
- `RT_QUOTA=1000000000 ./spike preempt` turns preemption off in practice,
  and the spinner finishes first

## Results on an M2 Max (macOS)

`make test` passes natively. `make bench`:

```
round trips:  1000000 in 232.1 ms = 232.1 ns each  (2000002 ticks, 2000002 activations, 2000001 messages)
idle procs:   10000 spawned and suspended in 19.3 ms = 1928 ns each, +164560 KiB resident = 16851 bytes each
```

- **232 ns per REQUEST/AWAIT round trip**, in the deterministic tick mode:
  every round trip is two ticks and two activations, and every message is a
  `calloc` + `free`. Not profiled yet; the context switch is only 25
  instructions, so I'd expect most of the time to be elsewhere.
- **16,851 bytes per idle process** is one 16 KiB stack page (the only one a
  suspended process touches), plus 416 bytes of `proc_t`, plus 16 bytes of
  table entries: 16,816. The page is the cost, not the stack: the deepest
  suspension in any scenario uses about 130 bytes of it (measured under qemu).
- **1.9 µs per spawn** is mostly `mmap` + `mprotect` and the first-touch page
  fault. A pool of reused stacks would remove most of it.

## Layout

| File | |
|------|---|
| `actor.h`           | The ops as assembler macros, and the register convention. Start here. |
| `actors/*.S`        | The actors: `/main`, `/multiplier`, `/adder`, and the smaller scenarios. |
| `rt_asm.S`          | The context switch, the process trampoline, and the op stubs. |
| `rt.c`              | Bookkeeping: process table, mailboxes, the bus, the tick scheduler. |
| `rt.h`              | Structs and the offsets shared with the assembly (all `_Static_assert`ed). |
| `harness.c`         | Picks a scenario, runs it, prints the report. |

## The register convention

The doc's stack effects, translated to registers:

| Register  | Role |
|-----------|------|
| `x28`     | **self.** Set by the runtime, never written by actor code (Go uses x28 the same way on arm64). |
| `x19-x27` | **The actor's own.** Preserved by every op, *including the ones that suspend*. This is where state lives across an `AWAIT`. |
| `x0`      | **Control value**, in and out: a pid, a pending, the `AWAIT` ok flag, an envelope. |
| `x1-x7`   | **Payload**: arg *i* is in x(*i*+1), going out (`SPAWN`, `SEND`, `REQUEST`, `REPLY`) and coming in (`.init`, `MSG_ACCEPT`, `AWAIT`). A message arrives in the same registers it was sent from. |
| `x9, x10` | An op's static operands (module, tag, reply tag), loaded by the macro. |
| `x11`     | argc, loaded by the macro and checked against the tag's arity at runtime. |
| the rest  | Caller-saved, as in AAPCS64. `x18` is never touched (it belongs to macOS). |

So the multiplier's recursive step reads:

```
    SPAWN mod_multiplier, 1             // x0 = m
    mov  x1, x19                        // x
    sub  x2, x20, #1                    // y - 1
    REQUEST tag_multiplier_mul, tag_multiplier_product, 2      // x0 = pending
    AWAIT   tag_multiplier_product, Lmul_failed                // x1 = p
```

and `x19`/`x20` (x and y) are still there after the `AWAIT`, though the
process was suspended in between while other processes ran on the same core.

## How the ops map

| Doc | Here | Underneath |
|-----|------|------------|
| process          | `proc_t` + an `mmap`'d stack with a guard page | |
| suspension       | `rt_switch`: 25 instructions | saves x19-x30, sp and d8-d15, loads another set, `ret` |
| `SPAWN`          | `SPAWN mod, argc` | a new stack whose first "return" is into `rt_trampoline`, which calls `.init(self, args...)` |
| `SEND`, `REQUEST`| `SEND tag, argc`, `REQUEST tag, reply, argc` | checks owner and arity, copies x1..x7 into an envelope on the bus |
| pending / reply address | one register: `ref:32 \| pid:24 \| tag:8` | no allocation |
| `AWAIT`          | `AWAIT reply, failed` | a `bl` that returns when the reply arrives: x0 = 1, x1.. = values; or x0 = 0 and the macro branches to `failed` |
| `RECV`           | `RECV` | returns the tag index in x0, suspending first if the mailbox is empty |
| `SWITCH`         | `SWITCH count, default, table` | `adr` + `ldrsw` + `br` through a table of `.word Lcase - table` |
| `MSG_ACCEPT`     | `MSG_ACCEPT` | the envelope becomes the top MESSAGE frame; its args are loaded into x1..x7 |
| `YIELD`          | `YIELD label` | the commit check (no MESSAGE frame, `sp` where it was at the first `YIELD`), resets the cursor, branches |
| quota            | `REDUCE` at loop back-edges | `ldr`/`subs`/`str`/`b.gt` on `[x28]`, `bl rt_preempt` when it hits zero |
| ticks            | `rt_run` | deliver, wake, run each READY process in pid order, reap, as in the doc |

"Restartable instructions" turn into something simpler natively: `RECV` and
`AWAIT` are loops inside a runtime call that block until their condition holds.
The C frames of that call are part of the suspended stack.

## What building it showed

- **The async convention collapses into the sync one.** No actor code knows
  it was suspended. An `AWAIT` is a function call that returns later, and
  everything the actor needs is in callee-saved registers or on its stack,
  which AAPCS64 already promises to preserve across a call.
- **The doc's frame unification pays off.** A MESSAGE frame is the envelope
  itself, pushed onto a per-process list; `MSG_DONE` pops it. Values computed
  from the message survive it automatically, because they're in registers.
- **The commit check works natively.** "Nothing above the PROCESS frame's
  base" becomes "`sp` is back where it was at the first `YIELD`".
- **"A pid knows its module" costs a table.** The ownership check needs the
  target's module even after the target is reaped (a stray `SEND` to a dead
  process is still checked), so `mods[pid]` outlives `procs[pid]`. The
  alternative is to encode a module id in the pid's bits.
- **The floor is a page per process.** Every process touches at least one
  page of its stack, and Apple Silicon pages are 16 KiB. That's the price
  of stackful processes. A module that never `AWAIT`s could run its handlers
  on the scheduler's stack (Pony-style) and skip it.
- **Determinism costs throughput.** A round trip is two ticks, and each tick
  walks the live list. A throughput mode would deliver immediately and keep a
  run queue; the tick mode is what makes `t/*.expected` exact.
- **AArch64 doesn't trap on division by zero** (`sdiv` returns 0), so that
  fault simply doesn't exist here. What's left are runtime-detected faults
  (implemented) and memory faults like a guard-page hit (not: today that kills
  the whole program).

## Not in the spike

- signals (`%EXIT`, `%STOP`), `NOTIFY`, `WATCH`, `WAIT`, `WAIT_ALL`
- `REPLY_TO` and `MSG_REPLY_ADDR` (so no run-to-completion style or delegation)
- `MSG_SKIP` is implemented but nothing exercises it (`AWAIT` does its own skipping)
- more than one core
- turning `SIGSEGV`/`SIGBUS` (guard page, bad pointer) into a process fault
- assembly-time arity checks (the runtime checks instead)
